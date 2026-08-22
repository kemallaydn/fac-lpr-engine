#include <fac_lpr/infrastructure/config/json_config_adapter.hpp>

#include <fac_lpr/application/error.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace fac_lpr::infrastructure {
namespace {

using Json = nlohmann::json;
using fac_lpr::application::ConfigurationError;

void reject_unknown_fields(
    const Json& object,
    std::string_view scope,
    std::initializer_list<std::string_view> allowed,
    UnknownFieldPolicy policy) {
    if (policy == UnknownFieldPolicy::ignore) {
        return;
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool known = false;
        for (const auto field : allowed) {
            if (it.key() == field) {
                known = true;
                break;
            }
        }
        if (!known) {
            throw ConfigurationError(
                "unknown JSON config field '" + std::string{scope} + "." + it.key() + "'");
        }
    }
}

const Json* optional_object(const Json& root, const char* key) {
    const auto it = root.find(key);
    if (it == root.end()) {
        return nullptr;
    }
    if (!it->is_object()) {
        throw ConfigurationError(std::string{key} + " must be a JSON object");
    }
    return &*it;
}

template <typename T>
void read_optional(const Json& object, const char* key, T& target) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return;
    }
    try {
        target = it->get<T>();
    } catch (const Json::exception& error) {
        throw ConfigurationError(std::string{key} + " has invalid type/value: " + error.what());
    }
}

void read_size(const Json& object, const char* key, std::size_t& target) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return;
    }
    if (!it->is_number_unsigned() && !it->is_number_integer()) {
        throw ConfigurationError(std::string{key} + " must be a non-negative integer");
    }
    const auto value = it->get<std::int64_t>();
    if (value < 0) {
        throw ConfigurationError(std::string{key} + " must be a non-negative integer");
    }
    if (static_cast<std::uint64_t>(value) > std::numeric_limits<std::size_t>::max()) {
        throw ConfigurationError(std::string{key} + " exceeds platform size limit");
    }
    target = static_cast<std::size_t>(value);
}

void apply_detector(const Json& value, application::DetectorConfig& config, UnknownFieldPolicy policy) {
    reject_unknown_fields(value, "detector", {
        "confidence_threshold", "nms_iou_threshold", "max_detections", "adaptive_tiling",
        "tile_width", "tile_height", "tile_overlap_ratio"}, policy);
    read_optional(value, "confidence_threshold", config.confidence_threshold);
    read_optional(value, "nms_iou_threshold", config.nms_iou_threshold);
    read_size(value, "max_detections", config.max_detections);
    read_optional(value, "adaptive_tiling", config.adaptive_tiling);
    read_size(value, "tile_width", config.tile_width);
    read_size(value, "tile_height", config.tile_height);
    read_optional(value, "tile_overlap_ratio", config.tile_overlap_ratio);
}

void apply_recognition(const Json& value, application::RecognitionConfig& config, UnknownFieldPolicy policy) {
    reject_unknown_fields(value, "recognition", {
        "beam_width", "result_limit", "classes_per_step", "max_recognizers",
        "minimum_candidate_confidence"}, policy);
    read_size(value, "beam_width", config.beam_width);
    read_size(value, "result_limit", config.result_limit);
    read_size(value, "classes_per_step", config.classes_per_step);
    read_size(value, "max_recognizers", config.max_recognizers);
    read_optional(value, "minimum_candidate_confidence", config.minimum_candidate_confidence);
}

void apply_crop(const Json& value, application::CropConfig& config, UnknownFieldPolicy policy) {
    reject_unknown_fields(value, "crop", {
        "max_hypotheses", "horizontal_padding_ratio", "vertical_padding_ratio",
        "minimum_width", "minimum_height", "enable_clahe", "enable_sharpen",
        "enable_adaptive_threshold", "enable_double_row"}, policy);
    read_size(value, "max_hypotheses", config.max_hypotheses);
    read_optional(value, "horizontal_padding_ratio", config.horizontal_padding_ratio);
    read_optional(value, "vertical_padding_ratio", config.vertical_padding_ratio);
    read_size(value, "minimum_width", config.minimum_width);
    read_size(value, "minimum_height", config.minimum_height);
    read_optional(value, "enable_clahe", config.enable_clahe);
    read_optional(value, "enable_sharpen", config.enable_sharpen);
    read_optional(value, "enable_adaptive_threshold", config.enable_adaptive_threshold);
    read_optional(value, "enable_double_row", config.enable_double_row);
}

