#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/temporal_plate_consensus.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace {

using fac_lpr::application::TemporalPlateConsensus;
using fac_lpr::application::TemporalPlateConsensusConfig;
using fac_lpr::domain::PlateRecognitionResult;
using fac_lpr::domain::RecognitionStatus;

PlateRecognitionResult result(std::string plate, float confidence, RecognitionStatus status = RecognitionStatus::accepted) {
    PlateRecognitionResult value{};
    value.plate = std::move(plate);
    value.confidence = confidence;
    value.status = status;
    return value;
}

TEST(TemporalPlateConsensusTests, RequiresConfiguredSupportBeforeStableResult) {
    TemporalPlateConsensus consensus{TemporalPlateConsensusConfig{.max_history = 4U, .minimum_supporting_frames = 2U}};
    const auto now = TemporalPlateConsensus::Clock::now();

    EXPECT_FALSE(consensus.observe(result("34ABC123", 0.90F), now).stable_result.has_value());
    const auto stable = consensus.observe(result("34ABC123", 0.92F), now + std::chrono::milliseconds{10});

    ASSERT_TRUE(stable.stable_result.has_value());
    EXPECT_EQ(stable.stable_result->plate, "34ABC123");
    EXPECT_EQ(stable.supporting_frames, 2U);
}

TEST(TemporalPlateConsensusTests, StabilizesCharacterJitterByWeightedAgreement) {
    TemporalPlateConsensus consensus{TemporalPlateConsensusConfig{
        .max_history = 5U,
        .minimum_supporting_frames = 2U,
        .stable_confidence_threshold = 0.60F,
        .conflict_margin = 0.10F}};
    const auto now = TemporalPlateConsensus::Clock::now();

    (void)consensus.observe(result("34A8C123", 0.55F), now);
    (void)consensus.observe(result("34ABC123", 0.92F), now + std::chrono::milliseconds{10});
    const auto stable = consensus.observe(result("34ABC123", 0.94F), now + std::chrono::milliseconds{20});

    ASSERT_TRUE(stable.stable_result.has_value());
    EXPECT_EQ(stable.stable_result->plate, "34ABC123");
}

TEST(TemporalPlateConsensusTests, StrongConflictDoesNotBecomeStable) {
    TemporalPlateConsensus consensus{TemporalPlateConsensusConfig{
        .max_history = 4U,
        .minimum_supporting_frames = 1U,
        .stable_confidence_threshold = 0.45F,
        .conflict_margin = 0.20F}};
    const auto now = TemporalPlateConsensus::Clock::now();

    (void)consensus.observe(result("34ABC123", 0.90F), now);
    const auto conflicted = consensus.observe(result("34ABC128", 0.89F), now + std::chrono::milliseconds{10});
    EXPECT_FALSE(conflicted.stable_result.has_value());
}

TEST(TemporalPlateConsensusTests, RejectedAndLowConfidenceFramesDoNotAddVotingWeight) {
    TemporalPlateConsensus consensus{TemporalPlateConsensusConfig{.max_history = 4U, .minimum_supporting_frames = 2U}};
    const auto now = TemporalPlateConsensus::Clock::now();

    (void)consensus.observe(result("34ABC123", 0.95F, RecognitionStatus::rejected), now);
    (void)consensus.observe(result("34ABC123", 0.20F), now + std::chrono::milliseconds{10});
    const auto output = consensus.observe(result("34ABC123", 0.95F), now + std::chrono::milliseconds{20});

    EXPECT_FALSE(output.stable_result.has_value());
    EXPECT_EQ(output.supporting_frames, 1U);
}

TEST(TemporalPlateConsensusTests, ReviewFramesNeverPromoteToAcceptedConsensus) {
    TemporalPlateConsensus consensus{TemporalPlateConsensusConfig{
        .max_history = 4U,
        .minimum_supporting_frames = 2U,
        .minimum_frame_confidence = 0.50F,
        .stable_confidence_threshold = 0.60F}};
    const auto now = TemporalPlateConsensus::Clock::now();

    (void)consensus.observe(result("34ABC123", 0.95F, RecognitionStatus::review), now);
    (void)consensus.observe(result("34ABC123", 0.96F, RecognitionStatus::review), now + std::chrono::milliseconds{10});
    const auto output = consensus.observe(
        result("34ABC123", 0.97F, RecognitionStatus::review),
        now + std::chrono::milliseconds{20});

    EXPECT_FALSE(output.stable_result.has_value());
    EXPECT_EQ(output.supporting_frames, 0U);
}

TEST(TemporalPlateConsensusTests, DegradedAcceptedEvidenceRemainsDegradedWhenStabilized) {
    TemporalPlateConsensus consensus{TemporalPlateConsensusConfig{
        .max_history = 4U,
        .minimum_supporting_frames = 2U,
        .minimum_frame_confidence = 0.50F,
        .stable_confidence_threshold = 0.60F}};
    const auto now = TemporalPlateConsensus::Clock::now();

    auto first = result("34ABC123", 0.92F);
    first.degraded = true;
    auto second = result("34ABC123", 0.94F);
    second.degraded = true;

    (void)consensus.observe(first, now);
    const auto stable = consensus.observe(second, now + std::chrono::milliseconds{10});

    ASSERT_TRUE(stable.stable_result.has_value());
    EXPECT_TRUE(stable.stable_result->degraded);
    EXPECT_EQ(stable.stable_result->status, RecognitionStatus::accepted);
}

TEST(TemporalPlateConsensusTests, HistoryIsBoundedAndExpires) {
    TemporalPlateConsensus consensus{TemporalPlateConsensusConfig{
        .max_history = 2U,
        .history_ttl = std::chrono::milliseconds{100},
        .minimum_supporting_frames = 2U}};
    const auto now = TemporalPlateConsensus::Clock::now();

    (void)consensus.observe(result("34ABC123", 0.90F), now);
    (void)consensus.observe(result("34ABC123", 0.91F), now + std::chrono::milliseconds{10});
    (void)consensus.observe(result("34ABC123", 0.92F), now + std::chrono::milliseconds{20});
    EXPECT_EQ(consensus.history_size(), 2U);

    const auto expired = consensus.observe(result("34ABC123", 0.93F), now + std::chrono::milliseconds{500});
    EXPECT_EQ(expired.history_size, 1U);
    EXPECT_FALSE(expired.stable_result.has_value());
}

TEST(TemporalPlateConsensusTests, ResetClearsState) {
    TemporalPlateConsensus consensus{};
    const auto now = TemporalPlateConsensus::Clock::now();
    (void)consensus.observe(result("34ABC123", 0.90F), now);
    consensus.reset();
    EXPECT_EQ(consensus.history_size(), 0U);
}

TEST(TemporalPlateConsensusTests, RejectsUnboundedOrInvalidConfiguration) {
    EXPECT_THROW(TemporalPlateConsensus(TemporalPlateConsensusConfig{.max_history = 0U}), fac_lpr::application::ConfigurationError);
    EXPECT_THROW(TemporalPlateConsensus(TemporalPlateConsensusConfig{.minimum_frame_confidence = 2.0F}), fac_lpr::application::ConfigurationError);
}

} // namespace
