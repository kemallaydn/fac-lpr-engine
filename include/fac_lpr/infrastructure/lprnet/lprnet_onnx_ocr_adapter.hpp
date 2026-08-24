#pragma once

#include <fac_lpr/infrastructure/lprnet/constrained_ctc_beam_search.hpp>
#include <fac_lpr/infrastructure/lprnet/lprnet_preprocessor.hpp>
#include <fac_lpr/infrastructure/onnx/generic_ocr_recognizer.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace fac_lpr::infrastructure::lprnet {

enum class LprNetOutputLayout {
    batch_classes_timesteps,
    batch_timesteps_classes
};

struct LprNetOnnxOcrAdapterConfig final {
    std::string provider_name{"lprnet_onnx"};
    std::string model_version{"unknown"};
    std::string input_name{};
    std::string output_name{};
    LprNetInputSpec input{};
    LprNetOutputLayout output_layout{LprNetOutputLayout::batch_classes_timesteps};
    ConstrainedCtcBeamSearchConfig decoder{};
};

class LprNetOnnxOcrAdapter final : public onnx::IOnnxOcrModelAdapter {
public:
    explicit LprNetOnnxOcrAdapter(LprNetOnnxOcrAdapterConfig config);

    [[nodiscard]] onnx::OnnxOcrProviderMetadata metadata() const override;
    [[nodiscard]] std::vector<std::string> input_names() const override;
    [[nodiscard]] std::vector<std::string> output_names() const override;

    [[nodiscard]] std::vector<Ort::Value> build_inputs(
        const application::ValidatedImage& image,
        native_image::NativeImageWorkspace& workspace,
        const application::OperationContext& context) override;

    [[nodiscard]] domain::RecognitionEvidence decode(
        std::span<const Ort::Value> outputs,
        const application::OperationContext& context) override;

private:
    static void check_context(const application::OperationContext& context);

    LprNetOnnxOcrAdapterConfig config_{};
    LprNetPreprocessor preprocessor_;
    ConstrainedCtcBeamSearch decoder_;
};

} // namespace fac_lpr::infrastructure::lprnet
