#include <fac_lpr/application/safe_decision_policy.hpp>
#include <fac_lpr/application/error.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace {
using namespace fac_lpr;

application::SafeRecognitionDecisionPolicy policy() {
    return application::SafeRecognitionDecisionPolicy{};
}

domain::Detection good_detection() {
    domain::Detection detection{};
    detection.bbox = {0.0F, 0.0F, 120.0F, 30.0F};
    detection.confidence = 0.92F;
    detection.geometry_score = 0.90F;
    detection.provider = "detector";
    return detection;
}

domain::RecognitionEvidence good_evidence() {
    domain::RecognitionEvidence evidence{};
    evidence.source = "lprnet";
    evidence.crop_quality = 0.85F;
    return evidence;
}

bool has_reason(
    const domain::PlateRecognitionResult& result,
    const domain::RecognitionDecisionReason reason) {
    return std::find(
        result.decision_reasons.begin(),
        result.decision_reasons.end(),
        reason) != result.decision_reasons.end();
}

TEST(SafeDecisionPolicy, AcceptsOnlyStrongConsistentTechnicalEvidence) {
    const std::vector evidence{good_evidence()};
    const std::vector<domain::PlateCandidate> candidates{{"34ABC123", 0.91F, 0.91F, true}};
    const auto result = policy().decide(good_detection(), evidence, candidates, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::accepted);
    EXPECT_EQ(result.plate, "34ABC123");
    EXPECT_TRUE(has_reason(result, domain::RecognitionDecisionReason::accepted_consensus));
}

