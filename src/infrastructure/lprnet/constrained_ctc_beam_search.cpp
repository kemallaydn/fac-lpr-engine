#include <fac_lpr/infrastructure/lprnet/constrained_ctc_beam_search.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace fac_lpr::infrastructure::lprnet {
namespace {

constexpr double negative_infinity = -std::numeric_limits<double>::infinity();

struct BeamState final {
    std::string text{};
    std::size_t last_class{0U};
    double log_probability{negative_infinity};
};

[[nodiscard]] double log_add(const double left, const double right) noexcept {
    if (left == negative_infinity) {
        return right;
    }
    if (right == negative_infinity) {
        return left;
    }
    const auto high = std::max(left, right);
    const auto low = std::min(left, right);
    return high + std::log1p(std::exp(low - high));
}

[[nodiscard]] char class_to_character(
    const GreedyCtcDecoderConfig& config,
    const std::size_t class_index) {
    if (class_index == config.blank_index) {
        throw application::InternalError("blank class cannot map to a character");
    }
    const auto charset_index = class_index < config.blank_index
        ? class_index
        : class_index - 1U;
    if (charset_index >= config.charset.size()) {
        throw application::InferenceError("beam class index is outside charset");
    }
    return config.charset[charset_index];
}

[[nodiscard]] bool charset_contains(
    const GreedyCtcDecoderConfig& config,
    const char character) noexcept {
    return std::find(config.charset.begin(), config.charset.end(), character) !=
           config.charset.end();
}

[[nodiscard]] std::vector<double> log_softmax(
    const std::span<const float> row) {
    if (row.empty()) {
        throw application::InferenceError("beam timestep has no classes");
    }

    float maximum = -std::numeric_limits<float>::infinity();
    for (const auto value : row) {
        if (!std::isfinite(value)) {
            throw application::InferenceError("beam logits contain NaN or Inf");
        }
        maximum = std::max(maximum, value);
    }

    double denominator = 0.0;
    for (const auto value : row) {
        denominator += std::exp(
            static_cast<double>(value) - static_cast<double>(maximum));
    }
    if (!std::isfinite(denominator) || denominator <= 0.0) {
        throw application::InferenceError("beam softmax denominator is invalid");
    }

    const auto log_denominator = std::log(denominator);
    std::vector<double> result(row.size());
    for (std::size_t index = 0U; index < row.size(); ++index) {
        result[index] = static_cast<double>(row[index]) -
                        static_cast<double>(maximum) -
                        log_denominator;
    }
    return result;
}

[[nodiscard]] std::vector<std::size_t> select_classes(
    const std::vector<double>& log_probabilities,
    const std::size_t count,
    const std::size_t blank_index) {
    std::vector<std::size_t> indices(log_probabilities.size());
    std::iota(indices.begin(), indices.end(), 0U);
    std::stable_sort(
        indices.begin(),
        indices.end(),
        [&log_probabilities](const std::size_t left, const std::size_t right) {
            if (log_probabilities[left] != log_probabilities[right]) {
                return log_probabilities[left] > log_probabilities[right];
            }
            return left < right;
        });

    if (indices.size() > count) {
        indices.resize(count);
    }
    if (std::find(indices.begin(), indices.end(), blank_index) == indices.end()) {
        indices.push_back(blank_index);
    }
    return indices;
}

[[nodiscard]] float confidence_from_log_probability(
    const double log_probability,
    const std::size_t timesteps) noexcept {
    if (!std::isfinite(log_probability) || timesteps == 0U) {
        return 0.0F;
    }
    const auto normalized = std::exp(
        log_probability / static_cast<double>(timesteps));
    return static_cast<float>(std::clamp(normalized, 0.0, 1.0));
}

} // namespace

ConstrainedCtcBeamSearch::ConstrainedCtcBeamSearch(
    ConstrainedCtcBeamSearchConfig config,
    application::TurkishPlateGrammar grammar)
    : config_(std::move(config)),
      grammar_(std::move(grammar)),
      greedy_decoder_(config_.ctc) {
    if (config_.beam_width == 0U || config_.beam_width > 512U ||
        config_.result_limit == 0U || config_.result_limit > config_.beam_width ||
        config_.classes_per_step == 0U ||
        config_.classes_per_step > config_.ctc.maximum_classes ||
        !std::isfinite(config_.confusion_weight) ||
        config_.confusion_weight <= 0.0F || config_.confusion_weight > 1.0F) {
        throw application::ConfigurationError(
            "constrained CTC beam search configuration is invalid");
    }

    for (const auto& [left, right] : config_.confusion_pairs) {
        if (left == '\0' || right == '\0' || left == right) {
            throw application::ConfigurationError(
                "CTC confusion pairs must contain two distinct characters");
        }
    }
}

