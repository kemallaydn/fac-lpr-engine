#pragma once

#include <fac_lpr/application/temporal_plate_consensus.hpp>

#include <chrono>
#include <cstddef>
#include <optional>

namespace fac_lpr::application {

struct StablePlateEventFilterConfig final {
    std::chrono::milliseconds duplicate_cooldown{2000};
};

struct StablePlateEventFilterStats final {
    std::size_t emitted{0U};
    std::size_t suppressed{0U};
};

struct StablePlateEventFilterResult final {
    std::optional<domain::PlateRecognitionResult> emitted_result{};
    bool duplicate_suppressed{false};
};

class StablePlateEventFilter final {
public:
    using Clock = TemporalPlateConsensus::Clock;
    using TimePoint = TemporalPlateConsensus::TimePoint;

    explicit StablePlateEventFilter(StablePlateEventFilterConfig config = {});

    [[nodiscard]] StablePlateEventFilterResult observe(
        const TemporalConsensusResult& consensus,
        TimePoint timestamp = Clock::now());

    void reset() noexcept;
    [[nodiscard]] StablePlateEventFilterStats stats() const noexcept;

private:
    StablePlateEventFilterConfig config_{};
    std::optional<domain::PlateRecognitionResult> last_emitted_{};
    std::optional<TimePoint> last_emitted_at_{};
    StablePlateEventFilterStats stats_{};
};

} // namespace fac_lpr::application
