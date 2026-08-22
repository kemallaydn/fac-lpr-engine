#include "cli_engine_factory.hpp"

#include <fac_lpr/application/candidate_fusion.hpp>
#include <fac_lpr/application/confidence_calibration.hpp>
#include <fac_lpr/application/engine_builder.hpp>
#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/recognition_ensemble.hpp>
#include <fac_lpr/application/safe_decision_policy.hpp>
#include <fac_lpr/infrastructure/crop/crop_hypothesis_generator.hpp>
#include <fac_lpr/infrastructure/geometry/plate_geometry_evaluator.hpp>
#include <fac_lpr/infrastructure/lprnet/lprnet_onnx_contract.hpp>
#include <fac_lpr/infrastructure/lprnet/lprnet_onnx_ocr_adapter.hpp>
#include <fac_lpr/infrastructure/onnx/generic_ocr_recognizer.hpp>
#include <fac_lpr/infrastructure/onnx/onnx_session.hpp>
#include <fac_lpr/infrastructure/opencv/connected_component_layout_analyzer.hpp>
#include <fac_lpr/infrastructure/opencv/perspective_aligner.hpp>
#include <fac_lpr/infrastructure/yolo/yolo_pose_onnx_detector.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
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
        throw application::ConfigurationError("cannot open CLI contract file");
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
        const auto equal = line.find('=');
        if (equal == std::string::npos) {
            throw application::ConfigurationError(
                "invalid CLI contract line " + std::to_string(line_number));
        }
        auto key = trim(line.substr(0U, equal));
        auto value = trim(line.substr(equal + 1U));
        if (key.empty() || value.empty() || !values.emplace(key, value).second) {
            throw application::ConfigurationError(
                "empty/duplicate CLI contract key at line " + std::to_string(line_number));
        }
    }
    return values;
}

[[nodiscard]] const std::string& required(const Contract& contract, const std::string& key) {
    const auto iterator = contract.find(key);
    if (iterator == contract.end() || iterator->second.empty()) {
        throw application::ConfigurationError("missing CLI contract key: " + key);
    }
    return iterator->second;
}

[[nodiscard]] std::size_t parse_size(const Contract& contract, const std::string& key) {
    const auto& text = required(contract, key);
    std::size_t value = 0U;
    const auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || ptr != text.data() + text.size()) {
        throw application::ConfigurationError("invalid integer CLI contract value: " + key);
    }
    return value;
}

[[nodiscard]] float parse_float(const Contract& contract, const std::string& key) {
    const auto& text = required(contract, key);
    try {
        std::size_t consumed = 0U;
        const auto value = std::stof(text, &consumed);
        if (consumed != text.size() || !std::isfinite(value)) {
            throw application::ConfigurationError("invalid float CLI contract value: " + key);
        }
        return value;
    } catch (const application::EngineError&) {
        throw;
    } catch (...) {
        throw application::ConfigurationError("invalid float CLI contract value: " + key);
    }
}

[[nodiscard]] std::optional<std::size_t> parse_optional_size(
    const Contract& contract,
    const std::string& key) {
    const auto& text = required(contract, key);
    if (text == "none") {
        return std::nullopt;
    }
    return parse_size(contract, key);
}

[[nodiscard]] std::array<float, 3U> parse_float3(
    const Contract& contract,
    const std::string& key) {
    const auto& text = required(contract, key);
    std::array<float, 3U> result{};
    std::stringstream stream{text};
    std::string item;
    for (std::size_t index = 0U; index < result.size(); ++index) {
        if (!std::getline(stream, item, ',')) {
            throw application::ConfigurationError("CLI contract value must contain three floats: " + key);
        }
        item = trim(std::move(item));
        try {
            std::size_t consumed = 0U;
            result[index] = std::stof(item, &consumed);
            if (consumed != item.size() || !std::isfinite(result[index])) {
                throw application::ConfigurationError("invalid float list in CLI contract: " + key);
            }
        } catch (const application::EngineError&) {
            throw;
        } catch (...) {
            throw application::ConfigurationError("invalid float list in CLI contract: " + key);
        }
    }
    if (std::getline(stream, item, ',')) {
        throw application::ConfigurationError("CLI contract value has more than three floats: " + key);
    }
    return result;
}

[[nodiscard]] std::filesystem::path safe_model_path(
    const std::filesystem::path& root,
    const std::string& relative) {
    const std::filesystem::path child{relative};
    if (child.empty() || child.is_absolute()) {
        throw application::ConfigurationError("model path must be a non-empty relative path");
    }
    for (const auto& component : child) {
        if (component == "..") {
            throw application::ConfigurationError("model path traversal is not allowed");
        }
    }
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error) {
        throw application::ConfigurationError("cannot resolve model directory");
    }
    const auto canonical_model = std::filesystem::weakly_canonical(root / child, error);
    if (error || !std::filesystem::is_regular_file(canonical_model)) {
        throw application::ModelLoadError("model file is missing: " + relative);
    }
    auto root_it = canonical_root.begin();
    auto model_it = canonical_model.begin();
    for (; root_it != canonical_root.end(); ++root_it, ++model_it) {
        if (model_it == canonical_model.end() || *root_it != *model_it) {
            throw application::ConfigurationError("resolved model path escapes model directory");
        }
    }
    return canonical_model;
}

