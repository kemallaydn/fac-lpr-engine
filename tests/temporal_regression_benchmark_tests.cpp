#include <fac_lpr/application/stable_plate_event_filter.hpp>
#include <fac_lpr/application/temporal_plate_consensus.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fac_lpr::application::StablePlateEventFilter;
using fac_lpr::application::StablePlateEventFilterConfig;
using fac_lpr::application::TemporalPlateConsensus;
using fac_lpr::application::TemporalPlateConsensusConfig;
using fac_lpr::domain::PlateRecognitionResult;
using fac_lpr::domain::RecognitionStatus;

struct Frame final {
    std::string plate{};
    float confidence{0.0F};
    RecognitionStatus status{RecognitionStatus::accepted};
};

struct SequenceMetrics final {
    std::size_t frame_count{0U};
    std::size_t stable_count{0U};
    std::size_t emitted_count{0U};
    std::size_t suppressed_count{0U};
    std::size_t false_stable_count{0U};
    std::optional<std::size_t> first_expected_stable_frame{};
    std::size_t peak_history{0U};
};

PlateRecognitionResult frame_result(const Frame& frame) {
    PlateRecognitionResult result{};
    result.plate = frame.plate;
    result.confidence = frame.confidence;
    result.status = frame.status;
    return result;
}

SequenceMetrics run_sequence(
    const std::vector<Frame>& frames,
    std::string_view expected_stable_plate,
    TemporalPlateConsensusConfig temporal_config,
    StablePlateEventFilterConfig emission_config = {}) {
    TemporalPlateConsensus consensus{temporal_config};
    StablePlateEventFilter emission{emission_config};
    const auto base = TemporalPlateConsensus::Clock::now();

    SequenceMetrics metrics{};
    for (std::size_t index = 0U; index < frames.size(); ++index) {
        const auto timestamp = base + std::chrono::milliseconds{static_cast<long long>(index * 50U)};
        const auto temporal = consensus.observe(frame_result(frames[index]), timestamp);
        const auto event = emission.observe(temporal, timestamp);

        ++metrics.frame_count;
        metrics.peak_history = std::max(metrics.peak_history, temporal.history_size);
        if (temporal.stable_result.has_value()) {
            ++metrics.stable_count;
            if (temporal.stable_result->plate != expected_stable_plate) {
                ++metrics.false_stable_count;
            } else if (!metrics.first_expected_stable_frame.has_value()) {
                metrics.first_expected_stable_frame = index + 1U;
            }
        }
        if (event.emitted_result.has_value()) {
            ++metrics.emitted_count;
        }
        if (event.duplicate_suppressed) {
            ++metrics.suppressed_count;
        }
    }

    return metrics;
}

TemporalPlateConsensusConfig default_temporal_config() {
    TemporalPlateConsensusConfig config{};
    config.max_history = 5U;
    config.history_ttl = std::chrono::seconds{2};
    config.minimum_supporting_frames = 2U;
    config.minimum_frame_confidence = 0.50F;
    config.stable_confidence_threshold = 0.60F;
    config.conflict_margin = 0.10F;
    return config;
}

TEST(TemporalRegressionBenchmark, JitterConvergesWithoutFalseStableResult) {
    const std::vector<Frame> frames{
        {"34ABC123", 0.92F},
        {"34A8C123", 0.55F},
        {"34ABC123", 0.94F},
        {"34ABC123", 0.95F},
    };

    const auto metrics = run_sequence(frames, "34ABC123", default_temporal_config());

    ASSERT_TRUE(metrics.first_expected_stable_frame.has_value());
    EXPECT_EQ(*metrics.first_expected_stable_frame, 3U);
    EXPECT_EQ(metrics.false_stable_count, 0U);
    EXPECT_GE(metrics.stable_count, 2U);
    EXPECT_LE(metrics.peak_history, 5U);
}

TEST(TemporalRegressionBenchmark, AlternatingStrongCandidatesNeverBecomeFalseStable) {
    auto config = default_temporal_config();
    config.max_history = 4U;
    config.minimum_supporting_frames = 3U;
    config.stable_confidence_threshold = 0.65F;
    config.conflict_margin = 0.20F;

    const std::vector<Frame> frames{
        {"34ABC123", 0.95F},
        {"34ABC128", 0.94F},
        {"34ABC123", 0.96F},
        {"34ABC128", 0.95F},
        {"34ABC123", 0.95F},
        {"34ABC128", 0.96F},
    };

    const auto metrics = run_sequence(frames, "34ABC123", config);

    EXPECT_EQ(metrics.stable_count, 0U);
    EXPECT_EQ(metrics.false_stable_count, 0U);
    EXPECT_EQ(metrics.emitted_count, 0U);
    EXPECT_LE(metrics.peak_history, 4U);
}

TEST(TemporalRegressionBenchmark, ReviewOnlyEvidenceCannotBePromotedByTime) {
    const std::vector<Frame> frames{
        {"34ABC123", 0.97F, RecognitionStatus::review},
        {"34ABC123", 0.98F, RecognitionStatus::review},
        {"34ABC123", 0.99F, RecognitionStatus::review},
    };

    const auto metrics = run_sequence(frames, "34ABC123", default_temporal_config());

    EXPECT_EQ(metrics.stable_count, 0U);
    EXPECT_EQ(metrics.emitted_count, 0U);
    EXPECT_FALSE(metrics.first_expected_stable_frame.has_value());
}

TEST(TemporalRegressionBenchmark, StableDuplicateEmissionIsSuppressed) {
    auto config = default_temporal_config();
    StablePlateEventFilterConfig emission{};
    emission.duplicate_cooldown = std::chrono::seconds{5};

    const std::vector<Frame> frames{
        {"34ABC123", 0.92F},
        {"34ABC123", 0.94F},
        {"34ABC123", 0.95F},
        {"34ABC123", 0.96F},
    };

    const auto metrics = run_sequence(frames, "34ABC123", config, emission);

    EXPECT_EQ(metrics.emitted_count, 1U);
    EXPECT_GE(metrics.suppressed_count, 2U);
    EXPECT_EQ(metrics.false_stable_count, 0U);
}

TEST(TemporalRegressionBenchmark, VehicleTransitionEventuallyConvergesToNewPlate) {
    auto config = default_temporal_config();
    config.max_history = 4U;

    TemporalPlateConsensus consensus{config};
    StablePlateEventFilter emission{};
    const auto base = TemporalPlateConsensus::Clock::now();

    const std::vector<Frame> frames{
        {"34ABC123", 0.95F},
        {"34ABC123", 0.96F},
        {"34ABC123", 0.94F},
        {"06XYZ789", 0.96F},
        {"06XYZ789", 0.95F},
        {"06XYZ789", 0.97F},
        {"06XYZ789", 0.96F},
    };

    bool emitted_old = false;
    bool emitted_new = false;
    std::size_t peak_history = 0U;
    for (std::size_t index = 0U; index < frames.size(); ++index) {
        const auto timestamp = base + std::chrono::milliseconds{static_cast<long long>(index * 100U)};
        const auto temporal = consensus.observe(frame_result(frames[index]), timestamp);
        peak_history = std::max(peak_history, temporal.history_size);
        const auto event = emission.observe(temporal, timestamp);
        if (!event.emitted_result.has_value()) {
            continue;
        }
        emitted_old = emitted_old || event.emitted_result->plate == "34ABC123";
        emitted_new = emitted_new || event.emitted_result->plate == "06XYZ789";
    }

    EXPECT_TRUE(emitted_old);
    EXPECT_TRUE(emitted_new);
    EXPECT_LE(peak_history, 4U);
}

} // namespace
