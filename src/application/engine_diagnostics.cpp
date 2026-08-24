#include <fac_lpr/application/engine_diagnostics.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace fac_lpr::application {

void EngineDiagnostics::record_recognition(const domain::RecognitionStatus status) noexcept {
    switch (status) {
        case domain::RecognitionStatus::accepted:
            accepted_.fetch_add(1U, std::memory_order_relaxed);
            break;
        case domain::RecognitionStatus::review:
            review_.fetch_add(1U, std::memory_order_relaxed);
            break;
        case domain::RecognitionStatus::rejected:
            rejected_.fetch_add(1U, std::memory_order_relaxed);
            break;
    }
}

void EngineDiagnostics::record_provider_failures(const std::size_t count) noexcept {
    provider_failures_.fetch_add(static_cast<std::uint64_t>(count), std::memory_order_relaxed);
}

void EngineDiagnostics::record_stage_latency(const std::string_view stage, const double latency_ms) {
    if (stage.empty() || !std::isfinite(latency_ms) || latency_ms < 0.0) {
        return;
    }
    std::scoped_lock lock{mutex_};
    auto& accumulator = stage_latencies_[std::string{stage}];
    ++accumulator.count;
    accumulator.total_ms += static_cast<long double>(latency_ms);
    accumulator.min_ms = std::min(accumulator.min_ms, latency_ms);
    accumulator.max_ms = std::max(accumulator.max_ms, latency_ms);
}

void EngineDiagnostics::set_readiness(
    const ReadinessState state,
    std::vector<StartupCheckSnapshot> checks) {
    std::scoped_lock lock{mutex_};
    readiness_ = state;
    startup_checks_ = std::move(checks);
}

void EngineDiagnostics::set_providers(std::vector<DiagnosticProviderInfo> providers) {
    std::scoped_lock lock{mutex_};
    providers_ = std::move(providers);
}

void EngineDiagnostics::set_models(std::vector<DiagnosticModelInfo> models) {
    std::scoped_lock lock{mutex_};
    models_ = std::move(models);
}

void EngineDiagnostics::set_worker_queue(const WorkerQueueSnapshot snapshot) noexcept {
    try {
        std::scoped_lock lock{mutex_};
        worker_queue_ = snapshot;
    } catch (...) {
    }
}

void EngineDiagnostics::set_last_error_summary(const std::string_view summary) {
    constexpr std::size_t max_summary_bytes = 256U;
    const auto count = std::min(summary.size(), max_summary_bytes);
    std::scoped_lock lock{mutex_};
    last_error_summary_.assign(summary.data(), count);
}

EngineDiagnosticsSnapshot EngineDiagnostics::snapshot() const {
    EngineDiagnosticsSnapshot result{};
    result.accepted = accepted_.load(std::memory_order_relaxed);
    result.review = review_.load(std::memory_order_relaxed);
    result.rejected = rejected_.load(std::memory_order_relaxed);
    result.provider_failures = provider_failures_.load(std::memory_order_relaxed);

    std::scoped_lock lock{mutex_};
    result.readiness = readiness_;
    result.startup_checks = startup_checks_;
    result.providers = providers_;
    result.models = models_;
    result.worker_queue = worker_queue_;
    result.last_error_summary = last_error_summary_;
    result.stage_latencies.reserve(stage_latencies_.size());
    for (const auto& [stage, accumulator] : stage_latencies_) {
        const auto mean = accumulator.count == 0U
            ? 0.0
            : static_cast<double>(accumulator.total_ms / static_cast<long double>(accumulator.count));
        result.stage_latencies.push_back(StageLatencySummary{
            .stage = stage,
            .count = accumulator.count,
            .mean_ms = mean,
            .min_ms = accumulator.count == 0U ? 0.0 : accumulator.min_ms,
            .max_ms = accumulator.max_ms});
    }
    std::sort(
        result.stage_latencies.begin(),
        result.stage_latencies.end(),
        [](const StageLatencySummary& left, const StageLatencySummary& right) {
            return left.stage < right.stage;
        });
    return result;
}

} // namespace fac_lpr::application
