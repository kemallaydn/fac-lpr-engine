#include <fac_lpr/infrastructure/lprnet/lprnet_preprocessor.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace fac_lpr::infrastructure::lprnet {
namespace {

struct RgbSample final {
    float red{0.0F};
    float green{0.0F};
    float blue{0.0F};
};

[[nodiscard]] std::size_t checked_tensor_size(const LprNetInputSpec& spec) {
    if (spec.width == 0U || spec.height == 0U ||
        (spec.channels != 1U && spec.channels != 3U)) {
        throw application::ConfigurationError(
            "LPRNet input must be non-zero HxW with 1 or 3 channels");
    }
    if ((spec.channels == 1U && spec.color_order != InputColorOrder::gray) ||
        (spec.channels == 3U && spec.color_order == InputColorOrder::gray)) {
        throw application::ConfigurationError(
            "LPRNet channel count and color order are inconsistent");
    }
    if (spec.element_type != TensorElementType::float32) {
        throw application::ConfigurationError(
            "only float32 LPRNet input tensors are currently supported");
    }
    if (!std::isfinite(spec.input_scale) || spec.input_scale <= 0.0F) {
        throw application::ConfigurationError(
            "LPRNet input scale must be finite and positive");
    }
    for (std::size_t channel = 0U; channel < spec.channels; ++channel) {
        if (!std::isfinite(spec.mean[channel]) ||
            !std::isfinite(spec.standard_deviation[channel]) ||
            spec.standard_deviation[channel] <= 0.0F) {
            throw application::ConfigurationError(
                "LPRNet normalization parameters are invalid");
        }
    }
    if (spec.width > std::numeric_limits<std::size_t>::max() / spec.height) {
        throw application::ConfigurationError("LPRNet input dimensions overflow");
    }
    const auto pixels = spec.width * spec.height;
    if (pixels > std::numeric_limits<std::size_t>::max() / spec.channels) {
        throw application::ConfigurationError("LPRNet input tensor size overflows");
    }
    return pixels * spec.channels;
}

[[nodiscard]] std::array<std::int64_t, 4U> make_shape(
    const LprNetInputSpec& spec) {
    const auto to_i64 = [](const std::size_t value, const char* name) {
        if (value > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
            throw application::ConfigurationError(
                std::string{name} + " exceeds int64 tensor dimension limits");
        }
        return static_cast<std::int64_t>(value);
    };

    if (spec.layout == TensorLayout::nchw) {
        return {
            1,
            to_i64(spec.channels, "LPRNet channels"),
            to_i64(spec.height, "LPRNet height"),
            to_i64(spec.width, "LPRNet width")};
    }
    return {
        1,
        to_i64(spec.height, "LPRNet height"),
        to_i64(spec.width, "LPRNet width"),
        to_i64(spec.channels, "LPRNet channels")};
}

[[nodiscard]] float byte_value(const std::byte value) noexcept {
    return static_cast<float>(std::to_integer<unsigned int>(value));
}

[[nodiscard]] RgbSample pixel_at(
    const application::ImageView& image,
    const std::size_t x,
    const std::size_t y) noexcept {
    const auto* row = image.bytes.data() + (y * image.stride_bytes);
    if (image.format == application::PixelFormat::gray8) {
        const auto gray = byte_value(row[x]);
        return RgbSample{gray, gray, gray};
    }

    const auto base = x * 3U;
    if (image.format == application::PixelFormat::rgb8) {
        return RgbSample{
            byte_value(row[base]),
            byte_value(row[base + 1U]),
            byte_value(row[base + 2U])};
    }
    return RgbSample{
        byte_value(row[base + 2U]),
        byte_value(row[base + 1U]),
        byte_value(row[base])};
}

[[nodiscard]] RgbSample bilinear_sample(
    const application::ImageView& image,
    const double source_x,
    const double source_y) noexcept {
    const auto max_x = static_cast<double>(image.width - 1U);
    const auto max_y = static_cast<double>(image.height - 1U);
    const auto clamped_x = std::clamp(source_x, 0.0, max_x);
    const auto clamped_y = std::clamp(source_y, 0.0, max_y);
    const auto x0 = static_cast<std::size_t>(std::floor(clamped_x));
    const auto y0 = static_cast<std::size_t>(std::floor(clamped_y));
    const auto x1 = std::min(x0 + 1U, image.width - 1U);
    const auto y1 = std::min(y0 + 1U, image.height - 1U);
    const auto dx = static_cast<float>(clamped_x - static_cast<double>(x0));
    const auto dy = static_cast<float>(clamped_y - static_cast<double>(y0));

    const auto p00 = pixel_at(image, x0, y0);
    const auto p10 = pixel_at(image, x1, y0);
    const auto p01 = pixel_at(image, x0, y1);
    const auto p11 = pixel_at(image, x1, y1);

    const auto interpolate = [dx, dy](
        const float a,
        const float b,
        const float c,
        const float d) noexcept {
        const auto top = (a * (1.0F - dx)) + (b * dx);
        const auto bottom = (c * (1.0F - dx)) + (d * dx);
        return (top * (1.0F - dy)) + (bottom * dy);
    };

    return RgbSample{
        interpolate(p00.red, p10.red, p01.red, p11.red),
        interpolate(p00.green, p10.green, p01.green, p11.green),
        interpolate(p00.blue, p10.blue, p01.blue, p11.blue)};
}

[[nodiscard]] float luminance(const RgbSample& sample) noexcept {
    return (0.299F * sample.red) +
           (0.587F * sample.green) +
           (0.114F * sample.blue);
}

[[nodiscard]] std::array<float, 3U> ordered_channels(
    const RgbSample& sample,
    const LprNetInputSpec& spec) noexcept {
    if (spec.color_order == InputColorOrder::bgr) {
        return {sample.blue, sample.green, sample.red};
    }
    if (spec.color_order == InputColorOrder::gray) {
        return {luminance(sample), 0.0F, 0.0F};
    }
    return {sample.red, sample.green, sample.blue};
}

[[nodiscard]] float normalize_value(
    const float value,
    const std::size_t channel,
    const LprNetInputSpec& spec) noexcept {
    return ((value * spec.input_scale) - spec.mean[channel]) /
           spec.standard_deviation[channel];
}

} // namespace

