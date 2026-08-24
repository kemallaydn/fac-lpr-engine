#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image.hpp>
#include <fac_lpr/infrastructure/opencv/perspective_aligner.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stop_token>
#include <vector>

namespace {
using fac_lpr::application::ImageView;
using fac_lpr::application::OperationContext;
using fac_lpr::application::PixelFormat;
using fac_lpr::domain::BoundingBox;
using fac_lpr::domain::Detection;
using fac_lpr::domain::PlateQuadrilateral;
using fac_lpr::domain::Point2f;
using fac_lpr::infrastructure::opencv::OpenCvPerspectiveAligner;
using fac_lpr::infrastructure::opencv::PerspectiveAlignerConfig;

Detection detection_with_quad() {
    PlateQuadrilateral quad{};
    quad.points = {Point2f{20.0F, 20.0F}, Point2f{100.0F, 18.0F}, Point2f{102.0F, 42.0F}, Point2f{18.0F, 44.0F}};
    quad.confidences = {0.95F, 0.95F, 0.95F, 0.95F};
    return Detection{.bbox = BoundingBox{18.0F, 18.0F, 84.0F, 26.0F}, .quadrilateral = quad, .confidence = 0.95F, .provider = "test"};
}

TEST(PerspectiveAligner, ProducesBoundedRectifiedCrop) {
    std::vector<std::byte> bytes(120U * 60U);
    for (std::size_t y = 0U; y < 60U; ++y) for (std::size_t x = 0U; x < 120U; ++x) bytes[(y * 120U) + x] = static_cast<std::byte>((x + y) % 256U);
    const ImageView source{bytes, 120U, 60U, 120U, PixelFormat::gray8};
    PerspectiveAlignerConfig config{};
    config.horizontal_padding_ratio = 0.0F;
    config.vertical_padding_ratio = 0.0F;
    config.minimum_output_width = 32U;
    config.minimum_output_height = 12U;
    config.maximum_output_width = 96U;
    config.maximum_output_height = 48U;
    OpenCvPerspectiveAligner aligner{config};
    const auto output = aligner.align(source, detection_with_quad(), OperationContext{});
    ASSERT_TRUE(output.has_value());
    EXPECT_GE(output->width, 32U);
    EXPECT_LE(output->width, 96U);
    EXPECT_GE(output->height, 12U);
    EXPECT_LE(output->height, 48U);
    EXPECT_FALSE(output->bytes.empty());
}

TEST(PerspectiveAligner, InvalidQuadrilateralFallsBackToBoundingBox) {
    std::vector<std::byte> bytes(120U * 60U, std::byte{42});
    const ImageView source{bytes, 120U, 60U, 120U, PixelFormat::gray8};
    auto detection = detection_with_quad();
    detection.quadrilateral->points[1] = detection.quadrilateral->points[0];
    PerspectiveAlignerConfig config{};
    config.horizontal_padding_ratio = 0.0F;
    config.vertical_padding_ratio = 0.0F;
    OpenCvPerspectiveAligner aligner{config};
    const auto output = aligner.align(source, detection, OperationContext{});
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->width, 84U);
    EXPECT_EQ(output->height, 26U);
}

TEST(PerspectiveAligner, CancellationIsFailClosed) {
    std::vector<std::byte> bytes(120U * 60U, std::byte{0});
    const ImageView source{bytes, 120U, 60U, 120U, PixelFormat::gray8};
    std::stop_source stop_source{};
    stop_source.request_stop();
    OperationContext context{};
    context.stop_token = stop_source.get_token();
    EXPECT_THROW((void)OpenCvPerspectiveAligner{}.align(source, detection_with_quad(), context), fac_lpr::application::CancelledError);
}

} // namespace