[[nodiscard]] const infrastructure::onnx::TensorDescriptor& descriptor_named(
    const std::vector<infrastructure::onnx::TensorDescriptor>& descriptors,
    const std::string& name,
    const char* kind) {
    const auto iterator = std::find_if(
        descriptors.begin(), descriptors.end(),
        [&name](const auto& descriptor) { return descriptor.name == name; });
    if (iterator == descriptors.end()) {
        throw application::ModelLoadError(std::string{"configured "} + kind + " node is missing: " + name);
    }
    return *iterator;
}

[[nodiscard]] infrastructure::lprnet::InputColorOrder color_order(const std::string& value) {
    if (value == "gray") return infrastructure::lprnet::InputColorOrder::gray;
    if (value == "rgb") return infrastructure::lprnet::InputColorOrder::rgb;
    if (value == "bgr") return infrastructure::lprnet::InputColorOrder::bgr;
    throw application::ConfigurationError("ocr.color_order must be gray, rgb or bgr");
}

[[nodiscard]] infrastructure::lprnet::LprNetOutputLayout output_layout(const std::string& value) {
    if (value == "bct") return infrastructure::lprnet::LprNetOutputLayout::batch_classes_timesteps;
    if (value == "btc") return infrastructure::lprnet::LprNetOutputLayout::batch_timesteps_classes;
    throw application::ConfigurationError("ocr.output_layout must be bct or btc");
}

[[nodiscard]] infrastructure::yolo::CandidateLayout detector_layout(const std::string& value) {
    if (value == "features_first") return infrastructure::yolo::CandidateLayout::features_first;
    if (value == "candidates_first") return infrastructure::yolo::CandidateLayout::candidates_first;
    throw application::ConfigurationError(
        "detector.output_layout must be features_first or candidates_first");
}

[[nodiscard]] OrtLoggingLevel ort_log_level(const std::string_view value) {
    if (value == "trace") return ORT_LOGGING_LEVEL_VERBOSE;
    if (value == "debug" || value == "info") return ORT_LOGGING_LEVEL_INFO;
    if (value == "warn") return ORT_LOGGING_LEVEL_WARNING;
    if (value == "error") return ORT_LOGGING_LEVEL_ERROR;
    throw application::ConfigurationError("log level must be trace, debug, info, warn or error");
}

} // namespace

