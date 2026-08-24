#pragma once

#include <fac_lpr/fac_lpr_logging.h>

#include <mutex>
#include <string>
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

            // C callbacks require null-terminated text, while string_view does
            // not guarantee a terminator at data()[size()]. Materialize bounded
            // copies at this ABI adapter boundary to avoid out-of-bounds reads.
            const std::string category{record.category};
            const std::string message{record.message};
            callback_(to_c_level(record.level), category.c_str(), message.c_str(), user_data_);
        } catch (...) {
            // Logging is strictly best-effort and cannot break recognition.
        }
    }

private:
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
