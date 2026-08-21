#include <fac_lpr/infrastructure/yolo/yolo_pose_parser.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <utility>

namespace fac_lpr::infrastructure::yolo {
namespace {

struct OutputMatrix final {
    std::size_t features{0U};
    std::size_t candidates{0U};
    CandidateLayout layout{CandidateLayout::features_first};
    std::span<const float> data{};

    [[nodiscard]] float at(const std::size_t feature, const std::size_t candidate) const {
        if (feature >= features || candidate >= candidates) {
            throw application::InferenceError("YOLO output index is outside tensor bounds");
        }
        const auto index = layout == CandidateLayout::features_first
            ? (feature * candidates) + candidate
            : (candidate * features) + feature;
        return data[index];
    }
};

[[nodiscard]] std::size_t checked_dimension(const std::int64_t value, const char* field) {
    if (value <= 0) {
        throw application::InferenceError(std::string{"YOLO output "} + field + " must be a positive static dimension");
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] OutputMatrix resolve_matrix(
    const std::span<const float> output,
    const std::span<const std::int64_t> shape,
    const CandidateLayout layout) {
    std::size_t first = 0U;
    std::size_t second = 0U;

    if (shape.size() == 3U) {
        if (shape[0] != 1) {
            throw application::InferenceError("YOLO output batch dimension must be 1");
        }
        first = checked_dimension(shape[1], "dimension 1");
        second = checked_dimension(shape[2], "dimension 2");
    } else if (shape.size() == 2U) {
        first = checked_dimension(shape[0], "dimension 0");
        second = checked_dimension(shape[1], "dimension 1");
    } else {
        throw application::InferenceError("YOLO output must be a rank-2 or rank-3 tensor");
    }

    if (first > std::numeric_limits<std::size_t>::max() / second || first * second != output.size()) {
        throw application::InferenceError("YOLO output shape does not match tensor element count");
    }

    return layout == CandidateLayout::features_first
        ? OutputMatrix{first, second, layout, output}
        : OutputMatrix{second, first, layout, output};
}

[[nodiscard]] float finite_probability(const float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
}

[[nodiscard]] float source_x(const float x, const LetterboxMetadata& metadata) noexcept {
    const auto value = (x - static_cast<float>(metadata.pad_left)) / metadata.scale;
    return std::clamp(value, 0.0F, static_cast<float>(metadata.source_width));
}

[[nodiscard]] float source_y(const float y, const LetterboxMetadata& metadata) noexcept {
    const auto value = (y - static_cast<float>(metadata.pad_top)) / metadata.scale;
    return std::clamp(value, 0.0F, static_cast<float>(metadata.source_height));
}

void validate_spec(const YoloPoseOutputSpec& spec) {
    if (spec.class_count == 0U) {
        throw application::ConfigurationError("YOLO pose output class_count must be positive");
    }
    if (spec.keypoint_count != domain::PlateQuadrilateral::point_count) {
        throw application::ConfigurationError("plate YOLO pose parser requires exactly four keypoints");
    }
    if (spec.keypoint_stride < 2U || spec.keypoint_x_offset >= spec.keypoint_stride ||
        spec.keypoint_y_offset >= spec.keypoint_stride) {
        throw application::ConfigurationError("YOLO keypoint stride/coordinate offsets are invalid");
    }
    if (spec.keypoint_confidence_offset.has_value() &&
        *spec.keypoint_confidence_offset >= spec.keypoint_stride) {
        throw application::ConfigurationError("YOLO keypoint confidence offset exceeds keypoint stride");
    }
    if (!std::isfinite(spec.confidence_threshold) || spec.confidence_threshold < 0.0F ||
        spec.confidence_threshold > 1.0F || !std::isfinite(spec.nms_iou_threshold) ||
        spec.nms_iou_threshold < 0.0F || spec.nms_iou_threshold > 1.0F) {
        throw application::ConfigurationError("YOLO confidence/NMS thresholds must be finite probabilities");
    }
    if (spec.maximum_detections == 0U || spec.maximum_detections > 4096U) {
        throw application::ConfigurationError("YOLO maximum_detections must be in [1, 4096]");
    }
}

[[nodiscard]] std::size_t required_features(const YoloPoseOutputSpec& spec) {
    auto required = spec.box_offset + 4U;
    required = std::max(required, spec.class_score_offset + spec.class_count);
    required = std::max(required, spec.keypoint_offset + (spec.keypoint_count * spec.keypoint_stride));
    if (spec.objectness_offset.has_value()) {
        required = std::max(required, *spec.objectness_offset + 1U);
    }
    return required;
}

} // namespace

float intersection_over_union(
    const domain::BoundingBox& left,
    const domain::BoundingBox& right) noexcept {
    if (!left.is_valid() || !right.is_valid()) {
        return 0.0F;
    }

    const auto left_x2 = left.x + left.width;
    const auto left_y2 = left.y + left.height;
    const auto right_x2 = right.x + right.width;
    const auto right_y2 = right.y + right.height;

    const auto intersection_width = std::max(0.0F, std::min(left_x2, right_x2) - std::max(left.x, right.x));
    const auto intersection_height = std::max(0.0F, std::min(left_y2, right_y2) - std::max(left.y, right.y));
    const auto intersection = intersection_width * intersection_height;
    const auto union_area = left.area() + right.area() - intersection;
    if (union_area <= 0.0F || !std::isfinite(union_area)) {
        return 0.0F;
    }
    return std::clamp(intersection / union_area, 0.0F, 1.0F);
}

YoloPoseOutputParser::YoloPoseOutputParser(YoloPoseOutputSpec spec)
    : spec_(std::move(spec)) {
    validate_spec(spec_);
}

std::vector<domain::Detection> YoloPoseOutputParser::parse(
    const std::span<const float> output,
    const std::span<const std::int64_t> shape,
    const LetterboxMetadata& letterbox) const {
    if (output.empty()) {
        return {};
    }
    if (!std::isfinite(letterbox.scale) || letterbox.scale <= 0.0F ||
        letterbox.source_width == 0U || letterbox.source_height == 0U) {
        throw application::InferenceError("YOLO letterbox metadata is invalid");
    }

    const auto matrix = resolve_matrix(output, shape, spec_.layout);
    if (matrix.features < required_features(spec_)) {
        throw application::InferenceError("YOLO output has fewer feature columns than configured parser contract");
    }

    struct RankedDetection final {
        domain::Detection detection{};
        std::size_t source_index{0U};
    };

    std::vector<RankedDetection> candidates{};
    candidates.reserve(std::min(matrix.candidates, spec_.maximum_detections * 8U));

    for (std::size_t candidate = 0; candidate < matrix.candidates; ++candidate) {
        float class_confidence = 0.0F;
        for (std::size_t class_index = 0; class_index < spec_.class_count; ++class_index) {
            class_confidence = std::max(
                class_confidence,
                finite_probability(matrix.at(spec_.class_score_offset + class_index, candidate)));
        }
        const auto objectness = spec_.objectness_offset.has_value()
            ? finite_probability(matrix.at(*spec_.objectness_offset, candidate))
            : 1.0F;
        const auto confidence = class_confidence * objectness;
        if (confidence < spec_.confidence_threshold) {
            continue;
        }

        const auto cx = matrix.at(spec_.box_offset, candidate);
        const auto cy = matrix.at(spec_.box_offset + 1U, candidate);
        const auto width = matrix.at(spec_.box_offset + 2U, candidate);
        const auto height = matrix.at(spec_.box_offset + 3U, candidate);
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(width) ||
            !std::isfinite(height) || width <= 0.0F || height <= 0.0F) {
            continue;
        }

        const auto model_x1 = cx - (width * 0.5F);
        const auto model_y1 = cy - (height * 0.5F);
        const auto model_x2 = cx + (width * 0.5F);
        const auto model_y2 = cy + (height * 0.5F);
        const auto x1 = source_x(model_x1, letterbox);
        const auto y1 = source_y(model_y1, letterbox);
        const auto x2 = source_x(model_x2, letterbox);
        const auto y2 = source_y(model_y2, letterbox);
        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        domain::PlateQuadrilateral quadrilateral{};
        bool keypoints_finite = true;
        for (std::size_t point = 0; point < spec_.keypoint_count; ++point) {
            const auto base = spec_.keypoint_offset + (point * spec_.keypoint_stride);
            const auto model_x = matrix.at(base + spec_.keypoint_x_offset, candidate);
            const auto model_y = matrix.at(base + spec_.keypoint_y_offset, candidate);
            if (!std::isfinite(model_x) || !std::isfinite(model_y)) {
                keypoints_finite = false;
                break;
            }
            quadrilateral.points[point] = domain::Point2f{
                source_x(model_x, letterbox),
                source_y(model_y, letterbox),
            };
            quadrilateral.confidences[point] = spec_.keypoint_confidence_offset.has_value()
                ? finite_probability(matrix.at(base + *spec_.keypoint_confidence_offset, candidate))
                : confidence;
        }

        candidates.push_back(RankedDetection{
            .detection = domain::Detection{
                .bbox = domain::BoundingBox{x1, y1, x2 - x1, y2 - y1},
                .quadrilateral = keypoints_finite
                    ? std::optional<domain::PlateQuadrilateral>{quadrilateral}
                    : std::nullopt,
                .confidence = confidence,
                .geometry_score = 0.0F,
                .provider = spec_.provider_name,
            },
            .source_index = candidate,
        });
    }

    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [](const RankedDetection& left, const RankedDetection& right) {
            if (left.detection.confidence != right.detection.confidence) {
                return left.detection.confidence > right.detection.confidence;
            }
            return left.source_index < right.source_index;
        });

    std::vector<domain::Detection> kept{};
    kept.reserve(std::min(candidates.size(), spec_.maximum_detections));
    for (const auto& candidate : candidates) {
        const auto suppressed = std::any_of(
            kept.begin(),
            kept.end(),
            [&candidate, this](const domain::Detection& accepted) {
                return intersection_over_union(candidate.detection.bbox, accepted.bbox) > spec_.nms_iou_threshold;
            });
        if (!suppressed) {
            kept.push_back(candidate.detection);
            if (kept.size() >= spec_.maximum_detections) {
                break;
            }
        }
    }

    return kept;
}

} // namespace fac_lpr::infrastructure::yolo