TEST(SafeDecisionPolicy, StrongConflictingOcrCannotAutoAccept) {
    const std::vector evidence{good_evidence()};
    const std::vector<domain::PlateCandidate> candidates{
        {"34ABC123", 0.93F, 0.93F, true},
        {"34ABC128", 0.82F, 0.82F, true}};
    const auto result = policy().decide(good_detection(), evidence, candidates, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_TRUE(has_reason(
        result,
        domain::RecognitionDecisionReason::conflicting_strong_candidates));
}

TEST(SafeDecisionPolicy, StrongCrossProviderCharacterConflictRequiresReview) {
    auto primary = good_evidence();
    primary.greedy_text = "34ABL489";
    primary.greedy_character_confidences = {0.99F, 0.99F, 0.99F, 0.97F,
                                            0.99F, 0.99F, 0.99F, 0.99F};
    auto secondary = good_evidence();
    secondary.source = "paddleocr";
    secondary.candidates = {{"34A8L489", 0.92F, 0.92F, false}};
    const std::vector evidence{primary, secondary};
    const std::vector<domain::PlateCandidate> fused{{"34ABL489", 0.95F, 0.95F, true}};
    const auto result = policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_TRUE(has_reason(result, domain::RecognitionDecisionReason::conflicting_strong_candidates));
}

TEST(SafeDecisionPolicy, WeakOrSameSourceCharacterConflictDoesNotInventReview) {
    auto primary = good_evidence();
    primary.greedy_text = "34ABL489";
    primary.greedy_character_confidences = {0.99F, 0.99F, 0.99F, 0.97F,
                                            0.99F, 0.99F, 0.99F, 0.99F};
    auto secondary = good_evidence();
    secondary.source = "paddleocr";
    secondary.candidates = {{"34A8L489", 0.40F, 0.40F, false}};
    const std::vector<domain::PlateCandidate> fused{{"34ABL489", 0.95F, 0.95F, true}};
    EXPECT_EQ(policy().decide(good_detection(), std::vector{primary, secondary}, fused, {}).status,
              domain::RecognitionStatus::accepted);

    secondary.source = primary.source;
    secondary.candidates.front().confidence = 0.95F;
    secondary.candidates.front().calibrated_confidence = 0.95F;
    EXPECT_EQ(policy().decide(good_detection(), std::vector{primary, secondary}, fused, {}).status,
              domain::RecognitionStatus::accepted);
}

TEST(SafeDecisionPolicy, GreedyScoresAreNeverAppliedToDifferentBeamCandidate) {
    auto primary = good_evidence();
    // Beam search may choose a valid plate different from raw greedy CTC output.
    // Raw positional scores must not be used for the beam-selected candidate.
    primary.greedy_text = "34A8L489";
    primary.greedy_character_confidences = {0.99F, 0.99F, 0.99F, 0.99F,
                                            0.99F, 0.99F, 0.99F, 0.99F};
    auto secondary = good_evidence();
    secondary.source = "paddleocr";
    secondary.candidates = {{"34ABL489", 0.93F, 0.93F, false}};
    const std::vector evidence{primary, secondary};
    const std::vector<domain::PlateCandidate> selected{
        {"34ABL489", 0.95F, 0.95F, true}};
    const auto result = policy().decide(good_detection(), evidence, selected, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::accepted);
}

TEST(SafeDecisionPolicy, DegradedProviderSetIsAtMostReview) {
    const std::vector evidence{good_evidence()};
    const std::vector<domain::PlateCandidate> candidates{{"34ABC123", 0.95F, 0.95F, true}};
    application::RecognitionDecisionContext context{};
    context.degraded = true;
    context.provider_failure_count = 1U;
    const auto result = policy().decide(good_detection(), evidence, candidates, context);
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_TRUE(result.degraded);
    EXPECT_TRUE(has_reason(
        result,
        domain::RecognitionDecisionReason::degraded_provider_set));
}

TEST(SafeDecisionPolicy, FatalProviderFailureRejectsFailClosed) {
    const std::vector evidence{good_evidence()};
    const std::vector<domain::PlateCandidate> candidates{{"34ABC123", 0.99F, 0.99F, true}};
    application::RecognitionDecisionContext context{};
    context.fatal_provider_failure = true;
    const auto result = policy().decide(good_detection(), evidence, candidates, context);
    EXPECT_EQ(result.status, domain::RecognitionStatus::rejected);
    EXPECT_TRUE(has_reason(
        result,
        domain::RecognitionDecisionReason::fatal_provider_failure));
}

TEST(SafeDecisionPolicy, WeakDetectorRejectsEvenWithStrongOcr) {
    auto detection = good_detection();
    detection.confidence = 0.20F;
    const std::vector evidence{good_evidence()};
    const std::vector<domain::PlateCandidate> candidates{{"34ABC123", 0.99F, 0.99F, true}};
    const auto result = policy().decide(detection, evidence, candidates, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::rejected);
    EXPECT_TRUE(has_reason(
        result,
        domain::RecognitionDecisionReason::detector_confidence_below_minimum));
}

TEST(SafeDecisionPolicy, MissingValidCandidateRejectsWithExplainableReason) {
    const std::vector evidence{good_evidence()};
    const std::vector<domain::PlateCandidate> candidates{
        {"NOT-A-VALID-PLATE", 0.99F, 0.99F, false}};
    const auto result = policy().decide(good_detection(), evidence, candidates, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::rejected);
    EXPECT_TRUE(has_reason(result, domain::RecognitionDecisionReason::no_valid_candidate));
}

TEST(SafeDecisionPolicy, WeakGeometryAndCropQualityRequireReviewWithReasons) {
    auto detection = good_detection();
    detection.geometry_score = 0.10F;
    auto evidence_item = good_evidence();
    evidence_item.crop_quality = 0.10F;
    const std::vector evidence{evidence_item};
    const std::vector<domain::PlateCandidate> candidates{{"34ABC123", 0.95F, 0.95F, true}};

    const auto result = policy().decide(detection, evidence, candidates, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_TRUE(has_reason(
        result,
        domain::RecognitionDecisionReason::geometry_below_minimum));
    EXPECT_TRUE(has_reason(
        result,
        domain::RecognitionDecisionReason::crop_quality_below_minimum));
}


application::SafeRecognitionDecisionPolicy paddle_policy() {
    application::DecisionConfig config{};
    config.enable_paddle_review_suggestions = true;
    config.paddle_accept_minimum_score = 1.0F; // Review-only test fixture.
    return application::SafeRecognitionDecisionPolicy{config};
}

domain::RecognitionEvidence paddle_reading(
    const std::string& text, const float score = 0.96F) {
    auto value = good_evidence();
    value.source = "paddleocr";
    value.candidates = {{text, score, score, false}};
    return value;
}

TEST(SafeDecisionPolicy, PaddleReviewOnlyFallbackWhenPrimaryHasNoValidPlate) {
    const std::vector evidence{good_evidence(), paddle_reading("34ABC123")};
    const std::vector<domain::PlateCandidate> fused{};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.plate, "34ABC123");
    EXPECT_FLOAT_EQ(result.confidence, 0.0F);
    EXPECT_TRUE(has_reason(
        result, domain::RecognitionDecisionReason::secondary_ocr_review_suggestion));
}

TEST(SafeDecisionPolicy, PaddleSuggestsOnWeakPrimaryWithoutAutoAccept) {
    auto primary = good_evidence();
    primary.greedy_text = "34ABC128";
    primary.greedy_character_confidences = {0.8F, 0.8F, 0.8F, 0.8F, 0.8F, 0.8F, 0.8F, 0.2F};
    const std::vector evidence{primary, paddle_reading("34ABC123")};
    const std::vector<domain::PlateCandidate> fused{{"34ABC128", 0.74F, 0.74F, true}};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.plate, "34ABC123");
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.confidence, 0.0F);
    EXPECT_TRUE(has_reason(result, domain::RecognitionDecisionReason::secondary_ocr_review_suggestion));
}

