#ifndef FAC_LPR_LOGGING_H
#define FAC_LPR_LOGGING_H

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

/*
 * category and message are valid only for the duration of the callback.
 * FAC LPR Engine serializes callback invocations per CallbackLogger instance.
 * Raw image bytes and recognized plate text are not part of the default
 * logging contract; consumers should treat all log output as operational data.
 */
typedef void (*fac_lpr_log_callback)(
    fac_lpr_log_level level,
    const char* category,
    const char* message,
    void* user_data);

#ifdef __cplusplus
}
#endif

#endif
