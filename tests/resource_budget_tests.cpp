#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/resource_budget.hpp>
#include <fac_lpr/infrastructure/concurrency/bounded_worker_pool.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>

namespace {
using namespace fac_lpr;

TEST(ResourceBudget, RejectsCountAboveLimit) {
    EXPECT_THROW(
        application::require_resource_count(17U, 16U, "test count"),
        application::ResourceExhaustedError);
}

TEST(ResourceBudget, RejectsMultiplicationOverflowBeforeAllocation) {
    EXPECT_THROW(
        application::checked_resource_multiply(
            std::numeric_limits<std::size_t>::max(), 2U,
            application::default_resource_budget.max_queue_memory_bytes,
            "test allocation"),
        application::ResourceExhaustedError);
}

TEST(ResourceBudget, RejectsAllocationAboveConfiguredMaximum) {
    EXPECT_THROW(
        application::checked_resource_multiply(1024U, 1024U, 1024U, "test allocation"),
        application::ResourceExhaustedError);
}

TEST(ResourceBudget, ValidatesSafeDefaultBudget) {
    EXPECT_NO_THROW(application::validate_resource_budget(application::default_resource_budget));
}

TEST(ResourceBudget, RejectsWorkerQueueEnvelopeAboveBudget) {
    infrastructure::concurrency::BoundedWorkerPoolConfig config{};
    config.worker_count = 1U;
    config.queue_capacity = 8U;
    config.estimated_task_bytes = 1024U;
    config.max_queue_memory_bytes = 4096U;
    EXPECT_THROW(
        (infrastructure::concurrency::BoundedWorkerPool{config}),
        application::ResourceExhaustedError);
}

} // namespace
