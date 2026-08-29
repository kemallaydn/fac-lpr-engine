#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/crop/crop_hypothesis_generator.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace {
using fac_lpr::application::ImageBuffer;
using fac_lpr::application::ImageView;
using fac_lpr::application::OperationContext;
using fac_lpr::application::PixelFormat;
using fac_lpr::domain::BoundingBox;
using fac_lpr::domain::Detection;
using fac_lpr::infrastructure::crop::CropGeneratorKind;
using fac_lpr::infrastructure::crop::CropHypothesisGenerator;
using fac_lpr::infrastructure::crop::CropHypothesisGeneratorConfig;

TEST(CropGenerator, DuplicateRectifiedAndRawCropIsProcessedOnce) {
    std::vector<std::byte> bytes(20U * 10U, std::byte{7});
    const ImageView source{bytes, 20U, 10U, 20U, PixelFormat::gray8};
    ImageBuffer aligned{};
    aligned.bytes = bytes;
    aligned.width = 20U;
    aligned.height = 10U;
    aligned.stride_bytes = 20U;
    aligned.format = PixelFormat::gray8;

    CropHypothesisGeneratorConfig config{};
    config.generator_order = {CropGeneratorKind::rectified, CropGeneratorKind::raw_bbox};
    config.minimum_width = 1U;
    config.minimum_height = 1U;
    config.prefer_source_when_detection_dominates_frame = false;
    CropHypothesisGenerator generator{config};
    const Detection detection{.bbox = BoundingBox{0.0F, 0.0F, 20.0F, 10.0F}};
    const auto result = generator.generate(source, detection, aligned, OperationContext{});
    ASSERT_EQ(result.size(), 1U);
    EXPECT_NE(result[0].fingerprint, 0U);
    EXPECT_FALSE(result[0].type.empty());
    EXPECT_FALSE(result[0].source.empty());
}

TEST(CropGenerator, PlateDominantDetectionPreservesSourceAndAddsTextRegion) {
    std::vector<std::byte> source_bytes(100U * 50U, std::byte{0});
    for (std::size_t index = 0U; index < source_bytes.size(); ++index) {
        source_bytes[index] = static_cast<std::byte>(index % 251U);
    }
    const ImageView source{source_bytes, 100U, 50U, 100U, PixelFormat::gray8};

    ImageBuffer aligned{};
    aligned.bytes.assign(60U * 20U, std::byte{99});
    aligned.width = 60U;
    aligned.height = 20U;
    aligned.stride_bytes = 60U;
    aligned.format = PixelFormat::gray8;

    CropHypothesisGeneratorConfig config{};
    config.minimum_width = 1U;
    config.minimum_height = 1U;
    config.plate_dominant_min_area_ratio = 0.65F;
    config.plate_dominant_text_left_inset_ratio = 0.10F;
    config.plate_dominant_text_right_inset_ratio = 0.02F;
    config.plate_dominant_text_vertical_inset_ratio = 0.04F;
    CropHypothesisGenerator generator{config};

    const Detection detection{.bbox = BoundingBox{1.0F, 1.0F, 98.0F, 48.0F}};
    const auto result = generator.generate(source, detection, aligned, OperationContext{});

    ASSERT_EQ(result.size(), 2U);
    EXPECT_EQ(result[0].type, "plate_dominant_source");
    EXPECT_EQ(result[0].source, "source_image");
    EXPECT_EQ(result[0].image.width, source.width);
    EXPECT_EQ(result[0].image.height, source.height);
    EXPECT_EQ(result[0].image.bytes, source_bytes);

    EXPECT_EQ(result[1].type, "plate_dominant_text_region");
    EXPECT_EQ(result[1].source, "source_image");
    EXPECT_EQ(result[1].image.width, 88U);
    EXPECT_EQ(result[1].image.height, 46U);
    EXPECT_NE(result[1].fingerprint, result[0].fingerprint);
}

TEST(CropGenerator, OrdinaryDetectionKeepsConfiguredCropPipeline) {
    std::vector<std::byte> bytes(40U * 20U, std::byte{11});
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(index % 251U);
    }
    const ImageView source{bytes, 40U, 20U, 40U, PixelFormat::gray8};
    CropHypothesisGeneratorConfig config{};
    config.generator_order = {CropGeneratorKind::raw_bbox};
    config.minimum_width = 1U;
    config.minimum_height = 1U;
    CropHypothesisGenerator generator{config};

    const Detection detection{.bbox = BoundingBox{5.0F, 5.0F, 20.0F, 10.0F}};
    const auto result = generator.generate(source, detection, std::nullopt, OperationContext{});

    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0].type, "raw_bbox");
    EXPECT_EQ(result[0].image.width, 20U);
    EXPECT_EQ(result[0].image.height, 10U);
}

TEST(CropGenerator, MaximumHypothesisCountIsRespected) {
    std::vector<std::byte> bytes(40U * 20U, std::byte{11});
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(index % 251U);
    }
    const ImageView source{bytes, 40U, 20U, 40U, PixelFormat::gray8};
    CropHypothesisGeneratorConfig config{};
    config.maximum_hypotheses = 1U;
    config.minimum_width = 1U;
    config.minimum_height = 1U;
    CropHypothesisGenerator generator{config};
    const Detection detection{.bbox = BoundingBox{5.0F, 5.0F, 20.0F, 10.0F}};
    const auto result = generator.generate(source, detection, std::nullopt, OperationContext{});
    EXPECT_LE(result.size(), 1U);
}

TEST(CropGenerator, DuplicateGeneratorRegistrationIsRejected) {
    CropHypothesisGeneratorConfig config{};
    config.generator_order = {CropGeneratorKind::raw_bbox, CropGeneratorKind::raw_bbox};
    EXPECT_THROW(CropHypothesisGenerator{config}, fac_lpr::application::ConfigurationError);
}

TEST(CropGenerator, AutomaticPlateDominantCropsCannotBeRegisteredExplicitly) {
    CropHypothesisGeneratorConfig source_config{};
    source_config.generator_order = {CropGeneratorKind::plate_dominant_source};
    EXPECT_THROW(CropHypothesisGenerator{source_config}, fac_lpr::application::ConfigurationError);

    CropHypothesisGeneratorConfig text_config{};
    text_config.generator_order = {CropGeneratorKind::plate_dominant_text_region};
    EXPECT_THROW(CropHypothesisGenerator{text_config}, fac_lpr::application::ConfigurationError);
}

TEST(CropGenerator, InvalidPlateDominantThresholdIsRejected) {
    CropHypothesisGeneratorConfig config{};
    config.plate_dominant_min_area_ratio = 0.0F;
    EXPECT_THROW(CropHypothesisGenerator{config}, fac_lpr::application::ConfigurationError);
}

TEST(CropGenerator, InvalidPlateDominantTextInsetsAreRejected) {
    CropHypothesisGeneratorConfig config{};
    config.plate_dominant_text_left_inset_ratio = 0.50F;
    EXPECT_THROW(CropHypothesisGenerator{config}, fac_lpr::application::ConfigurationError);
}

} // namespace