ConstrainedCtcBeamSearchResult ConstrainedCtcBeamSearch::decode(
    const std::span<const float> logits,
    const std::size_t timesteps,
    const std::size_t classes) const {
    ConstrainedCtcBeamSearchResult result{};
    result.greedy = greedy_decoder_.decode(logits, timesteps, classes);

    if (classes != greedy_decoder_.class_count()) {
        throw application::InferenceError(
            "beam class count does not match configured CTC contract");
    }
    if (timesteps == 0U || timesteps > config_.ctc.maximum_timesteps ||
        classes == 0U || classes > config_.ctc.maximum_classes ||
        timesteps > std::numeric_limits<std::size_t>::max() / classes ||
        logits.size() != timesteps * classes) {
        throw application::InferenceError("beam logits shape is invalid");
    }

    std::vector<BeamState> beam{
        BeamState{"", config_.ctc.blank_index, 0.0}};
    const auto confusion_penalty = std::log(
        static_cast<double>(config_.confusion_weight));

    for (std::size_t timestep = 0U; timestep < timesteps; ++timestep) {
        const auto row = logits.subspan(timestep * classes, classes);
        const auto log_probabilities = log_softmax(row);
        const auto selected = select_classes(
            log_probabilities,
            std::min(config_.classes_per_step, classes),
            config_.ctc.blank_index);

        std::map<std::pair<std::string, std::size_t>, double> merged{};
        const auto merge_state = [&merged](
            std::string text,
            const std::size_t last_class,
            const double log_probability) {
            auto key = std::make_pair(std::move(text), last_class);
            const auto iterator = merged.find(key);
            if (iterator == merged.end()) {
                merged.emplace(std::move(key), log_probability);
            } else {
                iterator->second = log_add(iterator->second, log_probability);
            }
        };

        for (const auto& state : beam) {
            for (const auto class_index : selected) {
                const auto next_probability =
                    state.log_probability + log_probabilities[class_index];
                if (class_index == config_.ctc.blank_index) {
                    merge_state(
                        state.text,
                        config_.ctc.blank_index,
                        next_probability);
                    continue;
                }

                if (class_index == state.last_class) {
                    merge_state(state.text, class_index, next_probability);
                    continue;
                }

                const auto emitted = class_to_character(config_.ctc, class_index);
                auto append_if_valid = [&](const char character, const double penalty) {
                    std::string prefix = state.text;
                    prefix.push_back(character);
                    if (grammar_.is_valid_prefix(prefix)) {
                        merge_state(
                            std::move(prefix),
                            class_index,
                            next_probability + penalty);
                    }
                };

                append_if_valid(emitted, 0.0);
                for (const auto& [left, right] : config_.confusion_pairs) {
                    char alternative = '\0';
                    if (emitted == left) {
                        alternative = right;
                    } else if (emitted == right) {
                        alternative = left;
                    }
                    if (alternative != '\0' &&
                        charset_contains(config_.ctc, alternative)) {
                        append_if_valid(alternative, confusion_penalty);
                    }
                }
            }
        }

        beam.clear();
        beam.reserve(std::min(config_.beam_width, merged.size()));
        for (auto& [key, probability] : merged) {
            beam.push_back(BeamState{
                std::move(key.first),
                key.second,
                probability});
        }
        std::stable_sort(
            beam.begin(),
            beam.end(),
            [](const BeamState& left, const BeamState& right) {
                if (left.log_probability != right.log_probability) {
                    return left.log_probability > right.log_probability;
                }
                if (left.text != right.text) {
                    return left.text < right.text;
                }
                return left.last_class < right.last_class;
            });
        if (beam.size() > config_.beam_width) {
            beam.resize(config_.beam_width);
        }
        if (beam.empty()) {
            break;
        }
    }

    std::map<std::string, double> final_scores{};
    for (const auto& state : beam) {
        if (!grammar_.is_valid_normalized(state.text)) {
            continue;
        }
        const auto iterator = final_scores.find(state.text);
        if (iterator == final_scores.end()) {
            final_scores.emplace(state.text, state.log_probability);
        } else {
            iterator->second = log_add(iterator->second, state.log_probability);
        }
    }

    result.candidates.reserve(final_scores.size());
    for (const auto& [text, log_probability] : final_scores) {
        const auto confidence = confidence_from_log_probability(
            log_probability,
            timesteps);
        result.candidates.push_back(domain::PlateCandidate{
            .text = text,
            .confidence = confidence,
            .calibrated_confidence = confidence,
            .format_valid = true});
    }
    std::stable_sort(
        result.candidates.begin(),
        result.candidates.end(),
        [](const domain::PlateCandidate& left, const domain::PlateCandidate& right) {
            if (left.confidence != right.confidence) {
                return left.confidence > right.confidence;
            }
            return left.text < right.text;
        });
    if (result.candidates.size() > config_.result_limit) {
        result.candidates.resize(config_.result_limit);
    }

    if (result.candidates.empty() && !result.greedy.text.empty()) {
        const auto normalized = grammar_.normalize(result.greedy.text);
        result.candidates.push_back(domain::PlateCandidate{
            .text = normalized.empty() ? result.greedy.text : normalized,
            .confidence = result.greedy.confidence,
            .calibrated_confidence = result.greedy.confidence,
            .format_valid = !normalized.empty() &&
                            grammar_.is_valid_normalized(normalized)});
        result.fallback_used = true;
    }

    if (result.candidates.size() >= 2U) {
        result.top_margin = std::clamp(
            result.candidates[0].confidence - result.candidates[1].confidence,
            0.0F,
            1.0F);
    } else if (result.candidates.size() == 1U) {
        result.top_margin = result.candidates[0].confidence;
    }
    return result;
}

} // namespace fac_lpr::infrastructure::lprnet
