#include <fac_lpr/infrastructure/crop/crop_hypothesis_generator.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/infrastructure/native_image/native_image.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <unordered_set>
#include <utility>

namespace fac_lpr::infrastructure::crop {
namespace {

void check_context(const application::OperationContext& context) {
    if (context.cancellation_requested()) {
        throw application::CancelledError("crop generation cancelled");
    }
    if (context.deadline_exceeded()) {
        throw application::TimeoutError("crop generation deadline exceeded");
    }
}

void validate_config(const CropHypothesisGeneratorConfig& config) {
    if (config.generator_order.empty() || config.generator_order.size() > 32U ||
        config.maximum_hypotheses == 0U || config.maximum_hypotheses > 32U ||
        config.minimum_width == 0U || config.minimum_height == 0U ||
        !std::isfinite(config.horizontal_padding_ratio) || config.horizontal_padding_ratio < 0.0F || config.horizontal_padding_ratio > 0.50F ||
        !std::isfinite(config.vertical_padding_ratio) || config.vertical_padding_ratio < 0.0F || config.vertical_padding_ratio > 0.50F ||
        !std::isfinite(config.plate_dominant_min_area_ratio) ||
        config.plate_dominant_min_area_ratio <= 0.0F ||
        config.plate_dominant_min_area_ratio > 1.0F) {
        throw application::ConfigurationError("crop hypothesis generator configuration is invalid");
    }
    std::unordered_set<int> seen{};
    for (const auto kind : config.generator_order) {
        if (kind == CropGeneratorKind::plate_dominant_source) {
            throw application::ConfigurationError(
                "plate-dominant source crop is automatic and must not be registered explicitly");
        }
        if (!seen.insert(static_cast<int>(kind)).second) {
            throw application::ConfigurationError("crop generator registry contains duplicate generator kinds");
        }
    }
}

[[nodiscard]] std::size_t checked_multiply(
    const std::size_t left,
    const std::size_t right,
    const char* field) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        throw application::ResourceExhaustedError(std::string{field} + " overflows size_t");
    }
    return left * right;
}

[[nodiscard]] application::ImageBuffer pack_copy(const application::ImageView& view) {
    const auto channels = application::pixel_format_channels(view.format);
    if (channels == 0U || view.width == 0U || view.height == 0U) {
        throw application::InvalidImageError("cannot pack invalid crop view");
    }
    const auto stride = checked_multiply(view.width, channels, "crop packed stride");
    const auto required = checked_multiply(stride, view.height, "crop packed bytes");
    application::ImageBuffer output{};
    try {
        output.bytes.resize(required);
    } catch (const std::bad_alloc&) {
        throw application::ResourceExhaustedError("cannot allocate crop hypothesis");
    } catch (const std::length_error&) {
        throw application::ResourceExhaustedError("crop hypothesis exceeds vector limits");
    }
    output.width = view.width;
    output.height = view.height;
    output.stride_bytes = stride;
    output.format = view.format;
    native_image::copy_crop(
        view,
        application::ImageRegion{0U, 0U, view.width, view.height},
        output.mutable_view());
    return output;
}

[[nodiscard]] application::ImageRegion bbox_region(
    const domain::BoundingBox& box,
    const application::ImageView& source,
    const float horizontal_padding,
    const float vertical_padding) {
    if (!box.is_valid()) {
        return {};
    }
    const auto pad_x = box.width * horizontal_padding;
    const auto pad_y = box.height * vertical_padding;
    const auto left = std::clamp(box.x - pad_x, 0.0F, static_cast<float>(source.width));
    const auto top = std::clamp(box.y - pad_y, 0.0F, static_cast<float>(source.height));
    const auto right = std::clamp(box.x + box.width + pad_x, 0.0F, static_cast<float>(source.width));
    const auto bottom = std::clamp(box.y + box.height + pad_y, 0.0F, static_cast<float>(source.height));
    if (!(right > left && bottom > top)) {
        return {};
    }
    const auto x = static_cast<std::size_t>(std::floor(left));
    const auto y = static_cast<std::size_t>(std::floor(top));
    const auto x2 = std::min(source.width, static_cast<std::size_t>(std::ceil(right)));
    const auto y2 = std::min(source.height, static_cast<std::size_t>(std::ceil(bottom)));
    return x2 > x && y2 > y
        ? application::ImageRegion{x, y, x2 - x, y2 - y}
        : application::ImageRegion{};
}

[[nodiscard]] float detection_area_ratio(
    const domain::BoundingBox& box,
    const application::ImageView& source) noexcept {
    if (!box.is_valid() || source.width == 0U || source.height == 0U) {
        return 0.0F;
    }
    const auto left = std::clamp(box.x, 0.0F, static_cast<float>(source.width));
    const auto top = std::clamp(box.y, 0.0F, static_cast<float>(source.height));
    const auto right = std::clamp(
        box.x + box.width,
        0.0F,
        static_cast<float>(source.width));
    const auto bottom = std::clamp(
        box.y + box.height,
        0.0F,
        static_cast<float>(source.height));
    if (!(right > left && bottom > top)) {
        return 0.0F;
    }
    const auto detection_area = static_cast<double>(right - left) *
                                static_cast<double>(bottom - top);
    const auto source_area = static_cast<double>(source.width) *
                             static_cast<double>(source.height);
    if (!(source_area > 0.0)) {
        return 0.0F;
    }
    return static_cast<float>(std::clamp(detection_area / source_area, 0.0, 1.0));
}

