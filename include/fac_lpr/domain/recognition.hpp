#pragma once

#include <fac_lpr/domain/detection.hpp>

#include <optional>
#include <string>
#include <vector>

namespace fac_lpr::domain {

enum class RecognitionStatus {
    accepted,
    review,
    rejected
};

struct PlateCandidate final {
    std::string text{};
    float confidence{0.0F};
    float calibrated_confidence{0.0F};
    bool format_valid{false};
};

struct RecognitionEvidence final {
    std::string source{};
    std::vector<PlateCandidate> candidates{};
    float crop_quality{0.0F};
    double latency_ms{0.0};
};

struct PlateRecognitionResult final {
    RecognitionStatus status{RecognitionStatus::rejected};
    std::string plate{};
    float confidence{0.0F};

    std::optional<BoundingBox> bbox{};
    std::optional<PlateQuadrilateral> quadrilateral{};

    float detector_confidence{0.0F};
    float geometry_score{0.0F};
    float crop_quality{0.0F};

    std::vector<RecognitionEvidence> evidence{};
    std::vector<PlateCandidate> alternatives{};

    double total_latency_ms{0.0};
};

} // namespace fac_lpr::domain
