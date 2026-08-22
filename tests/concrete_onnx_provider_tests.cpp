#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/lprnet/lprnet_onnx_ocr_adapter.hpp>
#include <fac_lpr/infrastructure/onnx/onnx_session.hpp>
#include <fac_lpr/infrastructure/yolo/yolo_pose_onnx_detector.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace fac_lpr;

class TensorSession final : public infrastructure::onnx::IOnnxInferenceSession {
public:
    TensorSession(
        std::vector<infrastructure::onnx::TensorDescriptor> inputs,
        std::vector<infrastructure::onnx::TensorDescriptor> outputs,
        std::vector<float> output_values,
        std::vector<std::int64_t> output_shape)
        : inputs_(std::move(inputs)),
          outputs_(std::move(outputs)),
          output_values_(std::move(output_values)),
          output_shape_(std::move(output_shape)) {}

    const std::vector<infrastructure::onnx::TensorDescriptor>& inputs() const noexcept override {
        return inputs_;
    }
    const std::vector<infrastructure::onnx::TensorDescriptor>& outputs() const noexcept override {
        return outputs_;
    }

    std::vector<Ort::Value> run(
        std::span<const char* const> input_names,
        std::span<const Ort::Value> input_values,
        std::span<const char* const> output_names) override {
        ++calls;
        EXPECT_EQ(input_names.size(), 1U);
        EXPECT_EQ(input_values.size(), 1U);
        EXPECT_EQ(output_names.size(), 1U);
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

    std::size_t calls{0U};

private:
    std::vector<infrastructure::onnx::TensorDescriptor> inputs_{};
    std::vector<infrastructure::onnx::TensorDescriptor> outputs_{};
    std::vector<float> output_values_{};
    std::vector<std::int64_t> output_shape_{};
};

TEST(YoloPoseOnnxDetector, RunsPreprocessSessionAndParserWithExplicitContract) {
    auto session = std::make_shared<TensorSession>(
        std::vector<infrastructure::onnx::TensorDescriptor>{
            {"images", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 3, 2, 2}}},
        std::vector<infrastructure::onnx::TensorDescriptor>{
            {"output0", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 17, 1}}},
        std::vector<float>(17U, 0.0F),
        std::vector<std::int64_t>{1, 17, 1});

    infrastructure::yolo::YoloPoseOnnxDetectorConfig config{};
    config.input_name = "images";
    config.output_name = "output0";
    config.input = {.width = 2U, .height = 2U, .channels = 3U};
    config.output.confidence_threshold = 0.5F;
    infrastructure::yolo::YoloPoseOnnxDetector detector{session, config};

    std::array<std::byte, 12U> pixels{};
    const application::ImageView image{
        pixels, 2U, 2U, 6U, application::PixelFormat::bgr8};
    const auto detections = detector.detect(image, {});
    EXPECT_TRUE(detections.empty());
    EXPECT_EQ(session->calls, 1U);
}

TEST(YoloPoseOnnxDetector, RejectsConfiguredInputShapeThatDoesNotMatchModel) {
    auto session = std::make_shared<TensorSession>(
        std::vector<infrastructure::onnx::TensorDescriptor>{
            {"images", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 3, 4, 4}}},
        std::vector<infrastructure::onnx::TensorDescriptor>{
            {"output0", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 17, 1}}},
        std::vector<float>(17U, 0.0F),
        std::vector<std::int64_t>{1, 17, 1});

    infrastructure::yolo::YoloPoseOnnxDetectorConfig config{};
    config.input_name = "images";
    config.output_name = "output0";
    config.input = {.width = 2U, .height = 2U, .channels = 3U};
    EXPECT_THROW(
        infrastructure::yolo::YoloPoseOnnxDetector{session, config},
        application::ModelLoadError);
}

