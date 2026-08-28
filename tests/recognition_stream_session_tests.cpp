#include <fac_lpr/application/candidate_fusion.hpp>
#include <fac_lpr/application/confidence_calibration.hpp>
#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/recognition_stream_session.hpp>
#include <fac_lpr/application/safe_decision_policy.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace fac_lpr;

class SessionDetector final : public application::IPlateDetector {
public:
    std::vector<domain::Detection> detections{};

    std::string_view name() const noexcept override { return "session-detector"; }

    std::vector<domain::Detection> detect(
        const application::ImageView&,
        const application::OperationContext&) override {
        return detections;
    }
};

class SessionGeometry final : public application::IPlateGeometryEvaluator {
public:
    application::GeometryEvidence evaluate(
        const domain::Detection&,
        const application::OperationContext&) override {
        return {.valid = true, .score = 0.95F};
    }
};

class SessionAligner final : public application::IPlateAligner {
public:
    std::optional<application::ImageBuffer> align(
        const application::ImageView&,
        const domain::Detection&,
        const application::OperationContext&) override {
        return std::nullopt;
    }
};

class SessionCropGenerator final : public application::ICropGenerator {
public:
    std::string_view name() const noexcept override { return "session-crop"; }

    std::vector<application::CropHypothesis> generate(
        const application::ImageView&,
        const domain::Detection&,
        const std::optional<application::ImageBuffer>&,
        const application::OperationContext&) override {
        application::CropHypothesis crop{};
        crop.image.bytes.assign(16U, std::byte{128});
        crop.image.width = 4U;
        crop.image.height = 4U;
        crop.image.stride_bytes = 4U;
        crop.image.format = application::PixelFormat::gray8;
        crop.type = "rectified";
        crop.source = "session-crop";
        crop.quality = 0.95F;
        return {std::move(crop)};
    }
};

class SessionRecognizer final : public application::IPlateRecognizer {
public:
    std::string_view name() const noexcept override { return "session-ocr"; }

    domain::RecognitionEvidence recognize(
        const application::ImageView&,
        const application::OperationContext&) override {
        domain::RecognitionEvidence evidence{};
        evidence.candidates.push_back({
            .text = "34ABC123",
            .confidence = 0.95F,
            .calibrated_confidence = 0.0F,
            .format_valid = true});
        return evidence;
    }
};

class SessionLayout final : public application::IPlateLayoutAnalyzer {
public:
    application::LayoutEvidence analyze(
        const application::ImageView&,
        std::span<const domain::PlateCandidate>,
        const application::OperationContext&) override {
        return {
            .reliable = true,
            .character_count = 8,
            .letter_group_size = 3,
            .confidence = 0.95F};
    }
};

domain::Detection detection(float x) {
    domain::Detection value{};
    value.bbox = {x, 1.0F, 6.0F, 3.0F};
    value.confidence = 0.95F;
    value.provider = "session-detector";
    return value;
}

std::shared_ptr<application::LprPipeline> make_pipeline(
    const std::shared_ptr<SessionDetector>& detector) {
    application::LprPipelineDependencies dependencies{};
    dependencies.detector = detector;
    dependencies.geometry = std::make_shared<SessionGeometry>();
    dependencies.aligner = std::make_shared<SessionAligner>();
    dependencies.crop_generator = std::make_shared<SessionCropGenerator>();
    dependencies.recognition_ensemble = std::make_shared<application::RecognitionEnsemble>(
        std::vector<application::RecognizerRegistration>{{
            .provider = std::make_shared<SessionRecognizer>(),
            .weight = 1.0F,
            .required = true}});
    dependencies.calibrator = std::make_shared<application::IdentityConfidenceCalibrator>();
    dependencies.layout_analyzer = std::make_shared<SessionLayout>();
    dependencies.candidate_fusion = std::make_shared<application::WeightedMultiCropCandidateFusion>();
    dependencies.decision_policy = std::make_shared<application::SafeRecognitionDecisionPolicy>();
    return std::make_shared<application::LprPipeline>(std::move(dependencies));
}

application::ImageView image(std::vector<std::byte>& storage) {
    storage.assign(64U, std::byte{64});
    return {storage, 8U, 8U, 8U, application::PixelFormat::gray8};
}

application::TemporalPlateConsensusConfig temporal_config() {
    application::TemporalPlateConsensusConfig config{};
    config.max_history = 4U;
    config.minimum_supporting_frames = 2U;
    config.minimum_frame_confidence = 0.50F;
    config.stable_confidence_threshold = 0.75F;
    return config;
}

TEST(RecognitionStreamSession, RequiresPipeline) {
    EXPECT_THROW(
        application::RecognitionStreamSession(nullptr, temporal_config()),
        application::ConfigurationError);
}

TEST(RecognitionStreamSession, ProducesStableResultAfterRepeatedSinglePlateFrames) {
    auto detector = std::make_shared<SessionDetector>();
    detector->detections = {detection(1.0F)};
    application::RecognitionStreamSession session(make_pipeline(detector), temporal_config());

    std::vector<std::byte> storage{};
    const auto view = image(storage);
    const auto base = application::RecognitionStreamSession::Clock::now();

    const auto first = session.recognize_at(view, base);
    EXPECT_TRUE(first.temporal_observation_applied);
    EXPECT_FALSE(first.temporal.stable_result.has_value());

    const auto second = session.recognize_at(view, base + std::chrono::milliseconds{10});
    ASSERT_TRUE(second.temporal.stable_result.has_value());
    EXPECT_EQ(second.temporal.stable_result->plate, "34ABC123");
    EXPECT_EQ(second.temporal.supporting_frames, 2U);
}

