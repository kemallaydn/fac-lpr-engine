#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/adaptive/adaptive_tile_detector.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace {
using fac_lpr::application::ImageView;
using fac_lpr::application::OperationContext;
using fac_lpr::application::PixelFormat;
using fac_lpr::domain::BoundingBox;
using fac_lpr::domain::Detection;
using fac_lpr::infrastructure::adaptive::AdaptiveTileConfig;
using fac_lpr::infrastructure::adaptive::AdaptiveTileDetector;

class FakeDetector final : public fac_lpr::application::IPlateDetector {
public:
    std::size_t calls{0U};
    std::vector<std::pair<std::size_t, std::size_t>> shapes{};

    [[nodiscard]] std::string_view name() const noexcept override { return "fake"; }

    [[nodiscard]] std::vector<Detection> detect(
        const ImageView& image,
        const OperationContext&) override {
        ++calls;
        shapes.emplace_back(image.width, image.height);
        if (image.width >= 100U) {
            return {};
        }
        return {Detection{
            .bbox = BoundingBox{5.0F, 6.0F, 10.0F, 8.0F},
            .confidence = 0.8F,
            .provider = "fake",
        }};
    }
};

TEST(AdaptiveTileDetector, SmallImageUsesOnlyFullFrame) {
    auto fake = std::make_shared<FakeDetector>();
    AdaptiveTileConfig config{};
    config.tile_width = 128U;
    config.tile_height = 128U;
    AdaptiveTileDetector detector{fake, config};
    std::vector<std::byte> bytes(80U * 60U, std::byte{0});
    const auto result = detector.detect(ImageView{bytes, 80U, 60U, 80U, PixelFormat::gray8}, OperationContext{});
    EXPECT_EQ(fake->calls, 1U);
    EXPECT_TRUE(result.empty());
}

TEST(AdaptiveTileDetector, TileCoordinatesAreTranslatedToSourceSpace) {
    auto fake = std::make_shared<FakeDetector>();
    AdaptiveTileConfig config{};
    config.tile_width = 60U;
    config.tile_height = 60U;
    config.overlap_ratio = 0.0F;
    config.maximum_tiles = 8U;
    config.merge_iou_threshold = 0.95F;
    AdaptiveTileDetector detector{fake, config};
    std::vector<std::byte> bytes(100U * 80U, std::byte{0});
    const auto result = detector.detect(ImageView{bytes, 100U, 80U, 100U, PixelFormat::gray8}, OperationContext{});
    ASSERT_EQ(fake->calls, 5U);
    ASSERT_EQ(result.size(), 4U);
    bool found_bottom_right = false;
    for (const auto& detection : result) {
        if (detection.bbox.x == 45.0F && detection.bbox.y == 26.0F) {
            found_bottom_right = true;
        }
    }
    EXPECT_TRUE(found_bottom_right);
}

TEST(AdaptiveTileDetector, ExcessiveTileCountFailsClosed) {
    auto fake = std::make_shared<FakeDetector>();
    AdaptiveTileConfig config{};
    config.tile_width = 32U;
    config.tile_height = 32U;
    config.maximum_tiles = 2U;
    AdaptiveTileDetector detector{fake, config};
    std::vector<std::byte> bytes(100U * 100U, std::byte{0});
    EXPECT_THROW(
        detector.detect(ImageView{bytes, 100U, 100U, 100U, PixelFormat::gray8}, OperationContext{}),
        fac_lpr::application::ResourceExhaustedError);
}

} // namespace
