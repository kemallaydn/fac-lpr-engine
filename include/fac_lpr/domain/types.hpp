#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fac_lpr::domain {

struct Point2f final {
    float x{0.0F};
    float y{0.0F};

    [[nodiscard]] constexpr bool is_finite() const noexcept {
        return std::isfinite(x) && std::isfinite(y);
    }

    friend constexpr bool operator==(const Point2f&, const Point2f&) = default;
};

struct BoundingBox final {
    float x{0.0F};
    float y{0.0F};
    float width{0.0F};
    float height{0.0F};

    [[nodiscard]] constexpr float area() const noexcept {
        return width > 0.0F && height > 0.0F ? width * height : 0.0F;
    }

    [[nodiscard]] constexpr bool is_finite() const noexcept {
        return std::isfinite(x) && std::isfinite(y) &&
               std::isfinite(width) && std::isfinite(height);
    }

    [[nodiscard]] constexpr bool is_positive() const noexcept {
        return width > 0.0F && height > 0.0F;
    }

    friend constexpr bool operator==(const BoundingBox&, const BoundingBox&) = default;
};

struct Detection final {
    BoundingBox bbox{};
    std::array<Point2f, 4> keypoints{};
    std::array<float, 4> keypoint_confidences{};
    float confidence{0.0F};
    float geometry_score{0.0F};
    std::string provider{};
    std::string model_version{};
};

struct PlateCandidate final {
    std::string text{};
    float confidence{0.0F};
    float calibrated_confidence{0.0F};
    bool format_valid{false};
    float margin{0.0F};
};

struct RecognitionEvidence final {
    std::string source{};
    std::string model_version{};
    std::string crop_type{};
    std::vector<PlateCandidate> candidates{};
    float crop_quality{0.0F};
    double latency_ms{0.0};
};

enum class RecognitionStatus {
    accepted,
    review,
    rejected,
};

struct PlateRecognitionResult final {
    RecognitionStatus status{RecognitionStatus::rejected};
    std::string plate{};
    float confidence{0.0F};

    BoundingBox bbox{};
    std::array<Point2f, 4> keypoints{};

    float detector_confidence{0.0F};
    float geometry_score{0.0F};
    float crop_quality{0.0F};

    std::vector<RecognitionEvidence> evidence{};
    std::vector<PlateCandidate> alternatives{};
    std::vector<std::string> decision_reasons{};

    double total_latency_ms{0.0};
};

} // namespace fac_lpr::domain
