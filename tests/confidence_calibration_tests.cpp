#include <fac_lpr/application/confidence_calibration.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace {
using namespace fac_lpr::application;

TEST(ConfidenceCalibration, IdentityLeavesConfidenceUnchangedWithoutDataset) {
    const IdentityConfidenceCalibrator calibrator{};
    EXPECT_FLOAT_EQ(calibrator.calibrate("lprnet", 0.73F, 0.5F, "rectified"), 0.73F);
}

TEST(ConfidenceCalibration, MissingOrInsufficientSegmentFallsBackToIdentity) {
    LogisticCalibrationConfig config{};
    config.minimum_samples = 100U;
    LogisticConfidenceCalibrator calibrator{
        config,
        {{"lprnet", "rectified", 1.5F, -0.2F, 20U}}};
    EXPECT_FLOAT_EQ(calibrator.calibrate("lprnet", 0.8F, 0.7F, "rectified"), 0.8F);
    EXPECT_FLOAT_EQ(calibrator.calibrate("other", 0.8F, 0.7F, "rectified"), 0.8F);
}

TEST(ConfidenceCalibration, ExactContextSegmentOverridesProviderFallback) {
    LogisticCalibrationConfig config{};
    config.minimum_samples = 10U;
    LogisticConfidenceCalibrator exact{
        config,
        {
            {"lprnet", "", 1.0F, -1.0F, 100U},
            {"lprnet", "rectified", 1.0F, 1.0F, 100U},
        }};
    const auto context_value = exact.calibrate("lprnet", 0.5F, 0.8F, "rectified");
    const auto fallback_value = exact.calibrate("lprnet", 0.5F, 0.8F, "raw");
    EXPECT_GT(context_value, 0.5F);
    EXPECT_LT(fallback_value, 0.5F);
}

TEST(ConfidenceCalibration, RuntimeSegmentsProduceBoundedCalibratedProbability) {
    LogisticCalibrationConfig config{};
    config.minimum_samples = 1U;
    const std::vector<LogisticCalibrationSegment> runtime_segments{
        {"runtime-provider", "rectified", 2.0F, 0.5F, 500U}};
    const LogisticConfidenceCalibrator calibrator{config, runtime_segments};

    const auto low = calibrator.calibrate("runtime-provider", 0.0F, 0.8F, "rectified");
    const auto high = calibrator.calibrate("runtime-provider", 1.0F, 0.8F, "rectified");
    EXPECT_GE(low, 0.0F);
    EXPECT_LE(low, 1.0F);
    EXPECT_GE(high, 0.0F);
    EXPECT_LE(high, 1.0F);
}

TEST(ConfidenceCalibration, RejectsInvalidRuntimeParameters) {
    LogisticCalibrationConfig config{};
    EXPECT_THROW(
        LogisticConfidenceCalibrator(config, {{"lprnet", "", 0.0F, 0.0F, 100U}}),
        ConfigurationError);
    EXPECT_THROW(
        LogisticConfidenceCalibrator(
            config,
            {{"lprnet", "", 1.0F, std::numeric_limits<float>::infinity(), 100U}}),
        ConfigurationError);

    config.minimum_samples = 0U;
    EXPECT_THROW(LogisticConfidenceCalibrator(config, {}), ConfigurationError);

    config.minimum_samples = 100U;
    config.probability_epsilon = 0.5F;
    EXPECT_THROW(LogisticConfidenceCalibrator(config, {}), ConfigurationError);
}

TEST(ConfidenceCalibration, RejectsDuplicateProviderContextSegments) {
    LogisticCalibrationConfig config{};
    EXPECT_THROW(
        LogisticConfidenceCalibrator(
            config,
            {
                {"lprnet", "rectified", 1.0F, 0.0F, 100U},
                {"lprnet", "rectified", 1.2F, 0.1F, 200U},
            }),
        ConfigurationError);
}

TEST(ConfidenceCalibration, RejectsInvalidInputs) {
    const IdentityConfidenceCalibrator calibrator{};
    EXPECT_THROW(
        calibrator.calibrate("lprnet", std::numeric_limits<float>::quiet_NaN(), 0.5F, "raw"),
        ProviderError);
    EXPECT_THROW(
        calibrator.calibrate("lprnet", 0.5F, -0.1F, "raw"),
        ProviderError);
    EXPECT_THROW(
        calibrator.calibrate("lprnet", 0.5F, 1.1F, "raw"),
        ProviderError);
}

} // namespace
