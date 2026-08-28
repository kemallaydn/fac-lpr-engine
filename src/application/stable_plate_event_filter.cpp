#include <fac_lpr/application/stable_plate_event_filter.hpp>

#include <fac_lpr/application/error.hpp>

#include <utility>

namespace fac_lpr::application {

StablePlateEventFilter::StablePlateEventFilter(StablePlateEventFilterConfig config)
    : config_(std::move(config)) {
    if (config_.duplicate_cooldown < std::chrono::milliseconds::zero()) {
        throw ConfigurationError("duplicate_cooldown cannot be negative");
    }
    if (config_.duplicate_cooldown > std::chrono::hours{24}) {
        throw ConfigurationError("duplicate_cooldown cannot exceed 24 hours");
    }
}

StablePlateEventFilterResult StablePlateEventFilter::observe(
    const TemporalConsensusResult& consensus,
    const TimePoint timestamp) {
    StablePlateEventFilterResult output{};
    if (!consensus.stable_result.has_value()) {
        return output;
    }

    const auto& current = *consensus.stable_result;
    const bool same_plate = last_emitted_.has_value() &&
                            last_emitted_->plate == current.plate;
    const bool inside_cooldown = same_plate && last_emitted_at_.has_value() &&
                                 timestamp >= *last_emitted_at_ &&
                                 timestamp - *last_emitted_at_ < config_.duplicate_cooldown;

    if (inside_cooldown) {
        output.duplicate_suppressed = true;
        ++stats_.suppressed;
        return output;
    }

    last_emitted_ = current;
    last_emitted_at_ = timestamp;
    output.emitted_result = current;
    ++stats_.emitted;
    return output;
}

void StablePlateEventFilter::reset() noexcept {
    last_emitted_.reset();
    last_emitted_at_.reset();
    stats_ = {};
}

StablePlateEventFilterStats StablePlateEventFilter::stats() const noexcept {
    return stats_;
}

} // namespace fac_lpr::application
