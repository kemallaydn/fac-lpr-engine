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
    CropHypothesisGenerator generator{config};
    const Detection detection{.bbox = BoundingBox{0.0F, 0.0F, 20.0F, 10.0F}};
    const auto result = generator.generate(source, detection, aligned, OperationContext{});
    ASSERT_EQ(result.size(), 1U);
    EXPECT_NE(result[0].fingerprint, 0U);
    EXPECT_FALSE(result[0].type.empty());
    EXPECT_FALSE(result[0].source.empty());
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

} // namespace
