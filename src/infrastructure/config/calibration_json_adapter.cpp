#include <fac_lpr/infrastructure/config/calibration_json_adapter.hpp>

#include <fac_lpr/application/error.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace fac_lpr::infrastructure {
namespace {
using Json = nlohmann::json;
using application::ConfigurationError;

LoadedCalibration parse(const Json& root) {
    if (!root.is_object()) {
        throw ConfigurationError("calibration JSON root must be an object");
    }
    if (root.value("schemaVersion", 0) != 1) {
        throw ConfigurationError("unsupported calibration schemaVersion");
    }

    LoadedCalibration loaded{};
    loaded.model_version = root.value("modelVersion", "");
    loaded.dataset_version = root.value("datasetVersion", "");
    if (loaded.model_version.empty() || loaded.dataset_version.empty()) {
        throw ConfigurationError("calibration modelVersion/datasetVersion are required");
    }
    loaded.config.minimum_samples = root.at("minimumSamples").get<std::size_t>();
    loaded.config.probability_epsilon = root.at("probabilityEpsilon").get<float>();

    const auto& segments = root.at("segments");
    if (!segments.is_array() || segments.empty()) {
        throw ConfigurationError("calibration segments must be a non-empty array");
    }
    for (const auto& item : segments) {
        if (!item.is_object()) {
            throw ConfigurationError("calibration segment must be an object");
        }
        loaded.segments.push_back(application::LogisticCalibrationSegment{
            .provider = item.at("provider").get<std::string>(),
            .crop_type = item.value("cropType", ""),
            .slope = item.at("slope").get<float>(),
            .intercept = item.at("intercept").get<float>(),
            .sample_count = item.at("sampleCount").get<std::size_t>(),
        });
    }

    (void)application::LogisticConfidenceCalibrator{loaded.config, loaded.segments};
    return loaded;
}
} // namespace

LoadedCalibration load_logistic_calibration_json(const std::string_view json_text) {
    if (json_text.empty()) {
        throw application::ConfigurationError("calibration JSON must not be empty");
    }
    try {
        return parse(Json::parse(json_text.begin(), json_text.end()));
    } catch (const application::ConfigurationError&) {
        throw;
    } catch (const Json::exception& error) {
        throw application::ConfigurationError(std::string{"invalid calibration JSON: "} + error.what());
    }
}

LoadedCalibration load_logistic_calibration_json_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw application::ConfigurationError("cannot open calibration JSON file: " + path.string());
    }
    std::ostringstream content;
    content << input.rdbuf();
    if (input.bad()) {
        throw application::ConfigurationError("failed reading calibration JSON file: " + path.string());
    }
    return load_logistic_calibration_json(content.str());
}

std::unique_ptr<application::IConfidenceCalibrator> make_logistic_calibrator_json_file(
    const std::filesystem::path& path) {
    auto loaded = load_logistic_calibration_json_file(path);
    return std::make_unique<application::LogisticConfidenceCalibrator>(
        loaded.config, std::move(loaded.segments));
}

} // namespace fac_lpr::infrastructure
