#include <fac_lpr/application/operation_context.hpp>
#include <fac_lpr/infrastructure/onnx/onnx_session.hpp>
#include <fac_lpr/infrastructure/yolo/yolo_pose_onnx_detector.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace fac_lpr;

class TimingSink final : public application::IStageTimingSink {
public:
    void record(const std::string_view stage, const double latency_ms) override {
        events.emplace_back(stage, latency_ms);
    }

    [[nodiscard]] bool contains(const std::string& stage) const {
        for (const auto& [name, latency] : events) {
            (void)latency;
            if (name == stage) return true;
        }
        return false;
    }

    std::vector<std::pair<std::string, double>> events{};
};

class DetectorSession final : public infrastructure::onnx::IOnnxInferenceSession {
public:
    DetectorSession()
        : inputs_{{"images", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 3, 2, 2}}},
          outputs_{{"output0", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 17, 1}}},
          output_values_(17U, 0.0F),
          output_shape_{1, 17, 1} {}

    const std::vector<infrastructure::onnx::TensorDescriptor>& inputs() const noexcept override {
        return inputs_;
    }

    const std::vector<infrastructure::onnx::TensorDescriptor>& outputs() const noexcept override {
        return outputs_;
    }

    std::vector<Ort::Value> run(
        std::span<const char* const>,
        std::span<const Ort::Value>,
        std::span<const char* const>) override {
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> result;
        result.emplace_back(Ort::Value::CreateTensor<float>(
            memory,
            output_values_.data(),
            output_values_.size(),
            output_shape_.data(),
            output_shape_.size()));
        return result;
    }

private:
    std::vector<infrastructure::onnx::TensorDescriptor> inputs_{};
    std::vector<infrastructure::onnx::TensorDescriptor> outputs_{};
    std::vector<float> output_values_{};
    std::array<std::int64_t, 3U> output_shape_{};
};

TEST(ProviderStageTiming, YoloDetectorEmitsPreprocessInferenceAndPostprocessStages) {
    auto session = std::make_shared<DetectorSession>();
    infrastructure::yolo::YoloPoseOnnxDetectorConfig config{};
    config.input_name = "images";
    config.output_name = "output0";
    config.input = {.width = 2U, .height = 2U, .channels = 3U};
    config.output.confidence_threshold = 0.5F;
    infrastructure::yolo::YoloPoseOnnxDetector detector{session, config};

    TimingSink sink{};
    application::OperationContext context{};
    context.timing_sink = &sink;
    std::array<std::byte, 12U> pixels{};
    const application::ImageView image{
        pixels, 2U, 2U, 6U, application::PixelFormat::bgr8};

    (void)detector.detect(image, context);

    EXPECT_TRUE(sink.contains("detector.preprocess"));
    EXPECT_TRUE(sink.contains("detector.inference"));
    EXPECT_TRUE(sink.contains("detector.postprocess"));
    for (const auto& [stage, latency] : sink.events) {
        (void)stage;
        EXPECT_GE(latency, 0.0);
    }
}

} // namespace
