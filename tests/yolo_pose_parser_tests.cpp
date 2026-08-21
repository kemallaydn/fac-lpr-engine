#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/yolo/yolo_pose_parser.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {
using fac_lpr::infrastructure::yolo::LetterboxMetadata;
using fac_lpr::infrastructure::yolo::YoloPoseOutputParser;
using fac_lpr::infrastructure::yolo::YoloPoseOutputSpec;

constexpr std::size_t feature_count = 17U;

void set_feature(
    std::vector<float>& tensor,
    const std::size_t candidates,
    const std::size_t feature,
    const std::size_t candidate,
    const float value) {
    tensor[(feature * candidates) + candidate] = value;
}

void write_candidate(
    std::vector<float>& tensor,
    const std::size_t candidates,
    const std::size_t candidate,
    const float confidence,
    const float cx,
    const float cy,
    const float width,
    const float height) {
    set_feature(tensor, candidates, 0U, candidate, cx);
    set_feature(tensor, candidates, 1U, candidate, cy);
    set_feature(tensor, candidates, 2U, candidate, width);
    set_feature(tensor, candidates, 3U, candidate, height);
    set_feature(tensor, candidates, 4U, candidate, confidence);

    const std::array<std::array<float, 2>, 4> points{{
        {{cx - (width * 0.5F), cy - (height * 0.5F)}},
        {{cx + (width * 0.5F), cy - (height * 0.5F)}},
        {{cx + (width * 0.5F), cy + (height * 0.5F)}},
        {{cx - (width * 0.5F), cy + (height * 0.5F)}},
    }};
    for (std::size_t point = 0U; point < points.size(); ++point) {
        const auto base = 5U + (point * 3U);
        set_feature(tensor, candidates, base, candidate, points[point][0]);
        set_feature(tensor, candidates, base + 1U, candidate, points[point][1]);
        set_feature(tensor, candidates, base + 2U, candidate, confidence);
    }
}

LetterboxMetadata metadata() {
    return LetterboxMetadata{
        .source_width = 100U,
        .source_height = 50U,
        .input_width = 200U,
        .input_height = 200U,
        .scale = 2.0F,
        .pad_left = 0U,
        .pad_top = 50U,
        .resized_width = 200U,
        .resized_height = 100U,
    };
}

TEST(YoloPoseParser, AcceptsInspectedProductionOutputShape) {
    // best.onnx: output0 float32 [1, 17, 18900]
    constexpr std::size_t candidates = 18900U;
    std::vector<float> output(feature_count * candidates, 0.0F);
    write_candidate(output, candidates, 0U, 0.95F, 100.0F, 100.0F, 80.0F, 40.0F);

    const YoloPoseOutputParser parser{YoloPoseOutputSpec{}};
    const std::array<std::int64_t, 3> shape{
        1,
        static_cast<std::int64_t>(feature_count),
        static_cast<std::int64_t>(candidates),
    };
    const auto detections = parser.parse(output, shape, metadata());
    ASSERT_EQ(detections.size(), 1U);
    EXPECT_NEAR(detections[0].confidence, 0.95F, 0.001F);
}

TEST(YoloPoseParser, MapsCoordinatesAndSuppressesOverlappingCandidate) {
    constexpr std::size_t candidates = 2U;
    std::vector<float> output(feature_count * candidates, 0.0F);
    write_candidate(output, candidates, 0U, 0.95F, 100.0F, 100.0F, 80.0F, 40.0F);
    write_candidate(output, candidates, 1U, 0.80F, 102.0F, 101.0F, 80.0F, 40.0F);

    const YoloPoseOutputParser parser{YoloPoseOutputSpec{}};
    const std::array<std::int64_t, 3> shape{1, static_cast<std::int64_t>(feature_count), 2};
    const auto detections = parser.parse(output, shape, metadata());

    ASSERT_EQ(detections.size(), 1U);
    EXPECT_NEAR(detections[0].bbox.x, 30.0F, 0.001F);
    EXPECT_NEAR(detections[0].bbox.y, 15.0F, 0.001F);
    EXPECT_NEAR(detections[0].bbox.width, 40.0F, 0.001F);
    EXPECT_NEAR(detections[0].bbox.height, 20.0F, 0.001F);
    ASSERT_TRUE(detections[0].quadrilateral.has_value());
    EXPECT_NEAR(detections[0].quadrilateral->confidences[0], 0.95F, 0.001F);
}

TEST(YoloPoseParser, ClampsOutOfBoundsCoordinatesToSourceImage) {
    constexpr std::size_t candidates = 1U;
    std::vector<float> output(feature_count, 0.0F);
    write_candidate(output, candidates, 0U, 0.90F, 100.0F, 100.0F, 400.0F, 300.0F);

    const YoloPoseOutputParser parser{YoloPoseOutputSpec{}};
    const std::array<std::int64_t, 3> shape{1, static_cast<std::int64_t>(feature_count), 1};
    const auto detections = parser.parse(output, shape, metadata());

    ASSERT_EQ(detections.size(), 1U);
    EXPECT_FLOAT_EQ(detections[0].bbox.x, 0.0F);
    EXPECT_FLOAT_EQ(detections[0].bbox.y, 0.0F);
    EXPECT_FLOAT_EQ(detections[0].bbox.width, 100.0F);
    EXPECT_FLOAT_EQ(detections[0].bbox.height, 50.0F);
}

TEST(YoloPoseParser, RejectsTensorShapeMismatch) {
    const YoloPoseOutputParser parser{YoloPoseOutputSpec{}};
    const std::vector<float> output(feature_count, 0.0F);
    const std::array<std::int64_t, 3> shape{1, static_cast<std::int64_t>(feature_count), 2};
    EXPECT_THROW(parser.parse(output, shape, metadata()), fac_lpr::application::InferenceError);
}

TEST(YoloPoseParser, RejectsOverflowingFeatureContract) {
    auto spec = YoloPoseOutputSpec{};
    spec.keypoint_offset = std::numeric_limits<std::size_t>::max() - 1U;
    EXPECT_THROW(YoloPoseOutputParser{spec}, fac_lpr::application::ConfigurationError);
}

} // namespace
