#include <fac_lpr/infrastructure/adaptive/adaptive_tile_detector.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/infrastructure/native_image/native_image.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace fac_lpr::infrastructure::adaptive {
namespace {

void check_context(const application::OperationContext& context) {
    if (context.cancellation_requested()) {
        throw application::CancelledError("adaptive detection cancelled");
    }
    if (context.deadline_exceeded()) {
        throw application::TimeoutError("adaptive detection deadline exceeded");
    }
}

void validate_config(const AdaptiveTileConfig& config) {
    if (config.tile_width == 0U || config.tile_height == 0U ||
        config.maximum_tiles == 0U || config.maximum_tiles > 4096U ||
        config.maximum_detections == 0U || config.maximum_detections > 4096U ||
        !std::isfinite(config.overlap_ratio) || config.overlap_ratio < 0.0F || config.overlap_ratio >= 0.50F ||
        !std::isfinite(config.merge_iou_threshold) || config.merge_iou_threshold < 0.0F || config.merge_iou_threshold > 1.0F) {
        throw application::ConfigurationError("adaptive tile detector configuration is invalid");
    }
}

[[nodiscard]] std::vector<std::size_t> axis_origins(
    const std::size_t extent,
    const std::size_t tile,
    const float overlap_ratio) {
    if (extent <= tile) {
        return {0U};
    }
    const auto overlap = static_cast<std::size_t>(std::floor(static_cast<double>(tile) * overlap_ratio));
    const auto step = tile - overlap;
    if (step == 0U) {
        throw application::ConfigurationError("adaptive tile step resolved to zero");
    }

    std::vector<std::size_t> origins{};
    for (std::size_t origin = 0U;;) {
        origins.push_back(origin);
        if (origin + tile >= extent) {
            break;
        }
        const auto last_origin = extent - tile;
        const auto next = origin > std::numeric_limits<std::size_t>::max() - step
            ? last_origin
            : std::min(origin + step, last_origin);
        if (next <= origin) {
            break;
        }
        origin = next;
    }
    return origins;
}

[[nodiscard]] float iou(const domain::BoundingBox& left, const domain::BoundingBox& right) noexcept {
    if (!left.is_valid() || !right.is_valid()) {
        return 0.0F;
    }
    const auto left_right = left.x + left.width;
    const auto left_bottom = left.y + left.height;
    const auto right_right = right.x + right.width;
    const auto right_bottom = right.y + right.height;
    if (!std::isfinite(left_right) || !std::isfinite(left_bottom) ||
        !std::isfinite(right_right) || !std::isfinite(right_bottom)) {
        return 0.0F;
    }
    const auto width = std::max(0.0F, std::min(left_right, right_right) - std::max(left.x, right.x));
    const auto height = std::max(0.0F, std::min(left_bottom, right_bottom) - std::max(left.y, right.y));
    const auto intersection = width * height;
    const auto union_area = left.area() + right.area() - intersection;
    return union_area > 0.0F && std::isfinite(union_area)
        ? std::clamp(intersection / union_area, 0.0F, 1.0F)
        : 0.0F;
}

[[nodiscard]] domain::Detection translate_detection(
    domain::Detection detection,
    const std::size_t offset_x,
    const std::size_t offset_y,
    const std::size_t source_width,
    const std::size_t source_height) {
    const auto x_offset = static_cast<float>(offset_x);
    const auto y_offset = static_cast<float>(offset_y);
    detection.bbox.x += x_offset;
    detection.bbox.y += y_offset;
    const auto x1 = std::clamp(detection.bbox.x, 0.0F, static_cast<float>(source_width));
    const auto y1 = std::clamp(detection.bbox.y, 0.0F, static_cast<float>(source_height));
    const auto x2 = std::clamp(detection.bbox.x + detection.bbox.width, 0.0F, static_cast<float>(source_width));
    const auto y2 = std::clamp(detection.bbox.y + detection.bbox.height, 0.0F, static_cast<float>(source_height));
    detection.bbox = domain::BoundingBox{x1, y1, std::max(0.0F, x2 - x1), std::max(0.0F, y2 - y1)};

    if (detection.quadrilateral.has_value()) {
        for (auto& point : detection.quadrilateral->points) {
            point.x = std::clamp(point.x + x_offset, 0.0F, static_cast<float>(source_width));
            point.y = std::clamp(point.y + y_offset, 0.0F, static_cast<float>(source_height));
        }
    }
    return detection;
}

[[nodiscard]] std::vector<domain::Detection> merge_detections(
    std::vector<domain::Detection> detections,
    const AdaptiveTileConfig& config) {
    std::stable_sort(
        detections.begin(), detections.end(),
        [](const domain::Detection& left, const domain::Detection& right) {
            if (left.confidence != right.confidence) {
                return left.confidence > right.confidence;
            }
            return left.geometry_score > right.geometry_score;
        });

    std::vector<domain::Detection> kept{};
    kept.reserve(std::min(detections.size(), config.maximum_detections));
    for (auto& detection : detections) {
        if (!detection.bbox.is_valid() || !std::isfinite(detection.confidence)) {
            continue;
        }
        const auto duplicate = std::any_of(
            kept.begin(), kept.end(),
            [&detection, &config](const domain::Detection& accepted) {
                return iou(detection.bbox, accepted.bbox) > config.merge_iou_threshold;
            });
        if (!duplicate) {
            kept.push_back(std::move(detection));
            if (kept.size() >= config.maximum_detections) {
                break;
            }
        }
    }
    return kept;
}

} // namespace

AdaptiveTileDetector::AdaptiveTileDetector(
    std::shared_ptr<application::IPlateDetector> detector,
    AdaptiveTileConfig config,
    application::PerformanceConfig image_limits)
    : detector_(std::move(detector)),
      config_(config),
      image_limits_(image_limits) {
    if (!detector_) {
        throw application::ConfigurationError("adaptive detector requires an underlying detector");
    }
    validate_config(config_);
}

std::string_view AdaptiveTileDetector::name() const noexcept {
    return "adaptive_tile";
}

std::vector<domain::Detection> AdaptiveTileDetector::detect(
    const application::ImageView& image,
    const application::OperationContext& context) {
    check_context(context);
    const auto validated = application::validate_image(image, image_limits_);
    auto combined = detector_->detect(validated.view, context);

    const auto needs_tiles = config_.enabled &&
        (validated.view.width > config_.tile_width || validated.view.height > config_.tile_height);
    if (!needs_tiles) {
        return merge_detections(std::move(combined), config_);
    }

    const auto x_origins = axis_origins(validated.view.width, config_.tile_width, config_.overlap_ratio);
    const auto y_origins = axis_origins(validated.view.height, config_.tile_height, config_.overlap_ratio);
    if (x_origins.size() > std::numeric_limits<std::size_t>::max() / y_origins.size() ||
        (x_origins.size() * y_origins.size()) > config_.maximum_tiles) {
        throw application::ResourceExhaustedError("adaptive tile count exceeds configured maximum");
    }

    for (const auto y : y_origins) {
        for (const auto x : x_origins) {
            check_context(context);
            const auto width = std::min(config_.tile_width, validated.view.width - x);
            const auto height = std::min(config_.tile_height, validated.view.height - y);
            const auto tile = native_image::make_crop_view(
                validated.view,
                application::ImageRegion{x, y, width, height});
            auto detections = detector_->detect(tile, context);
            for (auto& detection : detections) {
                combined.push_back(translate_detection(
                    std::move(detection), x, y, validated.view.width, validated.view.height));
            }
        }
    }

    return merge_detections(std::move(combined), config_);
}

} // namespace fac_lpr::infrastructure::adaptive
