#include <fac_lpr/application/engine_builder.hpp>

#include <fac_lpr/application/error.hpp>

#include <utility>

namespace fac_lpr::application {
namespace {

[[nodiscard]] std::string_view role_name(const ProviderRole role) noexcept {
    switch (role) {
        case ProviderRole::detector: return "detector";
        case ProviderRole::geometry: return "geometry";
        case ProviderRole::aligner: return "aligner";
        case ProviderRole::crop_generator: return "crop_generator";
        case ProviderRole::recognizer: return "recognizer";
        case ProviderRole::calibrator: return "calibrator";
        case ProviderRole::layout_analyzer: return "layout_analyzer";
        case ProviderRole::candidate_fusion: return "candidate_fusion";
        case ProviderRole::decision_policy: return "decision_policy";
    }
    return "unknown";
}

template <typename T>
void require_provider(const std::shared_ptr<T>& provider, const char* role) {
    if (!provider) {
        throw ConfigurationError(std::string{"missing provider for role: "} + role);
    }
}

} // namespace

std::string ProviderRegistry::role_key(const ProviderRole role) {
    return std::string{role_name(role)};
}

void ProviderRegistry::register_singleton(const ProviderRole role, const std::string_view name) {
    register_impl(role, name, true);
}

void ProviderRegistry::register_multi(const ProviderRole role, const std::string_view name) {
    register_impl(role, name, false);
}

void ProviderRegistry::register_impl(
    const ProviderRole role,
    const std::string_view name,
    const bool singleton) {
    if (name.empty()) {
        throw ConfigurationError("provider registration name cannot be empty");
    }

    const auto role_text = role_key(role);
    const auto key = role_text + ":" + std::string{name};
    if (!keys_.emplace(key).second) {
        throw ConfigurationError("duplicate provider name/type registration: " + key);
    }
    if (singleton && !singleton_roles_.emplace(role_text).second) {
        keys_.erase(key);
        throw ConfigurationError("duplicate singleton provider role registration: " + role_text);
    }
    registrations_.push_back({role, std::string{name}});
}

LprEngineBuilder& LprEngineBuilder::detector(std::shared_ptr<IPlateDetector> provider) {
    require_provider(provider, "detector");
    registry_.register_singleton(ProviderRole::detector, provider->name());
    dependencies_.detector = std::move(provider);
    return *this;
}

LprEngineBuilder& LprEngineBuilder::geometry(
    const std::string_view name,
    std::shared_ptr<IPlateGeometryEvaluator> provider) {
    require_provider(provider, "geometry");
    registry_.register_singleton(ProviderRole::geometry, name);
    dependencies_.geometry = std::move(provider);
    return *this;
}

LprEngineBuilder& LprEngineBuilder::aligner(
    const std::string_view name,
    std::shared_ptr<IPlateAligner> provider) {
    require_provider(provider, "aligner");
    registry_.register_singleton(ProviderRole::aligner, name);
    dependencies_.aligner = std::move(provider);
    return *this;
}

LprEngineBuilder& LprEngineBuilder::crop_generator(std::shared_ptr<ICropGenerator> provider) {
    require_provider(provider, "crop_generator");
    registry_.register_singleton(ProviderRole::crop_generator, provider->name());
    dependencies_.crop_generator = std::move(provider);
    return *this;
}

LprEngineBuilder& LprEngineBuilder::recognizer(RecognizerRegistration registration) {
    require_provider(registration.provider, "recognizer");
    if (!registration.enabled) {
        throw ConfigurationError("disabled recognizer cannot be registered in the active engine builder");
    }
    registry_.register_multi(ProviderRole::recognizer, registration.provider->name());
    recognizers_.push_back(std::move(registration));
    return *this;
}

LprEngineBuilder& LprEngineBuilder::calibrator(
    const std::string_view name,
    std::shared_ptr<IConfidenceCalibrator> provider) {
    require_provider(provider, "calibrator");
    registry_.register_singleton(ProviderRole::calibrator, name);
    dependencies_.calibrator = std::move(provider);
    return *this;
}

LprEngineBuilder& LprEngineBuilder::layout_analyzer(
    const std::string_view name,
    std::shared_ptr<IPlateLayoutAnalyzer> provider) {
    require_provider(provider, "layout_analyzer");
    registry_.register_singleton(ProviderRole::layout_analyzer, name);
    dependencies_.layout_analyzer = std::move(provider);
    return *this;
}

LprEngineBuilder& LprEngineBuilder::candidate_fusion(
    const std::string_view name,
    std::shared_ptr<ICandidateFusion> provider) {
    require_provider(provider, "candidate_fusion");
    registry_.register_singleton(ProviderRole::candidate_fusion, name);
    dependencies_.candidate_fusion = std::move(provider);
    return *this;
}

LprEngineBuilder& LprEngineBuilder::decision_policy(
    const std::string_view name,
    std::shared_ptr<IDecisionPolicy> provider) {
    require_provider(provider, "decision_policy");
    registry_.register_singleton(ProviderRole::decision_policy, name);
    dependencies_.decision_policy = std::move(provider);
    return *this;
}

LprEngineBuilder& LprEngineBuilder::diagnostics(std::shared_ptr<EngineDiagnostics> diagnostics) {
    if (!diagnostics) {
        throw ConfigurationError("engine diagnostics cannot be null");
    }
    dependencies_.diagnostics = std::move(diagnostics);
    return *this;
}

std::shared_ptr<LprPipeline> LprEngineBuilder::build() const {
    require_provider(dependencies_.detector, "detector");
    require_provider(dependencies_.geometry, "geometry");
    require_provider(dependencies_.aligner, "aligner");
    require_provider(dependencies_.crop_generator, "crop_generator");
    require_provider(dependencies_.calibrator, "calibrator");
    require_provider(dependencies_.layout_analyzer, "layout_analyzer");
    require_provider(dependencies_.candidate_fusion, "candidate_fusion");
    require_provider(dependencies_.decision_policy, "decision_policy");
    if (recognizers_.empty()) {
        throw ConfigurationError("at least one recognizer must be registered");
    }

    auto dependencies = dependencies_;
    dependencies.recognition_ensemble = std::make_shared<RecognitionEnsemble>(recognizers_);
    return std::make_shared<LprPipeline>(std::move(dependencies));
}

} // namespace fac_lpr::application
