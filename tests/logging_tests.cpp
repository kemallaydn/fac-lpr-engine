#include <fac_lpr/application/logging.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

struct CallbackState final {
    std::atomic<int> active{0};
    std::atomic<int> maximum_active{0};
    std::atomic<int> calls{0};
};

void serialized_callback(
    fac_lpr_log_level,
    const char*,
    const char*,
    void* user_data) {
    auto& state = *static_cast<CallbackState*>(user_data);
    const auto active = state.active.fetch_add(1, std::memory_order_acq_rel) + 1;
    auto observed = state.maximum_active.load(std::memory_order_relaxed);
    while (observed < active &&
           !state.maximum_active.compare_exchange_weak(
               observed,
               active,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
    std::this_thread::yield();
    state.calls.fetch_add(1, std::memory_order_relaxed);
    state.active.fetch_sub(1, std::memory_order_acq_rel);
}

void throwing_callback(
    fac_lpr_log_level,
    const char*,
    const char*,
    void*) {
    throw std::runtime_error{"consumer logger failure"};
}

TEST(CallbackLogger, SerializesConcurrentConsumerCallbacks) {
    CallbackState state{};
    fac_lpr::application::CallbackLogger logger{&serialized_callback, &state};

    constexpr std::size_t thread_count = 8U;
    constexpr std::size_t logs_per_thread = 64U;
    std::vector<std::thread> threads{};
    threads.reserve(thread_count);

    for (std::size_t index = 0; index < thread_count; ++index) {
        threads.emplace_back([&logger] {
            for (std::size_t log_index = 0; log_index < logs_per_thread; ++log_index) {
                logger.log({
                    .level = fac_lpr::application::LogLevel::debug,
                    .category = "test",
                    .message = "concurrent log",
                    .request_id = {},
                });
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(state.calls.load(), static_cast<int>(thread_count * logs_per_thread));
    EXPECT_EQ(state.maximum_active.load(), 1);
}

TEST(CallbackLogger, ConsumerExceptionNeverEscapesLoggingBoundary) {
    fac_lpr::application::CallbackLogger logger{&throwing_callback, nullptr};
    EXPECT_NO_THROW(logger.log({
        .level = fac_lpr::application::LogLevel::error,
        .category = "test",
        .message = "failure",
        .request_id = {},
    }));
}

} // namespace
