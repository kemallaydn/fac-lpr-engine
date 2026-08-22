#include <fac_lpr/infrastructure/concurrency/bounded_worker_pool.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
using fac_lpr::infrastructure::concurrency::BoundedWorkerPool;
using fac_lpr::infrastructure::concurrency::BoundedWorkerPoolConfig;
using fac_lpr::infrastructure::concurrency::QueueFullPolicy;
using fac_lpr::infrastructure::concurrency::ShutdownPolicy;

TEST(BoundedWorkerPool, RejectNewestKeepsQueueBounded) {
    BoundedWorkerPoolConfig config{};
    config.worker_count = 1U;
    config.queue_capacity = 1U;
    config.full_policy = QueueFullPolicy::reject_newest;
    const BoundedWorkerPool pool{config};

    std::promise<void> started{};
    auto started_future = started.get_future();
    std::promise<void> release{};
    auto release_future = release.get_future().share();

    ASSERT_TRUE(pool.submit([&](auto&) {
        started.set_value();
        release_future.wait();
    }));
    ASSERT_EQ(started_future.wait_for(std::chrono::seconds{2}), std::future_status::ready);

    ASSERT_TRUE(pool.submit([](auto&) {}));
    EXPECT_FALSE(pool.submit([](auto&) {}));

    release.set_value();
    pool.shutdown();

    const auto stats = pool.stats();
    EXPECT_EQ(stats.submitted, 2U);
    EXPECT_EQ(stats.completed, 2U);
    EXPECT_EQ(stats.dropped, 1U);
    EXPECT_LE(stats.peak_pending, 1U);
    EXPECT_EQ(stats.pending, 0U);
    EXPECT_EQ(stats.active, 0U);
}

TEST(BoundedWorkerPool, DiscardShutdownDropsPendingButLetsActiveTaskFinish) {
    BoundedWorkerPoolConfig config{};
    config.worker_count = 1U;
    config.queue_capacity = 4U;
    config.shutdown_policy = ShutdownPolicy::discard_pending;
    BoundedWorkerPool pool{config};

    std::promise<void> started{};
    auto started_future = started.get_future();
    std::promise<void> release{};
    auto release_future = release.get_future().share();

    ASSERT_TRUE(pool.submit([&](auto&) {
        started.set_value();
        release_future.wait();
    }));
    ASSERT_EQ(started_future.wait_for(std::chrono::seconds{2}), std::future_status::ready);
    ASSERT_TRUE(pool.submit([](auto&) {}));
    ASSERT_TRUE(pool.submit([](auto&) {}));

    std::thread shutdown_thread{[&] { pool.shutdown(); }};
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    release.set_value();
    shutdown_thread.join();

    const auto stats = pool.stats();
    EXPECT_EQ(stats.submitted, 3U);
    EXPECT_EQ(stats.completed, 1U);
    EXPECT_EQ(stats.dropped, 2U);
    EXPECT_EQ(stats.pending, 0U);
    EXPECT_EQ(stats.active, 0U);
}

TEST(BoundedWorkerPool, StressUsesBoundedQueueAndPerWorkerWorkspaces) {
    BoundedWorkerPoolConfig config{};
    config.worker_count = 4U;
    config.queue_capacity = 32U;
    config.full_policy = QueueFullPolicy::block;
    BoundedWorkerPool pool{config};

    constexpr std::size_t task_count = 2000U;
    std::atomic<std::size_t> executed{0U};
    std::mutex addresses_mutex{};
    std::set<const void*> workspace_addresses{};

    for (std::size_t index = 0U; index < task_count; ++index) {
        ASSERT_TRUE(pool.submit([&](auto& workspace) {
            (void)workspace.prepare_tensor(1024U);
            {
                std::scoped_lock lock{addresses_mutex};
                workspace_addresses.insert(static_cast<const void*>(&workspace));
            }
            executed.fetch_add(1U, std::memory_order_relaxed);
        }));
    }

    pool.shutdown();
    const auto stats = pool.stats();
    EXPECT_EQ(executed.load(std::memory_order_relaxed), task_count);
    EXPECT_EQ(stats.submitted, task_count);
    EXPECT_EQ(stats.completed, task_count);
    EXPECT_EQ(stats.failed, 0U);
    EXPECT_EQ(stats.dropped, 0U);
    EXPECT_EQ(stats.pending, 0U);
    EXPECT_EQ(stats.active, 0U);
    EXPECT_LE(stats.peak_pending, config.queue_capacity);
    EXPECT_GT(workspace_addresses.size(), 0U);
    EXPECT_LE(workspace_addresses.size(), config.worker_count);
}

TEST(BoundedWorkerPool, TaskFailureDoesNotKillWorkerThread) {
    BoundedWorkerPoolConfig config{};
    config.worker_count = 1U;
    config.queue_capacity = 4U;
    config.full_policy = QueueFullPolicy::block;
    BoundedWorkerPool pool{config};

    std::atomic<int> completed_after_failure{0};
    ASSERT_TRUE(pool.submit([](auto&) {
        throw std::runtime_error("synthetic task failure");
    }));
    ASSERT_TRUE(pool.submit([&](auto&) {
        completed_after_failure.fetch_add(1, std::memory_order_relaxed);
    }));

    pool.shutdown();
    const auto stats = pool.stats();
    EXPECT_EQ(stats.failed, 1U);
    EXPECT_EQ(stats.completed, 1U);
    EXPECT_EQ(completed_after_failure.load(std::memory_order_relaxed), 1);
}

TEST(BoundedWorkerPool, InvalidConfigurationFailsBeforeThreadsStart) {
    BoundedWorkerPoolConfig config{};
    config.worker_count = 0U;
    EXPECT_THROW(
        BoundedWorkerPool{config},
        fac_lpr::application::ConfigurationError);

    config.worker_count = 1U;
    config.workspace.max_tensor_elements = 0U;
    EXPECT_THROW(
        BoundedWorkerPool{config},
        fac_lpr::application::ConfigurationError);
}

} // namespace
