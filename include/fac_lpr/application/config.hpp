#pragma once

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace fac_lpr::application {

struct DetectorConfig final {
    float confidence_threshold{0.50F};
    float nms_iou_threshold{0.45F};
    std::size_t max_detections{32};
    bool adaptive_tiling{true};
    std::size_t tile_width{1280};
    std::size_t tile_height{960};
    float tile_overlap_ratio{0.20F};
};

struct RecognitionConfig final {
    std::size_t beam_width{16};
    std::size_t result_limit{5};
    std::size_t classes_per_step{6};
    std::size_t max_recognizers{8};
    float minimum_candidate_confidence{0.0F};
};

struct CropConfig final {
    std::size_t max_hypotheses{8};
    float horizontal_padding_ratio{0.06F};
    float vertical_padding_ratio{0.04F};
    std::size_t minimum_width{60};
    std::size_t minimum_height{18};
    bool enable_clahe{true};
    bool enable_sharpen{true};
    bool enable_adaptive_threshold{true};
    bool enable_double_row{true};
};

struct DecisionConfig final {
    float accepted_confidence_threshold{0.78F};
    float minimum_crop_quality{0.28F};
    float minimum_effective_detector_confidence{0.50F};
    bool fail_closed{true};
};

struct PerformanceConfig final {
    std::size_t worker_count{1};
    std::size_t queue_capacity{8};
    std::size_t max_image_width{8192};
    std::size_t max_image_height{8192};
    std::size_t max_image_bytes{128U * 1024U * 1024U};
    std::chrono::milliseconds recognition_timeout{5000};
};

struct EngineConfig final {
    DetectorConfig detector{};
    RecognitionConfig recognition{};
    CropConfig crop{};
    DecisionConfig decision{};
    PerformanceConfig performance{};
};

namespace detail {

inline void require_unit_interval(float value, const char* name) {
    if (!(value >= 0.0F && value <= 1.0F)) {
        throw std::invalid_argument(std::string{name} + " must be in [0, 1]");
    }
}

inline void require_positive(std::size_t value, const char* name) {
    if (value == 0U) {
        throw std::invalid_argument(std::string{name} + " must be greater than zero");
    }
}

} // namespace detail

inline void validate_engine_config(const EngineConfig& config) {
    detail::require_unit_interval(config.detector.confidence_threshold, "detector.confidence_threshold");
    detail::require_unit_interval(config.detector.nms_iou_threshold, "detector.nms_iou_threshold");
    detail::require_positive(config.detector.max_detections, "detector.max_detections");
    detail::require_positive(config.detector.tile_width, "detector.tile_width");
    detail::require_positive(config.detector.tile_height, "detector.tile_height");

    if (!(config.detector.tile_overlap_ratio >= 0.0F && config.detector.tile_overlap_ratio < 0.50F)) {
        throw std::invalid_argument("detector.tile_overlap_ratio must be in [0, 0.5)");
    }

    detail::require_positive(config.recognition.beam_width, "recognition.beam_width");
    detail::require_positive(config.recognition.result_limit, "recognition.result_limit");
    detail::require_positive(config.recognition.classes_per_step, "recognition.classes_per_step");
    detail::require_positive(config.recognition.max_recognizers, "recognition.max_recognizers");
    detail::require_unit_interval(
        config.recognition.minimum_candidate_confidence,
        "recognition.minimum_candidate_confidence");

    if (config.recognition.result_limit > config.recognition.beam_width) {
        throw std::invalid_argument("recognition.result_limit cannot exceed recognition.beam_width");
    }
    if (config.recognition.classes_per_step > 64U) {
        throw std::invalid_argument("recognition.classes_per_step cannot exceed 64");
    }
    if (config.recognition.max_recognizers > 32U) {
        throw std::invalid_argument("recognition.max_recognizers cannot exceed 32");
    }

    detail::require_positive(config.crop.max_hypotheses, "crop.max_hypotheses");
    detail::require_unit_interval(config.crop.horizontal_padding_ratio, "crop.horizontal_padding_ratio");
    detail::require_unit_interval(config.crop.vertical_padding_ratio, "crop.vertical_padding_ratio");
    detail::require_positive(config.crop.minimum_width, "crop.minimum_width");
    detail::require_positive(config.crop.minimum_height, "crop.minimum_height");
    if (config.crop.max_hypotheses > 32U) {
        throw std::invalid_argument("crop.max_hypotheses cannot exceed 32");
    }

    detail::require_unit_interval(
        config.decision.accepted_confidence_threshold,
        "decision.accepted_confidence_threshold");
    detail::require_unit_interval(config.decision.minimum_crop_quality, "decision.minimum_crop_quality");
    detail::require_unit_interval(
        config.decision.minimum_effective_detector_confidence,
        "decision.minimum_effective_detector_confidence");
    if (!config.decision.fail_closed) {
        throw std::invalid_argument("decision.fail_closed must remain enabled for the safe default policy");
    }

    detail::require_positive(config.performance.worker_count, "performance.worker_count");
    detail::require_positive(config.performance.queue_capacity, "performance.queue_capacity");
    detail::require_positive(config.performance.max_image_width, "performance.max_image_width");
    detail::require_positive(config.performance.max_image_height, "performance.max_image_height");
    detail::require_positive(config.performance.max_image_bytes, "performance.max_image_bytes");

    if (config.performance.worker_count > 256U) {
        throw std::invalid_argument("performance.worker_count cannot exceed 256");
    }
    if (config.performance.queue_capacity > 65536U) {
        throw std::invalid_argument("performance.queue_capacity cannot exceed 65536");
    }
    if (config.performance.max_image_width > 32768U || config.performance.max_image_height > 32768U) {
        throw std::invalid_argument("performance image dimensions exceed safe engine limits");
    }
    if (config.performance.max_image_bytes > 1024ULL * 1024ULL * 1024ULL) {
        throw std::invalid_argument("performance.max_image_bytes cannot exceed 1 GiB");
    }
    if (config.performance.recognition_timeout <= std::chrono::milliseconds::zero() ||
        config.performance.recognition_timeout > std::chrono::minutes{5}) {
        throw std::invalid_argument("performance.recognition_timeout must be in (0, 5min]");
    }
}

} // namespace fac_lpr::application