TEST(RecognitionStreamSession, IndependentSessionsDoNotShareHistory) {
    auto detector = std::make_shared<SessionDetector>();
    detector->detections = {detection(1.0F)};
    const auto pipeline = make_pipeline(detector);
    application::RecognitionStreamSession left(pipeline, temporal_config());
    application::RecognitionStreamSession right(pipeline, temporal_config());

    std::vector<std::byte> storage{};
    const auto view = image(storage);
    const auto now = application::RecognitionStreamSession::Clock::now();

    static_cast<void>(left.recognize_at(view, now));
    static_cast<void>(left.recognize_at(view, now + std::chrono::milliseconds{1}));
    static_cast<void>(right.recognize_at(view, now));

    EXPECT_EQ(left.history_size(), 2U);
    EXPECT_EQ(right.history_size(), 1U);
}

TEST(RecognitionStreamSession, ResetClearsHistoryAndTimestampState) {
    auto detector = std::make_shared<SessionDetector>();
    detector->detections = {detection(1.0F)};
    application::RecognitionStreamSession session(make_pipeline(detector), temporal_config());

    std::vector<std::byte> storage{};
    const auto view = image(storage);
    const auto now = application::RecognitionStreamSession::Clock::now();
    static_cast<void>(session.recognize_at(view, now));
    ASSERT_EQ(session.history_size(), 1U);

    session.reset();
    EXPECT_EQ(session.history_size(), 0U);
    EXPECT_NO_THROW(static_cast<void>(session.recognize_at(view, now - std::chrono::seconds{1})));
}

TEST(RecognitionStreamSession, RejectsOutOfOrderTimestampWithoutMutatingHistory) {
    auto detector = std::make_shared<SessionDetector>();
    detector->detections = {detection(1.0F)};
    application::RecognitionStreamSession session(make_pipeline(detector), temporal_config());

    std::vector<std::byte> storage{};
    const auto view = image(storage);
    const auto now = application::RecognitionStreamSession::Clock::now();
    static_cast<void>(session.recognize_at(view, now));

    EXPECT_THROW(
        static_cast<void>(session.recognize_at(view, now - std::chrono::milliseconds{1})),
        application::ConfigurationError);
    EXPECT_EQ(session.history_size(), 1U);
}

TEST(RecognitionStreamSession, AmbiguousMultiPlateFrameDoesNotPolluteConsensus) {
    auto detector = std::make_shared<SessionDetector>();
    detector->detections = {detection(1.0F), detection(10.0F)};
    application::RecognitionStreamSession session(make_pipeline(detector), temporal_config());

    std::vector<std::byte> storage{};
    const auto result = session.recognize(image(storage));

    EXPECT_TRUE(result.ambiguous_frame);
    EXPECT_FALSE(result.temporal_observation_applied);
    EXPECT_EQ(result.frame_result.recognitions.size(), 2U);
    EXPECT_EQ(session.history_size(), 0U);
}

TEST(RecognitionStreamSession, ConcurrentCallsAreSerializedAndHistoryRemainsBounded) {
    auto detector = std::make_shared<SessionDetector>();
    detector->detections = {detection(1.0F)};
    application::RecognitionStreamSession session(make_pipeline(detector), temporal_config());

    std::vector<std::byte> storage{};
    const auto view = image(storage);
    const auto timestamp = application::RecognitionStreamSession::Clock::now();
    std::atomic<std::size_t> completed{0U};
    std::atomic<std::size_t> failed{0U};
    std::vector<std::thread> workers{};
    workers.reserve(8U);

    for (std::size_t index = 0U; index < 8U; ++index) {
        workers.emplace_back([&]() {
            try {
                const auto result = session.recognize_at(view, timestamp);
                if (result.temporal_observation_applied) {
                    completed.fetch_add(1U, std::memory_order_relaxed);
                }
            } catch (...) {
                failed.fetch_add(1U, std::memory_order_relaxed);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    EXPECT_EQ(failed.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(completed.load(std::memory_order_relaxed), 8U);
    EXPECT_EQ(session.history_size(), temporal_config().max_history);
    EXPECT_LE(session.history_size(), temporal_config().max_history);
}

TEST(RecognitionStreamSession, CloseIsIdempotentAndRejectsFurtherWork) {
    auto detector = std::make_shared<SessionDetector>();
    detector->detections = {detection(1.0F)};
    application::RecognitionStreamSession session(make_pipeline(detector), temporal_config());

    session.close();
    session.close();
    EXPECT_TRUE(session.closed());
    EXPECT_EQ(session.history_size(), 0U);

    std::vector<std::byte> storage{};
    EXPECT_THROW(
        static_cast<void>(session.recognize(image(storage))),
        application::ConfigurationError);
    EXPECT_THROW(session.reset(), application::ConfigurationError);
}

} // namespace
