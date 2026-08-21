#pragma once

#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/providers.hpp>

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace fac_lpr::infrastructure::adaptive {

struct AdaptiveTileConfig final {
    bool enabled{true};
    std::size_t tile_width{1280U};
    std::size_t tile_height{960U};
    float overlap_ratio{0.20F};
    std::size_t maximum_tiles{32U};
    float merge_iou_threshold{0.45F};
    std::size_t maximum_detections{64U};
};

class AdaptiveTileDetector final : public application::IPlateDetector {
public:
    AdaptiveTileDetector(
        std::shared_ptr<application::IPlateDetector> detector,
        AdaptiveTileConfig config = {},
        application::PerformanceConfig image_limits = {});

    [[nodiscard]] std::string_view name() const noexcept override;

    [[nodiscard]] std::vector<domain::Detection> detect(
        const application::ImageView& image,
        const application::OperationContext& context) override;

private:
    std::shared_ptr<application::IPlateDetector> detector_;
    AdaptiveTileConfig config_{};
    application::PerformanceConfig image_limits_{};
};

} // namespace fac_lpr::infrastructure::adaptive
