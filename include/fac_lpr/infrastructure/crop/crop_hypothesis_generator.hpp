#pragma once

#include <fac_lpr/application/providers.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace fac_lpr::infrastructure::crop {

enum class CropGeneratorKind {
    plate_dominant_source,
    plate_dominant_text_region,
    rectified,
    raw_bbox,
    padded_bbox,
};

struct CropHypothesisGeneratorConfig final {
    std::vector<CropGeneratorKind> generator_order{
        CropGeneratorKind::rectified,
        CropGeneratorKind::raw_bbox,
        CropGeneratorKind::padded_bbox,
    };
    std::size_t maximum_hypotheses{8U};
    std::size_t minimum_width{16U};
    std::size_t minimum_height{8U};
    float horizontal_padding_ratio{0.06F};
    float vertical_padding_ratio{0.04F};
    bool prefer_source_when_detection_dominates_frame{true};
    float plate_dominant_min_area_ratio{0.65F};
    float plate_dominant_text_left_inset_ratio{0.10F};
    float plate_dominant_text_right_inset_ratio{0.02F};
    float plate_dominant_text_vertical_inset_ratio{0.04F};
};

[[nodiscard]] std::uint64_t fingerprint_crop(const application::ImageView& image);

class CropHypothesisGenerator final : public application::ICropGenerator {
public:
    explicit CropHypothesisGenerator(CropHypothesisGeneratorConfig config = {});

    [[nodiscard]] std::string_view name() const noexcept override {
        return "default_crop_generator";
    }

    [[nodiscard]] std::vector<application::CropHypothesis> generate(
        const application::ImageView& source,
        const domain::Detection& detection,
        const std::optional<application::ImageBuffer>& aligned,
        const application::OperationContext& context) override;

private:
    CropHypothesisGeneratorConfig config_{};
};

} // namespace fac_lpr::infrastructure::crop
