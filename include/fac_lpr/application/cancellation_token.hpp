#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace fac_lpr::application {

class CancellationToken final {
public:
    CancellationToken() = default;

    [[nodiscard]] bool stop_requested() const noexcept {
        return state_ != nullptr && state_->load(std::memory_order_acquire);
    }

private:
    explicit CancellationToken(std::shared_ptr<const std::atomic_bool> state) noexcept
        : state_(std::move(state)) {}

    std::shared_ptr<const std::atomic_bool> state_{};

    friend class CancellationSource;
};

class CancellationSource final {
public:
    CancellationSource() : state_(std::make_shared<std::atomic_bool>(false)) {}

    [[nodiscard]] CancellationToken get_token() const noexcept {
        return CancellationToken{state_};
    }

    [[nodiscard]] bool request_stop() noexcept {
        return !state_->exchange(true, std::memory_order_acq_rel);
    }

private:
    std::shared_ptr<std::atomic_bool> state_;
};

} // namespace fac_lpr::application
