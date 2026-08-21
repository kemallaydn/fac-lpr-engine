#include <fac_lpr/infrastructure/lprnet/constrained_ctc_beam_search.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

namespace {
using fac_lpr::infrastructure::lprnet::ConstrainedCtcBeamSearch;
using fac_lpr::infrastructure::lprnet::ConstrainedCtcBeamSearchConfig;

std::size_t class_index(
    const ConstrainedCtcBeamSearchConfig& config,
    const char character) {
    const auto iterator = std::find(
        config.ctc.charset.begin(),
        config.ctc.charset.end(),
        character);
    EXPECT_NE(iterator, config.ctc.charset.end());
    return static_cast<std::size_t>(
        std::distance(config.ctc.charset.begin(), iterator));
}

std::vector<float> logits_for_text(
    const ConstrainedCtcBeamSearchConfig& config,
    const std::string_view text) {
    const auto classes = config.ctc.charset.size() + 1U;
    std::vector<float> logits(text.size() * classes, -8.0F);
    for (std::size_t timestep = 0U; timestep < text.size(); ++timestep) {
        const auto winner = class_index(config, text[timestep]);
        logits[(timestep * classes) + winner] = 8.0F;
    }
    return logits;
}

ConstrainedCtcBeamSearchConfig make_config() {
    ConstrainedCtcBeamSearchConfig config{};
    config.ctc.charset = {'0', '1', '2', '3', '4', 'A', 'O', 'Q'};
    config.ctc.blank_index = config.ctc.charset.size();
    config.ctc.maximum_classes = 32U;
    config.ctc.maximum_timesteps = 32U;
    config.beam_width = 12U;
    config.result_limit = 5U;
    config.classes_per_step = 1U;
    config.confusion_weight = 0.20F;
    return config;
}

TEST(ConstrainedCtcBeamSearch, ProducesValidTurkishPlateCandidate) {
    const auto config = make_config();
    const ConstrainedCtcBeamSearch decoder{config};
    const auto logits = logits_for_text(config, "34A1234");
    const auto result = decoder.decode(
        logits,
        7U,
        config.ctc.charset.size() + 1U);

    ASSERT_FALSE(result.candidates.empty());
    EXPECT_EQ(result.candidates.front().text, "34A1234");
    EXPECT_TRUE(result.candidates.front().format_valid);
    EXPECT_FALSE(result.fallback_used);
}

TEST(ConstrainedCtcBeamSearch, ConfusionMapCanRescueInvalidProvincePrefix) {
    const auto config = make_config();
    const ConstrainedCtcBeamSearch decoder{config};
    const auto logits = logits_for_text(config, "O4A1234");
    const auto result = decoder.decode(
        logits,
        7U,
        config.ctc.charset.size() + 1U);

    ASSERT_FALSE(result.candidates.empty());
    EXPECT_EQ(result.candidates.front().text, "04A1234");
    EXPECT_TRUE(result.candidates.front().format_valid);
    EXPECT_EQ(result.greedy.text, "O4A1234");
}

TEST(ConstrainedCtcBeamSearch, UsesGreedyReferenceWhenGrammarPrunesAllPaths) {
    const auto config = make_config();
    const ConstrainedCtcBeamSearch decoder{config};
    const auto logits = logits_for_text(config, "34Q1234");
    const auto result = decoder.decode(
        logits,
        7U,
        config.ctc.charset.size() + 1U);

    ASSERT_EQ(result.candidates.size(), 1U);
    EXPECT_TRUE(result.fallback_used);
    EXPECT_EQ(result.greedy.text, "34Q1234");
    EXPECT_FALSE(result.candidates.front().format_valid);
}

TEST(ConstrainedCtcBeamSearch, CandidateOrderingIsDeterministic) {
    const auto config = make_config();
    const ConstrainedCtcBeamSearch decoder{config};
    const auto logits = logits_for_text(config, "O4A1234");
    const auto first = decoder.decode(
        logits,
        7U,
        config.ctc.charset.size() + 1U);
    const auto second = decoder.decode(
        logits,
        7U,
        config.ctc.charset.size() + 1U);

    ASSERT_EQ(first.candidates.size(), second.candidates.size());
    for (std::size_t index = 0U; index < first.candidates.size(); ++index) {
        EXPECT_EQ(first.candidates[index].text, second.candidates[index].text);
        EXPECT_FLOAT_EQ(
            first.candidates[index].confidence,
            second.candidates[index].confidence);
    }
}

} // namespace
