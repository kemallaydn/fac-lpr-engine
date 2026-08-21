#include <fac_lpr/infrastructure/spdlog_logger.hpp>

#include <spdlog/logger.h>

#include <utility>

namespace fac_lpr::infrastructure {

SpdlogLogger::SpdlogLogger(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

void SpdlogLogger::log(const application::LogRecord& record) noexcept {
    if (!logger_) {
        return;
    }

    try {
        switch (record.level) {
            case application::LogLevel::trace:
                logger_->trace("[{}] {}", record.category, record.message);
                break;
            case application::LogLevel::debug:
                logger_->debug("[{}] {}", record.category, record.message);
                break;
            case application::LogLevel::info:
                logger_->info("[{}] {}", record.category, record.message);
                break;
            case application::LogLevel::warn:
                logger_->warn("[{}] {}", record.category, record.message);
                break;
            case application::LogLevel::error:
                logger_->error("[{}] {}", record.category, record.message);
                break;
        }
    } catch (...) {
        // Logging must never change recognition behavior.
    }
}

} // namespace fac_lpr::infrastructure
