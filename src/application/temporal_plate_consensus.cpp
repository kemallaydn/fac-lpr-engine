#include <fac_lpr/application/temporal_plate_consensus.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace fac_lpr::application {
namespace {

void require_probability(const float value, const char* name) {
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
        throw ConfigurationError(std::string{name} + " must be finite and in [0,1]");
    }
}

[[nodiscard]] bool review_eligible(const domain::PlateRecognitionResult& r,
                                   const TemporalPlateConsensusConfig& cfg) {
    if (!cfg.enable_review_corroboration || r.status != domain::RecognitionStatus::review ||
        r.degraded || r.plate.empty() || r.detector_confidence < 0.50F ||
        r.geometry_score < 0.50F || r.crop_quality < 0.50F ||
        !std::isfinite(r.detector_confidence) || !std::isfinite(r.geometry_score) ||
        !std::isfinite(r.crop_quality)) return false;
    if (std::find(r.decision_reasons.begin(), r.decision_reasons.end(),
            domain::RecognitionDecisionReason::secondary_ocr_review_suggestion) == r.decision_reasons.end()) return false;
    bool paddle = false, beam = false;
    for (const auto& e : r.evidence) {
        if (e.source == "paddleocr") {
            for (const auto& v : e.candidates) {
                if (v.text == r.plate && v.format_valid &&
                    std::isfinite(v.confidence) && v.confidence >= cfg.minimum_review_paddle_confidence) paddle = true;
                if (v.text != r.plate && std::isfinite(v.confidence) && v.confidence >= 0.75F) return false;
            }
        }
        if (e.source == "lprnet" || e.source == "lprnet_onnx") {
            for (const auto& v : e.candidates) {
                const float score = v.calibrated_confidence > 0.0F ? v.calibrated_confidence : v.confidence;
                if (v.text == r.plate && v.format_valid && std::isfinite(score) &&
                    score >= cfg.minimum_review_lpr_confidence) beam = true;
            }
        }
    }
    return paddle && beam;
}

struct Aggregate final {
    float weighted_support{0.0F};
    std::size_t frames{0U};
    const domain::PlateRecognitionResult* best{nullptr};
};

} // namespace

TemporalPlateConsensus::TemporalPlateConsensus(TemporalPlateConsensusConfig config)
    : config_(std::move(config)) {
    if (config_.max_history == 0U || config_.max_history > 1024U) {
        throw ConfigurationError("temporal max_history must be in [1,1024]");
    }
    if (config_.history_ttl <= std::chrono::milliseconds::zero()) {
        throw ConfigurationError("temporal history_ttl must be positive");
    }
    if (config_.minimum_supporting_frames == 0U ||
        config_.minimum_supporting_frames > config_.max_history) {
        throw ConfigurationError("temporal minimum_supporting_frames must be in [1,max_history]");
    }
    require_probability(config_.minimum_frame_confidence, "minimum_frame_confidence");
    require_probability(config_.stable_confidence_threshold, "stable_confidence_threshold");
    require_probability(config_.conflict_margin, "conflict_margin");
    require_probability(config_.minimum_review_lpr_confidence, "minimum_review_lpr_confidence");
    require_probability(config_.minimum_review_paddle_confidence, "minimum_review_paddle_confidence");
    if (config_.minimum_review_supporting_frames < 3U ||
        (config_.enable_review_corroboration && config_.minimum_review_supporting_frames > config_.max_history)) {
        throw ConfigurationError("minimum_review_supporting_frames must be in [3,max_history]");
    }
}

void TemporalPlateConsensus::expire(const TimePoint now) noexcept {
    while (!history_.empty()) {
        const auto& oldest = history_.front();
        if (now < oldest.timestamp || now - oldest.timestamp <= config_.history_ttl) {
            break;
        }
        history_.pop_front();
    }
}

TemporalConsensusResult TemporalPlateConsensus::observe(
    const domain::PlateRecognitionResult& frame_result,
    const TimePoint timestamp) {
    expire(timestamp);
    history_.push_back(Observation{frame_result, timestamp});
    while (history_.size() > config_.max_history) {
        history_.pop_front();
    }

    std::map<std::string, Aggregate> aggregates{};
    float total_eligible_weight = 0.0F;
    std::map<std::string, std::size_t> review_counts{};
    std::map<std::string, TimePoint> last_review_timestamp{};
    for (const auto& observation : history_) {
        const auto& result = observation.result;
        const bool corroborated_review = review_eligible(result, config_);
        if (corroborated_review) {
            const auto previous = last_review_timestamp.find(result.plate);
            if (previous != last_review_timestamp.end() && previous->second == observation.timestamp) {
                continue; // Same timestamp is not independent temporal evidence.
            }
            last_review_timestamp[result.plate] = observation.timestamp;
            ++review_counts[result.plate];
        }
        if (result.plate.empty() ||
            (result.status != domain::RecognitionStatus::accepted && !corroborated_review) ||
            (!corroborated_review && (!std::isfinite(result.confidence) ||
            result.confidence < config_.minimum_frame_confidence || result.confidence > 1.0F))) {
            continue;
        }
        const float weight = corroborated_review ? config_.minimum_review_lpr_confidence : result.confidence;
        total_eligible_weight += weight;
        auto& aggregate = aggregates[result.plate];
        aggregate.weighted_support += weight;
        ++aggregate.frames;
        if (aggregate.best == nullptr || result.confidence > aggregate.best->confidence) {
            aggregate.best = &result;
        }
    }

    TemporalConsensusResult output{};
    output.history_size = history_.size();
    if (aggregates.empty() || total_eligible_weight <= 0.0F) {
        return output;
    }

    auto winner = aggregates.begin();
    float runner_up_support = 0.0F;
    for (auto iterator = aggregates.begin(); iterator != aggregates.end(); ++iterator) {
        if (iterator->second.weighted_support > winner->second.weighted_support ||
            (iterator->second.weighted_support == winner->second.weighted_support && iterator->first < winner->first)) {
            runner_up_support = std::max(runner_up_support, winner->second.weighted_support);
            winner = iterator;
        } else if (iterator != winner) {
            runner_up_support = std::max(runner_up_support, iterator->second.weighted_support);
        }
    }

    output.supporting_frames = winner->second.frames;
    output.support = std::clamp(winner->second.weighted_support / total_eligible_weight, 0.0F, 1.0F);
    const auto runner_up_ratio = std::clamp(runner_up_support / total_eligible_weight, 0.0F, 1.0F);
    const auto has_margin = output.support - runner_up_ratio >= config_.conflict_margin;
    if (winner->second.best != nullptr &&
        winner->second.best->status == domain::RecognitionStatus::review &&
        review_counts[winner->first] < config_.minimum_review_supporting_frames) return output;
    if (winner->second.frames < config_.minimum_supporting_frames ||
        output.support < config_.stable_confidence_threshold || !has_margin ||
        winner->second.best == nullptr) {
        return output;
    }

    auto stable = *winner->second.best;
    stable.plate = winner->first;
    // Keep the best frame's OCR confidence; consensus agreement is output.support.
    stable.status = domain::RecognitionStatus::accepted;
    stable.decision_reasons.clear();
    stable.decision_reasons.push_back(domain::RecognitionDecisionReason::accepted_consensus);
    output.stable_result = std::move(stable);
    return output;
}

void TemporalPlateConsensus::reset() noexcept {
    history_.clear();
}

std::size_t TemporalPlateConsensus::history_size() const noexcept {
    return history_.size();
}

} // namespace fac_lpr::application
