#ifndef FAC_LPR_ENGINE_H
#define FAC_LPR_ENGINE_H

#include <fac_lpr/fac_lpr_error.h>

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
    #if defined(FAC_LPR_SHARED)
        #if defined(FAC_LPR_EXPORTS)
            #define FAC_LPR_API __declspec(dllexport)
        #else
            #define FAC_LPR_API __declspec(dllimport)
        #endif
    #else
        #define FAC_LPR_API
    #endif
    #define FAC_LPR_CALL __cdecl
#else
    #if defined(__GNUC__) || defined(__clang__)
        #define FAC_LPR_API __attribute__((visibility("default")))
    #else
        #define FAC_LPR_API
    #endif
    #define FAC_LPR_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FAC_LPR_ABI_VERSION_V1 UINT32_C(1)
#define FAC_LPR_ENGINE_CONFIG_V1_INIT \
    { (uint32_t)sizeof(fac_lpr_engine_config_v1), FAC_LPR_ABI_VERSION_V1, 0U, 0U }
#define FAC_LPR_IMAGE_VIEW_V1_INIT \
    { (uint32_t)sizeof(fac_lpr_image_view_v1), FAC_LPR_ABI_VERSION_V1, NULL, 0U, 0U, 0U, 0U, FAC_LPR_PIXEL_FORMAT_BGR8 }

typedef struct fac_lpr_engine_handle fac_lpr_engine_handle;

typedef enum fac_lpr_pixel_format {
    FAC_LPR_PIXEL_FORMAT_GRAY8 = 0,
    FAC_LPR_PIXEL_FORMAT_BGR8 = 1,
    FAC_LPR_PIXEL_FORMAT_RGB8 = 2
} fac_lpr_pixel_format;

typedef uint32_t fac_lpr_recognition_status_v1;
#define FAC_LPR_RECOGNITION_ACCEPTED_V1 UINT32_C(0)
#define FAC_LPR_RECOGNITION_REVIEW_V1 UINT32_C(1)
#define FAC_LPR_RECOGNITION_REJECTED_V1 UINT32_C(2)

typedef uint32_t fac_lpr_decision_reason_v1;
#define FAC_LPR_REASON_ACCEPTED_CONSENSUS_V1 UINT32_C(0)
#define FAC_LPR_REASON_FATAL_PROVIDER_FAILURE_V1 UINT32_C(1)
#define FAC_LPR_REASON_DEGRADED_PROVIDER_SET_V1 UINT32_C(2)
#define FAC_LPR_REASON_DETECTOR_CONFIDENCE_BELOW_MINIMUM_V1 UINT32_C(3)
#define FAC_LPR_REASON_GEOMETRY_BELOW_MINIMUM_V1 UINT32_C(4)
#define FAC_LPR_REASON_CROP_QUALITY_BELOW_MINIMUM_V1 UINT32_C(5)
#define FAC_LPR_REASON_NO_VALID_CANDIDATE_V1 UINT32_C(6)
#define FAC_LPR_REASON_CANDIDATE_CONFIDENCE_BELOW_REVIEW_V1 UINT32_C(7)
#define FAC_LPR_REASON_CANDIDATE_CONFIDENCE_BELOW_ACCEPT_V1 UINT32_C(8)
#define FAC_LPR_REASON_CONFLICTING_STRONG_CANDIDATES_V1 UINT32_C(9)

typedef struct fac_lpr_engine_config_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_flags;
    uint32_t reserved_zero;
} fac_lpr_engine_config_v1;

typedef struct fac_lpr_image_view_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const uint8_t* data;
    size_t data_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    fac_lpr_pixel_format pixel_format;
} fac_lpr_image_view_v1;

typedef struct fac_lpr_text_ref_v1 {
    uint32_t offset;
    uint32_t length;
} fac_lpr_text_ref_v1;

typedef struct fac_lpr_point_v1 {
    float x;
    float y;
} fac_lpr_point_v1;

typedef struct fac_lpr_bbox_v1 {
    float x;
    float y;
    float width;
    float height;
} fac_lpr_bbox_v1;

typedef struct fac_lpr_quadrilateral_v1 {
    fac_lpr_point_v1 points[4];
    float confidences[4];
} fac_lpr_quadrilateral_v1;

typedef struct fac_lpr_candidate_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    fac_lpr_text_ref_v1 text;
    float confidence;
    float calibrated_confidence;
    uint32_t format_valid;
    uint32_t reserved_zero;
} fac_lpr_candidate_v1;

typedef struct fac_lpr_evidence_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    fac_lpr_text_ref_v1 source;
    float crop_quality;
    float latency_ms;
    uint32_t candidate_count;
    uint32_t candidates_offset;
} fac_lpr_evidence_v1;

typedef struct fac_lpr_plate_result_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    fac_lpr_recognition_status_v1 status;
    uint32_t degraded;
    fac_lpr_text_ref_v1 plate;
    float confidence;
    float detector_confidence;
    float geometry_score;
    float crop_quality;
    float total_latency_ms;
    uint32_t has_bbox;
    fac_lpr_bbox_v1 bbox;
    uint32_t has_quadrilateral;
    fac_lpr_quadrilateral_v1 quadrilateral;
    uint32_t evidence_count;
    uint32_t evidence_offset;
    uint32_t alternatives_count;
    uint32_t alternatives_offset;
    uint32_t decision_reason_count;
    uint32_t decision_reasons_offset;
} fac_lpr_plate_result_v1;

typedef struct fac_lpr_result_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t recognition_count;
    uint32_t recognitions_offset;
    float total_latency_ms;
    uint32_t degraded;
    uint32_t provider_failure_count;
    uint32_t reserved_zero;
} fac_lpr_result_v1;

/*
 * All offsets in fac_lpr_result_v1 and nested records are byte offsets from
 * the beginning of the caller-owned output_buffer passed to recognize_v1.
 * Text references are raw UTF-8/ASCII byte slices and are not NUL-terminated.
 * No pointer stored inside a result buffer is engine-owned.
 */

FAC_LPR_API fac_lpr_status FAC_LPR_CALL fac_lpr_engine_create_v1(
    const fac_lpr_engine_config_v1* config,
    fac_lpr_engine_handle** out_handle);

FAC_LPR_API fac_lpr_status FAC_LPR_CALL fac_lpr_engine_recognize_v1(
    fac_lpr_engine_handle* handle,
    const fac_lpr_image_view_v1* image,
    void* output_buffer,
    size_t output_capacity,
    size_t* required_output_size);

FAC_LPR_API fac_lpr_status FAC_LPR_CALL fac_lpr_engine_destroy_v1(
    fac_lpr_engine_handle** handle);

FAC_LPR_API fac_lpr_status FAC_LPR_CALL fac_lpr_get_last_error_v1(
    char* buffer,
    size_t buffer_capacity,
    size_t* required_size);

#ifdef __cplusplus
}
#endif

#endif
