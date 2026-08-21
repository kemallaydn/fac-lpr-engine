#pragma once

#include <fac_lpr/application/providers.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fac_lpr::application {

class IdentityConfidenceCalibrator final : public IConfidenceCalibrator {
public:
    [[nodiscard]] float calibrate(
        std::string_view source,
        float raw_confidence,
        float crop_quality,
        std::string_view crop_type) const override;
};

struct LogisticCalibrationSegment final {
    std::string provider{};
    std::string crop_type{};
    float slope{1.0F};
    float intercept{0.0F};
    std::size_t sample_count{0U};
};

struct LogisticCalibrationConfig final {
    std::size_t minimum_samples{100U};
    float probability_epsilon{1.0e-5F};
};

class LogisticConfidenceCalibrator final : public IConfidenceCalibrator {
public:
    LogisticConfidenceCalibrator(
        LogisticCalibrationConfig config,
        std::vector<LogisticCalibrationSegment> segments);

    [[nodiscard]] float calibrate(
        std::string_view source,
        float raw_confidence,
        float crop_quality,
        std::string_view crop_type) const override;

private:
    [[nodiscard]] static std::string key(std::string_view provider, std::string_view crop_type);
    [[nodiscard]] const LogisticCalibrationSegment* find_segment(
        std::string_view provider,
        std::string_view crop_type) const noexcept;
    static void validate_probability(float value, const char* field);

    LogisticCalibrationConfig config_{};
    std::unordered_map<std::string, LogisticCalibrationSegment> segments_{};
};

} // namespace fac_lpr::application
