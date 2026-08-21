#pragma once

#include <fac_lpr/domain/detection.hpp>

#include <array>
#include <string>

namespace fac_lpr::infrastructure::geometry {

struct GeometryConfig final {
    float minimum_keypoint_confidence{0.50F};
    float minimum_box_aspect_ratio{0.65F};
    float maximum_box_aspect_ratio{12.0F};
    float preferred_box_aspect_min{1.8F};
    float preferred_box_aspect_max{6.5F};
    float minimum_crop_aspect_ratio{0.8F};
    float maximum_crop_aspect_ratio{12.0F};
    float preferred_crop_aspect_min{1.6F};
    float preferred_crop_aspect_max{7.5F};
    float minimum_polygon_box_area_ratio{0.20F};
    float maximum_polygon_box_area_ratio{1.80F};
    float minimum_horizontal_edge_pixels{6.0F};
    float minimum_vertical_edge_pixels{4.0F};
    float maximum_point_distance_from_box_ratio{0.50F};
};

struct GeometryEvaluation final {
    bool valid{false};
    float score{0.0F};
    float box_ratio_score{0.0F};
    float crop_ratio_score{0.0F};
    float pixel_score{0.0F};
    float keypoint_score{0.0F};
    float deformation_score{0.0F};
    float polygon_fill_score{0.0F};
    float polygon_box_area_ratio{0.0F};
    float crop_aspect_ratio{0.0F};
    std::string reason{};
    std::array<domain::Point2f, 4> ordered_corners{};
};

[[nodiscard]] std::array<domain::Point2f, 4> order_plate_corners(
    const std::array<domain::Point2f, 4>& points);

class PlateGeometryValidator final {
public:
    explicit PlateGeometryValidator(GeometryConfig config = {});

    [[nodiscard]] GeometryEvaluation evaluate(
        const domain::Detection& detection) const;

private:
    GeometryConfig config_{};
};

} // namespace fac_lpr::infrastructure::geometry
