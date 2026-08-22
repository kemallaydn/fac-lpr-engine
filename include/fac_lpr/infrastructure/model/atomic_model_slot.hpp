#pragma once

#include <fac_lpr/application/error.hpp>

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <type_traits>
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
        : active_(std::move(initial)) {
        if (!active_.load()) {
            throw application::ConfigurationError("initial model/session cannot be null");
        }
    }

    AtomicModelSlot(const AtomicModelSlot&) = delete;
    AtomicModelSlot& operator=(const AtomicModelSlot&) = delete;

    [[nodiscard]] ModelPtr acquire() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

    template <typename Loader, typename Validator>
    [[nodiscard]] bool reload(Loader&& loader, Validator&& validator) {
        std::scoped_lock lock(reload_mutex_);

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

        active_.store(std::move(candidate), std::memory_order_release);
        return true;
    }

    template <typename Loader, typename Validator>
    [[nodiscard]] std::future<bool> reload_async(Loader loader, Validator validator) {
        return std::async(
            std::launch::async,
            [this, loader = std::move(loader), validator = std::move(validator)]() mutable {
                return reload(std::move(loader), std::move(validator));
            });
    }

private:
    std::atomic<ModelPtr> active_;
    mutable std::mutex reload_mutex_{};
};

} // namespace fac_lpr::infrastructure::model
