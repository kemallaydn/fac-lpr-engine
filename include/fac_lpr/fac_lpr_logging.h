#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fac_lpr_log_level {
    FAC_LPR_LOG_TRACE = 0,
    FAC_LPR_LOG_DEBUG = 1,
    FAC_LPR_LOG_INFO = 2,
    FAC_LPR_LOG_WARN = 3,
    FAC_LPR_LOG_ERROR = 4
} fac_lpr_log_level;

typedef void (*fac_lpr_log_callback)(
    fac_lpr_log_level level,
    const char* category,
    const char* message,
    void* user_data);

#ifdef __cplusplus
}
#endif