TEST(SafeDecisionPolicy, PaddleSingleWeakCharacterMaySuggestEvenWithStrongPrimary) {
    auto primary = good_evidence();
    primary.greedy_text = "34ABC128";
    primary.greedy_character_confidences = {0.99F, 0.99F, 0.99F, 0.99F, 0.99F, 0.99F, 0.99F, 0.30F};
    const std::vector evidence{primary, paddle_reading("34ABC123")};
    const std::vector<domain::PlateCandidate> fused{{"34ABC128", 0.92F, 0.92F, true}};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.plate, "34ABC123");
}

TEST(SafeDecisionPolicy, StrongLprnetConflictKeepsPrimaryButRequiresReview) {
    auto primary = good_evidence();
    primary.greedy_text = "34ABC128";
    primary.greedy_character_confidences = std::vector<float>(8U, 0.99F);
    const std::vector evidence{primary, paddle_reading("34ABC123")};
    const std::vector<domain::PlateCandidate> fused{{"34ABC128", 0.91F, 0.91F, true}};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.plate, "34ABC128");
    EXPECT_TRUE(has_reason(result, domain::RecognitionDecisionReason::conflicting_strong_candidates));
}

TEST(SafeDecisionPolicy, MultiCropPaddleConflictCannotChooseArbitraryPlate) {
    const std::vector evidence{
        good_evidence(), paddle_reading("34ABC123"), paddle_reading("34ABC128")};
    const std::vector<domain::PlateCandidate> fused{};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::rejected);
    EXPECT_TRUE(result.plate.empty());
}

TEST(SafeDecisionPolicy, PaddleWeakOrInvalidCannotRescueInvalidPrimary) {
    const std::vector<domain::PlateCandidate> fused{};
    for (const auto& secondary : {
             paddle_reading("34ABC123", 0.60F),
             paddle_reading("00ABC123", 0.98F),
             paddle_reading("34A8C123", 0.98F)}) {
        const std::vector evidence{good_evidence(), secondary};
        const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
        EXPECT_EQ(result.status, domain::RecognitionStatus::rejected);
    }
}

TEST(SafeDecisionPolicy, PaddleMatchingPrimaryDoesNotInflateLprnetConfidence) {
    const std::vector evidence{good_evidence(), paddle_reading("34ABC123")};
    const std::vector<domain::PlateCandidate> fused{{"34ABC123", 0.91F, 0.91F, true}};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::accepted);
    EXPECT_EQ(result.plate, "34ABC123");
    EXPECT_FLOAT_EQ(result.confidence, 0.91F);
}

TEST(SafeDecisionPolicy, DefaultPaddleConflictRequiresReview) {
    const std::vector evidence{good_evidence(), paddle_reading("34ABC128")};
    const std::vector<domain::PlateCandidate> fused{{"34ABC123", 0.91F, 0.91F, true}};
    const auto result = policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.plate, "34ABC123");
}

TEST(SafeDecisionPolicy, PaddleNeverOverridesFatalProviderOrDetectorFailure) {
    const std::vector evidence{good_evidence(), paddle_reading("34ABC123")};
    const std::vector<domain::PlateCandidate> fused{};
    auto weak = good_detection();
    weak.confidence = 0.1F;
    EXPECT_EQ(paddle_policy().decide(weak, evidence, fused, {}).status,
              domain::RecognitionStatus::rejected);
    application::RecognitionDecisionContext context{};
    context.fatal_provider_failure = true;
    EXPECT_EQ(paddle_policy().decide(good_detection(), evidence, fused, context).status,
              domain::RecognitionStatus::rejected);
}

