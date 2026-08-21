#include <fac_lpr/infrastructure/lprnet/ctc_decoder.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
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
