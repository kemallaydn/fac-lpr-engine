#include <fac_lpr/application/safe_decision_policy.hpp>

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

} // namespace
