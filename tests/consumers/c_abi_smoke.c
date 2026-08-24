#include <fac_lpr/fac_lpr_engine.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(fac_lpr_text_ref_v1) == 8U, "text ref ABI changed");
_Static_assert(sizeof(fac_lpr_point_v1) == 8U, "point ABI changed");
_Static_assert(sizeof(fac_lpr_bbox_v1) == 16U, "bbox ABI changed");
_Static_assert(sizeof(fac_lpr_quadrilateral_v1) == 48U, "quad ABI changed");
_Static_assert(sizeof(fac_lpr_candidate_v1) == 32U, "candidate ABI changed");
_Static_assert(sizeof(fac_lpr_evidence_v1) == 32U, "evidence ABI changed");
_Static_assert(sizeof(fac_lpr_plate_result_v1) == 140U, "plate result ABI changed");
_Static_assert(sizeof(fac_lpr_result_v1) == 32U, "result ABI changed");

static int fail(const char* message, int code) {
    fprintf(stderr, "c_abi_smoke: %s\n", message);
    return code;
}

static int print_last_error(void) {
    size_t required = 0U;
    if (fac_lpr_get_last_error_v1(NULL, 0U, &required) != FAC_LPR_STATUS_BUFFER_TOO_SMALL || required <= 1U) {
        return 0;
    }
    char* buffer = (char*)malloc(required);
    if (buffer == NULL) {
        return 0;
    }
    if (fac_lpr_get_last_error_v1(buffer, required, &required) == FAC_LPR_STATUS_OK) {
        fprintf(stderr, "c_abi_smoke native error: %s\n", buffer);
    }
    free(buffer);
    return 0;
}

static int run_production_smoke(
    const fac_lpr_engine_config_v1* config,
    const char* model_directory,
    const char* contract_path) {
    fac_lpr_engine_handle* handle = NULL;
    const fac_lpr_status create_status = fac_lpr_engine_create_from_contract_v1(
        config, contract_path, model_directory, &handle);
    if (create_status != FAC_LPR_STATUS_OK || handle == NULL) {
        print_last_error();
        return fail("production engine create failed", 20);
    }

    const uint32_t width = 640U;
    const uint32_t height = 480U;
    const uint32_t stride = width * 3U;
    const size_t pixel_count = (size_t)stride * (size_t)height;
    uint8_t* pixels = (uint8_t*)calloc(pixel_count, 1U);
    if (pixels == NULL) {
        fac_lpr_engine_destroy_v1(&handle);
        return fail("production frame allocation failed", 21);
    }

    fac_lpr_image_view_v1 image = FAC_LPR_IMAGE_VIEW_V1_INIT;
    image.data = pixels;
    image.data_size = pixel_count;
    image.width = width;
    image.height = height;
    image.stride_bytes = stride;
    image.pixel_format = FAC_LPR_PIXEL_FORMAT_BGR8;

    size_t required = 0U;
    const fac_lpr_status sizing_status = fac_lpr_engine_recognize_v1(
        handle, &image, NULL, 0U, &required);
    if (sizing_status != FAC_LPR_STATUS_BUFFER_TOO_SMALL || required < sizeof(fac_lpr_result_v1)) {
        print_last_error();
        free(pixels);
        fac_lpr_engine_destroy_v1(&handle);
        return fail("production recognition sizing failed", 22);
    }

    void* result_buffer = malloc(required);
    if (result_buffer == NULL) {
        free(pixels);
        fac_lpr_engine_destroy_v1(&handle);
        return fail("production result allocation failed", 23);
    }

    size_t required_again = required;
    const fac_lpr_status recognition_status = fac_lpr_engine_recognize_v1(
        handle, &image, result_buffer, required, &required_again);
    if (recognition_status != FAC_LPR_STATUS_OK || required_again > required) {
        print_last_error();
        free(result_buffer);
        free(pixels);
        fac_lpr_engine_destroy_v1(&handle);
        return fail("production recognition failed", 24);
    }

    const fac_lpr_result_v1* result = (const fac_lpr_result_v1*)result_buffer;
    if (result->struct_size != sizeof(fac_lpr_result_v1) ||
        result->abi_version != FAC_LPR_ABI_VERSION_V1) {
        free(result_buffer);
        free(pixels);
        fac_lpr_engine_destroy_v1(&handle);
        return fail("production result header invalid", 25);
    }

    free(result_buffer);
    free(pixels);
    if (fac_lpr_engine_destroy_v1(&handle) != FAC_LPR_STATUS_OK || handle != NULL) {
        return fail("production engine destroy failed", 26);
    }

    puts("c_abi_smoke: production inference ok");
    return 0;
}

int main(int argc, char** argv) {
    fac_lpr_version_info_v1 version = FAC_LPR_VERSION_INFO_V1_INIT;
    if (fac_lpr_get_version_v1(&version) != FAC_LPR_STATUS_OK) {
        return fail("version query failed", 1);
    }
    if (version.abi_major != FAC_LPR_ABI_VERSION_V1) {
        return fail("unexpected ABI major", 2);
    }

    fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
    fac_lpr_engine_handle* handle = NULL;
    if (fac_lpr_engine_create_v1(&config, &handle) != FAC_LPR_STATUS_OK || handle == NULL) {
        return fail("engine create failed", 3);
    }

    uint8_t pixel[3] = {0U, 0U, 0U};
    fac_lpr_image_view_v1 image = FAC_LPR_IMAGE_VIEW_V1_INIT;
    image.data = pixel;
    image.data_size = sizeof(pixel);
    image.width = 1U;
    image.height = 1U;
    image.stride_bytes = 3U;
    image.pixel_format = FAC_LPR_PIXEL_FORMAT_BGR8;

    size_t required = 123U;
    const fac_lpr_status recognition_status = fac_lpr_engine_recognize_v1(
        handle, &image, NULL, 0U, &required);
    if (recognition_status != FAC_LPR_STATUS_CONFIGURATION_ERROR) {
        fac_lpr_engine_destroy_v1(&handle);
        return fail("unexpected recognition status for unconfigured pipeline", 4);
    }

    size_t error_size = 0U;
    if (fac_lpr_get_last_error_v1(NULL, 0U, &error_size) != FAC_LPR_STATUS_BUFFER_TOO_SMALL || error_size <= 1U) {
        fac_lpr_engine_destroy_v1(&handle);
        return fail("last-error sizing contract failed", 5);
    }
    if (error_size > 512U) {
        fac_lpr_engine_destroy_v1(&handle);
        return fail("last-error size unreasonable", 6);
    }

    char error[512];
    memset(error, 0, sizeof(error));
    if (fac_lpr_get_last_error_v1(error, sizeof(error), &error_size) != FAC_LPR_STATUS_OK || error[0] == '\0') {
        fac_lpr_engine_destroy_v1(&handle);
        return fail("last-error retrieval failed", 7);
    }

    if (fac_lpr_engine_destroy_v1(&handle) != FAC_LPR_STATUS_OK || handle != NULL) {
        return fail("engine destroy failed", 8);
    }
    if (fac_lpr_engine_destroy_v1(&handle) != FAC_LPR_STATUS_OK) {
        return fail("idempotent destroy failed", 9);
    }

    if (argc == 3) {
        const int production_result = run_production_smoke(&config, argv[1], argv[2]);
        if (production_result != 0) {
            return production_result;
        }
    } else if (argc != 1) {
        return fail("usage: c_abi_smoke [model-directory contract-path]", 10);
    }

    puts("c_abi_smoke: ok");
    return 0;
}
