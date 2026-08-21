#include <fac_lpr/infrastructure/native_image/native_image.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace {
using fac_lpr::application::ImageRegion;
using fac_lpr::application::ImageView;
using fac_lpr::application::MutableImageView;
using fac_lpr::application::PixelFormat;
using fac_lpr::infrastructure::native_image::CropQualityConfig;
using fac_lpr::infrastructure::native_image::NativeImageWorkspace;
using fac_lpr::infrastructure::native_image::copy_crop;
using fac_lpr::infrastructure::native_image::evaluate_crop_quality;
using fac_lpr::infrastructure::native_image::make_crop_view;

TEST(NativeImage, CropViewIsZeroCopyAndPreservesStride) {
    std::vector<std::byte> bytes(4U * 3U * 3U);
    const ImageView source{bytes, 4U, 3U, 12U, PixelFormat::bgr8};
    const auto crop = make_crop_view(source, ImageRegion{1U, 1U, 2U, 2U});
    EXPECT_EQ(crop.bytes.data(), source.bytes.data() + 15U);
    EXPECT_EQ(crop.width, 2U);
    EXPECT_EQ(crop.height, 2U);
    EXPECT_EQ(crop.stride_bytes, 12U);
}

TEST(NativeImage, CopyCropProducesPackedOutput) {
    std::vector<std::byte> source_bytes{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    const ImageView source{source_bytes, 4U, 2U, 4U, PixelFormat::gray8};
    std::vector<std::byte> destination_bytes(4U);
    MutableImageView destination{destination_bytes, 2U, 2U, 2U, PixelFormat::gray8};
    copy_crop(source, ImageRegion{1U, 0U, 2U, 2U}, destination);
    EXPECT_EQ(destination_bytes[0], std::byte{2});
    EXPECT_EQ(destination_bytes[1], std::byte{3});
    EXPECT_EQ(destination_bytes[2], std::byte{6});
    EXPECT_EQ(destination_bytes[3], std::byte{7});
}

TEST(NativeImage, WorkspaceReusesTensorCapacity) {
    NativeImageWorkspace workspace{};
    const auto first = workspace.prepare_tensor(1024U);
    ASSERT_EQ(first.size(), 1024U);
    const auto capacity = workspace.tensor_capacity();
    const auto second = workspace.prepare_tensor(512U);
    EXPECT_EQ(second.size(), 512U);
    EXPECT_EQ(workspace.tensor_capacity(), capacity);
}

TEST(NativeImage, TinyCropHasZeroQuality) {
    std::vector<std::byte> bytes(4U * 4U, std::byte{128});
    const ImageView image{bytes, 4U, 4U, 4U, PixelFormat::gray8};
    const auto quality = evaluate_crop_quality(image, CropQualityConfig{});
    EXPECT_FLOAT_EQ(quality.overall, 0.0F);
}

TEST(NativeImage, QualityIsDeterministicAndBounded) {
    std::vector<std::byte> bytes(32U * 16U);
    for (std::size_t y = 0U; y < 16U; ++y) {
        for (std::size_t x = 0U; x < 32U; ++x) {
            bytes[(y * 32U) + x] = static_cast<std::byte>(((x + y) % 2U) == 0U ? 32U : 224U);
        }
    }
    const ImageView image{bytes, 32U, 16U, 32U, PixelFormat::gray8};
    const auto first = evaluate_crop_quality(image);
    const auto second = evaluate_crop_quality(image);
    EXPECT_FLOAT_EQ(first.overall, second.overall);
    EXPECT_GE(first.overall, 0.0F);
    EXPECT_LE(first.overall, 1.0F);
    EXPECT_GT(first.sharpness, 0.0F);
}

} // namespace
