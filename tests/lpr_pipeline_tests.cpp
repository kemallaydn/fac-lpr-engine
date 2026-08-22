#include <fac_lpr/application/candidate_fusion.hpp>
#include <fac_lpr/application/confidence_calibration.hpp>
#include <fac_lpr/application/lpr_pipeline.hpp>
#include <fac_lpr/application/safe_decision_policy.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace fac_lpr;

class FakeDetector final : public application::IPlateDetector {
public:
    std::vector<domain::Detection> detections{};
    std::string_view name() const noexcept override { return "fake-detector"; }
    std::vector<domain::Detection> detect(
        const application::ImageView&,
        const application::OperationContext&) override {
        return detections;
    }
};

class FakeGeometry final : public application::IPlateGeometryEvaluator {
public:
    application::GeometryEvidence evidence{true, 0.90F};
    application::GeometryEvidence evaluate(
        const domain::Detection&,
        const application::OperationContext&) override {
        return evidence;
    }
};

class FakeAligner final : public application::IPlateAligner {
public:
    bool fail{false};
    std::optional<application::ImageBuffer> align(
        const application::ImageView&,
        const domain::Detection&,
        const application::OperationContext&) override {
        if (fail) {
            throw application::ProviderError("synthetic alignment failure");
        }
        return std::nullopt;
    }
};

class FakeCropGenerator final : public application::ICropGenerator {
public:
    std::string_view name() const noexcept override { return "fake-crop"; }
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
        crop.source = "fake-crop";
        crop.quality = 0.85F;
        return {std::move(crop)};
    }
};

class HealthyRecognizer final : public application::IPlateRecognizer {
public:
    explicit HealthyRecognizer(std::string name = "healthy") : name_(std::move(name)) {}
    std::string_view name() const noexcept override { return name_; }
    domain::RecognitionEvidence recognize(
        const application::ImageView&,
        const application::OperationContext&) override {
        domain::RecognitionEvidence evidence{};
        evidence.candidates.push_back(domain::PlateCandidate{
            .text = "34ABC123",
            .confidence = 0.95F,
            .calibrated_confidence = 0.0F,
            .format_valid = true});
        return evidence;
    }
private:
    std::string name_{};
};

class FailingRecognizer final : public application::IPlateRecognizer {
public:
    std::string_view name() const noexcept override { return "optional-failure"; }
    domain::RecognitionEvidence recognize(
        const application::ImageView&,
        const application::OperationContext&) override {
        throw application::ProviderError("synthetic recognizer failure");
    }
};

class FakeLayout final : public application::IPlateLayoutAnalyzer {
public:
    bool fail{false};
    application::LayoutEvidence analyze(
        const application::ImageView&,
        std::span<const domain::PlateCandidate>,
        const application::OperationContext&) override {
        if (fail) {
            throw application::ProviderError("synthetic layout failure");
        }
        return application::LayoutEvidence{
            .reliable = true,
            .character_count = 8,
            .letter_group_size = 3,
            .confidence = 0.90F};
    }
};

application::ImageView image_fixture(std::vector<std::byte>& bytes) {
    bytes.assign(64U, std::byte{64});
    return application::ImageView{bytes, 8U, 8U, 8U, application::PixelFormat::gray8};
}

domain::Detection detection_fixture() {
    domain::Detection detection{};
    detection.bbox = {1.0F, 1.0F, 6.0F, 3.0F};
    detection.confidence = 0.95F;
    detection.geometry_score = 0.0F;
    detection.provider = "fake-detector";
    return detection;
}

application::LprPipeline make_pipeline(
    std::shared_ptr<FakeDetector> detector,
    std::shared_ptr<FakeGeometry> geometry,
    std::shared_ptr<FakeAligner> aligner,
    std::shared_ptr<FakeLayout> layout,
    std::vector<application::RecognizerRegistration> recognizers) {
    application::LprPipelineDependencies dependencies{};
    dependencies.detector = std::move(detector);
    dependencies.geometry = std::move(geometry);
    dependencies.aligner = std::move(aligner);
    dependencies.crop_generator = std::make_shared<FakeCropGenerator>();
    dependencies.recognition_ensemble = std::make_shared<application::RecognitionEnsemble>(
        std::move(recognizers));
    dependencies.calibrator = std::make_shared<application::IdentityConfidenceCalibrator>();
    dependencies.layout_analyzer = std::move(layout);
    dependencies.candidate_fusion = std::make_shared<application::WeightedMultiCropCandidateFusion>();
    dependencies.decision_policy = std::make_shared<application::SafeRecognitionDecisionPolicy>();
    return application::LprPipeline{std::move(dependencies)};
}

