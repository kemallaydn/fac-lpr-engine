#pragma once

#include <fac_lpr/application/providers.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace fac_lpr::application {

struct RecognizerRegistration final {
    std::shared_ptr<IPlateRecognizer> provider{};
    float weight{1.0F};
    std::chrono::milliseconds timeout{1000};
    bool required{false};
    bool enabled{true};
};

struct ProviderFailure final {
    std::string provider{};
    EngineErrorCode code{EngineErrorCode::provider};
};

struct RecognitionEnsembleResult final {
    std::vector<domain::RecognitionEvidence> evidence{};
    std::vector<ProviderFailure> failures{};
    bool degraded{false};
};

class RecognitionEnsemble final {
public:
    explicit RecognitionEnsemble(
        std::vector<RecognizerRegistration> recognizers);

    [[nodiscard]] const std::vector<RecognizerRegistration>& recognizers() const noexcept {
        return recognizers_;
    }

    [[nodiscard]] RecognitionEnsembleResult recognize(
        const ImageView& plate,
        float crop_quality,
        const OperationContext& context) const;

private:
    std::vector<RecognizerRegistration> recognizers_{};
};

} // namespace fac_lpr::application
