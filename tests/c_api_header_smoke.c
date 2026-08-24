#include <fac_lpr/fac_lpr_engine.h>

_Static_assert(sizeof(fac_lpr_text_ref_v1) == 8U, "fac_lpr_text_ref_v1 ABI size changed");
_Static_assert(sizeof(fac_lpr_point_v1) == 8U, "fac_lpr_point_v1 ABI size changed");
_Static_assert(sizeof(fac_lpr_bbox_v1) == 16U, "fac_lpr_bbox_v1 ABI size changed");
_Static_assert(sizeof(fac_lpr_quadrilateral_v1) == 48U, "fac_lpr_quadrilateral_v1 ABI size changed");
_Static_assert(sizeof(fac_lpr_candidate_v1) == 32U, "fac_lpr_candidate_v1 ABI size changed");
_Static_assert(sizeof(fac_lpr_evidence_v1) == 32U, "fac_lpr_evidence_v1 ABI size changed");
_Static_assert(sizeof(fac_lpr_plate_result_v1) == 140U, "fac_lpr_plate_result_v1 ABI size changed");
_Static_assert(sizeof(fac_lpr_result_v1) == 32U, "fac_lpr_result_v1 ABI size changed");

int fac_lpr_c_header_smoke(void) {
    fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
    fac_lpr_image_view_v1 image = FAC_LPR_IMAGE_VIEW_V1_INIT;
    fac_lpr_engine_handle* handle = NULL;

    if (config.abi_version != FAC_LPR_ABI_VERSION_V1) {
        return 1;
    }
    if (config.struct_size != sizeof(fac_lpr_engine_config_v1)) {
        return 2;
    }
    if (image.abi_version != FAC_LPR_ABI_VERSION_V1) {
        return 3;
    }
    if (image.struct_size != sizeof(fac_lpr_image_view_v1)) {
        return 4;
    }
    if (handle != NULL) {
        return 5;
    }
    if (FAC_LPR_RESULT_BUFFER_ALIGNMENT_V1 != 4U) {
        return 6;
    }
    if (FAC_LPR_STATUS_BUFFER_TOO_SMALL != 10) {
        return 7;
    }
    return 0;
}
