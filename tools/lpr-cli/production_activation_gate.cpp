#include "production_activation_gate.hpp"

#include <fac_lpr/application/engine_diagnostics.hpp>
#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/model/model_lifecycle.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fac_lpr::cli {
namespace {

using Contract = std::unordered_map<std::string, std::string>;

[[nodiscard]] std::string trim(std::string value) {
    const auto not_space = [](const unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

[[nodiscard]] Contract load_contract(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        throw application::ConfigurationError("cannot open production contract file");
    }
    Contract values;
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(std::move(line));
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw application::ConfigurationError(
                "invalid production contract line " + std::to_string(line_number));
        }
        auto key = trim(line.substr(0U, separator));
        auto value = trim(line.substr(separator + 1U));
        if (key.empty() || value.empty() || !values.emplace(key, value).second) {
            throw application::ConfigurationError(
                "empty/duplicate production contract key at line " + std::to_string(line_number));
        }
    }
    return values;
}

[[nodiscard]] const std::string& required(const Contract& contract, const std::string& key) {
    const auto iterator = contract.find(key);
    if (iterator == contract.end() || iterator->second.empty()) {
        throw application::ConfigurationError("missing production contract key: " + key);
    }
    return iterator->second;
}

[[nodiscard]] std::size_t required_size(const Contract& contract, const std::string& key) {
    const auto& text = required(contract, key);
    std::size_t value = 0U;
    const auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || ptr != text.data() + text.size() || value == 0U) {
        throw application::ConfigurationError("invalid positive integer production contract value: " + key);
    }
    return value;
}

[[nodiscard]] std::string detector_version(const std::string& sha256) {
    constexpr std::size_t short_hash_length = 12U;
    return std::string{"best.onnx@"} + sha256.substr(0U, std::min(short_hash_length, sha256.size()));
}

} // namespace

void activate_production_pipeline(
    const std::shared_ptr<application::LprPipeline>& pipeline,
    const std::filesystem::path& model_directory,
    const std::filesystem::path& contract_path) {
    if (!pipeline) {
        throw application::ConfigurationError("production activation requires a pipeline");
    }

    const auto contract = load_contract(contract_path);
    const auto& detector_sha = required(contract, "detector.sha256");
    const auto& ocr_sha = required(contract, "ocr.sha256");
    const auto& ocr_version = required(contract, "ocr.model_version");

    infrastructure::model::ModelManifest manifest{};
    manifest.root_directory = model_directory;
    manifest.entries = {
        infrastructure::model::ModelManifestEntry{
            .name = "best.onnx",
            .type = "detector",
            .version = detector_version(detector_sha),
            .relative_path = required(contract, "detector.model"),
            .sha256 = detector_sha,
            .size_bytes = required_size(contract, "detector.size_bytes")},
        infrastructure::model::ModelManifestEntry{
            .name = "lprnet_turkey.onnx",
            .type = "recognizer",
            .version = ocr_version,
            .relative_path = required(contract, "ocr.model"),
            .sha256 = ocr_sha,
            .size_bytes = required_size(contract, "ocr.size_bytes")}};

    const infrastructure::model::ModelLifecycleManager lifecycle{std::move(manifest)};
    const auto active_models = lifecycle.validate_and_activate();

    std::vector<application::DiagnosticModelInfo> diagnostic_models;
    diagnostic_models.reserve(active_models.size());
    for (const auto& model : active_models) {
        diagnostic_models.push_back(application::DiagnosticModelInfo{
            .name = model.name,
            .version = model.version,
            .sha256 = model.sha256,
            .integrity_verified = model.integrity_verified});
    }

    const auto diagnostics = pipeline->diagnostics();
    diagnostics->set_models(std::move(diagnostic_models));
    diagnostics->set_providers({
        application::DiagnosticProviderInfo{
            .name = "yolo_pose_onnx",
            .version = detector_version(detector_sha),
            .role = "detector"},
        application::DiagnosticProviderInfo{
            .name = "lprnet_onnx",
            .version = ocr_version,
            .role = "recognizer"}});

    const auto report = pipeline->startup_self_test();
    if (report.readiness == application::ReadinessState::failed) {
        throw application::ModelLoadError("production startup self-test failed; engine is not ready");
    }
}

} // namespace fac_lpr::cli
