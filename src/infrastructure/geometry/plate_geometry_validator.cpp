#include <fac_lpr/infrastructure/geometry/plate_geometry_validator.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>

namespace fac_lpr::infrastructure::geometry {
namespace {

constexpr float epsilon = 1.0e-4F;

[[nodiscard]] float clamp01(const float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float distance(const domain::Point2f& left, const domain::Point2f& right) noexcept {
    return std::hypot(right.x - left.x, right.y - left.y);
}

[[nodiscard]] float cross(
    const domain::Point2f& a,
    const domain::Point2f& b,
    const domain::Point2f& c) noexcept {
    return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

[[nodiscard]] float polygon_area(const std::array<domain::Point2f, 4>& points) noexcept {
    double sum = 0.0;
    for (std::size_t index = 0U; index < points.size(); ++index) {
        const auto& current = points[index];
        const auto& next = points[(index + 1U) % points.size()];
        sum += (static_cast<double>(current.x) * static_cast<double>(next.y)) -
               (static_cast<double>(current.y) * static_cast<double>(next.x));
    }
    return static_cast<float>(std::abs(sum) * 0.5);
}

[[nodiscard]] bool points_are_unique(const std::array<domain::Point2f, 4>& points) noexcept {
    for (std::size_t left = 0U; left < points.size(); ++left) {
        for (std::size_t right = left + 1U; right < points.size(); ++right) {
            if (distance(points[left], points[right]) <= epsilon) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool polygon_is_convex(const std::array<domain::Point2f, 4>& points) noexcept {
    float sign = 0.0F;
    for (std::size_t index = 0U; index < points.size(); ++index) {
        const auto value = cross(
            points[index],
            points[(index + 1U) % points.size()],
            points[(index + 2U) % points.size()]);
        if (!std::isfinite(value) || std::abs(value) <= epsilon) {
            return false;
        }
        if (sign == 0.0F) {
            sign = value;
        } else if ((sign > 0.0F) != (value > 0.0F)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] float range_score(
    const float value,
    const float hard_min,
    const float preferred_min,
    const float preferred_max,
    const float hard_max) noexcept {
    if (!std::isfinite(value) || value < hard_min || value > hard_max) {
        return 0.0F;
    }
    if (value >= preferred_min && value <= preferred_max) {
        return 1.0F;
    }
    if (value < preferred_min) {
        const auto denominator = preferred_min - hard_min;
        return denominator <= epsilon ? 1.0F : clamp01((value - hard_min) / denominator);
    }
    const auto denominator = hard_max - preferred_max;
    return denominator <= epsilon ? 1.0F : clamp01((hard_max - value) / denominator);
}

[[nodiscard]] float fill_score(
    const float ratio,
    const float minimum,
    const float maximum) noexcept {
    if (!std::isfinite(ratio) || ratio < minimum || ratio > maximum) {
        return 0.0F;
    }
    if (ratio <= 1.0F) {
        const auto denominator = 1.0F - minimum;
        return denominator <= epsilon ? 1.0F : clamp01((ratio - minimum) / denominator);
    }
    const auto denominator = maximum - 1.0F;
    return denominator <= epsilon ? 1.0F : clamp01((maximum - ratio) / denominator);
}

[[nodiscard]] bool point_near_box(
    const domain::Point2f& point,
    const domain::BoundingBox& box,
    const float maximum_ratio) noexcept {
    const auto x2 = box.x + box.width;
    const auto y2 = box.y + box.height;
    const auto dx = point.x < box.x ? box.x - point.x : (point.x > x2 ? point.x - x2 : 0.0F);
    const auto dy = point.y < box.y ? box.y - point.y : (point.y > y2 ? point.y - y2 : 0.0F);
    const auto reference = std::max(box.width, box.height);
    if (!std::isfinite(reference) || reference <= epsilon) {
        return false;
    }
    return (std::hypot(dx, dy) / reference) <= maximum_ratio;
}

void validate_config(const GeometryConfig& config) {
    const std::array<float, 13> values{
        config.minimum_keypoint_confidence,
        config.minimum_box_aspect_ratio,
        config.maximum_box_aspect_ratio,
        config.preferred_box_aspect_min,
        config.preferred_box_aspect_max,
        config.minimum_crop_aspect_ratio,
        config.maximum_crop_aspect_ratio,
        config.preferred_crop_aspect_min,
        config.preferred_crop_aspect_max,
        config.minimum_polygon_box_area_ratio,
        config.maximum_polygon_box_area_ratio,
        config.minimum_horizontal_edge_pixels,
        config.minimum_vertical_edge_pixels,
    };
    if (!std::all_of(values.begin(), values.end(), [](const float value) { return std::isfinite(value); }) ||
        !std::isfinite(config.maximum_point_distance_from_box_ratio)) {
        throw application::ConfigurationError("geometry configuration must contain finite values");
    }
    if (config.minimum_keypoint_confidence < 0.0F || config.minimum_keypoint_confidence > 1.0F) {
        throw application::ConfigurationError("minimum_keypoint_confidence must be in [0,1]");
    }
    if (!(config.minimum_box_aspect_ratio > 0.0F &&
          config.minimum_box_aspect_ratio <= config.preferred_box_aspect_min &&
          config.preferred_box_aspect_min <= config.preferred_box_aspect_max &&
          config.preferred_box_aspect_max <= config.maximum_box_aspect_ratio)) {
        throw application::ConfigurationError("box aspect-ratio ranges are inconsistent");
    }
    if (!(config.minimum_crop_aspect_ratio > 0.0F &&
          config.minimum_crop_aspect_ratio <= config.preferred_crop_aspect_min &&
          config.preferred_crop_aspect_min <= config.preferred_crop_aspect_max &&
          config.preferred_crop_aspect_max <= config.maximum_crop_aspect_ratio)) {
        throw application::ConfigurationError("crop aspect-ratio ranges are inconsistent");
    }
    if (!(config.minimum_polygon_box_area_ratio > 0.0F &&
          config.minimum_polygon_box_area_ratio <= 1.0F &&
          config.maximum_polygon_box_area_ratio >= 1.0F)) {
        throw application::ConfigurationError("polygon/bbox area-ratio range must contain 1.0");
    }
    if (config.minimum_horizontal_edge_pixels <= 0.0F ||
        config.minimum_vertical_edge_pixels <= 0.0F ||
        config.maximum_point_distance_from_box_ratio < 0.0F) {
        throw application::ConfigurationError("geometry pixel/distance limits are invalid");
    }
}

[[nodiscard]] GeometryEvaluation invalid_evaluation(
    std::string reason,
    const std::array<domain::Point2f, 4>& ordered = {}) {
    GeometryEvaluation result{};
    result.reason = std::move(reason);
    result.ordered_corners = ordered;
    return result;
}

} // namespace

std::array<domain::Point2f, 4> order_plate_corners(
    const std::array<domain::Point2f, 4>& points) {
    if (!std::all_of(points.begin(), points.end(), [](const domain::Point2f& point) {
            return point.is_finite();
        })) {
        throw application::InvalidImageError("plate corner contains non-finite coordinate");
    }

    const auto center_x = std::accumulate(
        points.begin(), points.end(), 0.0,
        [](const double value, const domain::Point2f& point) { return value + point.x; }) / 4.0;
    const auto center_y = std::accumulate(
        points.begin(), points.end(), 0.0,
        [](const double value, const domain::Point2f& point) { return value + point.y; }) / 4.0;

    auto ordered = points;
    std::stable_sort(
        ordered.begin(), ordered.end(),
        [center_x, center_y](const domain::Point2f& left, const domain::Point2f& right) {
            const auto left_angle = std::atan2(
                static_cast<double>(left.y) - center_y,
                static_cast<double>(left.x) - center_x);
            const auto right_angle = std::atan2(
                static_cast<double>(right.y) - center_y,
                static_cast<double>(right.x) - center_x);
            if (left_angle != right_angle) {
                return left_angle < right_angle;
            }
            if (left.y != right.y) {
                return left.y < right.y;
            }
            return left.x < right.x;
        });

    const auto top_left = static_cast<std::size_t>(std::distance(
        ordered.begin(),
        std::min_element(
            ordered.begin(), ordered.end(),
            [](const domain::Point2f& left, const domain::Point2f& right) {
                const auto left_sum = left.x + left.y;
                const auto right_sum = right.x + right.y;
                if (left_sum != right_sum) {
                    return left_sum < right_sum;
                }
                return left.y < right.y;
            })));

    std::array<domain::Point2f, 4> rotated{};
    for (std::size_t index = 0U; index < rotated.size(); ++index) {
        rotated[index] = ordered[(top_left + index) % ordered.size()];
    }
    return rotated;
}

PlateGeometryValidator::PlateGeometryValidator(GeometryConfig config)
    : config_(config) {
    validate_config(config_);
}

GeometryEvaluation PlateGeometryValidator::evaluate(const domain::Detection& detection) const {
    if (!detection.bbox.is_valid()) {
        return invalid_evaluation("invalid_bbox");
    }
    const auto box_ratio = detection.bbox.width / detection.bbox.height;
    const auto box_ratio_score = range_score(
        box_ratio,
        config_.minimum_box_aspect_ratio,
        config_.preferred_box_aspect_min,
        config_.preferred_box_aspect_max,
        config_.maximum_box_aspect_ratio);
    if (box_ratio_score <= 0.0F) {
        return invalid_evaluation("bbox_aspect_ratio_out_of_range");
    }
    if (!detection.quadrilateral.has_value()) {
        return invalid_evaluation("missing_quadrilateral");
    }

    const auto& quadrilateral = *detection.quadrilateral;
    if (!std::all_of(
            quadrilateral.points.begin(), quadrilateral.points.end(),
            [](const domain::Point2f& point) { return point.is_finite(); })) {
        return invalid_evaluation("non_finite_corner");
    }
    if (!points_are_unique(quadrilateral.points)) {
        return invalid_evaluation("duplicate_corner");
    }

    const auto ordered = order_plate_corners(quadrilateral.points);
    if (!polygon_is_convex(ordered)) {
        return invalid_evaluation("non_convex_quadrilateral", ordered);
    }

    for (const auto& point : ordered) {
        if (!point_near_box(point, detection.bbox, config_.maximum_point_distance_from_box_ratio)) {
            return invalid_evaluation("corner_too_far_from_bbox", ordered);
        }
    }

    float keypoint_sum = 0.0F;
    for (const auto confidence : quadrilateral.confidences) {
        if (!std::isfinite(confidence) || confidence < config_.minimum_keypoint_confidence || confidence > 1.0F) {
            return invalid_evaluation("keypoint_confidence_below_threshold", ordered);
        }
        keypoint_sum += confidence;
    }
    const auto keypoint_score = clamp01(keypoint_sum / static_cast<float>(quadrilateral.confidences.size()));

    const auto top = distance(ordered[0], ordered[1]);
    const auto right = distance(ordered[1], ordered[2]);
    const auto bottom = distance(ordered[2], ordered[3]);
    const auto left = distance(ordered[3], ordered[0]);
    if (!std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom) || !std::isfinite(left)) {
        return invalid_evaluation("non_finite_edge", ordered);
    }
    if (top < config_.minimum_horizontal_edge_pixels || bottom < config_.minimum_horizontal_edge_pixels ||
        left < config_.minimum_vertical_edge_pixels || right < config_.minimum_vertical_edge_pixels) {
        return invalid_evaluation("edge_too_short", ordered);
    }

    const auto average_width = (top + bottom) * 0.5F;
    const auto average_height = (left + right) * 0.5F;
    if (average_height <= epsilon) {
        return invalid_evaluation("degenerate_crop_height", ordered);
    }
    const auto crop_ratio = average_width / average_height;
    const auto crop_ratio_score = range_score(
        crop_ratio,
        config_.minimum_crop_aspect_ratio,
        config_.preferred_crop_aspect_min,
        config_.preferred_crop_aspect_max,
        config_.maximum_crop_aspect_ratio);
    if (crop_ratio_score <= 0.0F) {
        return invalid_evaluation("crop_aspect_ratio_out_of_range", ordered);
    }

    const auto area = polygon_area(ordered);
    const auto box_area = detection.bbox.area();
    if (!std::isfinite(area) || !std::isfinite(box_area) || area <= epsilon || box_area <= epsilon) {
        return invalid_evaluation("degenerate_polygon_area", ordered);
    }
    const auto area_ratio = area / box_area;
    if (area_ratio < config_.minimum_polygon_box_area_ratio ||
        area_ratio > config_.maximum_polygon_box_area_ratio) {
        return invalid_evaluation("polygon_bbox_area_ratio_out_of_range", ordered);
    }

    const auto horizontal_score = std::min(top, bottom) / std::max(top, bottom);
    const auto vertical_score = std::min(left, right) / std::max(left, right);
    const auto deformation_score = clamp01((horizontal_score + vertical_score) * 0.5F);
    const auto pixel_score = clamp01(std::min(
        std::min(top, bottom) / config_.minimum_horizontal_edge_pixels,
        std::min(left, right) / config_.minimum_vertical_edge_pixels));
    const auto polygon_fill_score = fill_score(
        area_ratio,
        config_.minimum_polygon_box_area_ratio,
        config_.maximum_polygon_box_area_ratio);

    GeometryEvaluation result{};
    result.valid = true;
    result.box_ratio_score = box_ratio_score;
    result.crop_ratio_score = crop_ratio_score;
    result.pixel_score = pixel_score;
    result.keypoint_score = keypoint_score;
    result.deformation_score = deformation_score;
    result.polygon_fill_score = polygon_fill_score;
    result.polygon_box_area_ratio = area_ratio;
    result.crop_aspect_ratio = crop_ratio;
    result.ordered_corners = ordered;
    result.reason = "ok";
    result.score = clamp01(
        (0.15F * box_ratio_score) +
        (0.20F * crop_ratio_score) +
        (0.15F * pixel_score) +
        (0.20F * keypoint_score) +
        (0.15F * deformation_score) +
        (0.15F * polygon_fill_score));
    return result;
}

} // namespace fac_lpr::infrastructure::geometry
