#pragma once

#include <fac_lpr/application/confidence_calibration.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fac_lpr::infrastructure {

struct LoadedCalibration final {
    std::string model_version{};
    std::string dataset_version{};
    application::LogisticCalibrationConfig config{};
    std::vector<application::LogisticCalibrationSegment> segments{};
};

[[nodiscard]] LoadedCalibration load_logistic_calibration_json(std::string_view json_text);
[[nodiscard]] LoadedCalibration load_logistic_calibration_json_file(const std::filesystem::path& path);
[[nodiscard]] std::unique_ptr<application::IConfidenceCalibrator> make_logistic_calibrator_json_file(
    const std::filesystem::path& path);

} // namespace fac_lpr::infrastructure
