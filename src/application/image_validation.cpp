#include <fac_lpr/application/image_validation.hpp>

#include <fac_lpr/application/error.hpp>

#include <limits>
#include <span>
#include <string>

namespace fac_lpr::application {
namespace {

[[nodiscard]] std::size_t checked_add(
    const std::size_t left,
    const std::size_t right,
    const char* field) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw InvalidImageError(std::string{field} + " overflows size_t");
    }
    return left + right;
}

[[nodiscard]] std::size_t checked_multiply(
    const std::size_t left,
    const std::size_t right,
    const char* field) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        throw InvalidImageError(std::string{field} + " overflows size_t");
    }
    return left * right;
}

} // namespace

std::size_t pixel_format_channels(const PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::gray8:
            return 1U;
        case PixelFormat::bgr8:
        case PixelFormat::rgb8:
            return 3U;
    }
    return 0U;
}

ImageView make_image_view(
    const void* data,
    const std::size_t byte_count,
    const std::size_t width,
    const std::size_t height,
    const std::size_t stride_bytes,
    const PixelFormat format) {
    if (data == nullptr && byte_count != 0U) {
        throw InvalidImageError("image data pointer is null while byte_count is non-zero");
    }

    const auto* bytes = static_cast<const std::byte*>(data);
    return ImageView{
        std::span<const std::byte>{bytes, byte_count},
        width,
        height,
        stride_bytes,
        format,
    };
}

ValidatedImage validate_image(
    const ImageView& image,
    const PerformanceConfig& limits) {
    if (image.width == 0U || image.height == 0U) {
        throw InvalidImageError("image width and height must be greater than zero");
    }
    if (image.bytes.empty()) {
        throw InvalidImageError("image buffer is empty");
    }
    if (image.width > limits.max_image_width || image.height > limits.max_image_height) {
        throw InvalidImageError("image dimensions exceed configured limits");
    }
    if (image.bytes.size() > limits.max_image_bytes) {
        throw InvalidImageError("image buffer exceeds configured byte limit");
    }

    const auto channels = pixel_format_channels(image.format);
    if (channels == 0U) {
        throw InvalidImageError("unsupported pixel format");
    }

    const auto minimum_row_bytes = checked_multiply(image.width, channels, "image row size");
    if (image.stride_bytes < minimum_row_bytes) {
        throw InvalidImageError("image stride is smaller than minimum packed row size");
    }

    const auto rows_before_last = image.height - 1U;
    const auto last_row_offset = checked_multiply(image.stride_bytes, rows_before_last, "image last row offset");
    const auto required_bytes = checked_add(last_row_offset, minimum_row_bytes, "image required byte size");
    if (required_bytes > image.bytes.size()) {
        throw InvalidImageError("image buffer is smaller than the strided image extent");
    }
    if (required_bytes > limits.max_image_bytes) {
        throw InvalidImageError("validated image size exceeds configured byte limit");
    }

    return ValidatedImage{
        .view = image,
        .channels = channels,
        .minimum_row_bytes = minimum_row_bytes,
        .required_bytes = required_bytes,
    };
}

} // namespace fac_lpr::application