[[nodiscard]] const char* kind_name(const CropGeneratorKind kind) noexcept {
    switch (kind) {
        case CropGeneratorKind::plate_dominant_source: return "plate_dominant_source";
        case CropGeneratorKind::rectified: return "rectified";
        case CropGeneratorKind::raw_bbox: return "raw_bbox";
        case CropGeneratorKind::padded_bbox: return "padded_bbox";
    }
    return "unknown";
}

} // namespace

std::uint64_t fingerprint_crop(const application::ImageView& image) {
    if (image.width == 0U || image.height == 0U || image.bytes.empty()) {
        throw application::InvalidImageError("cannot fingerprint empty crop");
    }
    const auto channels = application::pixel_format_channels(image.format);
    if (channels == 0U || image.width > std::numeric_limits<std::size_t>::max() / channels) {
        throw application::InvalidImageError("invalid crop shape for fingerprinting");
    }
    const auto row_bytes = image.width * channels;
    if (image.stride_bytes < row_bytes) {
        throw application::InvalidImageError("crop stride is invalid for fingerprinting");
    }

    constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    auto hash = offset_basis;
    const auto mix = [&hash](const unsigned char value) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= prime;
    };
    for (std::size_t shift = 0U; shift < sizeof(std::size_t); ++shift) {
        mix(static_cast<unsigned char>((image.width >> (shift * 8U)) & 0xFFU));
        mix(static_cast<unsigned char>((image.height >> (shift * 8U)) & 0xFFU));
    }
    mix(static_cast<unsigned char>(image.format));
    for (std::size_t y = 0U; y < image.height; ++y) {
        const auto* row = image.bytes.data() + (y * image.stride_bytes);
        for (std::size_t x = 0U; x < row_bytes; ++x) {
            mix(std::to_integer<unsigned char>(row[x]));
        }
    }
    return hash;
}

CropHypothesisGenerator::CropHypothesisGenerator(CropHypothesisGeneratorConfig config)
    : config_(std::move(config)) {
    validate_config(config_);
}

std::vector<application::CropHypothesis> CropHypothesisGenerator::generate(
    const application::ImageView& source,
    const domain::Detection& detection,
    const std::optional<application::ImageBuffer>& aligned,
    const application::OperationContext& context) {
    check_context(context);
    std::vector<application::CropHypothesis> hypotheses{};
    hypotheses.reserve(std::min(config_.maximum_hypotheses, config_.generator_order.size() + 1U));
    std::unordered_set<std::uint64_t> fingerprints{};

    const auto try_add = [&](const application::ImageView& view, const CropGeneratorKind kind, const char* source_name) {
        if (hypotheses.size() >= config_.maximum_hypotheses ||
            view.width < config_.minimum_width || view.height < config_.minimum_height || view.bytes.empty()) {
            return;
        }
        const auto fingerprint = fingerprint_crop(view);
        if (!fingerprints.insert(fingerprint).second) {
            return;
        }
        hypotheses.push_back(application::CropHypothesis{
            .image = pack_copy(view),
            .type = kind_name(kind),
            .source = source_name,
            .fingerprint = fingerprint,
            .quality = 0.0F,
        });
    };

    const auto plate_dominant = config_.prefer_source_when_detection_dominates_frame &&
        detection_area_ratio(detection.bbox, source) >= config_.plate_dominant_min_area_ratio;
    if (plate_dominant) {
        try_add(source, CropGeneratorKind::plate_dominant_source, "source_image");

        const auto raw_region = bbox_region(detection.bbox, source, 0.0F, 0.0F);
        if (!raw_region.empty()) {
            try_add(
                native_image::make_crop_view(source, raw_region),
                CropGeneratorKind::raw_bbox,
                "source_image");
        }

        const auto padded_region = bbox_region(
            detection.bbox,
            source,
            config_.horizontal_padding_ratio,
            config_.vertical_padding_ratio);
        if (!padded_region.empty()) {
            try_add(
                native_image::make_crop_view(source, padded_region),
                CropGeneratorKind::padded_bbox,
                "source_image");
        }
        return hypotheses;
    }

    for (const auto kind : config_.generator_order) {
        check_context(context);
        if (hypotheses.size() >= config_.maximum_hypotheses) {
            break;
        }
        switch (kind) {
            case CropGeneratorKind::plate_dominant_source:
                break;
            case CropGeneratorKind::rectified:
                if (aligned.has_value()) {
                    try_add(aligned->view(), kind, "perspective_aligner");
                }
                break;
            case CropGeneratorKind::raw_bbox: {
                const auto region = bbox_region(detection.bbox, source, 0.0F, 0.0F);
                if (!region.empty()) {
                    try_add(native_image::make_crop_view(source, region), kind, "source_image");
                }
                break;
            }
            case CropGeneratorKind::padded_bbox: {
                const auto region = bbox_region(
                    detection.bbox,
                    source,
                    config_.horizontal_padding_ratio,
                    config_.vertical_padding_ratio);
                if (!region.empty()) {
                    try_add(native_image::make_crop_view(source, region), kind, "source_image");
                }
                break;
            }
        }
    }
    return hypotheses;
}

} // namespace fac_lpr::infrastructure::crop
