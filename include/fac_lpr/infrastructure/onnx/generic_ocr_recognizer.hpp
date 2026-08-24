#pragma once

#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/application/providers.hpp>
#include <fac_lpr/infrastructure/native_image/native_image.hpp>
#include <fac_lpr/infrastructure/onnx/onnx_session.hpp>

#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fac_lpr::infrastructure::onnx {

struct OnnxOcrProviderMetadata final {
    std::string name{};
    std::string version{};
};

class IOnnxOcrModelAdapter {
public:
    virtual ~IOnnxOcrModelAdapter() = default;
    [[nodiscard]] virtual OnnxOcrProviderMetadata metadata() const = 0;
    [[nodiscard]] virtual std::vector<std::string> input_names() const = 0;
    [[nodiscard]] virtual std::vector<std::string> output_names() const = 0;
    [[nodiscard]] virtual std::vector<Ort::Value> build_inputs(
        const application::ValidatedImage& image,
        native_image::NativeImageWorkspace& workspace,
        const application::OperationContext& context) = 0;
    [[nodiscard]] virtual domain::RecognitionEvidence decode(
        std::span<const Ort::Value> outputs,
        const application::OperationContext& context) = 0;
};

class GenericOnnxOcrRecognizer final : public application::IPlateRecognizer {
public:
    GenericOnnxOcrRecognizer(
        std::shared_ptr<IOnnxInferenceSession> session,
        std::shared_ptr<IOnnxOcrModelAdapter> adapter,
        application::PerformanceConfig image_limits = {});

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view model_version() const noexcept;
    [[nodiscard]] domain::RecognitionEvidence recognize(
        const application::ImageView& plate,
        const application::OperationContext& context) override;

private:
    void validate_contract() const;
    static void check_context(const application::OperationContext& context);
    static void validate_evidence(domain::RecognitionEvidence& evidence);

    std::shared_ptr<IOnnxInferenceSession> session_{};
    std::shared_ptr<IOnnxOcrModelAdapter> adapter_{};
    application::PerformanceConfig image_limits_{};
    OnnxOcrProviderMetadata metadata_{};
    std::vector<std::string> input_names_{};
    std::vector<std::string> output_names_{};
    mutable std::mutex execution_mutex_{};
    native_image::NativeImageWorkspace workspace_{};
};

} // namespace fac_lpr::infrastructure::onnx
