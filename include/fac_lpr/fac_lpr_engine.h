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

/*
 * C ABI v1 lifecycle contract.
 *
 * - Every v1 struct starts with struct_size + abi_version.
 * - Callers should initialize structs with the *_V1_INIT macros.
 * - The handle is opaque; callers must never inspect or free it directly.
 * - destroy_v1 takes a pointer-to-handle, clears it to NULL, and is safe to
 *   call repeatedly. NULL handle pointers are accepted.
 * - No C++ exception is allowed to cross these functions.
 * - recognize_v1 reserves a caller-owned byte output surface. The concrete v1
 *   result-buffer layout and buffer-too-small semantics are defined by roadmap
 *   issue #36 without changing this function's symbol/version.
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

#ifdef __cplusplus
}
#endif

#endif
