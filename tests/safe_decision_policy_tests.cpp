#include <fac_lpr/application/safe_decision_policy.hpp>

#include <gtest/gtest.h>

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

TEST(SafeDecisionPolicy, AcceptsOnlyStrongConsistentTechnicalEvidence) {
    const std::vector evidence{good_evidence()};
    const std::vector<domain::PlateCandidate> candidates{{"34ABC123", 0.91F, 0.91F, true}};
    const auto result = policy().decide(good_detection(), evidence, candidates, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::accepted);
    EXPECT_EQ(result.plate, "34ABC123");
    ASSERT_FALSE(result.decision_reasons.empty());
    EXPECT_EQ(result.decision_reasons.back(), domain::RecognitionDecisionReason::accepted_consensus);
}

TEST(SafeDecisionPolicy, StrongConflictingOcrCannotAutoAccept) {
    const std::vector evidence{good_evidence()};
    const std::vector<domain::PlateCandidate> candidates{
        {"34ABC123", 0.93F, 0.93F, true},
        {"34ABC128", 0.82F, 0.82F, true}};
    const auto result = policy().decide(good_detection(), evidence, candidates, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::review);
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
}

TEST(SafeDecisionPolicy, FatalProviderFailureRejectsFailClosed) {
    const std::vector evidence{good_evidence()};
    const std::vector<domain::PlateCandidate> candidates{{"34ABC123", 0.99F, 0.99F, true}};
    application::RecognitionDecisionContext context{};
    context.fatal_provider_failure = true;
    const auto result = policy().decide(good_detection(), evidence, candidates, context);
    EXPECT_EQ(result.status, domain::RecognitionStatus::rejected);
}

TEST(SafeDecisionPolicy, WeakDetectorRejectsEvenWithStrongOcr) {
    auto detection = good_detection();
    detection.confidence = 0.20F;
    const std::vector evidence{good_evidence()};
    const std::vector<domain::PlateCandidate> candidates{{"34ABC123", 0.99F, 0.99F, true}};
    const auto result = policy().decide(detection, evidence, candidates, {});
    EXPECT_EQ(result.status, domain::RecognitionStatus::rejected);
}

} // namespace
