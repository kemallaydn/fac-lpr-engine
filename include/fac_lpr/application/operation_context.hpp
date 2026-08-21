#pragma once

#include <chrono>
#include <optional>
#include <stop_token>

namespace fac_lpr::application {

struct OperationContext final {
    std::stop_token stop_token{};
    std::optional<std::chrono::steady_clock::time_point> deadline{};

    [[nodiscard]] bool cancellation_requested() const noexcept {
        return stop_token.stop_requested();
    }

    [[nodiscard]] bool deadline_exceeded() const noexcept {
        return deadline.has_value() && std::chrono::steady_clock::now() >= *deadline;
    }
};

} // namespace fac_lpr::application
