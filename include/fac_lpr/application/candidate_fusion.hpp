#pragma once

#include <fac_lpr/application/providers.hpp>

#include <cstddef>
#include <map>
#include <string>

namespace fac_lpr::application {

struct CandidateFusionConfig final {
    std::size_t max_candidates{5U};
    float default_source_weight{1.0F};
    std::map<std::string, float> source_weights{};
    float recognition_weight{0.65F};
    float crop_quality_weight{0.15F};
    float candidate_margin_weight{0.10F};
    float layout_weight{0.10F};
    float minimum_candidate_confidence{0.01F};
};

class WeightedMultiCropCandidateFusion final : public ICandidateFusion {
public:
    explicit WeightedMultiCropCandidateFusion(CandidateFusionConfig config = {});

    [[nodiscard]] std::vector<domain::PlateCandidate> fuse(
        std::span<const domain::RecognitionEvidence> evidence,
        std::span<const LayoutEvidence> layout_evidence) const override;

private:
    [[nodiscard]] float source_weight(std::string_view source) const noexcept;

    CandidateFusionConfig config_{};
};

} // namespace fac_lpr::application
