#pragma once

#include <fac_lpr/domain/detection.hpp>
#include <fac_lpr/infrastructure/yolo/yolo_pose_preprocessor.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fac_lpr::infrastructure::yolo {

enum class CandidateLayout {
    features_first,
    candidates_first,
};

struct YoloPoseOutputSpec final {
    CandidateLayout layout{CandidateLayout::features_first};
    std::size_t box_offset{0U};
    std::optional<std::size_t> objectness_offset{};
    std::size_t class_score_offset{4U};
    std::size_t class_count{1U};
    std::size_t keypoint_offset{5U};
    std::size_t keypoint_count{4U};
    std::size_t keypoint_stride{3U};
    std::size_t keypoint_x_offset{0U};
    std::size_t keypoint_y_offset{1U};
    std::optional<std::size_t> keypoint_confidence_offset{2U};
    float confidence_threshold{0.50F};
    float nms_iou_threshold{0.45F};
    std::size_t maximum_detections{32U};
    std::string provider_name{"yolo_pose"};
};

class YoloPoseOutputParser final {
public:
    explicit YoloPoseOutputParser(YoloPoseOutputSpec spec);

    [[nodiscard]] std::vector<domain::Detection> parse(
        std::span<const float> output,
        std::span<const std::int64_t> shape,
        const LetterboxMetadata& letterbox) const;

private:
    YoloPoseOutputSpec spec_{};
};

[[nodiscard]] float intersection_over_union(
    const domain::BoundingBox& left,
    const domain::BoundingBox& right) noexcept;

} // namespace fac_lpr::infrastructure::yolo
