#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/native_image/native_image.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <vector>

namespace {

TEST(ImageValidation, RejectsNullPointerBeforeCreatingCallerOwnedView) {
    EXPECT_THROW(
        static_cast<void>(fac_lpr::application::make_image_view(
            nullptr,
            12U,
            2U,
            2U,
            6U,
            fac_lpr::application::PixelFormat::bgr8)),
        fac_lpr::application::InvalidImageError);
}

TEST(ImageValidation, RejectsArithmeticOverflow) {
    std::vector<std::byte> storage(1U);
    const fac_lpr::application::ImageView image{
        storage,
        std::numeric_limits<std::size_t>::max(),
        1U,
        std::numeric_limits<std::size_t>::max(),
        fac_lpr::application::PixelFormat::bgr8,
    };
    fac_lpr::application::PerformanceConfig limits{};
    limits.max_image_width = std::numeric_limits<std::size_t>::max();
    limits.max_image_height = std::numeric_limits<std::size_t>::max();
    limits.max_image_bytes = std::numeric_limits<std::size_t>::max();

    EXPECT_THROW(
        static_cast<void>(fac_lpr::application::validate_image(image, limits)),
        fac_lpr::application::InvalidImageError);
}

TEST(ImageValidation, AcceptsExactExtentOfStridedZeroCopyCrop) {
    std::vector<std::byte> storage(4U * 3U * 3U, std::byte{0});
    const fac_lpr::application::ImageView source{
        storage,
        4U,
        3U,
        12U,
        fac_lpr::application::PixelFormat::bgr8,
    };
    const auto crop = fac_lpr::infrastructure::native_image::make_crop_view(
        source,
        fac_lpr::application::ImageRegion{1U, 1U, 2U, 2U});

    const auto validated = fac_lpr::application::validate_image(
        crop,
        fac_lpr::application::PerformanceConfig{});
    EXPECT_EQ(validated.required_bytes, 18U);
    EXPECT_EQ(validated.minimum_row_bytes, 6U);
}

} // namespace
