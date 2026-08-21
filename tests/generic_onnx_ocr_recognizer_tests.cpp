#include <fac_lpr/infrastructure/onnx/generic_ocr_recognizer.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {
using namespace fac_lpr;

class FakeSession final : public infrastructure::onnx::IOnnxInferenceSession {
public:
    FakeSession() {
        inputs_.push_back({"image", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 3, 24, 94}});
        outputs_.push_back({"logits", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 16, 40}});
    }

    const std::vector<infrastructure::onnx::TensorDescriptor>& inputs() const noexcept override { return inputs_; }
    const std::vector<infrastructure::onnx::TensorDescriptor>& outputs() const noexcept override { return outputs_; }

    std::vector<Ort::Value> run(
        std::span<const char* const> input_names,
        std::span<const Ort::Value> input_values,
        std::span<const char* const> output_names) override {
        ++calls;
        EXPECT_EQ(input_names.size(), 1U);
        EXPECT_EQ(input_values.size(), 1U);
        EXPECT_EQ(output_names.size(), 1U);
        std::vector<Ort::Value> result;
        result.emplace_back(nullptr);
        return result;
    }

    std::size_t calls{0U};

private:
    std::vector<infrastructure::onnx::TensorDescriptor> inputs_{};
    std::vector<infrastructure::onnx::TensorDescriptor> outputs_{};
};

class FakeAdapter : public infrastructure::onnx::IOnnxOcrModelAdapter {
public:
    infrastructure::onnx::OnnxOcrProviderMetadata metadata() const override { return {"fake-ocr", "1.2.3"}; }
    std::vector<std::string> input_names() const override { return {"image"}; }
    std::vector<std::string> output_names() const override { return {"logits"}; }

    std::vector<Ort::Value> build_inputs(
        const application::ValidatedImage&,
        infrastructure::native_image::NativeImageWorkspace&,
        const application::OperationContext&) override {
        std::vector<Ort::Value> inputs;
        inputs.emplace_back(nullptr);
        return inputs;
    }

    domain::RecognitionEvidence decode(
        std::span<const Ort::Value> outputs,
        const application::OperationContext&) override {
        EXPECT_EQ(outputs.size(), 1U);
        domain::RecognitionEvidence evidence{};
        evidence.candidates.push_back({"34ABC123", 0.9F, 0.9F, true});
        return evidence;
    }
};

TEST(GenericOnnxOcrRecognizer, DelegatesModelSpecificWorkWithoutChangingPublicProviderContract) {
    auto session = std::make_shared<FakeSession>();
    auto adapter = std::make_shared<FakeAdapter>();
    infrastructure::onnx::GenericOnnxOcrRecognizer recognizer{session, adapter};
    EXPECT_EQ(recognizer.name(), "fake-ocr");
    EXPECT_EQ(recognizer.model_version(), "1.2.3");

    std::vector<std::byte> bytes(12U, std::byte{1});
    application::ImageView image{bytes, 2U, 2U, 6U, application::PixelFormat::bgr8};
    const auto evidence = recognizer.recognize(image, {});
    ASSERT_EQ(evidence.candidates.size(), 1U);
    EXPECT_EQ(evidence.source, "fake-ocr");
    EXPECT_EQ(session->calls, 1U);
}

TEST(GenericOnnxOcrRecognizer, RejectsAdapterNodeNamesNotPresentInModel) {
    class BadAdapter final : public FakeAdapter {
    public:
        std::vector<std::string> input_names() const override { return {"missing"}; }
    };
    EXPECT_THROW(
        infrastructure::onnx::GenericOnnxOcrRecognizer{
            std::make_shared<FakeSession>(), std::make_shared<BadAdapter>()},
        application::ModelLoadError);
}

} // namespace
