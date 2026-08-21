#pragma once

#include <fac_lpr/application/providers.hpp>

#include <cstddef>

namespace fac_lpr::infrastructure::opencv {

struct ConnectedComponentLayoutConfig final {
    bool enabled{true};
    double clahe_clip_limit{2.0};
    int blur_kernel_size{3};
    float minimum_component_height_ratio{0.30F};
    float maximum_component_height_ratio{0.95F};
    float minimum_component_width_ratio{0.008F};
    float maximum_component_width_ratio{0.25F};
    float minimum_component_aspect_ratio{0.08F};
    float maximum_component_aspect_ratio{1.25F};
    std::size_t minimum_character_count{5U};
    std::size_t maximum_character_count{10U};
    float maximum_height_coefficient_of_variation{0.35F};
    float minimum_boundary_gap_strength{1.20F};
    float minimum_confidence{0.45F};
};

class ConnectedComponentPlateLayoutAnalyzer final
    : public application::IPlateLayoutAnalyzer {
public:
    explicit ConnectedComponentPlateLayoutAnalyzer(
        ConnectedComponentLayoutConfig config = {});

    [[nodiscard]] application::LayoutEvidence analyze(
        const application::ImageView& plate,
        std::span<const domain::PlateCandidate> candidates,
        const application::OperationContext& context) override;

private:
    ConnectedComponentLayoutConfig config_{};
};

} // namespace fac_lpr::infrastructure::opencv
