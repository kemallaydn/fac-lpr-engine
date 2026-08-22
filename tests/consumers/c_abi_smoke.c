#include <fac_lpr/fac_lpr_engine.h>

#include <stdint.h>
#include <stdio.h>
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

int main(void) {
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

    puts("c_abi_smoke: ok");
    return 0;
}
