#include <fac_lpr/application/candidate_fusion.hpp>
#include <fac_lpr/application/confidence_calibration.hpp>
#include <fac_lpr/application/recognition_stream_session.hpp>
#include <fac_lpr/application/safe_decision_policy.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace fac_lpr;

class EmissionDetector final : public application::IPlateDetector {
public:
    std::string_view name() const noexcept override { return "emission-detector"; }
    std::vector<domain::Detection> detect(
        const application::ImageView&,
        const application::OperationContext&) override {
        domain::Detection value{};
        value.bbox = {1.0F, 1.0F, 6.0F, 3.0F};
        value.confidence = 0.95F;
        value.provider = "emission-detector";
        return {value};
    }
};

class EmissionGeometry final : public application::IPlateGeometryEvaluator {
public:
    application::GeometryEvidence evaluate(
        const domain::Detection&,
        const application::OperationContext&) override {
        return {.valid = true, .score = 0.95F};
    }
};

class EmissionAligner final : public application::IPlateAligner {
public:
    std::optional<application::ImageBuffer> align(
        const application::ImageView&,
        const domain::Detection&,
        const application::OperationContext&) override {
        return std::nullopt;
    }
};

class EmissionCrop final : public application::ICropGenerator {
public:
    std::string_view name() const noexcept override { return "emission-crop"; }
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
        crop.source = "emission-crop";
        crop.quality = 0.95F;
        return {std::move(crop)};
    }
};

class EmissionRecognizer final : public application::IPlateRecognizer {
public:
    std::string_view name() const noexcept override { return "emission-ocr"; }
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

class EmissionLayout final : public application::IPlateLayoutAnalyzer {
public:
    application::LayoutEvidence analyze(
        const application::ImageView&,
        std::span<const domain::PlateCandidate>,
        const application::OperationContext&) override {
        return {.reliable = true, .character_count = 8, .letter_group_size = 3, .confidence = 0.95F};
    }
};

std::shared_ptr<application::LprPipeline> pipeline() {
    application::LprPipelineDependencies dependencies{};
    dependencies.detector = std::make_shared<EmissionDetector>();
    dependencies.geometry = std::make_shared<EmissionGeometry>();
    dependencies.aligner = std::make_shared<EmissionAligner>();
    dependencies.crop_generator = std::make_shared<EmissionCrop>();
    dependencies.recognition_ensemble = std::make_shared<application::RecognitionEnsemble>(
        std::vector<application::RecognizerRegistration>{{
            .provider = std::make_shared<EmissionRecognizer>(), .weight = 1.0F, .required = true}});
    dependencies.calibrator = std::make_shared<application::IdentityConfidenceCalibrator>();
    dependencies.layout_analyzer = std::make_shared<EmissionLayout>();
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
    config.minimum_supporting_frames = 2U;
    return config;
}

TEST(RecognitionStreamEmission, SessionOwnsDuplicateSuppressionState) {
    application::StablePlateEventFilterConfig emission{};
    emission.duplicate_cooldown = std::chrono::seconds{2};
    application::RecognitionStreamSession session{pipeline(), temporal_config(), emission};

    std::vector<std::byte> storage{};
    const auto view = image(storage);
    const auto now = application::RecognitionStreamSession::Clock::now();

    const auto first = session.recognize_at(view, now);
    EXPECT_FALSE(first.emission.emitted_result.has_value());

    const auto second = session.recognize_at(view, now + std::chrono::milliseconds{10});
    ASSERT_TRUE(second.emission.emitted_result.has_value());
    EXPECT_EQ(second.emission.emitted_result->plate, "34ABC123");

    const auto third = session.recognize_at(view, now + std::chrono::milliseconds{20});
    EXPECT_TRUE(third.emission.duplicate_suppressed);
    EXPECT_EQ(session.emission_stats().emitted, 1U);
    EXPECT_EQ(session.emission_stats().suppressed, 1U);
}

TEST(RecognitionStreamEmission, IndependentSessionsDoNotShareEmissionCooldown) {
    auto shared_pipeline = pipeline();
    application::RecognitionStreamSession left{shared_pipeline, temporal_config()};
    application::RecognitionStreamSession right{shared_pipeline, temporal_config()};

    std::vector<std::byte> storage{};
    const auto view = image(storage);
    const auto now = application::RecognitionStreamSession::Clock::now();

    static_cast<void>(left.recognize_at(view, now));
    const auto left_stable = left.recognize_at(view, now + std::chrono::milliseconds{10});
    ASSERT_TRUE(left_stable.emission.emitted_result.has_value());

    static_cast<void>(right.recognize_at(view, now));
    const auto right_stable = right.recognize_at(view, now + std::chrono::milliseconds{10});
    EXPECT_TRUE(right_stable.emission.emitted_result.has_value());
    EXPECT_FALSE(right_stable.emission.duplicate_suppressed);
}

TEST(RecognitionStreamEmission, ResetClearsConsensusAndRearmsEmission) {
    application::RecognitionStreamSession session{pipeline(), temporal_config()};
    std::vector<std::byte> storage{};
    const auto view = image(storage);
    const auto now = application::RecognitionStreamSession::Clock::now();

    static_cast<void>(session.recognize_at(view, now));
    ASSERT_TRUE(session.recognize_at(view, now + std::chrono::milliseconds{10}).emission.emitted_result.has_value());

    session.reset();
    EXPECT_EQ(session.emission_stats().emitted, 0U);
    EXPECT_EQ(session.history_size(), 0U);

    static_cast<void>(session.recognize_at(view, now + std::chrono::milliseconds{20}));
    const auto rearmed = session.recognize_at(view, now + std::chrono::milliseconds{30});
    EXPECT_TRUE(rearmed.emission.emitted_result.has_value());
}

} // namespace
