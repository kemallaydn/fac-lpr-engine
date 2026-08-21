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
}

TEST(ConfidenceCalibration, RejectsNonFiniteInput) {
    const IdentityConfidenceCalibrator calibrator{};
    EXPECT_THROW(
        calibrator.calibrate("lprnet", std::numeric_limits<float>::quiet_NaN(), 0.5F, "raw"),
        ProviderError);
}

} // namespace