TEST(SafeDecisionPolicy, IndependentPaddleMatchToNonTopOneLprnetBeamCandidateIsReviewOnly) {
    // One provider's top-1 has a HIGH uncalibrated score; another provider
    // independently recognizes a grammar-valid LPRNet beam alternative.
    // The matched alternative becomes a suggestion, never auto-accepted.
    auto lpr = good_evidence();
    lpr.source = "lprnet_onnx";
    lpr.greedy_text = "34ABC128";
    lpr.greedy_character_confidences = std::vector<float>(8U, 0.99F);
    lpr.candidates = {{"34ABC128", 0.98F, 0.98F, true},
                      {"34ABC123", 0.59F, 0.59F, true}};
    const std::vector evidence{lpr, paddle_reading("34ABC123", 0.91F)};
    const std::vector<domain::PlateCandidate> fused{
        {"34ABC128", 0.97F, 0.97F, true},
        {"34ABC123", 0.79F, 0.79F, true}};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.plate, "34ABC123");
    EXPECT_FLOAT_EQ(result.confidence, 0.0F);
    EXPECT_TRUE(has_reason(result, domain::RecognitionDecisionReason::secondary_ocr_review_suggestion));
    EXPECT_TRUE(std::any_of(result.alternatives.begin(), result.alternatives.end(),
        [](const auto& candidate) { return candidate.text == "34ABC128"; }));
}

TEST(SafeDecisionPolicy, IndependentPaddleMatchDoesNotRequireGreedyTextToEqualBeamCandidate) {
    // A valid beam candidate is not necessarily the CTC greedy text.
    auto lpr = good_evidence();
    lpr.greedy_text = "34ABC28";
    lpr.greedy_character_confidences = std::vector<float>(7U, 0.99F);
    lpr.candidates = {{"34ABC128", 0.96F, 0.96F, true},
                      {"34ABC123", 0.45F, 0.45F, true}};
    const std::vector evidence{lpr, paddle_reading("34ABC123", 0.88F)};
    const std::vector<domain::PlateCandidate> fused{
        {"34ABC128", 0.96F, 0.96F, true}};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.plate, "34ABC123");
    EXPECT_FLOAT_EQ(result.confidence, 0.0F);
    EXPECT_TRUE(has_reason(result, domain::RecognitionDecisionReason::secondary_ocr_review_suggestion));
}

TEST(SafeDecisionPolicy, UnsupportedPaddleTextCannotReplaceHighConfidenceLprnetBeam) {
    auto lpr = good_evidence();
    lpr.candidates = {{"34ABC128", 0.96F, 0.96F, true},
                      {"34ABC129", 0.43F, 0.43F, true}};
    const std::vector evidence{lpr, paddle_reading("34ABC123", 0.92F)};
    const std::vector<domain::PlateCandidate> fused{{"34ABC128", 0.96F, 0.96F, true}};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.plate, "34ABC128");
    EXPECT_FALSE(has_reason(result, domain::RecognitionDecisionReason::secondary_ocr_review_suggestion));
}

TEST(SafeDecisionPolicy, MatchingNonLprSourceIsNotIndependentLprnetBeamSupport) {
    auto non_lpr = good_evidence();
    non_lpr.source = "some-other-ocr";
    non_lpr.candidates = {{"34ABC123", 0.98F, 0.98F, true}};
    const std::vector evidence{non_lpr, paddle_reading("34ABC123", 0.93F)};
    const std::vector<domain::PlateCandidate> fused{{"34ABC128", 0.96F, 0.96F, true}};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.plate, "34ABC128");
    EXPECT_FALSE(has_reason(result, domain::RecognitionDecisionReason::secondary_ocr_review_suggestion));
}

TEST(SafeDecisionPolicy, PaddleBeamAgreementCannotBeUsedWhenPaddleCropsConflict) {
    auto lpr = good_evidence();
    lpr.candidates = {{"34ABC128", 0.97F, 0.97F, true},
                      {"34ABC123", 0.55F, 0.55F, true},
                      {"34ABC124", 0.53F, 0.53F, true}};
    const std::vector evidence{
        lpr, paddle_reading("34ABC123", 0.96F), paddle_reading("34ABC124", 0.95F)};
    const std::vector<domain::PlateCandidate> fused{{"34ABC128", 0.97F, 0.97F, true}};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.plate, "34ABC128");
    EXPECT_FALSE(has_reason(result, domain::RecognitionDecisionReason::secondary_ocr_review_suggestion));
}

