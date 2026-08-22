#include <fac_lpr/application/recognition_ensemble.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>

namespace fac_lpr::application {
namespace {

[[nodiscard]] OperationContext provider_context(
    const OperationContext& parent,
    const std::chrono::milliseconds timeout) {
    OperationContext child{};
    child.stop_token = parent.stop_token;
    const auto local_deadline = std::chrono::steady_clock::now() + timeout;
    child.deadline = parent.deadline.has_value()
        ? std::min(*parent.deadline, local_deadline)
        : local_deadline;
    return child;
}

[[nodiscard]] float effective_provider_confidence(
    const domain::PlateCandidate& candidate,
    const float provider_weight) noexcept {
    const auto base = candidate.calibrated_confidence > 0.0F
        ? candidate.calibrated_confidence
        : candidate.confidence;
    return std::clamp(base * provider_weight, 0.0F, 1.0F);
}

void validate_evidence(
    domain::RecognitionEvidence& evidence,
    const std::string_view provider_name,
    const float provider_weight,
    const float crop_quality,
    const double latency_ms) {
    evidence.source = std::string{provider_name};
    evidence.crop_quality = std::clamp(crop_quality, 0.0F, 1.0F);
    evidence.latency_ms = std::max(0.0, latency_ms);
    for (auto& candidate : evidence.candidates) {
        if (candidate.text.empty() || !std::isfinite(candidate.confidence) ||
            candidate.confidence < 0.0F || candidate.confidence > 1.0F ||
            !std::isfinite(candidate.calibrated_confidence) ||
            candidate.calibrated_confidence < 0.0F ||
            candidate.calibrated_confidence > 1.0F) {
            throw ProviderError("recognizer returned invalid candidate evidence");
        }
        candidate.calibrated_confidence = effective_provider_confidence(
            candidate,
            provider_weight);
    }
}

} // namespace

RecognitionEnsemble::RecognitionEnsemble(
    std::vector<RecognizerRegistration> recognizers)
    : recognizers_(std::move(recognizers)) {
    if (recognizers_.empty()) {
        throw ConfigurationError("recognition ensemble requires at least one recognizer");
    }

    std::set<std::string> names{};
    for (const auto& registration : recognizers_) {
        if (!registration.provider) {
            throw ConfigurationError("recognizer registration cannot contain a null provider");
        }
        const auto name = registration.provider->name();
        if (name.empty() || !names.emplace(name).second) {
            throw ConfigurationError("recognizer provider names must be non-empty and unique");
        }
        if (!std::isfinite(registration.weight) || registration.weight < 0.0F ||
            registration.weight > 1.0F) {
            throw ConfigurationError("recognizer provider weight must be in [0,1]");
        }
        if (registration.timeout <= std::chrono::milliseconds::zero() ||
            registration.timeout > std::chrono::minutes{5}) {
            throw ConfigurationError("recognizer provider timeout is outside safe limits");
        }
    }
}

RecognitionEnsembleResult RecognitionEnsemble::recognize(
    const ImageView& plate,
    const float crop_quality,
    const OperationContext& context) const {
    if (!std::isfinite(crop_quality)) {
        throw InvalidImageError("crop quality must be finite");
    }
    if (context.cancellation_requested()) {
        throw CancelledError("recognition ensemble cancelled");
    }
    if (context.deadline_exceeded()) {
        throw TimeoutError("recognition ensemble deadline exceeded");
    }

    RecognitionEnsembleResult result{};
    for (const auto& registration : recognizers_) {
        if (!registration.enabled || registration.weight <= 0.0F) {
            continue;
        }

        const auto provider_name = std::string{registration.provider->name()};
        const auto child_context = provider_context(context, registration.timeout);
        const auto started = std::chrono::steady_clock::now();
        try {
            auto evidence = registration.provider->recognize(plate, child_context);
            const auto finished = std::chrono::steady_clock::now();
            const auto latency = std::chrono::duration<double, std::milli>(
                finished - started).count();
            validate_evidence(
                evidence,
                provider_name,
                registration.weight,
                crop_quality,
                latency);
            result.evidence.push_back(std::move(evidence));
        } catch (const EngineError& error) {
            if (registration.required) {
                throw ProviderError(
                    "required recognizer provider failed: " + provider_name);
            }
            result.degraded = true;
            result.failures.push_back(ProviderFailure{
                .provider = provider_name,
                .code = error.code()});
        } catch (...) {
            if (registration.required) {
                throw ProviderError(
                    "required recognizer provider failed: " + provider_name);
            }
            result.degraded = true;
            result.failures.push_back(ProviderFailure{
                .provider = provider_name,
                .code = EngineErrorCode::internal});
        }
    }

    if (result.evidence.empty() && !result.failures.empty()) {
        result.degraded = true;
    }
    return result;
}

} // namespace fac_lpr::application
