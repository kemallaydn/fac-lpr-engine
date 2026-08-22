#include <fac_lpr/application/engine_builder.hpp>
#include <fac_lpr/application/error.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace fac_lpr;

class FakeDetector final : public application::IPlateDetector {
public:
    explicit FakeDetector(std::string name) : name_(std::move(name)) {}
    std::string_view name() const noexcept override { return name_; }
    std::vector<domain::Detection> detect(
        const application::ImageView&,
        const application::OperationContext&) override { return {}; }
private:
    std::string name_;
};

class FakeGeometry final : public application::IPlateGeometryEvaluator {
public:
    application::GeometryEvidence evaluate(
        const domain::Detection&,
        const application::OperationContext&) override { return {true, 1.0F}; }
};

class FakeAligner final : public application::IPlateAligner {
public:
    std::optional<application::ImageBuffer> align(
        const application::ImageView&,
        const domain::Detection&,
        const application::OperationContext&) override { return std::nullopt; }
};

class FakeCropGenerator final : public application::ICropGenerator {
public:
    std::string_view name() const noexcept override { return "fake-crop"; }
    std::vector<application::CropHypothesis> generate(
        const application::ImageView&,
        const domain::Detection&,
        const std::optional<application::ImageBuffer>&,
        const application::OperationContext&) override { return {}; }
};

class FakeRecognizer final : public application::IPlateRecognizer {
public:
    explicit FakeRecognizer(std::string name) : name_(std::move(name)) {}
    std::string_view name() const noexcept override { return name_; }
    domain::RecognitionEvidence recognize(
        const application::ImageView&,
        const application::OperationContext&) override { return {}; }
private:
    std::string name_;
};

class FakeCalibrator final : public application::IConfidenceCalibrator {
public:
    float calibrate(std::string_view, float raw, float, std::string_view) const override { return raw; }
};

class FakeLayout final : public application::IPlateLayoutAnalyzer {
public:
    application::LayoutEvidence analyze(
        const application::ImageView&,
        std::span<const domain::PlateCandidate>,
        const application::OperationContext&) override { return {}; }
};

class FakeFusion final : public application::ICandidateFusion {
public:
    std::vector<domain::PlateCandidate> fuse(
        std::span<const domain::RecognitionEvidence>,
        std::span<const application::LayoutEvidence>) const override { return {}; }
};

class FakeDecision final : public application::IDecisionPolicy {
public:
    domain::PlateRecognitionResult decide(
        const domain::Detection&,
        std::span<const domain::RecognitionEvidence>,
        std::span<const domain::PlateCandidate>,
        const application::RecognitionDecisionContext&) const override { return {}; }
};

application::LprEngineBuilder complete_builder() {
    application::LprEngineBuilder builder;
    builder.detector(std::make_shared<FakeDetector>("fake-detector"))
        .geometry("fake-geometry", std::make_shared<FakeGeometry>())
        .aligner("fake-aligner", std::make_shared<FakeAligner>())
        .crop_generator(std::make_shared<FakeCropGenerator>())
        .recognizer({
            .provider = std::make_shared<FakeRecognizer>("fake-ocr"),
            .weight = 1.0F,
            .timeout = std::chrono::milliseconds{100},
            .required = true,
            .enabled = true})
        .calibrator("fake-calibrator", std::make_shared<FakeCalibrator>())
        .layout_analyzer("fake-layout", std::make_shared<FakeLayout>())
        .candidate_fusion("fake-fusion", std::make_shared<FakeFusion>())
        .decision_policy("fake-decision", std::make_shared<FakeDecision>());
    return builder;
}

TEST(LprEngineBuilder, ComposesPipelineFromFakeProviders) {
    auto builder = complete_builder();
    const auto pipeline = builder.build();
    ASSERT_NE(pipeline, nullptr);
    EXPECT_EQ(builder.registry().registrations().size(), 9U);
}

TEST(LprEngineBuilder, FailsFastWhenRequiredDependencyIsMissing) {
    application::LprEngineBuilder builder;
    builder.detector(std::make_shared<FakeDetector>("fake-detector"));
    EXPECT_THROW(builder.build(), application::ConfigurationError);
}

TEST(LprEngineBuilder, RejectsDuplicateSingletonProviderRole) {
    application::LprEngineBuilder builder;
    builder.detector(std::make_shared<FakeDetector>("detector-a"));
    EXPECT_THROW(
        builder.detector(std::make_shared<FakeDetector>("detector-b")),
        application::ConfigurationError);
}

TEST(LprEngineBuilder, RejectsDuplicateRecognizerNameWithinRole) {
    application::LprEngineBuilder builder;
    builder.recognizer({.provider = std::make_shared<FakeRecognizer>("ocr"), .enabled = true});
    EXPECT_THROW(
        builder.recognizer({.provider = std::make_shared<FakeRecognizer>("ocr"), .enabled = true}),
        application::ConfigurationError);
}

TEST(LprEngineBuilder, AllowsDistinctRecognizerNames) {
    application::LprEngineBuilder builder;
    builder.recognizer({.provider = std::make_shared<FakeRecognizer>("ocr-a"), .enabled = true});
    EXPECT_NO_THROW(
        builder.recognizer({.provider = std::make_shared<FakeRecognizer>("ocr-b"), .enabled = true}));
}

} // namespace
