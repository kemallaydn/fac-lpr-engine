#include <fac_lpr/application/multi_detector_fusion.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace fac_lpr;

class FakeDetector final : public application::IPlateDetector {
public:
    FakeDetector(std::string name, std::vector<domain::Detection> detections)
        : name_(std::move(name)), detections_(std::move(detections)) {}

    std::string_view name() const noexcept override { return name_; }

    std::vector<domain::Detection> detect(
        const application::ImageView&,
        const application::OperationContext&) override {
        ++calls;
        return detections_;
    }

    std::size_t calls{0U};

private:
    std::string name_{};
    std::vector<domain::Detection> detections_{};
};

domain::Detection detection(
    float x,
    float y,
    float width,
    float height,
    float confidence) {
    return domain::Detection{
        .bbox = {.x = x, .y = y, .width = width, .height = height},
        .confidence = confidence,
        .geometry_score = 0.8F};
}

application::ImageView tiny_image() {
    static std::array<std::byte, 3U> bytes{};
    return application::ImageView{bytes, 1U, 1U, 3U, application::PixelFormat::bgr8};
}

TEST(MultiDetectorFusion, SameModelDuplicateCountsAsSingleEvidence) {
    auto first = std::make_shared<FakeDetector>(
        "detector_a",
        std::vector<domain::Detection>{
            detection(10.0F, 10.0F, 100.0F, 30.0F, 0.90F),
            detection(12.0F, 11.0F, 98.0F, 29.0F, 0.80F)});
    auto second = std::make_shared<FakeDetector>(
        "detector_b",
        std::vector<domain::Detection>{
            detection(11.0F, 10.0F, 99.0F, 30.0F, 0.85F)});

    application::MultiDetectorFusion fusion{{
        {.provider = first, .weight = 1.0F},
        {.provider = second, .weight = 1.0F}}};

    const auto groups = fusion.detect_groups(tiny_image(), {});
    ASSERT_EQ(groups.size(), 1U);
    ASSERT_EQ(groups.front().evidence.size(), 2U);
    EXPECT_EQ(groups.front().evidence[0].provider, "detector_a");
    EXPECT_EQ(groups.front().evidence[1].provider, "detector_b");
    EXPECT_GT(groups.front().fused.confidence, 0.90F);
}

TEST(MultiDetectorFusion, UsesIntersectionOverMinAreaForNestedCrossModelBoxes) {
    auto large = std::make_shared<FakeDetector>(
        "large",
        std::vector<domain::Detection>{detection(0.0F, 0.0F, 200.0F, 80.0F, 0.8F)});
    auto small = std::make_shared<FakeDetector>(
        "small",
        std::vector<domain::Detection>{detection(50.0F, 20.0F, 80.0F, 30.0F, 0.8F)});

    application::MultiDetectorFusionConfig config{};
    config.cross_model_iou_threshold = 0.90F;
    config.cross_model_min_area_overlap_threshold = 0.95F;
    application::MultiDetectorFusion fusion{{
        {.provider = large},
        {.provider = small}}, config};

    const auto groups = fusion.detect_groups(tiny_image(), {});
    ASSERT_EQ(groups.size(), 1U);
    EXPECT_EQ(groups.front().evidence.size(), 2U);
}

TEST(MultiDetectorFusion, NonOverlappingDetectionsStayInSeparateGroups) {
    auto first = std::make_shared<FakeDetector>(
        "a",
        std::vector<domain::Detection>{detection(0.0F, 0.0F, 50.0F, 20.0F, 0.7F)});
    auto second = std::make_shared<FakeDetector>(
        "b",
        std::vector<domain::Detection>{detection(500.0F, 500.0F, 50.0F, 20.0F, 0.9F)});

    application::MultiDetectorFusion fusion{{
        {.provider = first},
        {.provider = second}}};
    const auto groups = fusion.detect_groups(tiny_image(), {});
    ASSERT_EQ(groups.size(), 2U);
    EXPECT_GT(groups[0].fused.confidence, groups[1].fused.confidence);
}

TEST(MultiDetectorFusion, RegistrationOrderDoesNotChangeFusedResult) {
    auto first = std::make_shared<FakeDetector>(
        "a",
        std::vector<domain::Detection>{detection(10.0F, 10.0F, 100.0F, 30.0F, 0.75F)});
    auto second = std::make_shared<FakeDetector>(
        "b",
        std::vector<domain::Detection>{detection(12.0F, 9.0F, 98.0F, 31.0F, 0.85F)});

    application::MultiDetectorFusion lhs{{
        {.provider = first, .weight = 0.8F},
        {.provider = second, .weight = 1.0F}}};
    application::MultiDetectorFusion rhs{{
        {.provider = second, .weight = 1.0F},
        {.provider = first, .weight = 0.8F}}};

    const auto lhs_groups = lhs.detect_groups(tiny_image(), {});
    const auto rhs_groups = rhs.detect_groups(tiny_image(), {});
    ASSERT_EQ(lhs_groups.size(), 1U);
    ASSERT_EQ(rhs_groups.size(), 1U);
    EXPECT_FLOAT_EQ(lhs_groups[0].fused.confidence, rhs_groups[0].fused.confidence);
    EXPECT_FLOAT_EQ(lhs_groups[0].fused.bbox.x, rhs_groups[0].fused.bbox.x);
    EXPECT_FLOAT_EQ(lhs_groups[0].fused.bbox.y, rhs_groups[0].fused.bbox.y);
    EXPECT_FLOAT_EQ(lhs_groups[0].fused.bbox.width, rhs_groups[0].fused.bbox.width);
    EXPECT_FLOAT_EQ(lhs_groups[0].fused.bbox.height, rhs_groups[0].fused.bbox.height);
}

TEST(MultiDetectorFusion, DisabledOrZeroWeightDetectorIsNotCalled) {
    auto disabled = std::make_shared<FakeDetector>(
        "disabled",
        std::vector<domain::Detection>{detection(0.0F, 0.0F, 10.0F, 10.0F, 0.9F)});
    auto zero = std::make_shared<FakeDetector>(
        "zero",
        std::vector<domain::Detection>{detection(0.0F, 0.0F, 10.0F, 10.0F, 0.9F)});
    auto active = std::make_shared<FakeDetector>(
        "active",
        std::vector<domain::Detection>{detection(0.0F, 0.0F, 10.0F, 10.0F, 0.9F)});

    application::MultiDetectorFusion fusion{{
        {.provider = disabled, .weight = 1.0F, .enabled = false},
        {.provider = zero, .weight = 0.0F, .enabled = true},
        {.provider = active, .weight = 1.0F, .enabled = true}}};

    const auto result = fusion.detect(tiny_image(), {});
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(disabled->calls, 0U);
    EXPECT_EQ(zero->calls, 0U);
    EXPECT_EQ(active->calls, 1U);
}

} // namespace
