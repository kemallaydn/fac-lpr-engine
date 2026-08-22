#include <fac_lpr/infrastructure/concurrency/bounded_worker_pool.hpp>

#include <fac_lpr/application/error.hpp>

#include <utility>

namespace fac_lpr::infrastructure::concurrency {

BoundedWorkerPool::BoundedWorkerPool(BoundedWorkerPoolConfig config)
    : config_(config) {
    if (config_.worker_count == 0U || config_.worker_count > 256U) {
        throw application::ConfigurationError("worker_count must be in [1,256]");
    }
    if (config_.queue_capacity == 0U || config_.queue_capacity > 65536U) {
        throw application::ConfigurationError("queue_capacity must be in [1,65536]");
    }

    workers_.reserve(config_.worker_count);
    try {
        for (std::size_t index = 0U; index < config_.worker_count; ++index) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        {
            std::scoped_lock lock{mutex_};
            accepting_ = false;
            stopping_ = true;
        }
        work_available_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        throw application::ResourceExhaustedError("cannot start bounded worker pool threads");
    }
}

BoundedWorkerPool::~BoundedWorkerPool() {
    shutdown();
}

bool BoundedWorkerPool::submit(Task task) {
    if (!task) {
        throw application::ConfigurationError("worker pool task cannot be empty");
    }

    std::unique_lock lock{mutex_};
    if (!accepting_) {
        return false;
    }

    if (config_.full_policy == QueueFullPolicy::block) {
        queue_space_available_.wait(lock, [this] {
            return !accepting_ || queue_.size() < config_.queue_capacity;
        });
        if (!accepting_) {
            return false;
        }
    } else if (queue_.size() >= config_.queue_capacity) {
        ++dropped_;
        return false;
    }

    queue_.push_back(std::move(task));
    ++submitted_;
    lock.unlock();
    work_available_.notify_one();
    return true;
}

void BoundedWorkerPool::shutdown() {
    {
        std::scoped_lock lock{mutex_};
        if (stopping_) {
            return;
        }
        accepting_ = false;
        stopping_ = true;
        if (config_.shutdown_policy == ShutdownPolicy::discard_pending) {
            dropped_ += queue_.size();
            queue_.clear();
        }
    }

    queue_space_available_.notify_all();
    work_available_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

BoundedWorkerPoolStats BoundedWorkerPool::stats() const {
    std::scoped_lock lock{mutex_};
    return BoundedWorkerPoolStats{
        .submitted = submitted_,
        .completed = completed_,
        .failed = failed_,
        .dropped = dropped_,
        .pending = queue_.size(),
        .active = active_};
}

void BoundedWorkerPool::worker_loop() {
    native_image::NativeImageWorkspace workspace{config_.workspace};

    for (;;) {
        Task task{};
        {
            std::unique_lock lock{mutex_};
            work_available_.wait(lock, [this] {
                return stopping_ || !queue_.empty();
            });

            if (stopping_ && queue_.empty()) {
                return;
            }

            task = std::move(queue_.front());
            queue_.pop_front();
            ++active_;
        }
        queue_space_available_.notify_one();

        bool succeeded = true;
        try {
            task(workspace);
        } catch (...) {
            succeeded = false;
        }

        {
            std::scoped_lock lock{mutex_};
            --active_;
            if (succeeded) {
                ++completed_;
            } else {
                ++failed_;
            }
        }
    }
}

} // namespace fac_lpr::infrastructure::concurrency
