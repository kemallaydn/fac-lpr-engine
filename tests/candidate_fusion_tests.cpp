#include <fac_lpr/application/candidate_fusion.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace {
using fac_lpr::application::CandidateFusionConfig;
using fac_lpr::application::LayoutEvidence;
using fac_lpr::application::WeightedMultiCropCandidateFusion;
using fac_lpr::domain::PlateCandidate;
using fac_lpr::domain::RecognitionEvidence;

RecognitionEvidence evidence(
    std::string source,
    const float crop_quality,
    std::vector<PlateCandidate> candidates) {
    RecognitionEvidence value{};
    value.source = std::move(source);
    value.crop_quality = crop_quality;
    value.candidates = std::move(candidates);
    return value;
}

TEST(CandidateFusion, TwoConsistentCropsCanBeatOneExtremeOutlier) {
    const WeightedMultiCropCandidateFusion fusion{};
    const std::vector<RecognitionEvidence> items{
        evidence("crop-a", 0.80F, {
            PlateCandidate{.text = "34X9999", .confidence = 0.99F, .format_valid = false},
            PlateCandidate{.text = "34A1234", .confidence = 0.10F, .format_valid = true}}),
        evidence("crop-b", 0.80F, {
            PlateCandidate{.text = "34A1234", .confidence = 0.75F, .format_valid = true},
            PlateCandidate{.text = "34B1234", .confidence = 0.55F, .format_valid = true}}),
        evidence("crop-c", 0.80F, {
            PlateCandidate{.text = "34A1234", .confidence = 0.75F, .format_valid = true},
            PlateCandidate{.text = "34C1234", .confidence = 0.55F, .format_valid = true}})};

    const auto result = fusion.fuse(items, {});
    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result.front().text, "34A1234");
    EXPECT_GT(result.front().confidence, 0.0F);
    EXPECT_LE(result.front().confidence, 1.0F);
}

TEST(CandidateFusion, LayoutAndSourceWeightsInfluenceSupport) {
    CandidateFusionConfig config{};
    config.source_weights["trusted"] = 1.0F;
    config.source_weights["weak"] = 0.30F;
    const WeightedMultiCropCandidateFusion fusion{config};

    const std::vector<RecognitionEvidence> items{
        evidence("trusted", 0.75F, {
            PlateCandidate{.text = "06AB123", .confidence = 0.70F, .format_valid = true}}),
        evidence("weak", 0.75F, {
            PlateCandidate{.text = "35AB123", .confidence = 0.90F, .format_valid = true}})};
    const std::vector<LayoutEvidence> layout{
        LayoutEvidence{.reliable = true, .character_count = 7, .letter_group_size = 2, .confidence = 0.90F},
        LayoutEvidence{.reliable = false, .confidence = 0.0F}};

    const auto result = fusion.fuse(items, layout);
    ASSERT_GE(result.size(), 2U);
    EXPECT_EQ(result.front().text, "06AB123");
}

TEST(CandidateFusion, DuplicateCandidateInsideOneCropVotesOnlyOnce) {
    const WeightedMultiCropCandidateFusion fusion{};
    const std::vector<RecognitionEvidence> duplicated{
        evidence("crop-a", 0.80F, {
            PlateCandidate{.text = "34A1234", .confidence = 0.80F, .format_valid = true},
            PlateCandidate{.text = "34A1234", .confidence = 0.70F, .format_valid = true}})};
    const std::vector<RecognitionEvidence> single{
        evidence("crop-a", 0.80F, {
            PlateCandidate{.text = "34A1234", .confidence = 0.80F, .format_valid = true}})};

    const auto duplicated_result = fusion.fuse(duplicated, {});
    const auto single_result = fusion.fuse(single, {});
    ASSERT_EQ(duplicated_result.size(), 1U);
    ASSERT_EQ(single_result.size(), 1U);
    EXPECT_FLOAT_EQ(
        duplicated_result.front().confidence,
        single_result.front().confidence);
}

TEST(CandidateFusion, ResultCountIsBoundedAndOrderingIsDeterministic) {
    CandidateFusionConfig config{};
    config.max_candidates = 2U;
    const WeightedMultiCropCandidateFusion fusion{config};
    const std::vector<RecognitionEvidence> items{
        evidence("crop", 0.8F, {
            PlateCandidate{.text = "34C1234", .confidence = 0.70F, .format_valid = true},
            PlateCandidate{.text = "34A1234", .confidence = 0.70F, .format_valid = true},
            PlateCandidate{.text = "34B1234", .confidence = 0.70F, .format_valid = true}})};

    const auto first = fusion.fuse(items, {});
    const auto second = fusion.fuse(items, {});
    ASSERT_EQ(first.size(), 2U);
    ASSERT_EQ(second.size(), first.size());
    for (std::size_t index = 0U; index < first.size(); ++index) {
        EXPECT_EQ(first[index].text, second[index].text);
        EXPECT_FLOAT_EQ(first[index].confidence, second[index].confidence);
    }
}

TEST(CandidateFusion, InvalidConfigurationFailsFast) {
    CandidateFusionConfig config{};
    config.max_candidates = 0U;
    EXPECT_THROW(
        WeightedMultiCropCandidateFusion{config},
        fac_lpr::application::ConfigurationError);
}

} // namespace
