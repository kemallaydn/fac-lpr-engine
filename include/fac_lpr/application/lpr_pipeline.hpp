#pragma once

#include <fac_lpr/application/confidence_calibration.hpp>
#include <fac_lpr/application/engine_diagnostics.hpp>
#include <fac_lpr/application/recognition_ensemble.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace fac_lpr::application {

struct PipelineStageTiming final {
    std::string stage{};
    std::size_t detection_index{0U};
    std::size_t crop_index{0U};
    double latency_ms{0.0};
};

struct LprPipelineResult final {
    std::vector<domain::PlateRecognitionResult> recognitions{};
    std::vector<PipelineStageTiming> stage_timings{};
    std::vector<ProviderFailure> failures{};
    double total_latency_ms{0.0};
    std::size_t provider_failure_count{0U};
    bool degraded{false};
};

struct LprPipelineDependencies final {
    std::shared_ptr<IPlateDetector> detector{};
    std::shared_ptr<IPlateGeometryEvaluator> geometry{};
    std::shared_ptr<IPlateAligner> aligner{};
    std::shared_ptr<ICropGenerator> crop_generator{};
    std::shared_ptr<RecognitionEnsemble> recognition_ensemble{};
    std::shared_ptr<IConfidenceCalibrator> calibrator{};
    std::shared_ptr<IPlateLayoutAnalyzer> layout_analyzer{};
    std::shared_ptr<ICandidateFusion> candidate_fusion{};
    std::shared_ptr<IDecisionPolicy> decision_policy{};
    std::shared_ptr<EngineDiagnostics> diagnostics{std::make_shared<EngineDiagnostics>()};
};

class LprPipeline final {
public:
    explicit LprPipeline(LprPipelineDependencies dependencies);

    [[nodiscard]] LprPipelineResult recognize(
        const ImageView& image,
        const OperationContext& context = {}) const;

    [[nodiscard]] EngineDiagnosticsSnapshot diagnostics_snapshot() const;
    [[nodiscard]] std::shared_ptr<EngineDiagnostics> diagnostics() const noexcept;

private:
    [[nodiscard]] float provider_weight(std::string_view provider) const noexcept;

    LprPipelineDependencies dependencies_{};
};

} // namespace fac_lpr::application
