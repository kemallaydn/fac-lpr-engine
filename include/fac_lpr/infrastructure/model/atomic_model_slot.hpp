#pragma once

#include <fac_lpr/application/error.hpp>

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <utility>

namespace fac_lpr::infrastructure::model {

// Keeps in-flight users isolated from model reloads. A request acquires a
// shared_ptr snapshot; reload validates a replacement before atomically
// publishing it. The previous instance is released by RAII only after its last
// request snapshot goes away.
template <typename T>
class AtomicModelSlot final {
public:
    using ModelPtr = std::shared_ptr<T>;

    explicit AtomicModelSlot(ModelPtr initial)
        : state_(std::make_shared<State>(std::move(initial))) {}

    AtomicModelSlot(const AtomicModelSlot&) = delete;
    AtomicModelSlot& operator=(const AtomicModelSlot&) = delete;
    AtomicModelSlot(AtomicModelSlot&&) noexcept = default;
    AtomicModelSlot& operator=(AtomicModelSlot&&) noexcept = default;

    [[nodiscard]] ModelPtr acquire() const noexcept {
        return state_->active.load(std::memory_order_acquire);
    }

    template <typename Loader, typename Validator>
    [[nodiscard]] bool reload(Loader&& loader, Validator&& validator) {
        return reload_state(
            state_,
            std::forward<Loader>(loader),
            std::forward<Validator>(validator));
    }

    template <typename Loader, typename Validator>
    [[nodiscard]] std::future<bool> reload_async(Loader loader, Validator validator) {
        auto state = state_;
        return std::async(
            std::launch::async,
            [state = std::move(state), loader = std::move(loader), validator = std::move(validator)]() mutable {
                return reload_state(state, std::move(loader), std::move(validator));
            });
    }

private:
    struct State final {
        explicit State(ModelPtr initial)
            : active(std::move(initial)) {
            if (!active.load()) {
                throw application::ConfigurationError("initial model/session cannot be null");
            }
        }

        std::atomic<ModelPtr> active;
        std::mutex reload_mutex{};
    };

    template <typename Loader, typename Validator>
    [[nodiscard]] static bool reload_state(
        const std::shared_ptr<State>& state,
        Loader&& loader,
        Validator&& validator) {
        std::scoped_lock lock(state->reload_mutex);

        ModelPtr candidate{};
        try {
            candidate = std::forward<Loader>(loader)();
            if (!candidate) {
                return false;
            }
            if (!std::forward<Validator>(validator)(candidate)) {
                return false;
            }
        } catch (...) {
            // Reload failure is deliberately fail-safe: the currently active
            // model stays published. Callers may log/report the false result.
            return false;
        }

        state->active.store(std::move(candidate), std::memory_order_release);
        return true;
    }

    std::shared_ptr<State> state_;
};

} // namespace fac_lpr::infrastructure::model
