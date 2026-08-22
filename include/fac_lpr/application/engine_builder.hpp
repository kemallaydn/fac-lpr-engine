#pragma once

#include <fac_lpr/application/lpr_pipeline.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fac_lpr::application {

enum class ProviderRole {
    detector,
    geometry,
    aligner,
    crop_generator,
    recognizer,
    calibrator,
    layout_analyzer,
    candidate_fusion,
    decision_policy
};

struct ProviderRegistrationInfo final {
    ProviderRole role{};
    std::string name{};
};

class ProviderRegistry final {
public:
    void register_singleton(ProviderRole role, std::string_view name);
    void register_multi(ProviderRole role, std::string_view name);

    [[nodiscard]] const std::vector<ProviderRegistrationInfo>& registrations() const noexcept {
        return registrations_;
    }

private:
    [[nodiscard]] static std::string role_key(ProviderRole role);
    void register_impl(ProviderRole role, std::string_view name, bool singleton);

    std::unordered_set<std::string> keys_{};
    std::unordered_set<std::string> singleton_roles_{};
    std::vector<ProviderRegistrationInfo> registrations_{};
};

class LprEngineBuilder final {
public:
    LprEngineBuilder& detector(std::shared_ptr<IPlateDetector> provider);
    LprEngineBuilder& geometry(std::string_view name, std::shared_ptr<IPlateGeometryEvaluator> provider);
    LprEngineBuilder& aligner(std::string_view name, std::shared_ptr<IPlateAligner> provider);
    LprEngineBuilder& crop_generator(std::shared_ptr<ICropGenerator> provider);
    LprEngineBuilder& recognizer(RecognizerRegistration registration);
    LprEngineBuilder& calibrator(std::string_view name, std::shared_ptr<IConfidenceCalibrator> provider);
    LprEngineBuilder& layout_analyzer(std::string_view name, std::shared_ptr<IPlateLayoutAnalyzer> provider);
    LprEngineBuilder& candidate_fusion(std::string_view name, std::shared_ptr<ICandidateFusion> provider);
    LprEngineBuilder& decision_policy(std::string_view name, std::shared_ptr<IDecisionPolicy> provider);
    LprEngineBuilder& diagnostics(std::shared_ptr<EngineDiagnostics> diagnostics);

    [[nodiscard]] std::shared_ptr<LprPipeline> build() const;
    [[nodiscard]] const ProviderRegistry& registry() const noexcept { return registry_; }

private:
    ProviderRegistry registry_{};
    LprPipelineDependencies dependencies_{};
    std::vector<RecognizerRegistration> recognizers_{};
};

} // namespace fac_lpr::application
