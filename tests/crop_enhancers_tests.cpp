#include <fac_lpr/infrastructure/opencv/crop_enhancers.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace {
using fac_lpr::application::ImageView;
using fac_lpr::application::OperationContext;
using fac_lpr::application::PixelFormat;
using fac_lpr::infrastructure::native_image::CropQuality;
using fac_lpr::infrastructure::opencv::AdaptiveThresholdCropEnhancer;
using fac_lpr::infrastructure::opencv::AdaptiveThresholdEnhancerConfig;
using fac_lpr::infrastructure::opencv::ClaheCropEnhancer;
using fac_lpr::infrastructure::opencv::ClaheEnhancerConfig;
using fac_lpr::infrastructure::opencv::SharpenCropEnhancer;
using fac_lpr::infrastructure::opencv::SharpenEnhancerConfig;

std::vector<std::byte> make_bgr_fixture() {
    std::vector<std::byte> bytes(32U * 16U * 3U);
    for (std::size_t y = 0U; y < 16U; ++y) {
        for (std::size_t x = 0U; x < 32U; ++x) {
            const auto value = static_cast<unsigned char>(32U + ((x * 5U + y * 3U) % 160U));
            const auto offset = ((y * 32U) + x) * 3U;
            bytes[offset] = static_cast<std::byte>(value);
            bytes[offset + 1U] = static_cast<std::byte>(value / 2U);
            bytes[offset + 2U] = static_cast<std::byte>(255U - value);
        }
    }
    return bytes;
}

CropQuality low_quality() {
    CropQuality quality{};
    quality.sharpness = 0.20F;
    quality.exposure = 0.40F;
    quality.clipping = 0.70F;
    quality.overall = 0.35F;
    return quality;
}

CropQuality high_quality() {
    CropQuality quality{};
    quality.sharpness = 0.95F;
    quality.exposure = 0.95F;
    quality.clipping = 0.95F;
    quality.overall = 0.95F;
    return quality;
}

TEST(CropEnhancers, StrategiesAreQualityGated) {
    const ClaheCropEnhancer clahe{};
    const SharpenCropEnhancer sharpen{};
    const AdaptiveThresholdCropEnhancer threshold{};

    EXPECT_TRUE(clahe.should_apply(low_quality()));
    EXPECT_TRUE(sharpen.should_apply(low_quality()));
    EXPECT_TRUE(threshold.should_apply(low_quality()));
    EXPECT_FALSE(clahe.should_apply(high_quality()));
    EXPECT_FALSE(sharpen.should_apply(high_quality()));
    EXPECT_FALSE(threshold.should_apply(high_quality()));
}

TEST(CropEnhancers, DisabledStrategyNeverRuns) {
    ClaheEnhancerConfig config{};
    config.enabled = false;
    const ClaheCropEnhancer enhancer{config};
    auto bytes = make_bgr_fixture();
    const ImageView input{bytes, 32U, 16U, 96U, PixelFormat::bgr8};
    EXPECT_FALSE(enhancer.enhance(input, low_quality(), OperationContext{}).has_value());
}

TEST(CropEnhancers, ClaheProducesOwnedOutputWithoutMutatingInput) {
    auto bytes = make_bgr_fixture();
    const auto before = bytes;
    const ImageView input{bytes, 32U, 16U, 96U, PixelFormat::bgr8};
    const auto output = ClaheCropEnhancer{}.enhance(
        input,
        low_quality(),
        OperationContext{});

    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(bytes, before);
    EXPECT_EQ(output->width, input.width);
    EXPECT_EQ(output->height, input.height);
    EXPECT_EQ(output->format, PixelFormat::bgr8);
    EXPECT_NE(output->bytes.data(), input.bytes.data());
}

TEST(CropEnhancers, SharpenProducesOwnedOutputWithoutMutatingInput) {
    auto bytes = make_bgr_fixture();
    const auto before = bytes;
    const ImageView input{bytes, 32U, 16U, 96U, PixelFormat::bgr8};
    const auto output = SharpenCropEnhancer{}.enhance(
        input,
        low_quality(),
        OperationContext{});

    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(bytes, before);
    EXPECT_EQ(output->format, PixelFormat::bgr8);
    EXPECT_NE(output->bytes.data(), input.bytes.data());
}

TEST(CropEnhancers, AdaptiveThresholdReturnsSeparateGrayCrop) {
    auto bytes = make_bgr_fixture();
    const auto before = bytes;
    const ImageView input{bytes, 32U, 16U, 96U, PixelFormat::bgr8};
    const auto output = AdaptiveThresholdCropEnhancer{}.enhance(
        input,
        low_quality(),
        OperationContext{});

    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(bytes, before);
    EXPECT_EQ(output->format, PixelFormat::gray8);
    EXPECT_EQ(output->stride_bytes, output->width);
    EXPECT_EQ(output->bytes.size(), output->width * output->height);
}

TEST(CropEnhancers, InvalidConfigurationFailsFast) {
    SharpenEnhancerConfig sharpen{};
    sharpen.sigma = 0.0;
    EXPECT_THROW(
        SharpenCropEnhancer{sharpen},
        fac_lpr::application::ConfigurationError);

    AdaptiveThresholdEnhancerConfig threshold{};
    threshold.block_size = 4;
    EXPECT_THROW(
        AdaptiveThresholdCropEnhancer{threshold},
        fac_lpr::application::ConfigurationError);
}

} // namespace
