#include <fac_lpr/application/recognition_stream_session.hpp>

#include <fac_lpr/application/error.hpp>

#include <utility>

namespace fac_lpr::application {

RecognitionStreamSession::RecognitionStreamSession(
    std::shared_ptr<const LprPipeline> pipeline,
    TemporalPlateConsensusConfig temporal_config)
    : pipeline_(std::move(pipeline)),
      consensus_(std::move(temporal_config)) {
    if (!pipeline_) {
        throw ConfigurationError("recognition stream session requires a pipeline");
    }
}

RecognitionStreamSessionResult RecognitionStreamSession::recognize(
    const ImageView& image,
    const OperationContext& context) {
    std::scoped_lock lock{mutex_};
    return recognize_locked(image, Clock::now(), context);
}

RecognitionStreamSessionResult RecognitionStreamSession::recognize_at(
    const ImageView& image,
    const TimePoint timestamp,
    const OperationContext& context) {
    std::scoped_lock lock{mutex_};
    return recognize_locked(image, timestamp, context);
}

RecognitionStreamSessionResult RecognitionStreamSession::recognize_locked(
    const ImageView& image,
    const TimePoint timestamp,
    const OperationContext& context) {
    if (closed_) {
        throw ConfigurationError("recognition stream session is closed");
    }
    if (last_timestamp_.has_value() && timestamp < *last_timestamp_) {
        throw ConfigurationError("recognition stream timestamp must be monotonic");
    }

    auto frame_result = pipeline_->recognize(image, context);
    last_timestamp_ = timestamp;

    RecognitionStreamSessionResult output{};
    output.frame_result = std::move(frame_result);

    // Temporal consensus must never merge unrelated plates from an ambiguous frame.
    // A logical stream session therefore observes only frames containing exactly
    // one recognition result. Zero/multi-plate frames remain visible to callers
    // through frame_result but do not mutate consensus state.
    if (output.frame_result.recognitions.size() == 1U) {
        output.temporal = consensus_.observe(
            output.frame_result.recognitions.front(),
            timestamp);
        output.temporal_observation_applied = true;
    } else if (output.frame_result.recognitions.size() > 1U) {
        output.ambiguous_frame = true;
        output.temporal.history_size = consensus_.history_size();
    } else {
        output.temporal.history_size = consensus_.history_size();
    }

    return output;
}

void RecognitionStreamSession::reset() {
    std::scoped_lock lock{mutex_};
    if (closed_) {
        throw ConfigurationError("recognition stream session is closed");
    }
    consensus_.reset();
    last_timestamp_.reset();
}

void RecognitionStreamSession::close() noexcept {
    std::scoped_lock lock{mutex_};
    if (closed_) {
        return;
    }
    consensus_.reset();
    last_timestamp_.reset();
    closed_ = true;
}

bool RecognitionStreamSession::closed() const noexcept {
    std::scoped_lock lock{mutex_};
    return closed_;
}

std::size_t RecognitionStreamSession::history_size() const noexcept {
    std::scoped_lock lock{mutex_};
    return consensus_.history_size();
}

} // namespace fac_lpr::application
