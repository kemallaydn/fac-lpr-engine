#include <fac_lpr/application/lpr_pipeline.hpp>
#include <fac_lpr/c_api/result_buffer.hpp>
#include <fac_lpr/fac_lpr_engine.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

using fac_lpr::application::LprPipelineResult;
using fac_lpr::c_api::serialize_result_v1;
using fac_lpr::domain::BoundingBox;
using fac_lpr::domain::PlateCandidate;
using fac_lpr::domain::PlateRecognitionResult;
using fac_lpr::domain::RecognitionDecisionReason;
using fac_lpr::domain::RecognitionEvidence;
using fac_lpr::domain::RecognitionStatus;

[[nodiscard]] std::string_view text_at(
    const std::vector<std::byte>& buffer,
    const fac_lpr_text_ref_v1 ref) {
    if (ref.length == 0U) {
        return {};
    }
    EXPECT_LE(static_cast<std::size_t>(ref.offset) + ref.length, buffer.size());
    return std::string_view{
        reinterpret_cast<const char*>(buffer.data() + ref.offset),
        ref.length};
}

template <typename T>
[[nodiscard]] const T& record_at(
    const std::vector<std::byte>& buffer,
    const std::uint32_t offset,
    const std::size_t index = 0U) {
    const auto begin = static_cast<std::size_t>(offset) + (index * sizeof(T));
    EXPECT_LE(begin + sizeof(T), buffer.size());
    return *reinterpret_cast<const T*>(buffer.data() + begin);
}

[[nodiscard]] LprPipelineResult sample_result() {
    PlateRecognitionResult recognition{};
    recognition.status = RecognitionStatus::accepted;
    recognition.plate = "34ABC123";
    recognition.confidence = 0.93F;
    recognition.bbox = BoundingBox{10.0F, 20.0F, 100.0F, 30.0F};
    recognition.detector_confidence = 0.90F;
    recognition.geometry_score = 0.88F;
    recognition.crop_quality = 0.81F;
    recognition.total_latency_ms = 12.5;
    recognition.decision_reasons = {RecognitionDecisionReason::accepted_consensus};
    recognition.alternatives = {
        PlateCandidate{"34ABC123", 0.91F, 0.93F, true},
        PlateCandidate{"34A8C123", 0.20F, 0.18F, true}};
    recognition.evidence = {
        RecognitionEvidence{
            .source = "lprnet-primary",
            .candidates = {PlateCandidate{"34ABC123", 0.91F, 0.93F, true}},
            .crop_quality = 0.81F,
            .latency_ms = 4.25}};

    LprPipelineResult result{};
    result.recognitions.push_back(std::move(recognition));
    result.total_latency_ms = 14.0;
    result.provider_failure_count = 0U;
    result.degraded = false;
    return result;
}

