#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/opencv/double_row_normalizer.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace {
using fac_lpr::application::ImageView;
using fac_lpr::application::OperationContext;
using fac_lpr::application::PixelFormat;
using fac_lpr::infrastructure::opencv::DoubleRowNormalizerConfig;
using fac_lpr::infrastructure::opencv::DoubleRowPlateNormalizer;

std::vector<std::byte> make_gray_two_row_fixture() {
    constexpr std::size_t width = 80U;
    constexpr std::size_t height = 60U;
    std::vector<std::byte> bytes(width * height, std::byte{255});
    const auto fill_rect = [&](const std::size_t x0, const std::size_t y0, const std::size_t x1, const std::size_t y1) {
        for (std::size_t y = y0; y < y1; ++y) {
            for (std::size_t x = x0; x < x1; ++x) {
                bytes[(y * width) + x] = std::byte{0};
            }
        }
    };
    fill_rect(10U, 8U, 28U, 20U);
    fill_rect(38U, 8U, 67U, 20U);
    fill_rect(8U, 38U, 34U, 52U);
    fill_rect(43U, 38U, 70U, 52U);
    return bytes;
}

TEST(DoubleRowNormalizer, WideSingleRowCandidateIsIgnored) {
    std::vector<std::byte> bytes(120U * 30U, std::byte{255});
    const ImageView image{bytes, 120U, 30U, 120U, PixelFormat::gray8};
    const DoubleRowPlateNormalizer normalizer{};
    EXPECT_FALSE(normalizer.should_apply(image));
    EXPECT_FALSE(normalizer.normalize(image, OperationContext{}).has_value());
}

TEST(DoubleRowNormalizer, TwoRowsAreFlattenedWithoutMutatingInput) {
    auto bytes = make_gray_two_row_fixture();
    const auto before = bytes;
    const ImageView image{bytes, 80U, 60U, 80U, PixelFormat::gray8};
    DoubleRowNormalizerConfig config{};
    config.target_row_height = 24U;
    config.separator_width = 3U;
    const DoubleRowPlateNormalizer normalizer{config};
    const auto normalized = normalizer.normalize(image, OperationContext{});
    ASSERT_TRUE(normalized.has_value());
    EXPECT_EQ(bytes, before);
    EXPECT_EQ(normalized->format, PixelFormat::gray8);
    EXPECT_EQ(normalized->height, 24U);
    EXPECT_GT(normalized->width, normalized->height);
    EXPECT_EQ(normalized->stride_bytes, normalized->width);
    EXPECT_EQ(normalized->bytes.size(), normalized->width * normalized->height);
}

TEST(DoubleRowNormalizer, MissingSecondRowIsRejected) {
    constexpr std::size_t width = 80U;
    constexpr std::size_t height = 60U;
    std::vector<std::byte> bytes(width * height, std::byte{255});
    for (std::size_t y = 8U; y < 20U; ++y) {
        for (std::size_t x = 10U; x < 65U; ++x) {
            bytes[(y * width) + x] = std::byte{0};
        }
    }
    const ImageView image{bytes, width, height, width, PixelFormat::gray8};
    const DoubleRowPlateNormalizer normalizer{};
    EXPECT_FALSE(normalizer.normalize(image, OperationContext{}).has_value());
}

TEST(DoubleRowNormalizer, InvalidConfigurationFailsFast) {
    DoubleRowNormalizerConfig config{};
    config.split_search_min_ratio = 0.80F;
    config.split_search_max_ratio = 0.60F;
    EXPECT_THROW(DoubleRowPlateNormalizer{config}, fac_lpr::application::ConfigurationError);
}

} // namespace