std::shared_ptr<application::LprPipeline> build_pipeline_from_contract(
    const std::filesystem::path& model_directory,
    const std::filesystem::path& contract_path,
    const std::string_view log_level) {
    const auto contract = load_contract(contract_path);
    const auto detector_model = safe_model_path(model_directory, required(contract, "detector.model"));
    const auto ocr_model = safe_model_path(model_directory, required(contract, "ocr.model"));

    auto environment = std::make_shared<infrastructure::onnx::OnnxRuntimeEnvironment>(
        ort_log_level(log_level));
    auto detector_session = std::make_shared<infrastructure::onnx::OnnxSession>(
        environment, detector_model);
    auto ocr_session = std::make_shared<infrastructure::onnx::OnnxSession>(
        environment, ocr_model);

    infrastructure::yolo::YoloPoseOnnxDetectorConfig detector_config{};
    detector_config.provider_name = "yolo_pose_onnx";
    detector_config.input_name = required(contract, "detector.input_name");
    detector_config.output_name = required(contract, "detector.output_name");
    const auto& detector_input = descriptor_named(
        detector_session->inputs(), detector_config.input_name, "detector input");
    if (detector_input.shape.size() != 4U || detector_input.shape[0] != 1 ||
        detector_input.shape[1] != 3 || detector_input.shape[2] <= 0 || detector_input.shape[3] <= 0) {
        throw application::ModelLoadError("detector input must be static [1,3,H,W]");
    }
    detector_config.input.width = static_cast<std::size_t>(detector_input.shape[3]);
    detector_config.input.height = static_cast<std::size_t>(detector_input.shape[2]);
    detector_config.input.channels = 3U;
    detector_config.input.scale = parse_float(contract, "detector.input_scale");
    detector_config.input.pad_value = parse_float(contract, "detector.pad_value");
    detector_config.output.layout = detector_layout(required(contract, "detector.output_layout"));
    detector_config.output.box_offset = parse_size(contract, "detector.box_offset");
    detector_config.output.objectness_offset = parse_optional_size(contract, "detector.objectness_offset");
    detector_config.output.class_score_offset = parse_size(contract, "detector.class_score_offset");
    detector_config.output.class_count = parse_size(contract, "detector.class_count");
    detector_config.output.keypoint_offset = parse_size(contract, "detector.keypoint_offset");
    detector_config.output.keypoint_count = parse_size(contract, "detector.keypoint_count");
    detector_config.output.keypoint_stride = parse_size(contract, "detector.keypoint_stride");
    detector_config.output.keypoint_x_offset = parse_size(contract, "detector.keypoint_x_offset");
    detector_config.output.keypoint_y_offset = parse_size(contract, "detector.keypoint_y_offset");
    detector_config.output.keypoint_confidence_offset =
        parse_optional_size(contract, "detector.keypoint_confidence_offset");
    detector_config.output.confidence_threshold = parse_float(contract, "detector.confidence_threshold");
    detector_config.output.nms_iou_threshold = parse_float(contract, "detector.nms_iou_threshold");
    detector_config.output.maximum_detections = parse_size(contract, "detector.maximum_detections");
    detector_config.output.provider_name = detector_config.provider_name;

    auto detector = std::make_shared<infrastructure::yolo::YoloPoseOnnxDetector>(
        detector_session, detector_config);

    const auto ocr_input_name = required(contract, "ocr.input_name");
    const auto& ocr_input_descriptor = descriptor_named(
        ocr_session->inputs(), ocr_input_name, "OCR input");
    infrastructure::lprnet::LprNetPreprocessSemantics semantics{};
    semantics.color_order = color_order(required(contract, "ocr.color_order"));
    semantics.input_scale = parse_float(contract, "ocr.input_scale");
    semantics.mean = parse_float3(contract, "ocr.mean");
    semantics.standard_deviation = parse_float3(contract, "ocr.std");
    const auto ocr_input_spec = infrastructure::lprnet::make_lprnet_input_spec(
        ocr_input_descriptor, semantics);

    infrastructure::lprnet::LprNetOnnxOcrAdapterConfig ocr_config{};
    ocr_config.provider_name = "lprnet_onnx";
    ocr_config.model_version = required(contract, "ocr.model_version");
    ocr_config.input_name = ocr_input_name;
    ocr_config.output_name = required(contract, "ocr.output_name");
    ocr_config.input = ocr_input_spec;
    ocr_config.output_layout = output_layout(required(contract, "ocr.output_layout"));
    const auto& charset = required(contract, "ocr.charset");
    ocr_config.decoder.ctc.charset.assign(charset.begin(), charset.end());
    ocr_config.decoder.ctc.blank_index = parse_size(contract, "ocr.blank_index");
    ocr_config.decoder.ctc.maximum_timesteps = parse_size(contract, "ocr.maximum_timesteps");
    ocr_config.decoder.ctc.maximum_classes = parse_size(contract, "ocr.maximum_classes");
    ocr_config.decoder.beam_width = parse_size(contract, "ocr.beam_width");
    ocr_config.decoder.result_limit = parse_size(contract, "ocr.result_limit");
    ocr_config.decoder.classes_per_step = parse_size(contract, "ocr.classes_per_step");
    ocr_config.decoder.confusion_weight = parse_float(contract, "ocr.confusion_weight");

    auto ocr_adapter = std::make_shared<infrastructure::lprnet::LprNetOnnxOcrAdapter>(ocr_config);
    auto recognizer = std::make_shared<infrastructure::onnx::GenericOnnxOcrRecognizer>(
        ocr_session, ocr_adapter);

    application::LprEngineBuilder builder{};
    builder.detector(std::move(detector))
        .geometry("plate_geometry", std::make_shared<infrastructure::geometry::PlateGeometryEvaluatorAdapter>())
        .aligner("opencv_perspective", std::make_shared<infrastructure::opencv::OpenCvPerspectiveAligner>())
        .crop_generator(std::make_shared<infrastructure::crop::CropHypothesisGenerator>())
        .recognizer(application::RecognizerRegistration{
            .provider = std::move(recognizer),
            .weight = 1.0F,
            .timeout = std::chrono::milliseconds{1000},
            .required = true,
            .enabled = true})
        .calibrator("identity", std::make_shared<application::IdentityConfidenceCalibrator>())
        .layout_analyzer(
            "connected_components",
            std::make_shared<infrastructure::opencv::ConnectedComponentPlateLayoutAnalyzer>())
        .candidate_fusion(
            "weighted_multi_crop",
            std::make_shared<application::WeightedMultiCropCandidateFusion>())
        .decision_policy(
            "safe_default",
            std::make_shared<application::SafeRecognitionDecisionPolicy>());
    return builder.build();
}

} // namespace fac_lpr::cli
