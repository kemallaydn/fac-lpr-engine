#pragma once

#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/providers.hpp>
#include <fac_lpr/infrastructure/geometry/plate_geometry_validator.hpp>

#include <cstddef>
#include <optional>

namespace fac_lpr::infrastructure::opencv {

struct PerspectiveAlignerConfig final {
    float horizontal_padding_ratio{0.04F};
    float vertical_padding_ratio{0.06F};
    std::size_t minimum_output_width{64U};
    std::size_t minimum_output_height{16U};
    std::size_t maximum_output_width{2048U};
    std::size_t maximum_output_height{512U};
    bool allow_bbox_fallback{true};
    bool prefer_landscape{true};
    float landscape_rotation_threshold{1.15F};
};

class OpenCvPerspectiveAligner final : public application::IPlateAligner {
public:
    OpenCvPerspectiveAligner(
        PerspectiveAlignerConfig config = {},
        application::PerformanceConfig image_limits = {},
        geometry::GeometryConfig geometry_config = {});

    [[nodiscard]] std::optional<application::ImageBuffer> align(
        const application::ImageView& source,
        const domain::Detection& detection,
        const application::OperationContext& context) override;

private:
    [[nodiscard]] std::optional<application::ImageBuffer> bbox_fallback(
        const application::ImageView& source,
        const domain::Detection& detection) const;

    PerspectiveAlignerConfig config_{};
    application::PerformanceConfig image_limits_{};
    geometry::PlateGeometryValidator geometry_validator_{};
};

} // namespace fac_lpr::infrastructure::opencv