TEST(LprPipeline, OrchestratesStagesAndPropagatesGeometryScore) {
    auto detector = std::make_shared<FakeDetector>();
    detector->detections = {detection_fixture()};
    auto geometry = std::make_shared<FakeGeometry>();
    auto aligner = std::make_shared<FakeAligner>();
    auto layout = std::make_shared<FakeLayout>();
    auto pipeline = make_pipeline(
        detector,
        geometry,
        aligner,
        layout,
        {{.provider = std::make_shared<HealthyRecognizer>(), .weight = 1.0F, .required = true}});

    std::vector<std::byte> bytes{};
    const auto result = pipeline.recognize(image_fixture(bytes));

    ASSERT_EQ(result.recognitions.size(), 1U);
    EXPECT_EQ(result.recognitions.front().status, domain::RecognitionStatus::accepted);
    EXPECT_EQ(result.recognitions.front().plate, "34ABC123");
    EXPECT_FLOAT_EQ(result.recognitions.front().geometry_score, 0.90F);
    EXPECT_FALSE(result.degraded);
    EXPECT_EQ(result.provider_failure_count, 0U);
    EXPECT_EQ(result.stage_timings.size(), 9U);
    for (const auto& timing : result.stage_timings) {
        EXPECT_GE(timing.latency_ms, 0.0);
    }
    EXPECT_GE(result.total_latency_ms, 0.0);
}

TEST(LprPipeline, OptionalRecognizerFailureContinuesDegraded) {
    auto detector = std::make_shared<FakeDetector>();
    detector->detections = {detection_fixture()};
    auto pipeline = make_pipeline(
        detector,
        std::make_shared<FakeGeometry>(),
        std::make_shared<FakeAligner>(),
        std::make_shared<FakeLayout>(),
        {
            {.provider = std::make_shared<FailingRecognizer>(), .weight = 1.0F, .required = false},
            {.provider = std::make_shared<HealthyRecognizer>(), .weight = 1.0F, .required = true},
        });

    std::vector<std::byte> bytes{};
    const auto result = pipeline.recognize(image_fixture(bytes));

    ASSERT_EQ(result.recognitions.size(), 1U);
    EXPECT_EQ(result.recognitions.front().status, domain::RecognitionStatus::review);
    EXPECT_TRUE(result.degraded);
    EXPECT_EQ(result.provider_failure_count, 1U);
    ASSERT_EQ(result.failures.size(), 1U);
    EXPECT_EQ(result.failures.front().provider, "optional-failure");
}

TEST(LprPipeline, AlignmentFailureUsesCropFallbackAndContinuesDegraded) {
    auto detector = std::make_shared<FakeDetector>();
    detector->detections = {detection_fixture()};
    auto aligner = std::make_shared<FakeAligner>();
    aligner->fail = true;
    auto pipeline = make_pipeline(
        detector,
        std::make_shared<FakeGeometry>(),
        aligner,
        std::make_shared<FakeLayout>(),
        {{.provider = std::make_shared<HealthyRecognizer>(), .weight = 1.0F, .required = true}});

    std::vector<std::byte> bytes{};
    const auto result = pipeline.recognize(image_fixture(bytes));

    ASSERT_EQ(result.recognitions.size(), 1U);
    EXPECT_EQ(result.recognitions.front().status, domain::RecognitionStatus::review);
    EXPECT_TRUE(result.degraded);
    ASSERT_EQ(result.failures.size(), 1U);
    EXPECT_EQ(result.failures.front().provider, "alignment");
}

TEST(LprPipeline, NoDetectionsStillReportsDetectionTiming) {
    auto detector = std::make_shared<FakeDetector>();
    auto pipeline = make_pipeline(
        detector,
        std::make_shared<FakeGeometry>(),
        std::make_shared<FakeAligner>(),
        std::make_shared<FakeLayout>(),
        {{.provider = std::make_shared<HealthyRecognizer>(), .weight = 1.0F, .required = true}});

    std::vector<std::byte> bytes{};
    const auto result = pipeline.recognize(image_fixture(bytes));

    EXPECT_TRUE(result.recognitions.empty());
    ASSERT_EQ(result.stage_timings.size(), 1U);
    EXPECT_EQ(result.stage_timings.front().stage, "detection");
}

TEST(LprPipeline, ProviderWeightIsPreservedAfterCalibration) {
    auto detector = std::make_shared<FakeDetector>();
    detector->detections = {detection_fixture()};
    auto pipeline = make_pipeline(
        detector,
        std::make_shared<FakeGeometry>(),
        std::make_shared<FakeAligner>(),
        std::make_shared<FakeLayout>(),
        {{.provider = std::make_shared<HealthyRecognizer>("weighted"), .weight = 0.50F, .required = true}});

    std::vector<std::byte> bytes{};
    const auto result = pipeline.recognize(image_fixture(bytes));

    ASSERT_EQ(result.recognitions.size(), 1U);
    ASSERT_EQ(result.recognitions.front().evidence.size(), 1U);
    ASSERT_EQ(result.recognitions.front().evidence.front().candidates.size(), 1U);
    EXPECT_FLOAT_EQ(
        result.recognitions.front().evidence.front().candidates.front().calibrated_confidence,
        0.475F);
}

} // namespace
