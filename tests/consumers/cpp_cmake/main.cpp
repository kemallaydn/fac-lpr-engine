#include <fac_lpr/fac_lpr_engine.h>

#include <cstdlib>

int main() {
    fac_lpr_version_info_v1 version = FAC_LPR_VERSION_INFO_V1_INIT;
    if (fac_lpr_get_version_v1(&version) != FAC_LPR_STATUS_OK) {
        return EXIT_FAILURE;
    }
    if (version.abi_major != FAC_LPR_ABI_VERSION_V1) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
