#pragma once

#include <fac_lpr/application/lpr_pipeline.hpp>
#include <fac_lpr/application/temporal_plate_consensus.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>

namespace fac_lpr::application {

struct RecognitionStreamSessionResult final {
    LprPipelineResult frame_result{};
    TemporalConsensusResult temporal{};
    bool temporal_observation_applied{false};
    bool ambiguous_frame{false};
};

class RecognitionStreamSession final {
public:
    using Clock = TemporalPlateConsensus::Clock;
    using TimePoint = TemporalPlateConsensus::TimePoint;

    RecognitionStreamSession(
        std::shared_ptr<const LprPipeline> pipeline,
        TemporalPlateConsensusConfig temporal_config = {});

    [[nodiscard]] RecognitionStreamSessionResult recognize(
        const ImageView& image,
        const OperationContext& context = {});

    [[nodiscard]] RecognitionStreamSessionResult recognize_at(
        const ImageView& image,
        TimePoint timestamp,
        const OperationContext& context = {});

    void reset();
    void close() noexcept;

    [[nodiscard]] bool closed() const noexcept;
    [[nodiscard]] std::size_t history_size() const noexcept;

private:
    [[nodiscard]] RecognitionStreamSessionResult recognize_locked(
        const ImageView& image,
        TimePoint timestamp,
        const OperationContext& context);

    std::shared_ptr<const LprPipeline> pipeline_{};
    TemporalPlateConsensus consensus_;
    mutable std::mutex mutex_{};
    std::optional<TimePoint> last_timestamp_{};
    bool closed_{false};
};

} // namespace fac_lpr::application
