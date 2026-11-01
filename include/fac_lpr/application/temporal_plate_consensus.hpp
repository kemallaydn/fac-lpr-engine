#pragma once

#include <fac_lpr/domain/recognition.hpp>

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>

namespace fac_lpr::application {

struct TemporalPlateConsensusConfig final {
    std::size_t max_history{8U};
    std::chrono::milliseconds history_ttl{1500};
    std::size_t minimum_supporting_frames{2U};
    float minimum_frame_confidence{0.50F};
    float stable_confidence_threshold{0.75F};
    float conflict_margin{0.10F};
    // Opt-in: only independent Paddle + native LPRNet beam REVIEW suggestions
    // may become stable after three distinct, healthy observations.
    bool enable_review_corroboration{false};
    std::size_t minimum_review_supporting_frames{3U};
    float minimum_review_lpr_confidence{0.78F};
    float minimum_review_paddle_confidence{0.85F};
};

struct TemporalConsensusResult final {
    std::optional<domain::PlateRecognitionResult> stable_result{};
    std::size_t history_size{0U};
    std::size_t supporting_frames{0U};
    float support{0.0F};
};

class TemporalPlateConsensus final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit TemporalPlateConsensus(TemporalPlateConsensusConfig config = {});

    [[nodiscard]] TemporalConsensusResult observe(
        const domain::PlateRecognitionResult& frame_result,
        TimePoint timestamp = Clock::now());

    void reset() noexcept;
    [[nodiscard]] std::size_t history_size() const noexcept;

private:
    struct Observation final {
        domain::PlateRecognitionResult result{};
        TimePoint timestamp{};
    };

    void expire(TimePoint now) noexcept;

    TemporalPlateConsensusConfig config_{};
    std::deque<Observation> history_{};
};

} // namespace fac_lpr::application
