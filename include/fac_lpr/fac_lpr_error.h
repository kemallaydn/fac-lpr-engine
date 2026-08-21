#ifndef FAC_LPR_ERROR_H
#define FAC_LPR_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fac_lpr_status {
    FAC_LPR_STATUS_OK = 0,
    FAC_LPR_STATUS_CONFIGURATION_ERROR = 1,
    FAC_LPR_STATUS_MODEL_LOAD_ERROR = 2,
    FAC_LPR_STATUS_INFERENCE_ERROR = 3,
    FAC_LPR_STATUS_INVALID_IMAGE = 4,
    FAC_LPR_STATUS_PROVIDER_ERROR = 5,
    FAC_LPR_STATUS_CANCELLED = 6,
    FAC_LPR_STATUS_TIMEOUT = 7,
    FAC_LPR_STATUS_RESOURCE_EXHAUSTED = 8,
    FAC_LPR_STATUS_INTERNAL_ERROR = 9
} fac_lpr_status;

#ifdef __cplusplus
}
#endif

#endif
