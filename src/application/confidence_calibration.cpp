#include <fac_lpr/application/confidence_calibration.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace fac_lpr::application {

namespace {

void validate_common_input(const float raw_confidence, const float crop_quality) {
    if (!std::isfinite(raw_confidence) || raw_confidence < 0.0F || raw_confidence > 1.0F) {
        throw ProviderError("raw confidence must be finite and within [0, 1]");
    }
    if (!std::isfinite(crop_quality) || crop_quality < 0.0F || crop_quality > 1.0F) {
        throw ProviderError("crop quality must be finite and within [0, 1]");
    }
}

} // namespace

float IdentityConfidenceCalibrator::calibrate(
    const std::string_view,
    const float raw_confidence,
    const float crop_quality,
    const std::string_view) const {
    validate_common_input(raw_confidence, crop_quality);
    return raw_confidence;
}

std::string LogisticConfidenceCalibrator::key(
    const std::string_view provider,
    const std::string_view crop_type) {
    std::string value;
    value.reserve(provider.size() + crop_type.size() + 1U);
    value.append(provider);
    value.push_back('\x1f');
    value.append(crop_type);
    return value;
}

void LogisticConfidenceCalibrator::validate_probability(
    const float value,
    const char* field) {
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
        throw ProviderError(std::string{field} + " must be finite and within [0, 1]");
    }
}

LogisticConfidenceCalibrator::LogisticConfidenceCalibrator(
    LogisticCalibrationConfig config,
    std::vector<LogisticCalibrationSegment> segments)
    : config_(config) {
    if (config_.minimum_samples == 0U) {
        throw ConfigurationError("calibration minimum_samples must be greater than zero");
    }
    if (!std::isfinite(config_.probability_epsilon) ||
        config_.probability_epsilon <= 0.0F || config_.probability_epsilon >= 0.5F) {
        throw ConfigurationError("calibration probability_epsilon must be finite and in (0, 0.5)");
    }

    for (auto& segment : segments) {
        if (segment.provider.empty()) {
            throw ConfigurationError("calibration provider cannot be empty");
        }
        if (!std::isfinite(segment.slope) || segment.slope <= 0.0F ||
            !std::isfinite(segment.intercept)) {
            throw ConfigurationError("calibration slope must be positive and parameters must be finite");
        }
        const auto segment_key = key(segment.provider, segment.crop_type);
        if (!segments_.emplace(segment_key, std::move(segment)).second) {
            throw ConfigurationError("duplicate provider/crop calibration segment");
        }
    }
}

const LogisticCalibrationSegment* LogisticConfidenceCalibrator::find_segment(
    const std::string_view provider,
    const std::string_view crop_type) const noexcept {
    if (const auto exact = segments_.find(key(provider, crop_type)); exact != segments_.end()) {
        return &exact->second;
    }
    if (!crop_type.empty()) {
        if (const auto fallback = segments_.find(key(provider, {})); fallback != segments_.end()) {
            return &fallback->second;
        }
    }
    return nullptr;
}

float LogisticConfidenceCalibrator::calibrate(
    const std::string_view source,
    const float raw_confidence,
    const float crop_quality,
    const std::string_view crop_type) const {
    validate_common_input(raw_confidence, crop_quality);
    if (source.empty()) {
        throw ProviderError("calibration source cannot be empty");
    }

    const auto* segment = find_segment(source, crop_type);
    if (segment == nullptr || segment->sample_count < config_.minimum_samples) {
        return raw_confidence;
    }

    const auto probability = std::clamp(
        raw_confidence,
        config_.probability_epsilon,
        1.0F - config_.probability_epsilon);
    const auto logit = std::log(probability / (1.0F - probability));
    const auto calibrated_logit = (segment->slope * logit) + segment->intercept;
    const auto calibrated = 1.0F / (1.0F + std::exp(-calibrated_logit));
    if (!std::isfinite(calibrated)) {
        throw ProviderError("calibration produced a non-finite confidence");
    }
    return std::clamp(calibrated, 0.0F, 1.0F);
}

} // namespace fac_lpr::application
