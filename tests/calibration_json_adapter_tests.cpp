#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/config/calibration_json_adapter.hpp>

#include <gtest/gtest.h>

namespace {

TEST(CalibrationJsonAdapter, LoadsFitOutputIntoRuntimeCalibrator) {
    constexpr auto json = R"json({
      "schemaVersion": 1,
      "modelVersion": "ocr-v7",
      "datasetVersion": "validation-2026-08",
      "minimumSamples": 100,
      "probabilityEpsilon": 0.00001,
      "segments": [
        {
          "provider": "lprnet",
          "cropType": "rectified",
          "slope": 1.4,
          "intercept": -0.2,
          "sampleCount": 240,
          "metrics": {"brier": 0.08, "ece": 0.03}
        }
      ],
      "rejectedSegments": []
    })json";

    const auto loaded = fac_lpr::infrastructure::load_logistic_calibration_json(json);
    ASSERT_EQ(loaded.model_version, "ocr-v7");
    ASSERT_EQ(loaded.dataset_version, "validation-2026-08");
    ASSERT_EQ(loaded.segments.size(), 1U);
    EXPECT_EQ(loaded.segments.front().provider, "lprnet");
    EXPECT_EQ(loaded.segments.front().sample_count, 240U);

    fac_lpr::application::LogisticConfidenceCalibrator calibrator{
        loaded.config, loaded.segments};
    const auto calibrated = calibrator.calibrate("lprnet", 0.8F, 1.0F, "rectified");
    EXPECT_GT(calibrated, 0.0F);
    EXPECT_LT(calibrated, 1.0F);
}

TEST(CalibrationJsonAdapter, RejectsMissingDatasetOrModelVersion) {
    constexpr auto json = R"json({
      "schemaVersion": 1,
      "modelVersion": "",
      "datasetVersion": "validation",
      "minimumSamples": 100,
      "probabilityEpsilon": 0.00001,
      "segments": [{"provider":"lprnet","cropType":"","slope":1.0,"intercept":0.0,"sampleCount":100}]
    })json";
    EXPECT_THROW(
        fac_lpr::infrastructure::load_logistic_calibration_json(json),
        fac_lpr::application::ConfigurationError);
}

TEST(CalibrationJsonAdapter, RejectsNonPositiveFittedSlope) {
    constexpr auto json = R"json({
      "schemaVersion": 1,
      "modelVersion": "ocr-v7",
      "datasetVersion": "validation",
      "minimumSamples": 100,
      "probabilityEpsilon": 0.00001,
      "segments": [{"provider":"lprnet","cropType":"","slope":0.0,"intercept":0.0,"sampleCount":100}]
    })json";
    EXPECT_THROW(
        fac_lpr::infrastructure::load_logistic_calibration_json(json),
        fac_lpr::application::ConfigurationError);
}

} // namespace
