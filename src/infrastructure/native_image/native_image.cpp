#include <fac_lpr/infrastructure/native_image/native_image.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>

namespace fac_lpr::infrastructure::native_image {
namespace {

[[nodiscard]] std::size_t checked_add(
    const std::size_t left,
    const std::size_t right,
    const char* field) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw application::InvalidImageError(std::string{field} + " overflows size_t");
    }
    return left + right;
}

[[nodiscard]] std::size_t checked_multiply(
    const std::size_t left,
    const std::size_t right,
    const char* field) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        throw application::InvalidImageError(std::string{field} + " overflows size_t");
    }
    return left * right;
}

[[nodiscard]] std::size_t pixel_bytes(const application::PixelFormat format) {
    const auto channels = application::pixel_format_channels(format);
    if (channels == 0U) {
        throw application::InvalidImageError("unsupported pixel format");
    }
    return channels;
}

void validate_region(
    const std::size_t source_width,
    const std::size_t source_height,
    const application::ImageRegion& region) {
    if (region.empty()) {
        throw application::InvalidImageError("crop region must be non-empty");
    }
    const auto right = checked_add(region.x, region.width, "crop x + width");
    const auto bottom = checked_add(region.y, region.height, "crop y + height");
    if (right > source_width || bottom > source_height) {
        throw application::InvalidImageError("crop region is outside source image bounds");
    }
}

template <typename ByteSpan>
[[nodiscard]] ByteSpan crop_bytes(
    const ByteSpan source,
    const std::size_t source_stride,
    const application::PixelFormat format,
    const application::ImageRegion& region) {
    const auto bytes_per_pixel = pixel_bytes(format);
    const auto x_offset = checked_multiply(region.x, bytes_per_pixel, "crop x byte offset");
    const auto y_offset = checked_multiply(region.y, source_stride, "crop y byte offset");
    const auto offset = checked_add(y_offset, x_offset, "crop byte offset");
    const auto row_bytes = checked_multiply(region.width, bytes_per_pixel, "crop row bytes");
    const auto tail_rows = region.height - 1U;
    const auto tail_offset = checked_multiply(tail_rows, source_stride, "crop tail offset");
    const auto required = checked_add(tail_offset, row_bytes, "crop required bytes");
    if (offset > source.size() || required > source.size() - offset) {
        throw application::InvalidImageError("crop view exceeds source buffer");
    }
    return source.subspan(offset, required);
}

[[nodiscard]] unsigned int luminance_at(
    const application::ImageView& image,
    const std::size_t x,
    const std::size_t y) noexcept {
    const auto* row = image.bytes.data() + (y * image.stride_bytes);
    if (image.format == application::PixelFormat::gray8) {
        return std::to_integer<unsigned int>(row[x]);
    }

    const auto base = x * 3U;
    unsigned int red = 0U;
    unsigned int green = std::to_integer<unsigned int>(row[base + 1U]);
    unsigned int blue = 0U;
    if (image.format == application::PixelFormat::rgb8) {
        red = std::to_integer<unsigned int>(row[base]);
        blue = std::to_integer<unsigned int>(row[base + 2U]);
    } else {
        blue = std::to_integer<unsigned int>(row[base]);
        red = std::to_integer<unsigned int>(row[base + 2U]);
    }
    return ((77U * red) + (150U * green) + (29U * blue) + 128U) >> 8U;
}

[[nodiscard]] float clamp01(const float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

void validate_quality_config(const CropQualityConfig& config) {
    if (config.min_width == 0U || config.min_height == 0U ||
        !std::isfinite(config.target_exposure) || config.target_exposure < 0.0F || config.target_exposure > 255.0F ||
        config.clipping_low > 255U || config.clipping_high > 255U || config.clipping_low >= config.clipping_high ||
        !std::isfinite(config.max_clipped_ratio) || config.max_clipped_ratio <= 0.0F || config.max_clipped_ratio > 1.0F ||
        !std::isfinite(config.sharpness_reference) || config.sharpness_reference <= 0.0F ||
        !std::isfinite(config.sharpness_weight) || config.sharpness_weight < 0.0F ||
        !std::isfinite(config.exposure_weight) || config.exposure_weight < 0.0F ||
        !std::isfinite(config.clipping_weight) || config.clipping_weight < 0.0F) {
        throw application::ConfigurationError("crop quality configuration is invalid");
    }
    if ((config.sharpness_weight + config.exposure_weight + config.clipping_weight) <= 0.0F) {
        throw application::ConfigurationError("crop quality weights must have a positive sum");
    }
}

} // namespace

std::span<float> NativeImageWorkspace::prepare_tensor(const std::size_t elements) {
    try {
        tensor_.resize(elements);
    } catch (const std::bad_alloc&) {
        throw application::ResourceExhaustedError("cannot allocate native tensor workspace");
    } catch (const std::length_error&) {
        throw application::ResourceExhaustedError("native tensor workspace exceeds vector limits");
    }
    return tensor_;
}

application::MutableImageView NativeImageWorkspace::prepare_image(
    const std::size_t width,
    const std::size_t height,
    const application::PixelFormat format) {
    if (width == 0U || height == 0U) {
        throw application::InvalidImageError("workspace image dimensions must be non-zero");
    }
    const auto channels = pixel_bytes(format);
    const auto stride = checked_multiply(width, channels, "workspace image stride");
    const auto required = checked_multiply(stride, height, "workspace image bytes");
    try {
        scratch_.resize(required);
    } catch (const std::bad_alloc&) {
        throw application::ResourceExhaustedError("cannot allocate native image workspace");
    } catch (const std::length_error&) {
        throw application::ResourceExhaustedError("native image workspace exceeds vector limits");
    }
    return application::MutableImageView{scratch_, width, height, stride, format};
}

