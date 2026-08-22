#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/onnx/generic_ocr_recognizer.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
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
        for (std::size_t index = 0U; index < returned_output_count; ++index) {
            result.emplace_back(nullptr);
        }
        return result;
    }

    std::size_t calls{0U};
    std::size_t returned_output_count{1U};

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
        evidence.candidates.push_back({
            "34ABC123",
            0.9F,
            invalid_calibrated_confidence ? 1.5F : 0.9F,
            true});
        return evidence;
    }

    bool invalid_calibrated_confidence{false};
};

class TimingSink final : public application::IStageTimingSink {
public:
    void record(const std::string_view stage, const double latency_ms) override {
        stages.emplace_back(stage, latency_ms);
    }

    [[nodiscard]] bool contains(const std::string& stage) const {
        for (const auto& [recorded, latency] : stages) {
            (void)latency;
            if (recorded == stage) return true;
        }
        return false;
    }

    std::vector<std::pair<std::string, double>> stages{};
};

application::ImageView image_fixture(std::vector<std::byte>& bytes) {
    bytes.assign(12U, std::byte{1});
    return {bytes, 2U, 2U, 6U, application::PixelFormat::bgr8};
}

TEST(GenericOnnxOcrRecognizer, DelegatesModelSpecificWorkWithoutChangingPublicProviderContract) {
    auto session = std::make_shared<FakeSession>();
    auto adapter = std::make_shared<FakeAdapter>();
    infrastructure::onnx::GenericOnnxOcrRecognizer recognizer{session, adapter};
    EXPECT_EQ(recognizer.name(), "fake-ocr");
    EXPECT_EQ(recognizer.model_version(), "1.2.3");

    std::vector<std::byte> bytes;
    const auto evidence = recognizer.recognize(image_fixture(bytes), {});
    ASSERT_EQ(evidence.candidates.size(), 1U);
    EXPECT_EQ(evidence.source, "fake-ocr");
    EXPECT_EQ(session->calls, 1U);
}

TEST(GenericOnnxOcrRecognizer, EmitsPreprocessInferenceAndDecodeTimingsWhenRequested) {
    auto session = std::make_shared<FakeSession>();
    auto adapter = std::make_shared<FakeAdapter>();
    infrastructure::onnx::GenericOnnxOcrRecognizer recognizer{session, adapter};
    TimingSink sink{};
    application::OperationContext context{};
    context.timing_sink = &sink;
    std::vector<std::byte> bytes;

    (void)recognizer.recognize(image_fixture(bytes), context);

    EXPECT_TRUE(sink.contains("recognizer.preprocess"));
    EXPECT_TRUE(sink.contains("recognizer.inference"));
    EXPECT_TRUE(sink.contains("recognizer.decode"));
    for (const auto& [stage, latency] : sink.stages) {
        (void)stage;
        EXPECT_GE(latency, 0.0);
    }
}

TEST(GenericOnnxOcrRecognizer, RejectsAdapterNodeNamesNotPresentInModel) {
    class BadAdapter final : public FakeAdapter {
    public:
        std::vector<std::string> input_names() const override { return {"missing"}; }
    };
    EXPECT_THROW(
        (infrastructure::onnx::GenericOnnxOcrRecognizer{
            std::make_shared<FakeSession>(), std::make_shared<BadAdapter>()}),
        application::ModelLoadError);
}

TEST(GenericOnnxOcrRecognizer, RejectsUnexpectedSessionOutputCountBeforeDecode) {
    auto session = std::make_shared<FakeSession>();
    session->returned_output_count = 0U;
    auto adapter = std::make_shared<FakeAdapter>();
    infrastructure::onnx::GenericOnnxOcrRecognizer recognizer{session, adapter};
    std::vector<std::byte> bytes;

    EXPECT_THROW(
        recognizer.recognize(image_fixture(bytes), {}),
        application::InferenceError);
}

TEST(GenericOnnxOcrRecognizer, RejectsMalformedAdapterEvidence) {
    auto session = std::make_shared<FakeSession>();
    auto adapter = std::make_shared<FakeAdapter>();
    adapter->invalid_calibrated_confidence = true;
    infrastructure::onnx::GenericOnnxOcrRecognizer recognizer{session, adapter};
    std::vector<std::byte> bytes;

    EXPECT_THROW(
        recognizer.recognize(image_fixture(bytes), {}),
        application::InferenceError);
}

TEST(GenericOnnxOcrRecognizer, SessionAndAdapterLifetimeAreOwnedByRaii) {
    std::weak_ptr<FakeSession> weak_session;
    std::weak_ptr<FakeAdapter> weak_adapter;
    {
        auto session = std::make_shared<FakeSession>();
        auto adapter = std::make_shared<FakeAdapter>();
        weak_session = session;
        weak_adapter = adapter;
        infrastructure::onnx::GenericOnnxOcrRecognizer recognizer{
            std::move(session), std::move(adapter)};
        EXPECT_FALSE(weak_session.expired());
        EXPECT_FALSE(weak_adapter.expired());
    }
    EXPECT_TRUE(weak_session.expired());
    EXPECT_TRUE(weak_adapter.expired());
}

} // namespace