void apply_decision(const Json& value, application::DecisionConfig& config, UnknownFieldPolicy policy) {
    reject_unknown_fields(value, "decision", {
        "accepted_confidence_threshold", "review_confidence_threshold", "strong_conflict_threshold",
        "minimum_crop_quality", "minimum_geometry_score", "minimum_effective_detector_confidence",
        "allow_accept_when_degraded", "fail_closed"}, policy);
    read_optional(value, "accepted_confidence_threshold", config.accepted_confidence_threshold);
    read_optional(value, "review_confidence_threshold", config.review_confidence_threshold);
    read_optional(value, "strong_conflict_threshold", config.strong_conflict_threshold);
    read_optional(value, "minimum_crop_quality", config.minimum_crop_quality);
    read_optional(value, "minimum_geometry_score", config.minimum_geometry_score);
    read_optional(value, "minimum_effective_detector_confidence", config.minimum_effective_detector_confidence);
    read_optional(value, "allow_accept_when_degraded", config.allow_accept_when_degraded);
    read_optional(value, "fail_closed", config.fail_closed);
}

void apply_performance(const Json& value, application::PerformanceConfig& config, UnknownFieldPolicy policy) {
    reject_unknown_fields(value, "performance", {
        "worker_count", "queue_capacity", "max_image_width", "max_image_height",
        "max_image_bytes", "recognition_timeout_ms"}, policy);
    read_size(value, "worker_count", config.worker_count);
    read_size(value, "queue_capacity", config.queue_capacity);
    read_size(value, "max_image_width", config.max_image_width);
    read_size(value, "max_image_height", config.max_image_height);
    read_size(value, "max_image_bytes", config.max_image_bytes);

    const auto timeout = value.find("recognition_timeout_ms");
    if (timeout != value.end()) {
        if (!timeout->is_number_integer()) {
            throw ConfigurationError("recognition_timeout_ms must be an integer");
        }
        const auto milliseconds = timeout->get<std::int64_t>();
        if (milliseconds <= 0) {
            throw ConfigurationError("recognition_timeout_ms must be greater than zero");
        }
        config.recognition_timeout = std::chrono::milliseconds{milliseconds};
    }
}

application::EngineConfig parse_config(const Json& root, JsonConfigLoadOptions options) {
    if (!root.is_object()) {
        throw ConfigurationError("JSON config root must be an object");
    }
    reject_unknown_fields(root, "root", {
        "schema_version", "detector", "recognition", "crop", "decision", "performance"},
        options.unknown_fields);

    const auto schema = root.find("schema_version");
    if (schema == root.end()) {
        throw ConfigurationError("schema_version is required");
    }
    if (!schema->is_number_unsigned() && !schema->is_number_integer()) {
        throw ConfigurationError("schema_version must be an integer");
    }
    const auto schema_version = schema->get<std::int64_t>();
    if (schema_version != static_cast<std::int64_t>(k_engine_config_schema_version)) {
        throw ConfigurationError("unsupported schema_version: " + std::to_string(schema_version));
    }

    application::EngineConfig config{};
    if (const auto* value = optional_object(root, "detector")) {
        apply_detector(*value, config.detector, options.unknown_fields);
    }
    if (const auto* value = optional_object(root, "recognition")) {
        apply_recognition(*value, config.recognition, options.unknown_fields);
    }
    if (const auto* value = optional_object(root, "crop")) {
        apply_crop(*value, config.crop, options.unknown_fields);
    }
    if (const auto* value = optional_object(root, "decision")) {
        apply_decision(*value, config.decision, options.unknown_fields);
    }
    if (const auto* value = optional_object(root, "performance")) {
        apply_performance(*value, config.performance, options.unknown_fields);
    }
    application::validate_engine_config(config);
    return config;
}

} // namespace

application::EngineConfig load_engine_config_json(std::string_view json_text, JsonConfigLoadOptions options) {
    if (json_text.empty()) {
        throw ConfigurationError("JSON config must not be empty");
    }
    try {
        return parse_config(Json::parse(json_text.begin(), json_text.end()), options);
    } catch (const ConfigurationError&) {
        throw;
    } catch (const Json::exception& error) {
        throw ConfigurationError(std::string{"invalid JSON config: "} + error.what());
    }
}

application::EngineConfig load_engine_config_json_file(
    const std::filesystem::path& path,
    JsonConfigLoadOptions options) {
    if (path.empty()) {
        throw ConfigurationError("JSON config path must not be empty");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ConfigurationError("cannot open JSON config file: " + path.string());
    }
    std::ostringstream content;
    content << input.rdbuf();
    if (input.bad()) {
        throw ConfigurationError("failed reading JSON config file: " + path.string());
    }
    return load_engine_config_json(content.str(), options);
}

} // namespace fac_lpr::infrastructure
