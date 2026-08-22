#include <fac_lpr/infrastructure/lprnet/lprnet_onnx_ocr_adapter.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace fac_lpr::infrastructure::lprnet {

LprNetOnnxOcrAdapter::LprNetOnnxOcrAdapter(LprNetOnnxOcrAdapterConfig config)
    : config_(std::move(config)),
      preprocessor_(config_.input),
      decoder_(config_.decoder) {
    if (config_.provider_name.empty() || config_.model_version.empty() ||
        config_.input_name.empty() || config_.output_name.empty()) {
        throw application::ConfigurationError(
            "LPRNet ONNX provider/version/input/output names are required");
    }
}

onnx::OnnxOcrProviderMetadata LprNetOnnxOcrAdapter::metadata() const {
    return {.name = config_.provider_name, .version = config_.model_version};
}

std::vector<std::string> LprNetOnnxOcrAdapter::input_names() const {
    return {config_.input_name};
}

std::vector<std::string> LprNetOnnxOcrAdapter::output_names() const {
    return {config_.output_name};
}

void LprNetOnnxOcrAdapter::check_context(const application::OperationContext& context) {
    if (context.cancellation_requested()) {
        throw application::CancelledError("LPRNet ONNX recognition cancelled");
    }
    if (context.deadline_exceeded()) {
        throw application::TimeoutError("LPRNet ONNX recognition deadline exceeded");
    }
}

std::vector<Ort::Value> LprNetOnnxOcrAdapter::build_inputs(
    const application::ValidatedImage& image,
    native_image::NativeImageWorkspace& workspace,
    const application::OperationContext& context) {
    check_context(context);
    const auto tensor = preprocessor_.preprocess(image, workspace);
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<Ort::Value> values;
    values.reserve(1U);
    values.emplace_back(Ort::Value::CreateTensor<float>(
        memory_info,
        const_cast<float*>(tensor.values.data()),
        tensor.values.size(),
        tensor.shape.data(),
        tensor.shape.size()));
    return values;
}

domain::RecognitionEvidence LprNetOnnxOcrAdapter::decode(
    const std::span<const Ort::Value> outputs,
    const application::OperationContext& context) {
    check_context(context);
    if (outputs.size() != 1U || !outputs.front().IsTensor()) {
        throw application::InferenceError("LPRNet ONNX adapter expects exactly one tensor output");
    }

    const auto info = outputs.front().GetTensorTypeAndShapeInfo();
    if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        throw application::InferenceError("LPRNet ONNX logits must be float32");
    }
    const auto shape = info.GetShape();
    if (shape.size() != 3U || shape[0] != 1 || shape[1] <= 0 || shape[2] <= 0) {
        throw application::InferenceError("LPRNet ONNX output must have static [1,C,T] or [1,T,C] shape");
    }

    std::size_t timesteps = 0U;
    std::size_t classes = 0U;
    if (config_.output_layout == LprNetOutputLayout::batch_classes_timesteps) {
        classes = static_cast<std::size_t>(shape[1]);
        timesteps = static_cast<std::size_t>(shape[2]);
    } else {
        timesteps = static_cast<std::size_t>(shape[1]);
        classes = static_cast<std::size_t>(shape[2]);
    }

    const auto expected_classes = config_.decoder.ctc.charset.size() + 1U;
    if (classes != expected_classes) {
        throw application::InferenceError(
            "LPRNet ONNX class count does not match configured charset/blank contract");
    }
    if (timesteps > config_.decoder.ctc.maximum_timesteps ||
        classes > config_.decoder.ctc.maximum_classes) {
        throw application::InferenceError("LPRNet ONNX logits exceed configured decoder bounds");
    }

    const auto element_count = info.GetElementCount();
    if (element_count != timesteps * classes) {
        throw application::InferenceError("LPRNet ONNX output element count does not match shape");
    }
    const auto* data = outputs.front().GetTensorData<float>();
    if (data == nullptr) {
        throw application::InferenceError("LPRNet ONNX logits pointer is null");
    }

    std::vector<float> timestep_major;
    std::span<const float> logits{data, element_count};
    if (config_.output_layout == LprNetOutputLayout::batch_classes_timesteps) {
        timestep_major.resize(element_count);
        for (std::size_t timestep = 0U; timestep < timesteps; ++timestep) {
            for (std::size_t class_index = 0U; class_index < classes; ++class_index) {
                timestep_major[timestep * classes + class_index] =
                    data[class_index * timesteps + timestep];
            }
        }
        logits = std::span<const float>{timestep_major.data(), timestep_major.size()};
    }

    const auto decoded = decoder_.decode(logits, timesteps, classes);
    domain::RecognitionEvidence evidence{};
    evidence.source = config_.provider_name;
    evidence.candidates = decoded.candidates;
    for (auto& candidate : evidence.candidates) {
        candidate.calibrated_confidence = candidate.confidence;
    }
    if (evidence.candidates.empty() && !decoded.greedy.text.empty()) {
        evidence.candidates.push_back(domain::PlateCandidate{
            .text = decoded.greedy.text,
            .confidence = decoded.greedy.confidence,
            .calibrated_confidence = decoded.greedy.confidence,
            .format_valid = false});
    }
    return evidence;
}

} // namespace fac_lpr::infrastructure::lprnet