TEST(SafeDecisionPolicy, PaddleBeamSupportIsEnabledByDefaultAndCanSuggestReview) {
    auto lpr = good_evidence();
    lpr.candidates = {{"34ABC128", 0.97F, 0.97F, true},
                      {"34ABC123", 0.45F, 0.45F, true}};
    const std::vector evidence{lpr, paddle_reading("34ABC123", 0.93F)};
    const std::vector<domain::PlateCandidate> fused{{"34ABC128", 0.97F, 0.97F, true}};
    const auto result = policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.plate, "34ABC123");
    EXPECT_TRUE(has_reason(result, domain::RecognitionDecisionReason::secondary_ocr_review_suggestion));
}

TEST(SafeDecisionPolicy, PaddleBeamSupportCannotOverrideInvalidCandidate) {
    auto lpr = good_evidence();
    lpr.candidates = {{"34ABC128", 0.97F, 0.97F, true},
                      {"34ABC123", 0.45F, 0.45F, false}};
    const std::vector evidence{lpr, paddle_reading("34ABC123", 0.92F)};
    const std::vector<domain::PlateCandidate> fused{{"34ABC128", 0.97F, 0.97F, true}};
    const auto result = paddle_policy().decide(good_detection(), evidence, fused, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
    EXPECT_EQ(result.plate, "34ABC128");
}

TEST(SafeDecisionPolicy, PaddleBeamSupportDoesNotBypassDetectorOrFatalFailure) {
    auto lpr = good_evidence();
    lpr.candidates = {{"34ABC128", 0.97F, 0.97F, true},
                      {"34ABC123", 0.45F, 0.45F, true}};
    const std::vector evidence{lpr, paddle_reading("34ABC123", 0.91F)};
    const std::vector<domain::PlateCandidate> fused{{"34ABC128", 0.97F, 0.97F, true}};
    auto weak = good_detection();
    weak.confidence = 0.1F;
    EXPECT_EQ(paddle_policy().decide(weak, evidence, fused, {}).status,
              domain::RecognitionStatus::rejected);
    application::RecognitionDecisionContext fatal{};
    fatal.fatal_provider_failure = true;
    EXPECT_EQ(paddle_policy().decide(good_detection(), evidence, fused, fatal).status,
              domain::RecognitionStatus::rejected);
}

application::SafeRecognitionDecisionPolicy standard_policy() {
    return application::SafeRecognitionDecisionPolicy{};
}

TEST(SafeDecisionPolicy, StandardTopOneNeedsStrongIndependentReadingAndMargin) {
    auto lpr = good_evidence();
    lpr.candidates = {{"34ABC123", 0.94F, 0.94F, true},
                      {"34ABC128", 0.79F, 0.79F, true}};
    const std::vector evidence{lpr, paddle_reading("34ABC123", 0.95F)};
    EXPECT_EQ(paddle_policy().decide(good_detection(), evidence, lpr.candidates, {}).status,
              domain::RecognitionStatus::review);
    const auto result = standard_policy().decide(good_detection(), evidence, lpr.candidates, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::accepted);
    EXPECT_EQ(result.plate, "34ABC123");
    EXPECT_FLOAT_EQ(result.confidence, 0.94F);
    EXPECT_TRUE(has_reason(
        result, domain::RecognitionDecisionReason::accepted_cross_provider_corroboration));
}

TEST(SafeDecisionPolicy, StandardNonTopOneRequiresTightTieAndNativeBeamSupport) {
    auto lpr = good_evidence();
    lpr.candidates = {{"34ABC128", 0.91F, 0.91F, true},
                      {"34ABC123", 0.88F, 0.88F, true}};
    const auto result = standard_policy().decide(good_detection(),
        std::vector{lpr, paddle_reading("34ABC123", 0.95F)}, lpr.candidates, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::accepted);
    EXPECT_EQ(result.plate, "34ABC123");
    EXPECT_FLOAT_EQ(result.confidence, 0.88F);
    lpr.candidates[1].confidence = 0.50F;
    lpr.candidates[1].calibrated_confidence = 0.50F;
    EXPECT_NE(standard_policy().decide(good_detection(),
        std::vector{lpr, paddle_reading("34ABC123", 0.95F)},
        lpr.candidates, {}).status, domain::RecognitionStatus::accepted);
}

TEST(SafeDecisionPolicy, ConditionalTopFiveBeamMatchCanAccept) {
    auto lpr = good_evidence();
    lpr.candidates = {{"34ABC128", 0.92F, 0.92F, true},
                      {"34ABC129", 0.91F, 0.91F, true},
                      {"34ABC120", 0.90F, 0.90F, true},
                      {"34ABC123", 0.89F, 0.89F, true},
                      {"34ABC124", 0.88F, 0.88F, true}};
    const auto accepted = standard_policy().decide(good_detection(),
        std::vector{lpr, paddle_reading("34ABC123", 0.96F)}, lpr.candidates, {});
    EXPECT_EQ(accepted.status, domain::RecognitionStatus::accepted);
    EXPECT_EQ(accepted.plate, "34ABC123");
    EXPECT_FLOAT_EQ(accepted.confidence, 0.89F);
    lpr.candidates[3] = {"34ABC123", 0.79F, 0.79F, true};
    EXPECT_NE(standard_policy().decide(good_detection(),
        std::vector{lpr, paddle_reading("34ABC123", 0.96F)},
        lpr.candidates, {}).status, domain::RecognitionStatus::accepted);
    lpr.candidates[3] = {"34ABC123", 0.89F, 0.89F, true};
    lpr.candidates.push_back({"34ABC125", 0.88F, 0.88F, true});
    EXPECT_NE(standard_policy().decide(good_detection(),
        std::vector{lpr, paddle_reading("34ABC125", 0.96F)},
        lpr.candidates, {}).status, domain::RecognitionStatus::accepted);
}

TEST(SafeDecisionPolicy, StandardLargeGapOrConflictingPaddleNeverAutoAccepts) {
    auto lpr = good_evidence();
    lpr.candidates = {{"34ABC128", 0.96F, 0.96F, true},
                      {"34ABC123", 0.80F, 0.80F, true}};
    EXPECT_EQ(standard_policy().decide(good_detection(),
        std::vector{lpr, paddle_reading("34ABC123", 0.95F)},
        lpr.candidates, {}).status, domain::RecognitionStatus::review);
    lpr.candidates[0] = {"34ABC128", 0.90F, 0.90F, true};
    lpr.candidates[1] = {"34ABC123", 0.88F, 0.88F, true};
    EXPECT_NE(standard_policy().decide(good_detection(),
        std::vector{lpr, paddle_reading("34ABC123", 0.95F),
                    paddle_reading("34ABC128", 0.82F)},
        lpr.candidates, {}).status, domain::RecognitionStatus::accepted);
}

TEST(SafeDecisionPolicy, StandardStrongGreedyDisagreementPreventsAcceptance) {
    auto lpr = good_evidence();
    lpr.greedy_text = "34ABC128";
    lpr.greedy_character_confidences.assign(8U, 0.99F);
    lpr.candidates = {{"34ABC128", 0.91F, 0.91F, true},
                      {"34ABC123", 0.89F, 0.89F, true}};
    EXPECT_NE(standard_policy().decide(good_detection(),
        std::vector{lpr, paddle_reading("34ABC123", 0.96F)},
        lpr.candidates, {}).status, domain::RecognitionStatus::accepted);
}

TEST(SafeDecisionPolicy, StandardRequiresHealthyProvidersAndDetection) {
    auto lpr = good_evidence();
    lpr.candidates = {{"34ABC128", 0.91F, 0.91F, true},
                      {"34ABC123", 0.89F, 0.89F, true}};
    const std::vector evidence{lpr, paddle_reading("34ABC123", 0.95F)};
    application::RecognitionDecisionContext degraded{};
    degraded.degraded = true;
    EXPECT_NE(standard_policy().decide(good_detection(), evidence, lpr.candidates, degraded).status,
              domain::RecognitionStatus::accepted);
    auto geometry = good_detection();
    geometry.geometry_score = 0.0F;
    EXPECT_NE(standard_policy().decide(geometry, evidence, lpr.candidates, {}).status,
              domain::RecognitionStatus::accepted);
    auto detector = good_detection();
    detector.confidence = 0.1F;
    EXPECT_EQ(standard_policy().decide(detector, evidence, lpr.candidates, {}).status,
              domain::RecognitionStatus::rejected);
}

TEST(SafeDecisionPolicy, StandardPolicyRejectsInvalidAcceptanceThreshold) {
    application::DecisionConfig config{};
    config.paddle_accept_minimum_lpr_score = 0.50F;
    EXPECT_THROW(application::SafeRecognitionDecisionPolicy{config},
                 application::ConfigurationError);
}

} // namespace
