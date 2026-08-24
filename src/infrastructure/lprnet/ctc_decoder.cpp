#include <fac_lpr/infrastructure/lprnet/ctc_decoder.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace fac_lpr::infrastructure::lprnet {
namespace {

[[nodiscard]] std::size_t checked_element_count(
    const std::size_t timesteps,
    const std::size_t classes) {
    if (timesteps == 0U || classes == 0U) {
        throw application::InferenceError(
            "CTC logits dimensions must be non-zero");
    }
    if (timesteps > std::numeric_limits<std::size_t>::max() / classes) {
        throw application::InferenceError("CTC logits element count overflows");
    }
    return timesteps * classes;
}

[[nodiscard]] float argmax_probability(
    const std::span<const float> row,
    std::size_t& best_index) {
    if (row.empty()) {
        throw application::InferenceError("CTC timestep has no classes");
    }

    float max_logit = -std::numeric_limits<float>::infinity();
    best_index = 0U;
    for (std::size_t index = 0U; index < row.size(); ++index) {
        const auto value = row[index];
        if (!std::isfinite(value)) {
            throw application::InferenceError("CTC logits contain NaN or Inf");
        }
        if (value > max_logit) {
            max_logit = value;
            best_index = index;
        }
    }

    double denominator = 0.0;
    for (const auto value : row) {
        denominator += std::exp(
            static_cast<double>(value) - static_cast<double>(max_logit));
    }
    if (!std::isfinite(denominator) || denominator <= 0.0) {
        throw application::InferenceError("CTC softmax denominator is invalid");
    }

    const auto probability = 1.0 / denominator;
    return static_cast<float>(std::clamp(probability, 0.0, 1.0));
}

[[nodiscard]] char class_to_character(
    const GreedyCtcDecoderConfig& config,
    const std::size_t class_index) {
    if (class_index == config.blank_index) {
        throw application::InternalError("blank CTC class cannot map to a character");
    }
    const auto charset_index = class_index < config.blank_index
        ? class_index
        : class_index - 1U;
    if (charset_index >= config.charset.size()) {
        throw application::InferenceError("CTC class index is outside configured charset");
    }
    return config.charset[charset_index];
}

} // namespace

GreedyCtcDecoder::GreedyCtcDecoder(GreedyCtcDecoderConfig config)
    : config_(std::move(config)) {
    if (config_.charset.empty()) {
        throw application::ConfigurationError("CTC charset cannot be empty");
    }
    if (config_.charset.size() >= std::numeric_limits<std::size_t>::max()) {
        throw application::ConfigurationError("CTC charset size is invalid");
    }
    const auto classes = config_.charset.size() + 1U;
    if (config_.blank_index >= classes) {
        throw application::ConfigurationError("CTC blank index is outside class range");
    }
    if (config_.maximum_timesteps == 0U || config_.maximum_classes == 0U ||
        classes > config_.maximum_classes) {
        throw application::ConfigurationError("CTC decoder limits are invalid");
    }

    std::array<bool, 256U> seen{};
    for (const auto character : config_.charset) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '\0' || seen[byte]) {
            throw application::ConfigurationError(
                "CTC charset contains null or duplicate characters");
        }
        seen[byte] = true;
    }
}

CtcDecodeResult GreedyCtcDecoder::decode(
    const std::span<const float> logits,
    const std::size_t timesteps,
    const std::size_t classes) const {
    if (timesteps > config_.maximum_timesteps || classes > config_.maximum_classes) {
        throw application::InferenceError("CTC logits exceed configured decoder limits");
    }
    if (classes != class_count()) {
        throw application::InferenceError(
            "CTC class count does not match charset and blank configuration");
    }
    const auto expected = checked_element_count(timesteps, classes);
    if (logits.size() != expected) {
        throw application::InferenceError("CTC logits buffer size does not match shape");
    }

    CtcDecodeResult result{};
    result.text.reserve(timesteps);
    result.character_confidences.reserve(timesteps);

    std::size_t previous_class = std::numeric_limits<std::size_t>::max();
    double confidence_sum = 0.0;
    for (std::size_t timestep = 0U; timestep < timesteps; ++timestep) {
        const auto row = logits.subspan(timestep * classes, classes);
        std::size_t best_class = 0U;
        const auto probability = argmax_probability(row, best_class);

        if (best_class != config_.blank_index && best_class != previous_class) {
            result.text.push_back(class_to_character(config_, best_class));
            result.character_confidences.push_back(probability);
            confidence_sum += static_cast<double>(probability);
        }
        previous_class = best_class;
    }

    if (!result.character_confidences.empty()) {
        result.confidence = static_cast<float>(
            confidence_sum /
            static_cast<double>(result.character_confidences.size()));
        result.confidence = std::clamp(result.confidence, 0.0F, 1.0F);
    }
    return result;
}

} // namespace fac_lpr::infrastructure::lprnet
