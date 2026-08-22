#include <fac_lpr/infrastructure/lprnet/ctc_decoder.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace {
using fac_lpr::infrastructure::lprnet::GreedyCtcDecoder;
using fac_lpr::infrastructure::lprnet::GreedyCtcDecoderConfig;

std::vector<float> make_logits(
    const std::vector<std::size_t>& winners,
    const std::size_t classes) {
    std::vector<float> logits(winners.size() * classes, -5.0F);
    for (std::size_t timestep = 0U; timestep < winners.size(); ++timestep) {
        logits[(timestep * classes) + winners[timestep]] = 5.0F;
    }
    return logits;
}

TEST(GreedyCtcDecoder, CollapsesRepeatsAndRemovesBlank) {
    GreedyCtcDecoderConfig config{};
    config.charset = {'A', 'B'};
    config.blank_index = 2U;
    const GreedyCtcDecoder decoder{config};

    const std::vector<std::size_t> winners{0U, 0U, 2U, 0U, 1U, 1U};
    const auto logits = make_logits(winners, 3U);
    const auto result = decoder.decode(logits, winners.size(), 3U);

    EXPECT_EQ(result.text, "AAB");
    ASSERT_EQ(result.character_confidences.size(), 3U);
    EXPECT_GT(result.confidence, 0.99F);
    EXPECT_LE(result.confidence, 1.0F);
}

TEST(GreedyCtcDecoder, SupportsBlankAtArbitraryConfiguredIndex) {
    GreedyCtcDecoderConfig config{};
    config.charset = {'A', 'B'};
    config.blank_index = 0U;
    const GreedyCtcDecoder decoder{config};

    const std::vector<std::size_t> winners{1U, 0U, 2U};
    const auto logits = make_logits(winners, 3U);
    const auto result = decoder.decode(logits, winners.size(), 3U);
    EXPECT_EQ(result.text, "AB");
}

TEST(GreedyCtcDecoder, ActiveV2MixedEpoch7UsesExactly33CharactersAndBlankAt33) {
    constexpr char active_charset[] = "0123456789ABCDEFGHIJKLMNOPRSTUVYZ";
    const std::string charset{active_charset};
    ASSERT_EQ(charset.size(), 33U);
    EXPECT_EQ(charset.find('-'), std::string::npos);

    GreedyCtcDecoderConfig config{};
    config.charset.assign(charset.begin(), charset.end());
    config.blank_index = 33U;
    config.maximum_timesteps = 24U;
    config.maximum_classes = 34U;
    const GreedyCtcDecoder decoder{config};

    EXPECT_EQ(decoder.class_count(), 34U);

    // 34ABC123 with deliberate duplicate runs and CTC blanks.
    const std::vector<std::size_t> winners{
        3U, 3U, 33U,
        4U, 33U,
        10U, 10U, 33U,
        11U, 33U,
        12U, 33U,
        1U, 33U,
        2U, 2U, 33U,
        3U};
    const auto logits = make_logits(winners, 34U);
    const auto result = decoder.decode(logits, winners.size(), 34U);

    EXPECT_EQ(result.text, "34ABC123");
    EXPECT_GT(result.confidence, 0.99F);
}

TEST(GreedyCtcDecoder, RejectsMismatchedClassCount) {
    GreedyCtcDecoderConfig config{};
    config.charset = {'A', 'B'};
    config.blank_index = 2U;
    const GreedyCtcDecoder decoder{config};
    const std::vector<float> logits(8U, 0.0F);
    EXPECT_THROW(
        decoder.decode(logits, 2U, 4U),
        fac_lpr::application::InferenceError);
}

TEST(GreedyCtcDecoder, RejectsNonFiniteLogits) {
    GreedyCtcDecoderConfig config{};
    config.charset = {'A'};
    config.blank_index = 1U;
    const GreedyCtcDecoder decoder{config};
    std::vector<float> logits{0.0F, 1.0F};
    logits[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(
        decoder.decode(logits, 1U, 2U),
        fac_lpr::application::InferenceError);
}

TEST(GreedyCtcDecoder, DuplicateCharsetFailsFast) {
    GreedyCtcDecoderConfig config{};
    config.charset = {'A', 'A'};
    config.blank_index = 2U;
    EXPECT_THROW(
        GreedyCtcDecoder{config},
        fac_lpr::application::ConfigurationError);
}

} // namespace
