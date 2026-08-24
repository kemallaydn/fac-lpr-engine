#include <fac_lpr/infrastructure/lprnet/lprnet_onnx_contract.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace fac_lpr::infrastructure::lprnet {
namespace {

[[nodiscard]] bool is_channel_dimension(const std::int64_t value) noexcept {
    return value == 1 || value == 3;
}

[[nodiscard]] std::size_t checked_dimension(
    const std::int64_t value,
    const char* name) {
    if (value <= 0) {
        throw application::ModelLoadError(
            std::string{"LPRNet input "} + name + " must be a fixed positive dimension");
    }
    const auto unsigned_value = static_cast<std::uint64_t>(value);
    if (unsigned_value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw application::ModelLoadError(
            std::string{"LPRNet input "} + name + " exceeds platform size limits");
    }
    return static_cast<std::size_t>(unsigned_value);
}

void validate_semantics(
    const LprNetPreprocessSemantics& semantics,
    const std::size_t channels) {
    if (!std::isfinite(semantics.input_scale) || semantics.input_scale <= 0.0F) {
        throw application::ConfigurationError(
            "LPRNet preprocess input_scale must be finite and greater than zero");
    }

    for (std::size_t index = 0U; index < semantics.mean.size(); ++index) {
        if (!std::isfinite(semantics.mean[index])) {
            throw application::ConfigurationError(
                "LPRNet preprocess mean values must be finite");
        }
        if (!std::isfinite(semantics.standard_deviation[index]) ||
            semantics.standard_deviation[index] <= 0.0F) {
            throw application::ConfigurationError(
                "LPRNet preprocess standard deviation values must be finite and positive");
        }
    }

    if (channels == 1U && semantics.color_order != InputColorOrder::gray) {
        throw application::ConfigurationError(
            "single-channel LPRNet input requires gray preprocessing semantics");
    }
    if (channels == 3U && semantics.color_order == InputColorOrder::gray) {
        throw application::ConfigurationError(
            "three-channel LPRNet input requires RGB or BGR preprocessing semantics");
    }
}

} // namespace

LprNetInputSpec make_lprnet_input_spec(
    const onnx::TensorDescriptor& input,
    const LprNetPreprocessSemantics& semantics) {
    if (input.element_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        throw application::ModelLoadError(
            "LPRNet input tensor must use float32 elements");
    }
    if (input.shape.size() != 4U) {
        throw application::ModelLoadError(
            "LPRNet input tensor must be rank 4");
    }
    if (input.shape[0] != 1 && input.shape[0] != -1) {
        throw application::ModelLoadError(
            "LPRNet input batch dimension must be 1 or dynamic");
    }

    const bool nchw_candidate = is_channel_dimension(input.shape[1]);
    const bool nhwc_candidate = is_channel_dimension(input.shape[3]);
    if (nchw_candidate == nhwc_candidate) {
        throw application::ModelLoadError(
            "LPRNet input layout is ambiguous or has unsupported channel dimensions");
    }

    LprNetInputSpec spec{};
    if (nchw_candidate) {
        spec.layout = TensorLayout::nchw;
        spec.channels = checked_dimension(input.shape[1], "channels");
        spec.height = checked_dimension(input.shape[2], "height");
        spec.width = checked_dimension(input.shape[3], "width");
    } else {
        spec.layout = TensorLayout::nhwc;
        spec.height = checked_dimension(input.shape[1], "height");
        spec.width = checked_dimension(input.shape[2], "width");
        spec.channels = checked_dimension(input.shape[3], "channels");
    }

    validate_semantics(semantics, spec.channels);
    spec.color_order = semantics.color_order;
    spec.element_type = TensorElementType::float32;
    spec.input_scale = semantics.input_scale;
    spec.mean = semantics.mean;
    spec.standard_deviation = semantics.standard_deviation;
    return spec;
}

LprNetInputSpec inspect_lprnet_input_spec(
    const onnx::OnnxSession& session,
    const LprNetPreprocessSemantics& semantics,
    const std::string_view input_name) {
    const auto& inputs = session.inputs();
    if (inputs.empty()) {
        throw application::ModelLoadError("LPRNet model has no input tensors");
    }

    if (input_name.empty()) {
        if (inputs.size() != 1U) {
            throw application::ModelLoadError(
                "LPRNet model has multiple inputs; an explicit input name is required");
        }
        return make_lprnet_input_spec(inputs.front(), semantics);
    }

    const auto iterator = std::find_if(
        inputs.begin(),
        inputs.end(),
        [input_name](const onnx::TensorDescriptor& input) {
            return input.name == input_name;
        });
    if (iterator == inputs.end()) {
        throw application::ModelLoadError(
            "configured LPRNet input name was not found in model metadata");
    }
    return make_lprnet_input_spec(*iterator, semantics);
}

} // namespace fac_lpr::infrastructure::lprnet
