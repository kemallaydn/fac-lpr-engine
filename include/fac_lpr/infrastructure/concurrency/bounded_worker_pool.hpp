#pragma once

#include <fac_lpr/infrastructure/native_image/native_image.hpp>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace fac_lpr::infrastructure::concurrency {

enum class QueueFullPolicy {
    reject_newest,
    block
};

enum class ShutdownPolicy {
    drain,
    discard_pending
};

struct BoundedWorkerPoolConfig final {
    std::size_t worker_count{1U};
    std::size_t queue_capacity{8U};
    QueueFullPolicy full_policy{QueueFullPolicy::reject_newest};
    ShutdownPolicy shutdown_policy{ShutdownPolicy::drain};
    native_image::NativeImageWorkspaceConfig workspace{};
};

struct BoundedWorkerPoolStats final {
    std::size_t submitted{0U};
    std::size_t completed{0U};
    std::size_t failed{0U};
    std::size_t dropped{0U};
    std::size_t pending{0U};
    std::size_t active{0U};
};

class BoundedWorkerPool final {
public:
    using Task = std::function<void(native_image::NativeImageWorkspace&)>;

    explicit BoundedWorkerPool(BoundedWorkerPoolConfig config = {});
    BoundedWorkerPool(const BoundedWorkerPool&) = delete;
    BoundedWorkerPool& operator=(const BoundedWorkerPool&) = delete;
    BoundedWorkerPool(BoundedWorkerPool&&) = delete;
    BoundedWorkerPool& operator=(BoundedWorkerPool&&) = delete;
    ~BoundedWorkerPool();

    [[nodiscard]] bool submit(Task task);
    void shutdown();

    [[nodiscard]] BoundedWorkerPoolStats stats() const;
    [[nodiscard]] std::size_t worker_count() const noexcept { return config_.worker_count; }
    [[nodiscard]] std::size_t queue_capacity() const noexcept { return config_.queue_capacity; }

private:
    void worker_loop();

    BoundedWorkerPoolConfig config_{};
    mutable std::mutex mutex_{};
    std::condition_variable work_available_{};
    std::condition_variable queue_space_available_{};
    std::deque<Task> queue_{};
    std::vector<std::thread> workers_{};
    bool accepting_{true};
    bool stopping_{false};
    std::size_t submitted_{0U};
    std::size_t completed_{0U};
    std::size_t failed_{0U};
    std::size_t dropped_{0U};
    std::size_t active_{0U};
};

} // namespace fac_lpr::infrastructure::concurrency
