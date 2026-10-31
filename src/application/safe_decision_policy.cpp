#include <fac_lpr/application/safe_decision_policy.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/turkish_plate_grammar.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
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

// Compare only raw CTC characters with their own scores. Grammar/beam-search
// candidates can differ from greedy output, so their positions cannot reuse
// these scores. Provider score scales are not combined or treated as calibrated
// probabilities: a strong disagreement merely prevents automatic acceptance.
[[nodiscard]] bool has_strong_character_disagreement(
    const std::span<const domain::RecognitionEvidence> evidence,
    const std::string_view selected_plate,
    const float threshold) {
    for (const auto& primary : evidence) {
        if (primary.greedy_text.empty() || primary.greedy_text != selected_plate ||
            primary.greedy_text.size() != primary.greedy_character_confidences.size()) {
            continue;
        }
        for (const auto& secondary : evidence) {
            if (secondary.source == primary.source) {
                continue;
            }
            for (const auto& candidate : secondary.candidates) {
                if (candidate.text.size() != primary.greedy_text.size() ||
                    effective_confidence(candidate) < threshold) {
                    continue;
                }
                for (std::size_t index = 0U; index < primary.greedy_text.size(); ++index) {
                    const auto char_confidence = primary.greedy_character_confidences[index];
                    if (primary.greedy_text[index] != candidate.text[index] &&
                        std::isfinite(char_confidence) && char_confidence >= threshold) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}


// PaddleOCR is a secondary source with uncalibrated confidence. Gather only
// grammar-valid, high-confidence suggestions, without turning them into fused
// probabilities. Conflicting crop readings must not create an arbitrary winner.
struct PaddleReview final {
    std::optional<domain::PlateCandidate> unanimous{};
    bool ambiguous{false};
    bool disagrees{false};
};

[[nodiscard]] PaddleReview paddle_review_evidence(
    const std::span<const domain::RecognitionEvidence> evidence,
    const std::string_view selected,
    const float conflict_threshold) {
    PaddleReview review{};
    const TurkishPlateGrammar grammar{};
    constexpr float suggestion_threshold = 0.85F;
    for (const auto& source : evidence) {
        if (source.source != "paddleocr") {
            continue;
        }
        for (const auto& candidate : source.candidates) {
            // Compare raw scores only within PaddleOCR; never add Paddle's
            // uncalibrated confidence to LPRNet's candidate confidence.
            const float score = candidate.confidence;
            if (!std::isfinite(score) || score < 0.0F || score > 1.0F) {
                throw ProviderError("invalid PaddleOCR candidate confidence");
            }
            if (score < conflict_threshold ||
                !grammar.is_valid_normalized(candidate.text)) {
                continue;
            }
            if (!selected.empty() && candidate.text != selected) {
                review.disagrees = true;
            }
            if (score < suggestion_threshold) {
                continue;
            }
            if (review.unanimous && review.unanimous->text != candidate.text) {
                review.ambiguous = true;
            } else if (!review.unanimous || score > review.unanimous->confidence) {
                review.unanimous = domain::PlateCandidate{
                    .text = candidate.text,
                    .confidence = score,
                    .calibrated_confidence = 0.0F,
                    .format_valid = true};
            }
        }
    }
    return review;
}

// A position score can only be used for the exact greedy text that produced
// it. If any aligned LPR crop is confident in the disputed position, do not
// silently replace that character.
[[nodiscard]] bool weak_single_character_disagreement(
    const std::span<const domain::RecognitionEvidence> evidence,
    const std::string_view primary,
    const std::string_view secondary) {
    if (primary.size() != secondary.size() || primary.empty()) {
        return false;
    }
    std::optional<std::size_t> difference{};
    for (std::size_t index = 0U; index < primary.size(); ++index) {
        if (primary[index] != secondary[index]) {
            if (difference) {
                return false;
            }
            difference = index;
        }
    }
    if (!difference) {
        return false;
    }
    bool has_aligned_greedy = false;
    for (const auto& source : evidence) {
        if (source.source != "lprnet_onnx" && source.source != "lprnet") {
            continue;
        }
        if (source.greedy_text != primary ||
            source.greedy_character_confidences.size() != primary.size()) {
            continue;
        }
        has_aligned_greedy = true;
        const float confidence = source.greedy_character_confidences[*difference];
        if (!std::isfinite(confidence) || confidence >= 0.65F ||
            confidence < 0.0F) {
            return false;
        }
    }
    return has_aligned_greedy;
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

    // Opt-in second opinion: a single grammar-valid Paddle reading can be
    // exposed as a REVIEW-only candidate when LPRNet is missing/weak, or a
    // single aligned greedy character is explicitly uncertain. Never upgrade
    // to ACCEPTED or claim the two providers' scores are calibrated alike.
    bool paddle_disagreement = false;
    if (config_.enable_paddle_review_suggestions) {
        const auto paddle = paddle_review_evidence(
            evidence, result.plate, config_.strong_conflict_threshold);
        paddle_disagreement = paddle.disagrees || paddle.ambiguous;
        const bool primary_ambiguous =
            valid.size() > 1U && valid[1]->text != valid.front()->text &&
            effective_confidence(*valid[1]) >= config_.strong_conflict_threshold;
        if (!paddle.ambiguous && paddle.unanimous &&
            paddle.unanimous->text != result.plate && !primary_ambiguous &&
            (valid.empty() || result.confidence < config_.accepted_confidence_threshold ||
             weak_single_character_disagreement(
                 evidence, result.plate, paddle.unanimous->text))) {
            result.plate = paddle.unanimous->text;
            result.confidence = 0.0F; // not calibrated as LPRNet confidence
            result.alternatives.push_back(*paddle.unanimous);
            result.status = domain::RecognitionStatus::review;
            result.decision_reasons.push_back(
                domain::RecognitionDecisionReason::secondary_ocr_review_suggestion);
            if (result.degraded) {
                result.decision_reasons.push_back(
                    domain::RecognitionDecisionReason::degraded_provider_set);
            }
            return result;
        }
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
    if (paddle_disagreement || has_strong_character_disagreement(
            evidence, result.plate, config_.strong_conflict_threshold)) {
        review = true;
        if (std::find(result.decision_reasons.begin(), result.decision_reasons.end(),
                      domain::RecognitionDecisionReason::conflicting_strong_candidates) ==
            result.decision_reasons.end()) {
            result.decision_reasons.push_back(
                domain::RecognitionDecisionReason::conflicting_strong_candidates);
        }
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
