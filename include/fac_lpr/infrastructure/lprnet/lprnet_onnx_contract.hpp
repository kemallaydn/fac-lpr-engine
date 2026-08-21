#pragma once

#include <fac_lpr/infrastructure/lprnet/lprnet_preprocessor.hpp>
#include <fac_lpr/infrastructure/onnx/onnx_session.hpp>

#include <array>
#include <string_view>

namespace fac_lpr::infrastructure::lprnet {

struct LprNetPreprocessSemantics final {
    InputColorOrder color_order{InputColorOrder::gray};
    float input_scale{0.0F};
    std::array<float, 3U> mean{0.0F, 0.0F, 0.0F};
    std::array<float, 3U> standard_deviation{0.0F, 0.0F, 0.0F};
};

[[nodiscard]] LprNetInputSpec make_lprnet_input_spec(
    const onnx::TensorDescriptor& input,
    const LprNetPreprocessSemantics& semantics);

[[nodiscard]] LprNetInputSpec inspect_lprnet_input_spec(
    const onnx::OnnxSession& session,
    const LprNetPreprocessSemantics& semantics,
    std::string_view input_name = {});

} // namespace fac_lpr::infrastructure::lprnet
