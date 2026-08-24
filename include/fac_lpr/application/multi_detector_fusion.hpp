#pragma once

#include <fac_lpr/application/providers.hpp>

#include <memory>
#include <string>
#include <vector>

namespace fac_lpr::application {

struct DetectorRegistration final {
    std::shared_ptr<IPlateDetector> provider{};
    float weight{1.0F};
    bool enabled{true};
};

struct MultiDetectorFusionConfig final {
    float same_model_iou_threshold{0.70F};
    float same_model_min_area_overlap_threshold{0.85F};
    float cross_model_iou_threshold{0.45F};
    float cross_model_min_area_overlap_threshold{0.70F};
    std::size_t maximum_groups{64U};
};

struct DetectionEvidence final {
    domain::Detection detection{};
    std::string provider{};
    float provider_weight{1.0F};
};

struct DetectionGroup final {
    domain::Detection fused{};
    std::vector<DetectionEvidence> evidence{};
};

class MultiDetectorFusion final : public IPlateDetector {
public:
    explicit MultiDetectorFusion(
        std::vector<DetectorRegistration> detectors,
        MultiDetectorFusionConfig config = {});

    [[nodiscard]] std::string_view name() const noexcept override {
        return "multi_detector_fusion";
    }

    [[nodiscard]] std::vector<domain::Detection> detect(
        const ImageView& image,
        const OperationContext& context) override;

    [[nodiscard]] std::vector<DetectionGroup> detect_groups(
        const ImageView& image,
        const OperationContext& context);

    [[nodiscard]] const std::vector<DetectorRegistration>& detectors() const noexcept {
        return detectors_;
    }

private:
    std::vector<DetectorRegistration> detectors_{};
    MultiDetectorFusionConfig config_{};
};

} // namespace fac_lpr::application
