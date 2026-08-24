#include <fac_lpr/infrastructure/yolo/yolo_pose_preprocessor.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fac_lpr::infrastructure::yolo {
namespace {

[[nodiscard]] std::size_t checked_tensor_size(const YoloInputSpec& spec) {
    if (spec.width == 0U || spec.height == 0U || spec.channels != 3U) {
        throw application::ConfigurationError("YOLO input must be non-zero HxW with exactly 3 channels");
    }
    if (!std::isfinite(spec.scale) || spec.scale <= 0.0F ||
        !std::isfinite(spec.pad_value) || spec.pad_value < 0.0F || spec.pad_value > 255.0F) {
        throw application::ConfigurationError("YOLO normalization or padding configuration is invalid");
    }
    if (spec.width > std::numeric_limits<std::size_t>::max() / spec.height) {
        throw application::ConfigurationError("YOLO input dimensions overflow");
    }
    const auto pixels = spec.width * spec.height;
    if (pixels > std::numeric_limits<std::size_t>::max() / spec.channels) {
        throw application::ConfigurationError("YOLO input tensor size overflows");
    }
    return pixels * spec.channels;
}

struct RgbSample final {
    float red{0.0F};
    float green{0.0F};
    float blue{0.0F};
};

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
        return RgbSample{byte_value(row[base]), byte_value(row[base + 1U]), byte_value(row[base + 2U])};
    }
    return RgbSample{byte_value(row[base + 2U]), byte_value(row[base + 1U]), byte_value(row[base])};
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
    const auto top_weight_left = 1.0F - dx;
    const auto bottom_weight_top = 1.0F - dy;

    const auto interpolate = [=](const float a, const float b, const float c, const float d) noexcept {
        const auto top = (a * top_weight_left) + (b * dx);
        const auto bottom = (c * top_weight_left) + (d * dx);
        return (top * bottom_weight_top) + (bottom * dy);
    };
    return RgbSample{
        interpolate(p00.red, p10.red, p01.red, p11.red),
        interpolate(p00.green, p10.green, p01.green, p11.green),
        interpolate(p00.blue, p10.blue, p01.blue, p11.blue)};
}

[[nodiscard]] LetterboxMetadata make_letterbox_metadata(
    const application::ImageView& image,
    const YoloInputSpec& spec) {
    const auto scale_x = static_cast<double>(spec.width) / static_cast<double>(image.width);
    const auto scale_y = static_cast<double>(spec.height) / static_cast<double>(image.height);
    const auto scale = std::min(scale_x, scale_y);
    if (!std::isfinite(scale) || scale <= 0.0) {
        throw application::InvalidImageError("cannot compute a valid YOLO letterbox scale");
    }

    const auto resized_width = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::llround(static_cast<double>(image.width) * scale)),
        1U,
        spec.width);
    const auto resized_height = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::llround(static_cast<double>(image.height) * scale)),
        1U,
        spec.height);
    return LetterboxMetadata{
        .source_width = image.width,
        .source_height = image.height,
        .input_width = spec.width,
        .input_height = spec.height,
        .scale = static_cast<float>(scale),
        .pad_left = (spec.width - resized_width) / 2U,
        .pad_top = (spec.height - resized_height) / 2U,
        .resized_width = resized_width,
        .resized_height = resized_height,
    };
}

} // namespace

YoloPosePreprocessor::YoloPosePreprocessor(YoloInputSpec spec)
    : spec_(spec),
      tensor_elements_(checked_tensor_size(spec_)) {}

YoloInputTensor YoloPosePreprocessor::preprocess(
    const application::ValidatedImage& image) const {
    YoloInputTensor result{};
    result.chw.resize(tensor_elements_);
    preprocess_into(image, result.chw, result.letterbox);
    return result;
}

YoloInputTensorView YoloPosePreprocessor::preprocess(
    const application::ValidatedImage& image,
    native_image::NativeImageWorkspace& workspace) const {
    auto output = workspace.prepare_tensor(tensor_elements_);
    LetterboxMetadata metadata{};
    preprocess_into(image, output, metadata);
    return YoloInputTensorView{output, metadata};
}

void YoloPosePreprocessor::preprocess_into(
    const application::ValidatedImage& image,
    const std::span<float> output,
    LetterboxMetadata& metadata) const {
    if (output.size() != tensor_elements_) {
        throw application::InvalidImageError("YOLO output tensor workspace has the wrong element count");
    }
    metadata = make_letterbox_metadata(image.view, spec_);
    const auto plane_size = spec_.width * spec_.height;
    const auto pad_normalized = spec_.pad_value * spec_.scale;
    std::fill(output.begin(), output.end(), pad_normalized);

    const auto scale_x = static_cast<double>(image.view.width) / static_cast<double>(metadata.resized_width);
    const auto scale_y = static_cast<double>(image.view.height) / static_cast<double>(metadata.resized_height);
    for (std::size_t resized_y = 0U; resized_y < metadata.resized_height; ++resized_y) {
        const auto source_y = ((static_cast<double>(resized_y) + 0.5) * scale_y) - 0.5;
        const auto output_y = metadata.pad_top + resized_y;
        for (std::size_t resized_x = 0U; resized_x < metadata.resized_width; ++resized_x) {
            const auto source_x = ((static_cast<double>(resized_x) + 0.5) * scale_x) - 0.5;
            const auto output_x = metadata.pad_left + resized_x;
            const auto sample = bilinear_sample(image.view, source_x, source_y);
            const auto index = (output_y * spec_.width) + output_x;
            output[index] = sample.red * spec_.scale;
            output[plane_size + index] = sample.green * spec_.scale;
            output[(2U * plane_size) + index] = sample.blue * spec_.scale;
        }
    }
}

} // namespace fac_lpr::infrastructure::yolo
