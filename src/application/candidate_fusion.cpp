#include <fac_lpr/application/candidate_fusion.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>

namespace fac_lpr::application {
namespace {

[[nodiscard]] float clamp01(const float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

void require_probability(const float value, const char* name) {
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
        throw ConfigurationError(std::string{name} + " must be finite and in [0,1]");
    }
}

struct Aggregate final {
    float support{0.0F};
    bool format_valid{false};
};

[[nodiscard]] float candidate_margin(
    const domain::RecognitionEvidence& evidence,
    const std::size_t candidate_index) noexcept {
    if (candidate_index >= evidence.candidates.size() || evidence.candidates.empty()) {
        return 0.0F;
    }

    const auto candidate_confidence = evidence.candidates[candidate_index].confidence;
    float highest_other = 0.0F;
    for (std::size_t index = 0U; index < evidence.candidates.size(); ++index) {
        if (index == candidate_index) {
            continue;
        }
        highest_other = std::max(highest_other, evidence.candidates[index].confidence);
    }
    return clamp01(candidate_confidence - highest_other);
}

} // namespace

WeightedMultiCropCandidateFusion::WeightedMultiCropCandidateFusion(
    CandidateFusionConfig config)
    : config_(std::move(config)) {
    if (config_.max_candidates == 0U || config_.max_candidates > 64U) {
        throw ConfigurationError("candidate fusion max_candidates must be in [1,64]");
    }
    require_probability(config_.default_source_weight, "default_source_weight");
    require_probability(config_.recognition_weight, "recognition_weight");
    require_probability(config_.crop_quality_weight, "crop_quality_weight");
    require_probability(config_.candidate_margin_weight, "candidate_margin_weight");
    require_probability(config_.layout_weight, "layout_weight");
    require_probability(
        config_.minimum_candidate_confidence,
        "minimum_candidate_confidence");

    const auto total_weight = config_.recognition_weight +
                              config_.crop_quality_weight +
                              config_.candidate_margin_weight +
                              config_.layout_weight;
    if (!std::isfinite(total_weight) || total_weight <= 0.0F) {
        throw ConfigurationError("candidate fusion scoring weights must have a positive sum");
    }
    for (const auto& [source, weight] : config_.source_weights) {
        if (source.empty()) {
            throw ConfigurationError("candidate fusion source name cannot be empty");
        }
        require_probability(weight, "source_weight");
    }
}

float WeightedMultiCropCandidateFusion::source_weight(
    const std::string_view source) const noexcept {
    const auto iterator = config_.source_weights.find(std::string{source});
    return iterator == config_.source_weights.end()
        ? config_.default_source_weight
        : iterator->second;
}

std::vector<domain::PlateCandidate> WeightedMultiCropCandidateFusion::fuse(
    const std::span<const domain::RecognitionEvidence> evidence,
    const std::span<const LayoutEvidence> layout_evidence) const {
    std::map<std::string, Aggregate> aggregate{};
    const auto total_weight = config_.recognition_weight +
                              config_.crop_quality_weight +
                              config_.candidate_margin_weight +
                              config_.layout_weight;

    for (std::size_t evidence_index = 0U; evidence_index < evidence.size(); ++evidence_index) {
        const auto& source_evidence = evidence[evidence_index];
        const auto source_factor = source_weight(source_evidence.source);
        if (source_factor <= 0.0F || !std::isfinite(source_evidence.crop_quality)) {
            continue;
        }

        const auto crop_quality = clamp01(source_evidence.crop_quality);
        const auto layout_quality = evidence_index < layout_evidence.size() &&
                                    layout_evidence[evidence_index].reliable &&
                                    std::isfinite(layout_evidence[evidence_index].confidence)
            ? clamp01(layout_evidence[evidence_index].confidence)
            : 0.0F;

        // One crop is allowed to vote for a plate only once, even if a provider
        // accidentally returns duplicate candidates in the same evidence item.
        std::map<std::string, std::pair<float, bool>> per_crop_support{};
        for (std::size_t candidate_index = 0U;
             candidate_index < source_evidence.candidates.size();
             ++candidate_index) {
            const auto& candidate = source_evidence.candidates[candidate_index];
            if (candidate.text.empty() || !std::isfinite(candidate.confidence) ||
                candidate.confidence < config_.minimum_candidate_confidence) {
                continue;
            }

            const auto recognition = clamp01(candidate.confidence);
            const auto margin = candidate_margin(source_evidence, candidate_index);
            const auto score = source_factor * clamp01((
                (recognition * config_.recognition_weight) +
                (crop_quality * config_.crop_quality_weight) +
                (margin * config_.candidate_margin_weight) +
                (layout_quality * config_.layout_weight)) / total_weight);

            auto& local = per_crop_support[candidate.text];
            if (score > local.first) {
                local.first = score;
            }
            local.second = local.second || candidate.format_valid;
        }

        for (const auto& [text, local] : per_crop_support) {
            auto& target = aggregate[text];
            // Probabilistic-OR style accumulation rewards independent agreement
            // while keeping the final score naturally bounded in [0,1].
            target.support = clamp01(
                1.0F - ((1.0F - target.support) * (1.0F - local.first)));
            target.format_valid = target.format_valid || local.second;
        }
    }

    std::vector<domain::PlateCandidate> result{};
    result.reserve(std::min(config_.max_candidates, aggregate.size()));
    for (const auto& [text, value] : aggregate) {
        result.push_back(domain::PlateCandidate{
            .text = text,
            .confidence = value.support,
            .calibrated_confidence = value.support,
            .format_valid = value.format_valid});
    }
    std::stable_sort(
        result.begin(),
        result.end(),
        [](const domain::PlateCandidate& left, const domain::PlateCandidate& right) {
            if (left.confidence != right.confidence) {
                return left.confidence > right.confidence;
            }
            return left.text < right.text;
        });
    if (result.size() > config_.max_candidates) {
        result.resize(config_.max_candidates);
    }
    return result;
}

} // namespace fac_lpr::application
