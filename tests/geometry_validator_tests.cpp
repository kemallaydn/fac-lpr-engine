#include <fac_lpr/infrastructure/geometry/plate_geometry_validator.hpp>

#include <gtest/gtest.h>

#include <array>

namespace {
using fac_lpr::domain::BoundingBox;
using fac_lpr::domain::Detection;
using fac_lpr::domain::PlateQuadrilateral;
using fac_lpr::domain::Point2f;
using fac_lpr::infrastructure::geometry::PlateGeometryValidator;
using fac_lpr::infrastructure::geometry::order_plate_corners;

Detection valid_detection() {
    PlateQuadrilateral quad{};
    quad.points = {
        Point2f{110.0F, 40.0F},
        Point2f{10.0F, 10.0F},
        Point2f{10.0F, 40.0F},
        Point2f{110.0F, 10.0F},
    };
    quad.confidences = {0.95F, 0.92F, 0.94F, 0.93F};
    return Detection{
        .bbox = BoundingBox{10.0F, 10.0F, 100.0F, 30.0F},
        .quadrilateral = quad,
        .confidence = 0.95F,
        .geometry_score = 0.0F,
        .provider = "test",
    };
}

TEST(GeometryValidator, OrdersCornersAndAcceptsValidPlate) {
    const PlateGeometryValidator validator{};
    const auto evaluation = validator.evaluate(valid_detection());
    ASSERT_TRUE(evaluation.valid) << evaluation.reason;
    EXPECT_EQ(evaluation.reason, "ok");
    EXPECT_GE(evaluation.score, 0.0F);
    EXPECT_LE(evaluation.score, 1.0F);
    EXPECT_FLOAT_EQ(evaluation.ordered_corners[0].x, 10.0F);
    EXPECT_FLOAT_EQ(evaluation.ordered_corners[0].y, 10.0F);
    EXPECT_FLOAT_EQ(evaluation.ordered_corners[1].x, 110.0F);
    EXPECT_FLOAT_EQ(evaluation.ordered_corners[1].y, 10.0F);
}

TEST(GeometryValidator, RejectsDuplicateCorner) {
    auto detection = valid_detection();
    detection.quadrilateral->points[1] = detection.quadrilateral->points[0];
    const auto evaluation = PlateGeometryValidator{}.evaluate(detection);
    EXPECT_FALSE(evaluation.valid);
    EXPECT_EQ(evaluation.reason, "duplicate_corner");
    EXPECT_FLOAT_EQ(evaluation.score, 0.0F);
}

TEST(GeometryValidator, RejectsConcaveQuadrilateral) {
    auto detection = valid_detection();
    detection.quadrilateral->points = {
        Point2f{10.0F, 10.0F},
        Point2f{110.0F, 10.0F},
        Point2f{50.0F, 20.0F},
        Point2f{10.0F, 40.0F},
    };
    const auto evaluation = PlateGeometryValidator{}.evaluate(detection);
    EXPECT_FALSE(evaluation.valid);
}

TEST(GeometryValidator, RejectsLowKeypointConfidence) {
    auto detection = valid_detection();
    detection.quadrilateral->confidences[2] = 0.20F;
    const auto evaluation = PlateGeometryValidator{}.evaluate(detection);
    EXPECT_FALSE(evaluation.valid);
    EXPECT_EQ(evaluation.reason, "keypoint_confidence_below_threshold");
}

TEST(GeometryValidator, CornerOrderingIsDeterministic) {
    const std::array<Point2f, 4> points{
        Point2f{110.0F, 40.0F},
        Point2f{10.0F, 40.0F},
        Point2f{110.0F, 10.0F},
        Point2f{10.0F, 10.0F},
    };
    const auto ordered = order_plate_corners(points);
    EXPECT_FLOAT_EQ(ordered[0].x, 10.0F);
    EXPECT_FLOAT_EQ(ordered[0].y, 10.0F);
    EXPECT_FLOAT_EQ(ordered[2].x, 110.0F);
    EXPECT_FLOAT_EQ(ordered[2].y, 40.0F);
}

} // namespace
