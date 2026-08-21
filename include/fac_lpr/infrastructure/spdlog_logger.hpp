#pragma once

#include <fac_lpr/application/logging.hpp>

#include <memory>

namespace spdlog {
class logger;
}

namespace fac_lpr::infrastructure {

class SpdlogLogger final : public application::ILogger {
public:
    explicit SpdlogLogger(std::shared_ptr<spdlog::logger> logger);
    void log(const application::LogRecord& record) noexcept override;

private:
    std::shared_ptr<spdlog::logger> logger_;
};

} // namespace fac_lpr::infrastructure