TEST(ResultBufferV1, SizeQueryAndExactBufferSerializeDeterministically) {
    const auto result = sample_result();
    std::size_t required = 0U;

    EXPECT_EQ(
        serialize_result_v1(result, nullptr, 0U, &required),
        FAC_LPR_STATUS_BUFFER_TOO_SMALL);
    ASSERT_GT(required, sizeof(fac_lpr_result_v1));

    std::vector<std::byte> buffer(required);
    std::size_t written_required = 0U;
    EXPECT_EQ(
        serialize_result_v1(result, buffer.data(), buffer.size(), &written_required),
        FAC_LPR_STATUS_OK);
    EXPECT_EQ(written_required, required);

    const auto& root = record_at<fac_lpr_result_v1>(buffer, 0U);
    EXPECT_EQ(root.struct_size, sizeof(fac_lpr_result_v1));
    EXPECT_EQ(root.abi_version, FAC_LPR_ABI_VERSION_V1);
    ASSERT_EQ(root.recognition_count, 1U);
    EXPECT_EQ(root.degraded, 0U);

    const auto& plate = record_at<fac_lpr_plate_result_v1>(buffer, root.recognitions_offset);
    EXPECT_EQ(plate.status, FAC_LPR_RECOGNITION_ACCEPTED_V1);
    EXPECT_EQ(text_at(buffer, plate.plate), "34ABC123");
    EXPECT_EQ(plate.has_bbox, 1U);
    EXPECT_FLOAT_EQ(plate.bbox.width, 100.0F);
    ASSERT_EQ(plate.evidence_count, 1U);
    ASSERT_EQ(plate.alternatives_count, 2U);
    ASSERT_EQ(plate.decision_reason_count, 1U);

    const auto& evidence = record_at<fac_lpr_evidence_v1>(buffer, plate.evidence_offset);
    EXPECT_EQ(text_at(buffer, evidence.source), "lprnet-primary");
    ASSERT_EQ(evidence.candidate_count, 1U);
    const auto& evidence_candidate =
        record_at<fac_lpr_candidate_v1>(buffer, evidence.candidates_offset);
    EXPECT_EQ(text_at(buffer, evidence_candidate.text), "34ABC123");

    const auto& alternative =
        record_at<fac_lpr_candidate_v1>(buffer, plate.alternatives_offset, 1U);
    EXPECT_EQ(text_at(buffer, alternative.text), "34A8C123");

    const auto& reason =
        record_at<fac_lpr_decision_reason_v1>(buffer, plate.decision_reasons_offset);
    EXPECT_EQ(reason, FAC_LPR_REASON_ACCEPTED_CONSENSUS_V1);
}

TEST(ResultBufferV1, OneByteTooSmallReturnsExplicitStatusAndRequiredSize) {
    const auto result = sample_result();
    std::size_t required = 0U;
    ASSERT_EQ(
        serialize_result_v1(result, nullptr, 0U, &required),
        FAC_LPR_STATUS_BUFFER_TOO_SMALL);
    ASSERT_GT(required, 0U);

    std::vector<std::byte> buffer(required - 1U);
    std::size_t reported = 0U;
    EXPECT_EQ(
        serialize_result_v1(result, buffer.data(), buffer.size(), &reported),
        FAC_LPR_STATUS_BUFFER_TOO_SMALL);
    EXPECT_EQ(reported, required);
}

TEST(ResultBufferV1, EmptyResultStillHasStableRootRecord) {
    LprPipelineResult result{};
    result.total_latency_ms = 1.0;

    std::size_t required = 0U;
    ASSERT_EQ(
        serialize_result_v1(result, nullptr, 0U, &required),
        FAC_LPR_STATUS_BUFFER_TOO_SMALL);
    EXPECT_EQ(required, sizeof(fac_lpr_result_v1));

    std::vector<std::byte> buffer(required);
    ASSERT_EQ(
        serialize_result_v1(result, buffer.data(), buffer.size(), &required),
        FAC_LPR_STATUS_OK);
    const auto& root = record_at<fac_lpr_result_v1>(buffer, 0U);
    EXPECT_EQ(root.recognition_count, 0U);
    EXPECT_EQ(root.recognitions_offset, 0U);
}

TEST(ResultBufferV1, MisalignedOutputBufferIsRejected) {
    const auto result = sample_result();
    std::size_t required = 0U;
    ASSERT_EQ(
        serialize_result_v1(result, nullptr, 0U, &required),
        FAC_LPR_STATUS_BUFFER_TOO_SMALL);

    std::vector<std::byte> storage(required + FAC_LPR_RESULT_BUFFER_ALIGNMENT_V1);
    auto* misaligned = storage.data() + 1U;
    ASSERT_NE(
        reinterpret_cast<std::uintptr_t>(misaligned) % FAC_LPR_RESULT_BUFFER_ALIGNMENT_V1,
        0U);
    EXPECT_EQ(
        serialize_result_v1(result, misaligned, required, &required),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);
}

} // namespace
