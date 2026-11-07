#pragma once

#include <fac_lpr/application/cancellation_token.hpp>

#include <chrono>
#include <optional>
#include <string_view>

namespace fac_lpr::application {

class IStageTimingSink {
public:
    virtual ~IStageTimingSink() = default;

    virtual void record(std::string_view stage, double latency_ms) = 0;
};

struct OperationContext final {
    CancellationToken stop_token{};
    std::optional<std::chrono::steady_clock::time_point> deadline{};
    IStageTimingSink* timing_sink{nullptr};

    [[nodiscard]] bool cancellation_requested() const noexcept {
        return stop_token.stop_requested();
    }

    [[nodiscard]] bool deadline_exceeded() const noexcept {
        return deadline.has_value() && std::chrono::steady_clock::now() >= *deadline;
    }

    void record_timing(const std::string_view stage, const double latency_ms) const {
        if (timing_sink != nullptr) {
            timing_sink->record(stage, latency_ms);
        }
    }
};

} // namespace fac_lpr::application
