#include <fac_lpr/infrastructure/onnx/generic_ocr_recognizer.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace fac_lpr::infrastructure::onnx {

GenericOnnxOcrRecognizer::GenericOnnxOcrRecognizer(
    std::shared_ptr<IOnnxInferenceSession> session,
    std::shared_ptr<IOnnxOcrModelAdapter> adapter,
    application::PerformanceConfig image_limits)
    : session_(std::move(session)),
      adapter_(std::move(adapter)),
      image_limits_(image_limits) {
    if (!session_ || !adapter_) {
        throw application::ConfigurationError("generic ONNX OCR requires a session and model adapter");
    }
    metadata_ = adapter_->metadata();
    input_names_ = adapter_->input_names();
    output_names_ = adapter_->output_names();
    validate_contract();
}

std::string_view GenericOnnxOcrRecognizer::name() const noexcept {
    return metadata_.name;
}

std::string_view GenericOnnxOcrRecognizer::model_version() const noexcept {
    return metadata_.version;
}

void GenericOnnxOcrRecognizer::check_context(const application::OperationContext& context) {
    if (context.cancellation_requested()) {
        throw application::CancelledError("ONNX OCR recognition cancelled");
    }
    if (context.deadline_exceeded()) {
        throw application::TimeoutError("ONNX OCR recognition deadline exceeded");
    }
}

void GenericOnnxOcrRecognizer::validate_contract() const {
    if (metadata_.name.empty() || metadata_.version.empty()) {
        throw application::ConfigurationError("ONNX OCR adapter name and version are required");
    }
    if (input_names_.empty() || output_names_.empty()) {
        throw application::ConfigurationError("ONNX OCR adapter must declare input and output names");
    }

    const auto unique = [](const std::vector<std::string>& names) {
        std::unordered_set<std::string> values;
        for (const auto& name : names) {
            if (name.empty() || !values.emplace(name).second) {
                return false;
            }
        }
        return true;
    };
    if (!unique(input_names_) || !unique(output_names_)) {
        throw application::ConfigurationError("ONNX OCR adapter node names must be non-empty and unique");
    }

    const auto& model_inputs = session_->inputs();
    const auto& model_outputs = session_->outputs();
    const auto contains_name = [](const auto& descriptors, const std::string& name) {
        return std::any_of(descriptors.begin(), descriptors.end(), [&name](const TensorDescriptor& descriptor) {
            return descriptor.name == name;
        });
    };
    for (const auto& input : input_names_) {
        if (!contains_name(model_inputs, input)) {
            throw application::ModelLoadError("ONNX OCR adapter input does not exist in model contract");
        }
    }
    for (const auto& output : output_names_) {
        if (!contains_name(model_outputs, output)) {
            throw application::ModelLoadError("ONNX OCR adapter output does not exist in model contract");
        }
    }
}

void GenericOnnxOcrRecognizer::validate_evidence(domain::RecognitionEvidence& evidence) {
    for (auto& candidate : evidence.candidates) {
        if (candidate.text.empty() || !std::isfinite(candidate.confidence) ||
            candidate.confidence < 0.0F || candidate.confidence > 1.0F) {
            throw application::InferenceError("ONNX OCR adapter returned malformed recognition evidence");
        }
        if (!std::isfinite(candidate.calibrated_confidence) ||
            candidate.calibrated_confidence < 0.0F || candidate.calibrated_confidence > 1.0F) {
            candidate.calibrated_confidence = candidate.confidence;
        }
    }
}

domain::RecognitionEvidence GenericOnnxOcrRecognizer::recognize(
    const application::ImageView& plate,
    const application::OperationContext& context) {
    check_context(context);
    const auto validated = application::validate_image(plate, image_limits_);
    const auto started = std::chrono::steady_clock::now();

    std::scoped_lock lock{execution_mutex_};
    check_context(context);
    auto input_values = adapter_->build_inputs(validated, workspace_, context);
    if (input_values.size() != input_names_.size()) {
        throw application::InferenceError("ONNX OCR adapter produced an unexpected input count");
    }

    std::vector<const char*> input_name_pointers;
    input_name_pointers.reserve(input_names_.size());
    for (const auto& name : input_names_) {
        input_name_pointers.push_back(name.c_str());
    }
    std::vector<const char*> output_name_pointers;
    output_name_pointers.reserve(output_names_.size());
    for (const auto& name : output_names_) {
        output_name_pointers.push_back(name.c_str());
    }

    auto outputs = session_->run(input_name_pointers, input_values, output_name_pointers);
    check_context(context);
    auto evidence = adapter_->decode(outputs, context);
    validate_evidence(evidence);
    evidence.source = metadata_.name;
    evidence.latency_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return evidence;
}

} // namespace fac_lpr::infrastructure::onnx
