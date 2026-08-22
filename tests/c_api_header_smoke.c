#include <fac_lpr/fac_lpr_engine.h>

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
    return 0;
}
