#include <fac_lpr/application/lpr_pipeline.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <string>
#include <utility>

namespace fac_lpr::application {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsed_ms(const Clock::time_point started) noexcept {
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

void check_context(const OperationContext& context) {
    if (context.cancellation_requested()) {
        throw CancelledError("LPR pipeline cancelled");
    }
    if (context.deadline_exceeded()) {
        throw TimeoutError("LPR pipeline deadline exceeded");
    }
}

void add_timing(
    LprPipelineResult& result,
    std::string stage,
    const std::size_t detection_index,
    const std::size_t crop_index,
    const Clock::time_point started) {
    result.stage_timings.push_back(PipelineStageTiming{
        .stage = std::move(stage),
        .detection_index = detection_index,
        .crop_index = crop_index,
        .latency_ms = std::max(0.0, elapsed_ms(started))});
}

void record_failure(
    LprPipelineResult& result,
    RecognitionDecisionContext& decision_context,
    std::string provider,
    const EngineErrorCode code) {
    result.degraded = true;
    ++result.provider_failure_count;
    result.failures.push_back(ProviderFailure{
        .provider = std::move(provider),
        .code = code});
    decision_context.degraded = true;
    ++decision_context.provider_failure_count;
}

} // namespace

LprPipeline::LprPipeline(LprPipelineDependencies dependencies)
    : dependencies_(std::move(dependencies)) {
    if (!dependencies_.detector ||
        !dependencies_.geometry ||
        !dependencies_.aligner ||
        !dependencies_.crop_generator ||
        !dependencies_.recognition_ensemble ||
        !dependencies_.calibrator ||
        !dependencies_.layout_analyzer ||
        !dependencies_.candidate_fusion ||
        !dependencies_.decision_policy) {
        throw ConfigurationError("LPR pipeline requires all orchestration dependencies");
    }
}

float LprPipeline::provider_weight(const std::string_view provider) const noexcept {
    for (const auto& registration : dependencies_.recognition_ensemble->recognizers()) {
        if (registration.provider && registration.provider->name() == provider) {
            return registration.enabled ? std::clamp(registration.weight, 0.0F, 1.0F) : 0.0F;
        }
    }
    return 1.0F;
}

LprPipelineResult LprPipeline::recognize(
    const ImageView& image,
    const OperationContext& context) const {
    const auto pipeline_started = Clock::now();
    LprPipelineResult pipeline_result{};
    check_context(context);
    const auto validated = validate_image(image, PerformanceConfig{});

    const auto detection_started = Clock::now();
    auto detections = dependencies_.detector->detect(validated.view, context);
    add_timing(pipeline_result, "detection", 0U, 0U, detection_started);
    check_context(context);

    for (std::size_t detection_index = 0U;
         detection_index < detections.size();
         ++detection_index) {
        auto detection = detections[detection_index];
        RecognitionDecisionContext decision_context{};

        const auto geometry_started = Clock::now();
        try {
            const auto geometry = dependencies_.geometry->evaluate(detection, context);
            detection.geometry_score = std::isfinite(geometry.score)
                ? std::clamp(geometry.score, 0.0F, 1.0F)
                : 0.0F;
        } catch (const ProviderError& error) {
            detection.geometry_score = 0.0F;
            record_failure(
                pipeline_result,
                decision_context,
                "geometry",
                error.code());
        }
        add_timing(
            pipeline_result,
            "geometry",
            detection_index,
            0U,
            geometry_started);
        check_context(context);

        const auto alignment_started = Clock::now();
        std::optional<ImageBuffer> aligned{};
        try {
            aligned = dependencies_.aligner->align(validated.view, detection, context);
        } catch (const ProviderError& error) {
            record_failure(
                pipeline_result,
                decision_context,
                "alignment",
                error.code());
        }
        add_timing(
            pipeline_result,
            "alignment",
            detection_index,
            0U,
            alignment_started);
        check_context(context);

        const auto crop_started = Clock::now();
        auto crops = dependencies_.crop_generator->generate(
            validated.view,
            detection,
            aligned,
            context);
        add_timing(
            pipeline_result,
            "crop_generation",
            detection_index,
            0U,
            crop_started);
        check_context(context);

        std::vector<domain::RecognitionEvidence> all_evidence{};
        std::vector<LayoutEvidence> all_layout{};
        float best_crop_quality = 0.0F;

        for (std::size_t crop_index = 0U; crop_index < crops.size(); ++crop_index) {
            const auto& crop = crops[crop_index];
            best_crop_quality = std::max(best_crop_quality, std::clamp(crop.quality, 0.0F, 1.0F));

            const auto recognition_started = Clock::now();
            auto ensemble = dependencies_.recognition_ensemble->recognize(
                crop.image.view(),
                crop.quality,
                context);
            add_timing(
                pipeline_result,
                "recognition",
                detection_index,
                crop_index,
                recognition_started);

            for (const auto& failure : ensemble.failures) {
                pipeline_result.failures.push_back(failure);
            }
            pipeline_result.provider_failure_count += ensemble.failures.size();
            pipeline_result.degraded = pipeline_result.degraded || ensemble.degraded;
            decision_context.degraded = decision_context.degraded || ensemble.degraded;
            decision_context.provider_failure_count += ensemble.failures.size();

            const auto calibration_started = Clock::now();
            for (auto& evidence : ensemble.evidence) {
                const auto weight = provider_weight(evidence.source);
                for (auto& candidate : evidence.candidates) {
                    const auto calibrated = dependencies_.calibrator->calibrate(
                        evidence.source,
                        candidate.confidence,
                        crop.quality,
                        crop.type);
                    candidate.calibrated_confidence = std::clamp(
                        calibrated * weight,
                        0.0F,
                        1.0F);
                }
            }
            add_timing(
                pipeline_result,
                "calibration",
                detection_index,
                crop_index,
                calibration_started);

            std::vector<domain::PlateCandidate> crop_candidates{};
            for (const auto& evidence : ensemble.evidence) {
                crop_candidates.insert(
                    crop_candidates.end(),
                    evidence.candidates.begin(),
                    evidence.candidates.end());
            }

            const auto layout_started = Clock::now();
            LayoutEvidence layout{};
            try {
                layout = dependencies_.layout_analyzer->analyze(
                    crop.image.view(),
                    crop_candidates,
                    context);
            } catch (const ProviderError& error) {
                record_failure(
                    pipeline_result,
                    decision_context,
                    "layout",
                    error.code());
            }
            add_timing(
                pipeline_result,
                "layout",
                detection_index,
                crop_index,
                layout_started);

            all_layout.insert(
                all_layout.end(),
                ensemble.evidence.size(),
                layout);
            all_evidence.insert(
                all_evidence.end(),
                std::make_move_iterator(ensemble.evidence.begin()),
                std::make_move_iterator(ensemble.evidence.end()));
            check_context(context);
        }

        const auto fusion_started = Clock::now();
        auto fused = dependencies_.candidate_fusion->fuse(all_evidence, all_layout);
        add_timing(
            pipeline_result,
            "fusion",
            detection_index,
            0U,
            fusion_started);

        const auto decision_started = Clock::now();
        auto recognition = dependencies_.decision_policy->decide(
            detection,
            all_evidence,
            fused,
            decision_context);
        add_timing(
            pipeline_result,
            "decision",
            detection_index,
            0U,
            decision_started);

        recognition.crop_quality = best_crop_quality;
        recognition.degraded = recognition.degraded || decision_context.degraded;
        recognition.total_latency_ms = elapsed_ms(pipeline_started);
        pipeline_result.recognitions.push_back(std::move(recognition));
        check_context(context);
    }

    pipeline_result.total_latency_ms = std::max(0.0, elapsed_ms(pipeline_started));
    return pipeline_result;
}

} // namespace fac_lpr::application