application::ImageView make_crop_view(
    const application::ImageView& source,
    const application::ImageRegion& region) {
    validate_region(source.width, source.height, region);
    return application::ImageView{
        crop_bytes(source.bytes, source.stride_bytes, source.format, region),
        region.width,
        region.height,
        source.stride_bytes,
        source.format};
}

application::MutableImageView make_crop_view(
    const application::MutableImageView& source,
    const application::ImageRegion& region) {
    validate_region(source.width, source.height, region);
    return application::MutableImageView{
        crop_bytes(source.bytes, source.stride_bytes, source.format, region),
        region.width,
        region.height,
        source.stride_bytes,
        source.format};
}

void copy_crop(
    const application::ImageView& source,
    const application::ImageRegion& region,
    application::MutableImageView destination) {
    const auto crop = make_crop_view(source, region);
    if (destination.width != crop.width || destination.height != crop.height || destination.format != crop.format) {
        throw application::InvalidImageError("crop destination shape or pixel format does not match source region");
    }
    const auto row_bytes = checked_multiply(crop.width, pixel_bytes(crop.format), "copy crop row bytes");
    if (destination.stride_bytes < row_bytes) {
        throw application::InvalidImageError("crop destination stride is too small");
    }
    const auto required_destination = checked_add(
        checked_multiply(destination.height - 1U, destination.stride_bytes, "crop destination tail"),
        row_bytes,
        "crop destination required bytes");
    if (required_destination > destination.bytes.size()) {
        throw application::InvalidImageError("crop destination buffer is too small");
    }
    for (std::size_t y = 0U; y < crop.height; ++y) {
        const auto* source_row = crop.bytes.data() + (y * crop.stride_bytes);
        auto* destination_row = destination.bytes.data() + (y * destination.stride_bytes);
        std::memcpy(destination_row, source_row, row_bytes);
    }
}

CropQuality evaluate_crop_quality(
    const application::ImageView& image,
    const CropQualityConfig& config) {
    validate_quality_config(config);
    if (image.width < config.min_width || image.height < config.min_height || image.bytes.empty()) {
        return {};
    }

    const auto channels = pixel_bytes(image.format);
    const auto packed_row = checked_multiply(image.width, channels, "quality row bytes");
    if (image.stride_bytes < packed_row) {
        throw application::InvalidImageError("quality input stride is too small");
    }
    const auto required = checked_add(
        checked_multiply(image.height - 1U, image.stride_bytes, "quality image tail"),
        packed_row,
        "quality image bytes");
    if (required > image.bytes.size()) {
        throw application::InvalidImageError("quality input buffer is too small");
    }

    const auto pixel_count = checked_multiply(image.width, image.height, "quality pixel count");
    double intensity_sum = 0.0;
    std::size_t clipped = 0U;
    for (std::size_t y = 0U; y < image.height; ++y) {
        for (std::size_t x = 0U; x < image.width; ++x) {
            const auto value = luminance_at(image, x, y);
            intensity_sum += static_cast<double>(value);
            if (value <= config.clipping_low || value >= config.clipping_high) {
                ++clipped;
            }
        }
    }

    const auto mean = static_cast<float>(intensity_sum / static_cast<double>(pixel_count));
    const auto clipped_ratio = static_cast<float>(clipped) / static_cast<float>(pixel_count);
    const auto exposure_denominator = std::max(config.target_exposure, 255.0F - config.target_exposure);
    const auto exposure = exposure_denominator > 0.0F
        ? clamp01(1.0F - (std::abs(mean - config.target_exposure) / exposure_denominator))
        : 1.0F;
    const auto clipping = clamp01(1.0F - (clipped_ratio / config.max_clipped_ratio));

    double laplacian_sum = 0.0;
    double laplacian_square_sum = 0.0;
    std::size_t laplacian_count = 0U;
    if (image.width >= 3U && image.height >= 3U) {
        for (std::size_t y = 1U; y + 1U < image.height; ++y) {
            for (std::size_t x = 1U; x + 1U < image.width; ++x) {
                const auto center = static_cast<int>(luminance_at(image, x, y));
                const auto laplacian =
                    static_cast<int>(luminance_at(image, x - 1U, y)) +
                    static_cast<int>(luminance_at(image, x + 1U, y)) +
                    static_cast<int>(luminance_at(image, x, y - 1U)) +
                    static_cast<int>(luminance_at(image, x, y + 1U)) -
                    (4 * center);
                const auto value = static_cast<double>(laplacian);
                laplacian_sum += value;
                laplacian_square_sum += value * value;
                ++laplacian_count;
            }
        }
    }

    float variance = 0.0F;
    if (laplacian_count > 0U) {
        const auto count = static_cast<double>(laplacian_count);
        const auto laplacian_mean = laplacian_sum / count;
        variance = static_cast<float>(std::max(0.0, (laplacian_square_sum / count) - (laplacian_mean * laplacian_mean)));
    }
    const auto sharpness = clamp01(variance / config.sharpness_reference);
    const auto weight_sum = config.sharpness_weight + config.exposure_weight + config.clipping_weight;
    const auto overall = clamp01((
        (sharpness * config.sharpness_weight) +
        (exposure * config.exposure_weight) +
        (clipping * config.clipping_weight)) / weight_sum);

    return CropQuality{
        .sharpness = sharpness,
        .exposure = exposure,
        .clipping = clipping,
        .overall = overall,
        .mean_intensity = mean,
        .clipped_ratio = clipped_ratio,
        .laplacian_variance = variance,
    };
}

} // namespace fac_lpr::infrastructure::native_image
