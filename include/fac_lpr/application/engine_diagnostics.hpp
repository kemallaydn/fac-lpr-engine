#pragma once

#include <fac_lpr/domain/recognition.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fac_lpr::application {

struct DiagnosticProviderInfo final {
    std::string name{};
    std::string version{};
    std::string role{};
};

struct DiagnosticModelInfo final {
    std::string name{};
    std::string version{};
    std::string sha256{};
    bool integrity_verified{false};
};

struct StageLatencySummary final {
    std::string stage{};
    std::uint64_t count{0U};
    double mean_ms{0.0};
    double min_ms{0.0};
    double max_ms{0.0};
};

struct WorkerQueueSnapshot final {
    std::size_t workers{0U};
    std::size_t active{0U};
    std::size_t pending{0U};
    std::size_t capacity{0U};
    std::uint64_t dropped{0U};
};

struct EngineDiagnosticsSnapshot final {
    std::uint64_t accepted{0U};
    std::uint64_t review{0U};
    std::uint64_t rejected{0U};
    std::uint64_t provider_failures{0U};
    std::vector<DiagnosticProviderInfo> providers{};
    std::vector<DiagnosticModelInfo> models{};
    std::vector<StageLatencySummary> stage_latencies{};
    WorkerQueueSnapshot worker_queue{};
    std::string last_error_summary{};
};

class EngineDiagnostics final {
public:
    void record_recognition(domain::RecognitionStatus status) noexcept;
    void record_provider_failures(std::size_t count) noexcept;
    void record_stage_latency(std::string_view stage, double latency_ms);

    void set_providers(std::vector<DiagnosticProviderInfo> providers);
    void set_models(std::vector<DiagnosticModelInfo> models);
    void set_worker_queue(WorkerQueueSnapshot snapshot) noexcept;
    void set_last_error_summary(std::string_view summary);

    [[nodiscard]] EngineDiagnosticsSnapshot snapshot() const;

private:
    struct LatencyAccumulator final {
        std::uint64_t count{0U};
        long double total_ms{0.0L};
        double min_ms{std::numeric_limits<double>::infinity()};
        double max_ms{0.0};
    };

    std::atomic<std::uint64_t> accepted_{0U};
    std::atomic<std::uint64_t> review_{0U};
    std::atomic<std::uint64_t> rejected_{0U};
    std::atomic<std::uint64_t> provider_failures_{0U};

    mutable std::mutex mutex_{};
    std::unordered_map<std::string, LatencyAccumulator> stage_latencies_{};
    std::vector<DiagnosticProviderInfo> providers_{};
    std::vector<DiagnosticModelInfo> models_{};
    WorkerQueueSnapshot worker_queue_{};
    std::string last_error_summary_{};
};

} // namespace fac_lpr::application
