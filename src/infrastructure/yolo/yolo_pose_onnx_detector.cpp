#include <fac_lpr/infrastructure/yolo/yolo_pose_onnx_detector.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace fac_lpr::infrastructure::yolo {
namespace {

[[nodiscard]] const onnx::TensorDescriptor* find_descriptor(
    const std::vector<onnx::TensorDescriptor>& descriptors,
    const std::string& name) {
    const auto it = std::find_if(
        descriptors.begin(), descriptors.end(),
        [&name](const onnx::TensorDescriptor& descriptor) { return descriptor.name == name; });
    return it == descriptors.end() ? nullptr : &*it;
}

[[nodiscard]] std::size_t checked_elements(const YoloInputSpec& spec) {
    if (spec.width == 0U || spec.height == 0U || spec.channels == 0U) {
        throw application::ConfigurationError("YOLO ONNX input dimensions must be non-zero");
    }
    if (spec.width > std::numeric_limits<std::size_t>::max() / spec.height) {
        throw application::ConfigurationError("YOLO ONNX input dimensions overflow");
    }
    const auto pixels = spec.width * spec.height;
    if (pixels > std::numeric_limits<std::size_t>::max() / spec.channels) {
        throw application::ConfigurationError("YOLO ONNX input element count overflows");
    }
    return pixels * spec.channels;
}

} // namespace

YoloPoseOnnxDetector::YoloPoseOnnxDetector(
    std::shared_ptr<onnx::IOnnxInferenceSession> session,
    YoloPoseOnnxDetectorConfig config)
    : session_(std::move(session)),
      config_(std::move(config)),
      preprocessor_(config_.input),
      parser_(config_.output) {
    if (!session_) {
        throw application::ConfigurationError("YOLO ONNX detector requires a session");
    }
    if (config_.provider_name.empty() || config_.input_name.empty() || config_.output_name.empty()) {
        throw application::ConfigurationError("YOLO ONNX provider/input/output names are required");
    }
    validate_contract();
}

std::string_view YoloPoseOnnxDetector::name() const noexcept {
    return config_.provider_name;
}

void YoloPoseOnnxDetector::check_context(const application::OperationContext& context) {
    if (context.cancellation_requested()) {
        throw application::CancelledError("YOLO ONNX detection cancelled");
    }
    if (context.deadline_exceeded()) {
        throw application::TimeoutError("YOLO ONNX detection deadline exceeded");
    }
}

void YoloPoseOnnxDetector::validate_contract() const {
    const auto* input = find_descriptor(session_->inputs(), config_.input_name);
    const auto* output = find_descriptor(session_->outputs(), config_.output_name);
    if (input == nullptr || output == nullptr) {
        throw application::ModelLoadError("YOLO ONNX configured node name is missing from model contract");
    }
    if (input->element_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        throw application::ModelLoadError("YOLO ONNX input must be float32");
    }
    if (output->element_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        throw application::ModelLoadError("YOLO ONNX output must be float32");
    }
    if (input->shape.size() != 4U || input->shape[0] != 1 || input->shape[1] != 3) {
        throw application::ModelLoadError("YOLO ONNX input must have static NCHW [1,3,H,W] contract");
    }
    if (input->shape[2] <= 0 || input->shape[3] <= 0) {
        throw application::ModelLoadError("YOLO ONNX input height/width must be static positive values");
    }
    if (static_cast<std::size_t>(input->shape[2]) != config_.input.height ||
        static_cast<std::size_t>(input->shape[3]) != config_.input.width ||
        config_.input.channels != 3U) {
        throw application::ModelLoadError("YOLO ONNX configured preprocessing dimensions do not match model input");
    }
    if (output->shape.size() != 3U || output->shape[0] != 1) {
        throw application::ModelLoadError("YOLO ONNX output must have rank-3 batch-one contract");
    }
    (void)checked_elements(config_.input);
}

std::vector<domain::Detection> YoloPoseOnnxDetector::detect(
    const application::ImageView& image,
    const application::OperationContext& context) {
    check_context(context);
    const auto validated = application::validate_image(image, config_.image_limits);

    std::scoped_lock lock{execution_mutex_};
    check_context(context);
    const auto tensor = preprocessor_.preprocess(validated, workspace_);
    if (tensor.chw.size() != checked_elements(config_.input)) {
        throw application::InferenceError("YOLO preprocessor produced unexpected tensor size");
    }

    const std::array<std::int64_t, 4U> shape{
        1,
        static_cast<std::int64_t>(config_.input.channels),
        static_cast<std::int64_t>(config_.input.height),
        static_cast<std::int64_t>(config_.input.width)};
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto input_value = Ort::Value::CreateTensor<float>(
        memory_info,
        const_cast<float*>(tensor.chw.data()),
        tensor.chw.size(),
        shape.data(),
        shape.size());

    const std::array<const char*, 1U> input_names{config_.input_name.c_str()};
    const std::array<const char*, 1U> output_names{config_.output_name.c_str()};
    const std::array<Ort::Value, 1U> input_values{std::move(input_value)};
    auto outputs = session_->run(input_names, input_values, output_names);
    check_context(context);
    if (outputs.size() != 1U || !outputs.front().IsTensor()) {
        throw application::InferenceError("YOLO ONNX session returned invalid output count/type");
    }

    const auto info = outputs.front().GetTensorTypeAndShapeInfo();
    const auto output_shape = info.GetShape();
    const auto element_count = info.GetElementCount();
    const auto* data = outputs.front().GetTensorData<float>();
    if (data == nullptr || element_count == 0U) {
        throw application::InferenceError("YOLO ONNX output tensor is empty");
    }

    auto detections = parser_.parse(
        std::span<const float>{data, element_count},
        output_shape,
        tensor.letterbox);
    for (auto& detection : detections) {
        detection.provider = config_.provider_name;
    }
    return detections;
}

} // namespace fac_lpr::infrastructure::yolo
