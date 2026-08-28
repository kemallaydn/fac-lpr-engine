#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/stable_plate_event_filter.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace {
using namespace fac_lpr;

application::TemporalConsensusResult stable(std::string plate, float confidence = 0.90F) {
    domain::PlateRecognitionResult result{};
    result.status = domain::RecognitionStatus::accepted;
    result.plate = std::move(plate);
    result.confidence = confidence;

    application::TemporalConsensusResult consensus{};
    consensus.stable_result = std::move(result);
    consensus.supporting_frames = 3U;
    consensus.support = confidence;
    return consensus;
}

TEST(StablePlateEventFilter, RejectsInvalidCooldown) {
    application::StablePlateEventFilterConfig config{};
    config.duplicate_cooldown = std::chrono::milliseconds{-1};
    EXPECT_THROW(
        static_cast<void>(application::StablePlateEventFilter{config}),
        application::ConfigurationError);
}

TEST(StablePlateEventFilter, EmitsFirstStableResult) {
    application::StablePlateEventFilter filter{};
    const auto now = application::StablePlateEventFilter::Clock::now();

    const auto result = filter.observe(stable("34ABC123"), now);

    ASSERT_TRUE(result.emitted_result.has_value());
    EXPECT_EQ(result.emitted_result->plate, "34ABC123");
    EXPECT_FALSE(result.duplicate_suppressed);
    EXPECT_EQ(filter.stats().emitted, 1U);
}

TEST(StablePlateEventFilter, SuppressesSamePlateInsideCooldown) {
    application::StablePlateEventFilter filter{};
    const auto now = application::StablePlateEventFilter::Clock::now();

    static_cast<void>(filter.observe(stable("34ABC123"), now));
    const auto duplicate = filter.observe(
        stable("34ABC123", 0.99F),
        now + std::chrono::milliseconds{100});

    EXPECT_FALSE(duplicate.emitted_result.has_value());
    EXPECT_TRUE(duplicate.duplicate_suppressed);
    EXPECT_EQ(filter.stats().emitted, 1U);
    EXPECT_EQ(filter.stats().suppressed, 1U);
}

TEST(StablePlateEventFilter, DifferentPlateBreaksSuppressionImmediately) {
    application::StablePlateEventFilter filter{};
    const auto now = application::StablePlateEventFilter::Clock::now();

    static_cast<void>(filter.observe(stable("34ABC123"), now));
    const auto changed = filter.observe(
        stable("06XYZ456"),
        now + std::chrono::milliseconds{100});

    ASSERT_TRUE(changed.emitted_result.has_value());
    EXPECT_EQ(changed.emitted_result->plate, "06XYZ456");
    EXPECT_FALSE(changed.duplicate_suppressed);
}

TEST(StablePlateEventFilter, SamePlateReemitsAfterCooldown) {
    application::StablePlateEventFilterConfig config{};
    config.duplicate_cooldown = std::chrono::milliseconds{500};
    application::StablePlateEventFilter filter(config);
    const auto now = application::StablePlateEventFilter::Clock::now();

    static_cast<void>(filter.observe(stable("34ABC123"), now));
    const auto result = filter.observe(
        stable("34ABC123"),
        now + std::chrono::milliseconds{500});

    EXPECT_TRUE(result.emitted_result.has_value());
    EXPECT_FALSE(result.duplicate_suppressed);
    EXPECT_EQ(filter.stats().emitted, 2U);
}

TEST(StablePlateEventFilter, UnstableObservationDoesNotEmitOrResetCooldown) {
    application::StablePlateEventFilter filter{};
    const auto now = application::StablePlateEventFilter::Clock::now();
    static_cast<void>(filter.observe(stable("34ABC123"), now));

    application::TemporalConsensusResult unstable{};
    EXPECT_FALSE(filter.observe(unstable, now + std::chrono::milliseconds{100}).emitted_result.has_value());

    const auto recovered = filter.observe(
        stable("34ABC123"),
        now + std::chrono::milliseconds{200});
    EXPECT_TRUE(recovered.duplicate_suppressed);
}

TEST(StablePlateEventFilter, ResetRearmsEmissionAndClearsStats) {
    application::StablePlateEventFilter filter{};
    const auto now = application::StablePlateEventFilter::Clock::now();
    static_cast<void>(filter.observe(stable("34ABC123"), now));

    filter.reset();
    EXPECT_EQ(filter.stats().emitted, 0U);
    EXPECT_EQ(filter.stats().suppressed, 0U);

    const auto result = filter.observe(
        stable("34ABC123"),
        now + std::chrono::milliseconds{1});
    EXPECT_TRUE(result.emitted_result.has_value());
}

} // namespace
