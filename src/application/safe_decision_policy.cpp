#include <fac_lpr/application/safe_decision_policy.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace fac_lpr::application {
namespace {

[[nodiscard]] float effective_confidence(const domain::PlateCandidate& candidate) {
    const auto calibrated = candidate.calibrated_confidence;
    const auto value = calibrated > 0.0F ? calibrated : candidate.confidence;
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
        throw ProviderError("decision policy received invalid candidate confidence");
    }
    return value;
}

} // namespace

SafeRecognitionDecisionPolicy::SafeRecognitionDecisionPolicy(DecisionConfig config)
    : config_(config) {
    EngineConfig validation{};
    validation.decision = config_;
    validate_engine_config(validation);
}

domain::PlateRecognitionResult SafeRecognitionDecisionPolicy::decide(
    const domain::Detection& detection,
    const std::span<const domain::RecognitionEvidence> evidence,
    const std::span<const domain::PlateCandidate> fused_candidates,
    const RecognitionDecisionContext& context) const {
    domain::PlateRecognitionResult result{};
    result.bbox = detection.bbox;
    result.quadrilateral = detection.quadrilateral;
    result.detector_confidence = detection.confidence;
    result.geometry_score = detection.geometry_score;
    result.evidence.assign(evidence.begin(), evidence.end());
    result.alternatives.assign(fused_candidates.begin(), fused_candidates.end());
    result.degraded = context.degraded || context.provider_failure_count > 0U;

    for (const auto& item : evidence) {
        if (!std::isfinite(item.crop_quality) || item.crop_quality < 0.0F || item.crop_quality > 1.0F) {
            throw ProviderError("decision policy received invalid crop quality");
        }
        result.crop_quality = std::max(result.crop_quality, item.crop_quality);
    }

    std::vector<const domain::PlateCandidate*> valid;
    valid.reserve(fused_candidates.size());
    for (const auto& candidate : fused_candidates) {
        (void)effective_confidence(candidate);
        if (candidate.format_valid && !candidate.text.empty()) {
            valid.push_back(&candidate);
        }
    }
    std::stable_sort(valid.begin(), valid.end(), [](const auto* left, const auto* right) {
        const auto left_confidence = effective_confidence(*left);
        const auto right_confidence = effective_confidence(*right);
        if (left_confidence != right_confidence) {
            return left_confidence > right_confidence;
        }
        return left->text < right->text;
    });

    if (!valid.empty()) {
        result.plate = valid.front()->text;
        result.confidence = effective_confidence(*valid.front());
    }

    if (context.fatal_provider_failure) {
        result.status = domain::RecognitionStatus::rejected;
        result.decision_reasons.push_back(domain::RecognitionDecisionReason::fatal_provider_failure);
        return result;
    }
    if (!std::isfinite(detection.confidence) ||
        detection.confidence < config_.minimum_effective_detector_confidence) {
        result.status = domain::RecognitionStatus::rejected;
        result.decision_reasons.push_back(domain::RecognitionDecisionReason::detector_confidence_below_minimum);
        return result;
    }
    if (valid.empty()) {
        result.status = domain::RecognitionStatus::rejected;
        result.decision_reasons.push_back(domain::RecognitionDecisionReason::no_valid_candidate);
        return result;
    }
    if (result.confidence < config_.review_confidence_threshold) {
        result.status = domain::RecognitionStatus::rejected;
        result.decision_reasons.push_back(domain::RecognitionDecisionReason::candidate_confidence_below_review);
        return result;
    }

    bool review = false;
    if (!std::isfinite(detection.geometry_score) || detection.geometry_score < config_.minimum_geometry_score) {
        review = true;
        result.decision_reasons.push_back(domain::RecognitionDecisionReason::geometry_below_minimum);
    }
    if (result.crop_quality < config_.minimum_crop_quality) {
        review = true;
        result.decision_reasons.push_back(domain::RecognitionDecisionReason::crop_quality_below_minimum);
    }
    if (result.confidence < config_.accepted_confidence_threshold) {
        review = true;
        result.decision_reasons.push_back(domain::RecognitionDecisionReason::candidate_confidence_below_accept);
    }

    if (valid.size() > 1U && valid[1]->text != valid[0]->text &&
        effective_confidence(*valid[1]) >= config_.strong_conflict_threshold) {
        review = true;
        result.decision_reasons.push_back(domain::RecognitionDecisionReason::conflicting_strong_candidates);
    }
    if (result.degraded) {
        review = true;
        result.decision_reasons.push_back(domain::RecognitionDecisionReason::degraded_provider_set);
    }

    if (review) {
        result.status = domain::RecognitionStatus::review;
        return result;
    }

    result.status = domain::RecognitionStatus::accepted;
    result.decision_reasons.push_back(domain::RecognitionDecisionReason::accepted_consensus);
    return result;
}

} // namespace fac_lpr::application