TEST(LprNetOnnxOcrAdapter, DecodesExplicitBctLayoutWithoutGuessingCharsetOrBlank) {
    infrastructure::lprnet::LprNetOnnxOcrAdapterConfig config{};
    config.input_name = "input";
    config.output_name = "logits";
    config.model_version = "test";
    config.input = {
        .width = 4U,
        .height = 2U,
        .channels = 3U,
        .layout = infrastructure::lprnet::TensorLayout::nchw,
        .color_order = infrastructure::lprnet::InputColorOrder::rgb};
    config.output_layout = infrastructure::lprnet::LprNetOutputLayout::batch_classes_timesteps;
    config.decoder.ctc.charset = {'1', '2', 'A'};
    config.decoder.ctc.blank_index = 3U;
    config.decoder.ctc.maximum_timesteps = 8U;
    config.decoder.ctc.maximum_classes = 8U;
    config.decoder.beam_width = 4U;
    config.decoder.result_limit = 3U;
    config.decoder.classes_per_step = 4U;
    infrastructure::lprnet::LprNetOnnxOcrAdapter adapter{config};

    std::vector<float> bct{
        8.0F, 0.0F, 0.0F,
        0.0F, 8.0F, 0.0F,
        0.0F, 0.0F, 8.0F,
        -8.0F, -8.0F, -8.0F};
    const std::array<std::int64_t, 3U> shape{1, 4, 3};
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<Ort::Value> outputs;
    outputs.emplace_back(Ort::Value::CreateTensor<float>(
        memory, bct.data(), bct.size(), shape.data(), shape.size()));

    const auto evidence = adapter.decode(outputs, {});
    ASSERT_FALSE(evidence.candidates.empty());
    EXPECT_EQ(evidence.candidates.front().text, "12A");
}

TEST(LprNetOnnxOcrAdapter, ActiveV2MixedEpoch7DecodesBct34By24Contract) {
    constexpr char active_charset[] = "0123456789ABCDEFGHIJKLMNOPRSTUVYZ";

    infrastructure::lprnet::LprNetOnnxOcrAdapterConfig config{};
    config.input_name = "input";
    config.output_name = "output";
    config.model_version = "V2 Mixed Epoch 7";
    config.input = {
        .width = 160U,
        .height = 40U,
        .channels = 3U,
        .layout = infrastructure::lprnet::TensorLayout::nchw,
        .color_order = infrastructure::lprnet::InputColorOrder::bgr,
        .element_type = infrastructure::lprnet::TensorElementType::float32,
        .input_scale = 1.0F,
        .mean = {127.5F, 127.5F, 127.5F},
        .standard_deviation = {128.0F, 128.0F, 128.0F}};
    config.output_layout = infrastructure::lprnet::LprNetOutputLayout::batch_classes_timesteps;
    config.decoder.ctc.charset.assign(std::begin(active_charset), std::end(active_charset) - 1);
    config.decoder.ctc.blank_index = 33U;
    config.decoder.ctc.maximum_timesteps = 24U;
    config.decoder.ctc.maximum_classes = 34U;
    config.decoder.beam_width = 16U;
    config.decoder.result_limit = 5U;
    config.decoder.classes_per_step = 6U;
    infrastructure::lprnet::LprNetOnnxOcrAdapter adapter{config};

    constexpr std::size_t classes = 34U;
    constexpr std::size_t timesteps = 24U;
    std::vector<float> bct(classes * timesteps, -12.0F);
    const std::array<std::size_t, 8U> plate_classes{3U, 4U, 10U, 11U, 12U, 1U, 2U, 3U};
    for (std::size_t timestep = 0U; timestep < timesteps; ++timestep) {
        const auto class_index = timestep < plate_classes.size() ? plate_classes[timestep] : 33U;
        bct[(class_index * timesteps) + timestep] = 12.0F;
    }

    const std::array<std::int64_t, 3U> shape{1, 34, 24};
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<Ort::Value> outputs;
    outputs.emplace_back(Ort::Value::CreateTensor<float>(
        memory, bct.data(), bct.size(), shape.data(), shape.size()));

    const auto evidence = adapter.decode(outputs, {});
    ASSERT_FALSE(evidence.candidates.empty());
    EXPECT_EQ(evidence.candidates.front().text, "34ABC123");
}

} // namespace
