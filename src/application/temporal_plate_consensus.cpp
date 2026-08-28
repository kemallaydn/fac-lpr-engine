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
    for (const auto& observation : history_) {
        const auto& result = observation.result;
        if (result.plate.empty() || result.status != domain::RecognitionStatus::accepted ||
            !std::isfinite(result.confidence) ||
            result.confidence < config_.minimum_frame_confidence || result.confidence > 1.0F) {
            continue;
        }
        total_eligible_weight += result.confidence;
        auto& aggregate = aggregates[result.plate];
        aggregate.weighted_support += result.confidence;
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
    if (winner->second.frames < config_.minimum_supporting_frames ||
        output.support < config_.stable_confidence_threshold || !has_margin ||
        winner->second.best == nullptr) {
        return output;
    }

    auto stable = *winner->second.best;
    stable.plate = winner->first;
    stable.confidence = output.support;
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
