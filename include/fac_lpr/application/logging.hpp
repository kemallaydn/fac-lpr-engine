#pragma once

#include <fac_lpr/fac_lpr_logging.h>

#include <mutex>
#include <string_view>

namespace fac_lpr::application {

enum class LogLevel {
    trace,
    debug,
    info,
    warn,
    error
};

struct LogRecord final {
    LogLevel level{LogLevel::info};
    std::string_view category{};
    std::string_view message{};
    std::string_view request_id{};
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(const LogRecord& record) noexcept = 0;
};

class NullLogger final : public ILogger {
public:
    void log(const LogRecord&) noexcept override {}
};

class CallbackLogger final : public ILogger {
public:
    CallbackLogger(fac_lpr_log_callback callback, void* user_data) noexcept
        : callback_(callback), user_data_(user_data) {}

    CallbackLogger(const CallbackLogger&) = delete;
    CallbackLogger& operator=(const CallbackLogger&) = delete;
    CallbackLogger(CallbackLogger&&) = delete;
    CallbackLogger& operator=(CallbackLogger&&) = delete;

    void log(const LogRecord& record) noexcept override {
        if (callback_ == nullptr) {
            return;
        }

        try {
            // Consumer callbacks are serialized so callers do not need to make
            // their logging backend re-entrant merely to consume engine logs.
            const std::scoped_lock lock{callback_mutex_};

            // The C callback receives null-terminated strings. Avoiding a hidden
            // allocation here is only safe when the incoming views are already
            // backed by null-terminated storage. Engine-generated records must
            // therefore use string literals or owned strings whose lifetime spans
            // the callback. For arbitrary views, the logger intentionally falls
            // back to empty text rather than reading past the view boundary.
            const char* category = safe_c_string(record.category);
            const char* message = safe_c_string(record.message);
            callback_(to_c_level(record.level), category, message, user_data_);
        } catch (...) {
            // Logging is strictly best-effort and cannot break recognition.
        }
    }

private:
    [[nodiscard]] static const char* safe_c_string(std::string_view value) noexcept {
        // A string_view does not guarantee a terminator at value.data()[size()].
        // Empty views are always safe; non-empty arbitrary views are not. Keeping
        // this conservative prevents UB at the C ABI logging boundary.
        return value.empty() ? "" : value.data();
    }

    [[nodiscard]] static constexpr fac_lpr_log_level to_c_level(LogLevel level) noexcept {
        switch (level) {
            case LogLevel::trace: return FAC_LPR_LOG_TRACE;
            case LogLevel::debug: return FAC_LPR_LOG_DEBUG;
            case LogLevel::info: return FAC_LPR_LOG_INFO;
            case LogLevel::warn: return FAC_LPR_LOG_WARN;
            case LogLevel::error: return FAC_LPR_LOG_ERROR;
        }
        return FAC_LPR_LOG_ERROR;
    }

    fac_lpr_log_callback callback_{nullptr};
    void* user_data_{nullptr};
    std::mutex callback_mutex_{};
};

} // namespace fac_lpr::application
