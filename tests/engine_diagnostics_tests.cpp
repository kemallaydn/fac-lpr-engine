#include <fac_lpr/application/engine_diagnostics.hpp>

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

namespace {
using namespace fac_lpr;

TEST(EngineDiagnostics, RecordsDecisionCountersAndProviderFailuresThreadSafely) {
    application::EngineDiagnostics diagnostics;

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&diagnostics]() {
            for (int n = 0; n < 1000; ++n) {
                diagnostics.record_recognition(domain::RecognitionStatus::accepted);
                diagnostics.record_recognition(domain::RecognitionStatus::review);
                diagnostics.record_recognition(domain::RecognitionStatus::rejected);
                diagnostics.record_provider_failures(1U);
            }
        });
    }
    for (auto& thread : threads) thread.join();

    const auto snapshot = diagnostics.snapshot();
    EXPECT_EQ(snapshot.accepted, 4000U);
    EXPECT_EQ(snapshot.review, 4000U);
    EXPECT_EQ(snapshot.rejected, 4000U);
    EXPECT_EQ(snapshot.provider_failures, 4000U);
}

TEST(EngineDiagnostics, SummarizesStageLatencyWithoutRetainingInputData) {
    application::EngineDiagnostics diagnostics;
    diagnostics.record_stage_latency("detection", 2.0);
    diagnostics.record_stage_latency("detection", 4.0);
    diagnostics.record_stage_latency("recognition", 8.0);

    const auto snapshot = diagnostics.snapshot();
    ASSERT_EQ(snapshot.stage_latencies.size(), 2U);
    EXPECT_EQ(snapshot.stage_latencies[0].stage, "detection");
    EXPECT_EQ(snapshot.stage_latencies[0].count, 2U);
    EXPECT_DOUBLE_EQ(snapshot.stage_latencies[0].mean_ms, 3.0);
    EXPECT_DOUBLE_EQ(snapshot.stage_latencies[0].min_ms, 2.0);
    EXPECT_DOUBLE_EQ(snapshot.stage_latencies[0].max_ms, 4.0);
}

TEST(EngineDiagnostics, StoresOnlyBoundedOperationalMetadata) {
    application::EngineDiagnostics diagnostics;
    diagnostics.set_providers({
        {.name = "yolo_pose_onnx", .version = "1", .role = "detector"},
        {.name = "lprnet", .version = "V2 Mixed Epoch 7", .role = "recognizer"}});
    diagnostics.set_models({
        {.name = "lprnet_turkey.onnx", .version = "V2 Mixed Epoch 7", .sha256 = "abc", .integrity_verified = true}});
    diagnostics.set_worker_queue({
        .workers = 4U,
        .active = 2U,
        .pending = 3U,
        .capacity = 32U,
        .dropped = 1U});

    diagnostics.set_last_error_summary(std::string(512U, 'x'));
    const auto snapshot = diagnostics.snapshot();

    ASSERT_EQ(snapshot.providers.size(), 2U);
    ASSERT_EQ(snapshot.models.size(), 1U);
    EXPECT_TRUE(snapshot.models.front().integrity_verified);
    EXPECT_EQ(snapshot.worker_queue.workers, 4U);
    EXPECT_EQ(snapshot.worker_queue.capacity, 32U);
    EXPECT_EQ(snapshot.last_error_summary.size(), 256U);
}

TEST(EngineDiagnostics, IgnoresInvalidLatencySamples) {
    application::EngineDiagnostics diagnostics;
    diagnostics.record_stage_latency("", 1.0);
    diagnostics.record_stage_latency("detection", -1.0);
    const auto snapshot = diagnostics.snapshot();
    EXPECT_TRUE(snapshot.stage_latencies.empty());
}

} // namespace
