#pragma once

#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/providers.hpp>

namespace fac_lpr::application {

class SafeRecognitionDecisionPolicy final : public IDecisionPolicy {
public:
    explicit SafeRecognitionDecisionPolicy(DecisionConfig config = {});

    [[nodiscard]] domain::PlateRecognitionResult decide(
        const domain::Detection& detection,
        std::span<const domain::RecognitionEvidence> evidence,
        std::span<const domain::PlateCandidate> fused_candidates,
        const RecognitionDecisionContext& context) const override;

private:
    DecisionConfig config_{};
};

} // namespace fac_lpr::application