LprNetPreprocessor::LprNetPreprocessor(LprNetInputSpec spec)
    : spec_(spec),
      tensor_elements_(checked_tensor_size(spec_)),
      tensor_shape_(make_shape(spec_)) {}

LprNetInputTensor LprNetPreprocessor::preprocess(
    const application::ValidatedImage& image) const {
    LprNetInputTensor result{};
    result.values.resize(tensor_elements_);
    result.shape = tensor_shape_;
    preprocess_into(image, result.values);
    return result;
}

LprNetInputTensorView LprNetPreprocessor::preprocess(
    const application::ValidatedImage& image,
    native_image::NativeImageWorkspace& workspace) const {
    auto output = workspace.prepare_tensor(tensor_elements_);
    preprocess_into(image, output);
    return LprNetInputTensorView{output, tensor_shape_};
}

void LprNetPreprocessor::preprocess_into(
    const application::ValidatedImage& image,
    const std::span<float> output) const {
    if (output.size() != tensor_elements_) {
        throw application::InvalidImageError(
            "LPRNet output tensor workspace has the wrong element count");
    }

    const auto source_scale_x =
        static_cast<double>(image.view.width) / static_cast<double>(spec_.width);
    const auto source_scale_y =
        static_cast<double>(image.view.height) / static_cast<double>(spec_.height);
    const auto plane_size = spec_.width * spec_.height;

    for (std::size_t output_y = 0U; output_y < spec_.height; ++output_y) {
        const auto source_y =
            ((static_cast<double>(output_y) + 0.5) * source_scale_y) - 0.5;
        for (std::size_t output_x = 0U; output_x < spec_.width; ++output_x) {
            const auto source_x =
                ((static_cast<double>(output_x) + 0.5) * source_scale_x) - 0.5;
            const auto sample = bilinear_sample(image.view, source_x, source_y);
            const auto channels = ordered_channels(sample, spec_);
            const auto pixel_index = (output_y * spec_.width) + output_x;

            for (std::size_t channel = 0U; channel < spec_.channels; ++channel) {
                const auto normalized = normalize_value(channels[channel], channel, spec_);
                if (spec_.layout == TensorLayout::nchw) {
                    output[(channel * plane_size) + pixel_index] = normalized;
                } else {
                    output[(pixel_index * spec_.channels) + channel] = normalized;
                }
            }
        }
    }
}

} // namespace fac_lpr::infrastructure::lprnet
