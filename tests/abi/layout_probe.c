#include <fac_lpr/fac_lpr_engine.h>

#include <stdalign.h>
#include <stdio.h>

#define ENTRY(type, comma) \
    printf("  \"%s\": {\"size\": %zu, \"align\": %zu}%s\n", #type, sizeof(type), alignof(type), comma)

int main(void) {
    puts("{");
    ENTRY(fac_lpr_version_info_v1, ",");
    ENTRY(fac_lpr_engine_config_v1, ",");
    ENTRY(fac_lpr_image_view_v1, ",");
    ENTRY(fac_lpr_text_ref_v1, ",");
    ENTRY(fac_lpr_point_v1, ",");
    ENTRY(fac_lpr_bbox_v1, ",");
    ENTRY(fac_lpr_quadrilateral_v1, ",");
    ENTRY(fac_lpr_candidate_v1, ",");
    ENTRY(fac_lpr_evidence_v1, ",");
    ENTRY(fac_lpr_plate_result_v1, ",");
    ENTRY(fac_lpr_result_v1, "");
    puts("}");
    return 0;
}
