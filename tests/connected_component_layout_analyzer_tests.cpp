#include <fac_lpr/infrastructure/opencv/connected_component_layout_analyzer.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace {
using fac_lpr::application::ImageView;
using fac_lpr::application::OperationContext;
using fac_lpr::application::PixelFormat;
using fac_lpr::domain::PlateCandidate;
using fac_lpr::infrastructure::opencv::ConnectedComponentLayoutConfig;
using fac_lpr::infrastructure::opencv::ConnectedComponentPlateLayoutAnalyzer;

constexpr std::size_t width = 120U;
constexpr std::size_t height = 40U;

void fill_rect(
    std::vector<std::byte>& bytes,
    const std::size_t x0,
    const std::size_t y0,
    const std::size_t x1,
    const std::size_t y1) {
    for (std::size_t y = y0; y < y1; ++y) {
        for (std::size_t x = x0; x < x1; ++x) {
            bytes[(y * width) + x] = std::byte{0};
        }
    }
}

std::vector<std::byte> make_reliable_fixture() {
    std::vector<std::byte> bytes(width * height, std::byte{255});
    // Seven character-like components: 2 province + 1 letter + 4 digits.
    // Group boundaries have intentionally larger horizontal gaps.
    fill_rect(bytes, 5U, 8U, 11U, 33U);
    fill_rect(bytes, 14U, 8U, 20U, 33U);
    fill_rect(bytes, 31U, 8U, 38U, 33U);
    fill_rect(bytes, 50U, 8U, 56U, 33U);
    fill_rect(bytes, 59U, 8U, 65U, 33U);
    fill_rect(bytes, 68U, 8U, 74U, 33U);
    fill_rect(bytes, 77U, 8U, 83U, 33U);
    return bytes;
}

TEST(ConnectedComponentLayoutAnalyzer, ProducesReliableEvidenceForGroupedComponents) {
    auto bytes = make_reliable_fixture();
    const ImageView image{bytes, width, height, width, PixelFormat::gray8};

    ConnectedComponentLayoutConfig config{};
    config.minimum_confidence = 0.20F;
    const ConnectedComponentPlateLayoutAnalyzer analyzer{config};
    const auto evidence = analyzer.analyze(image, {}, OperationContext{});

    EXPECT_TRUE(evidence.reliable);
    ASSERT_TRUE(evidence.character_count.has_value());
    EXPECT_EQ(*evidence.character_count, 7);
    ASSERT_TRUE(evidence.letter_group_size.has_value());
    EXPECT_EQ(*evidence.letter_group_size, 1);
    EXPECT_GT(evidence.confidence, 0.0F);
    EXPECT_LE(evidence.confidence, 1.0F);
}

TEST(ConnectedComponentLayoutAnalyzer, OcrCandidatesDoNotChangeGeometricEvidence) {
    auto bytes = make_reliable_fixture();
    const ImageView image{bytes, width, height, width, PixelFormat::gray8};

    ConnectedComponentLayoutConfig config{};
    config.minimum_confidence = 0.20F;
    ConnectedComponentPlateLayoutAnalyzer analyzer{config};
    const auto without_candidates = analyzer.analyze(image, {}, OperationContext{});

    const std::vector<PlateCandidate> candidates{
        PlateCandidate{.text = "34A1234", .confidence = 0.99F, .calibrated_confidence = 0.99F, .format_valid = true}};
    const auto with_candidates = analyzer.analyze(image, candidates, OperationContext{});

    EXPECT_EQ(without_candidates.reliable, with_candidates.reliable);
    EXPECT_EQ(without_candidates.character_count, with_candidates.character_count);
    EXPECT_EQ(without_candidates.letter_group_size, with_candidates.letter_group_size);
    EXPECT_FLOAT_EQ(without_candidates.confidence, with_candidates.confidence);
}

TEST(ConnectedComponentLayoutAnalyzer, TooFewComponentsReturnsNeutralEvidence) {
    std::vector<std::byte> bytes(width * height, std::byte{255});
    fill_rect(bytes, 8U, 8U, 15U, 33U);
    fill_rect(bytes, 22U, 8U, 29U, 33U);
    fill_rect(bytes, 36U, 8U, 43U, 33U);
    const ImageView image{bytes, width, height, width, PixelFormat::gray8};

    ConnectedComponentPlateLayoutAnalyzer analyzer{};
    const auto evidence = analyzer.analyze(image, {}, OperationContext{});
    EXPECT_FALSE(evidence.reliable);
    EXPECT_FLOAT_EQ(evidence.confidence, 0.0F);
}

TEST(ConnectedComponentLayoutAnalyzer, PerspectiveLikeHeightDistortionReturnsNeutralEvidence) {
    std::vector<std::byte> bytes(width * height, std::byte{255});
    // Simulate a heavily perspective-distorted crop: character-like components
    // progressively change height enough that layout evidence should not be trusted.
    fill_rect(bytes, 5U, 14U, 11U, 28U);
    fill_rect(bytes, 14U, 12U, 20U, 30U);
    fill_rect(bytes, 31U, 10U, 38U, 32U);
    fill_rect(bytes, 50U, 8U, 56U, 34U);
    fill_rect(bytes, 59U, 6U, 65U, 35U);
    fill_rect(bytes, 68U, 4U, 74U, 36U);
    fill_rect(bytes, 77U, 2U, 83U, 38U);
    const ImageView image{bytes, width, height, width, PixelFormat::gray8};

    ConnectedComponentLayoutConfig config{};
    config.minimum_component_height_ratio = 0.25F;
    config.maximum_height_coefficient_of_variation = 0.20F;
    const ConnectedComponentPlateLayoutAnalyzer analyzer{config};
    const auto evidence = analyzer.analyze(image, {}, OperationContext{});

    EXPECT_FALSE(evidence.reliable);
    EXPECT_FLOAT_EQ(evidence.confidence, 0.0F);
}

TEST(ConnectedComponentLayoutAnalyzer, OverlappingXComponentsAreNotTrusted) {
    std::vector<std::byte> bytes(width * height, std::byte{255});
    // Keep components disconnected vertically while deliberately overlapping in X.
    fill_rect(bytes, 5U, 5U, 12U, 18U);
    fill_rect(bytes, 8U, 22U, 15U, 35U);
    fill_rect(bytes, 26U, 7U, 33U, 32U);
    fill_rect(bytes, 44U, 7U, 51U, 32U);
    fill_rect(bytes, 60U, 7U, 67U, 32U);
    fill_rect(bytes, 76U, 7U, 83U, 32U);
    fill_rect(bytes, 92U, 7U, 99U, 32U);
    const ImageView image{bytes, width, height, width, PixelFormat::gray8};

    ConnectedComponentLayoutConfig config{};
    config.minimum_component_height_ratio = 0.20F;
    config.maximum_height_coefficient_of_variation = 0.60F;
    const ConnectedComponentPlateLayoutAnalyzer analyzer{config};
    const auto evidence = analyzer.analyze(image, {}, OperationContext{});
    EXPECT_FALSE(evidence.reliable);
}

TEST(ConnectedComponentLayoutAnalyzer, InvalidConfigurationFailsFast) {
    ConnectedComponentLayoutConfig config{};
    config.blur_kernel_size = 4;
    EXPECT_THROW(
        ConnectedComponentPlateLayoutAnalyzer{config},
        fac_lpr::application::ConfigurationError);
}

} // namespace
