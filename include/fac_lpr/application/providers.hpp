#pragma once

#include <fac_lpr/application/image.hpp>
#include <fac_lpr/application/operation_context.hpp>
#include <fac_lpr/domain/detection.hpp>
#include <fac_lpr/domain/recognition.hpp>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fac_lpr::application {

struct LayoutEvidence final {
    bool reliable{false};
    std::optional<int> character_count{};
    std::optional<int> letter_group_size{};
    float confidence{0.0F};
};

class IPlateDetector {
public:
    virtual ~IPlateDetector() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::vector<domain::Detection> detect(
        const ImageView& image,
        const OperationContext& context) = 0;
};

class IPlateAligner {
public:
    virtual ~IPlateAligner() = default;

    [[nodiscard]] virtual std::optional<ImageBuffer> align(
        const ImageView& source,
        const domain::Detection& detection,
        const OperationContext& context) = 0;
};

class ICropGenerator {
public:
    virtual ~ICropGenerator() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::vector<CropHypothesis> generate(
        const ImageView& source,
        const domain::Detection& detection,
        const std::optional<ImageBuffer>& aligned,
        const OperationContext& context) = 0;
};

class IPlateRecognizer {
public:
    virtual ~IPlateRecognizer() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual domain::RecognitionEvidence recognize(
        const ImageView& plate,
        const OperationContext& context) = 0;
};

class IPlateLayoutAnalyzer {
public:
    virtual ~IPlateLayoutAnalyzer() = default;

    [[nodiscard]] virtual LayoutEvidence analyze(
        const ImageView& plate,
        std::span<const domain::PlateCandidate> candidates,
        const OperationContext& context) = 0;
};

class ICandidateFusion {
public:
    virtual ~ICandidateFusion() = default;

    [[nodiscard]] virtual std::vector<domain::PlateCandidate> fuse(
        std::span<const domain::RecognitionEvidence> evidence,
        std::span<const LayoutEvidence> layout_evidence) const = 0;
};

class IConfidenceCalibrator {
public:
    virtual ~IConfidenceCalibrator() = default;

    [[nodiscard]] virtual float calibrate(
        std::string_view source,
        float raw_confidence,
        float crop_quality,
        std::string_view crop_type) const = 0;
};

class IDecisionPolicy {
public:
    virtual ~IDecisionPolicy() = default;

    [[nodiscard]] virtual domain::PlateRecognitionResult decide(
        const domain::Detection& detection,
        std::span<const domain::RecognitionEvidence> evidence,
        std::span<const domain::PlateCandidate> fused_candidates) const = 0;
};

} // namespace fac_lpr::application
