#include <fac_lpr/c_api/result_buffer.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/c_api/error_boundary.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <string_view>

namespace fac_lpr::c_api {
namespace {

constexpr std::size_t wire_alignment = 4U;

[[nodiscard]] std::uint32_t to_u32(const std::size_t value, const char* field) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw application::ResourceExhaustedError(std::string{field} + " exceeds v1 32-bit wire limit");
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::size_t checked_array_bytes(
    const std::size_t count,
    const std::size_t element_size,
    const char* field) {
    if (element_size != 0U && count > std::numeric_limits<std::size_t>::max() / element_size) {
        throw application::ResourceExhaustedError(std::string{field} + " overflows size_t");
    }
    return count * element_size;
}

[[nodiscard]] std::uint32_t element_offset(
    const std::uint32_t base,
    const std::size_t index,
    const std::size_t element_size) {
    const auto delta = checked_array_bytes(index, element_size, "result element offset");
    const auto value = static_cast<std::size_t>(base) + delta;
    return to_u32(value, "result element offset");
}

[[nodiscard]] float finite_float(const float value, const char* field) {
    if (!std::isfinite(value)) {
        throw application::ProviderError(std::string{field} + " must be finite");
    }
    return value;
}

[[nodiscard]] float probability_float(const float value, const char* field) {
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
        throw application::ProviderError(std::string{field} + " must be in [0,1]");
    }
    return value;
}

[[nodiscard]] float latency_to_float(const double value) {
    if (!std::isfinite(value) || value < 0.0 ||
        value > static_cast<double>(std::numeric_limits<float>::max())) {
        throw application::ProviderError("result latency is outside v1 wire range");
    }
    return static_cast<float>(value);
}

[[nodiscard]] fac_lpr_recognition_status_v1 map_status(
    const domain::RecognitionStatus value) {
    switch (value) {
        case domain::RecognitionStatus::accepted: return FAC_LPR_RECOGNITION_ACCEPTED_V1;
        case domain::RecognitionStatus::review: return FAC_LPR_RECOGNITION_REVIEW_V1;
        case domain::RecognitionStatus::rejected: return FAC_LPR_RECOGNITION_REJECTED_V1;
    }
    throw application::InternalError("unknown recognition status");
}

[[nodiscard]] fac_lpr_decision_reason_v1 map_reason(
    const domain::RecognitionDecisionReason value) {
    switch (value) {
        case domain::RecognitionDecisionReason::accepted_consensus:
            return FAC_LPR_REASON_ACCEPTED_CONSENSUS_V1;
        case domain::RecognitionDecisionReason::fatal_provider_failure:
            return FAC_LPR_REASON_FATAL_PROVIDER_FAILURE_V1;
        case domain::RecognitionDecisionReason::degraded_provider_set:
            return FAC_LPR_REASON_DEGRADED_PROVIDER_SET_V1;
        case domain::RecognitionDecisionReason::detector_confidence_below_minimum:
            return FAC_LPR_REASON_DETECTOR_CONFIDENCE_BELOW_MINIMUM_V1;
        case domain::RecognitionDecisionReason::geometry_below_minimum:
            return FAC_LPR_REASON_GEOMETRY_BELOW_MINIMUM_V1;
        case domain::RecognitionDecisionReason::crop_quality_below_minimum:
            return FAC_LPR_REASON_CROP_QUALITY_BELOW_MINIMUM_V1;
        case domain::RecognitionDecisionReason::no_valid_candidate:
            return FAC_LPR_REASON_NO_VALID_CANDIDATE_V1;
        case domain::RecognitionDecisionReason::candidate_confidence_below_review:
            return FAC_LPR_REASON_CANDIDATE_CONFIDENCE_BELOW_REVIEW_V1;
        case domain::RecognitionDecisionReason::candidate_confidence_below_accept:
            return FAC_LPR_REASON_CANDIDATE_CONFIDENCE_BELOW_ACCEPT_V1;
        case domain::RecognitionDecisionReason::conflicting_strong_candidates:
            return FAC_LPR_REASON_CONFLICTING_STRONG_CANDIDATES_V1;
    }
    throw application::InternalError("unknown decision reason");
}

class BufferWriter final {
public:
    BufferWriter(void* buffer, const std::size_t capacity)
        : bytes_(static_cast<std::byte*>(buffer)), capacity_(capacity) {}

    [[nodiscard]] std::uint32_t reserve(const std::size_t bytes) {
        const auto aligned = align_position(position_);
        if (bytes > std::numeric_limits<std::size_t>::max() - aligned) {
            throw application::ResourceExhaustedError("result buffer size overflows size_t");
        }
        const auto end = aligned + bytes;
        if (end > std::numeric_limits<std::uint32_t>::max()) {
            throw application::ResourceExhaustedError("result buffer exceeds v1 32-bit offset range");
        }
        if (bytes_ != nullptr && end > capacity_) {
            throw application::ResourceExhaustedError("internal result writer exceeded caller capacity");
        }
        position_ = end;
        return static_cast<std::uint32_t>(aligned);
    }

    template <typename T>
    void write(const std::uint32_t offset, const T& value) {
        if (bytes_ == nullptr) {
            return;
        }
        const auto begin = static_cast<std::size_t>(offset);
        if (begin > capacity_ || sizeof(T) > capacity_ - begin) {
            throw application::ResourceExhaustedError("internal result write exceeded caller capacity");
        }
        std::memcpy(bytes_ + begin, &value, sizeof(T));
    }

    void write_bytes(const std::uint32_t offset, const std::span<const std::byte> value) {
        if (bytes_ == nullptr || value.empty()) {
            return;
        }
        const auto begin = static_cast<std::size_t>(offset);
        if (begin > capacity_ || value.size() > capacity_ - begin) {
            throw application::ResourceExhaustedError("internal text write exceeded caller capacity");
        }
        std::memcpy(bytes_ + begin, value.data(), value.size());
    }

    [[nodiscard]] std::size_t size() const noexcept { return position_; }

private:
    [[nodiscard]] static std::size_t align_position(const std::size_t value) {
        const auto remainder = value % wire_alignment;
        if (remainder == 0U) {
            return value;
        }
        const auto padding = wire_alignment - remainder;
        if (padding > std::numeric_limits<std::size_t>::max() - value) {
            throw application::ResourceExhaustedError("result alignment overflows size_t");
        }
        return value + padding;
    }

    std::byte* bytes_{nullptr};
    std::size_t capacity_{0U};
    std::size_t position_{0U};
};

[[nodiscard]] fac_lpr_text_ref_v1 append_text(BufferWriter& writer, const std::string_view text) {
    if (text.empty()) {
        return {0U, 0U};
    }
    const auto offset = writer.reserve(text.size());
    writer.write_bytes(offset, std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(text.data()), text.size()});
    return {offset, to_u32(text.size(), "text length")};
}

[[nodiscard]] fac_lpr_candidate_v1 serialize_candidate(
    BufferWriter& writer,
    const domain::PlateCandidate& candidate) {
    return fac_lpr_candidate_v1{
        .struct_size = static_cast<std::uint32_t>(sizeof(fac_lpr_candidate_v1)),
        .abi_version = FAC_LPR_ABI_VERSION_V1,
        .text = append_text(writer, candidate.text),
        .confidence = probability_float(candidate.confidence, "candidate confidence"),
        .calibrated_confidence = probability_float(
            candidate.calibrated_confidence,
            "candidate calibrated confidence"),
        .format_valid = candidate.format_valid ? 1U : 0U,
        .reserved_zero = 0U};
}

[[nodiscard]] std::uint32_t append_candidates(
    BufferWriter& writer,
    const std::span<const domain::PlateCandidate> candidates) {
    if (candidates.empty()) {
        return 0U;
    }
    const auto bytes = checked_array_bytes(
        candidates.size(), sizeof(fac_lpr_candidate_v1), "candidate array size");
    const auto offset = writer.reserve(bytes);
    for (std::size_t index = 0U; index < candidates.size(); ++index) {
        const auto wire = serialize_candidate(writer, candidates[index]);
        writer.write(element_offset(offset, index, sizeof(fac_lpr_candidate_v1)), wire);
    }
    return offset;
}

[[nodiscard]] std::uint32_t append_evidence(
    BufferWriter& writer,
    const std::span<const domain::RecognitionEvidence> evidence) {
    if (evidence.empty()) {
        return 0U;
    }
    const auto bytes = checked_array_bytes(
        evidence.size(), sizeof(fac_lpr_evidence_v1), "evidence array size");
    const auto offset = writer.reserve(bytes);
    for (std::size_t index = 0U; index < evidence.size(); ++index) {
        const auto& item = evidence[index];
        const auto candidates_offset = append_candidates(writer, item.candidates);
        const fac_lpr_evidence_v1 wire{
            .struct_size = static_cast<std::uint32_t>(sizeof(fac_lpr_evidence_v1)),
            .abi_version = FAC_LPR_ABI_VERSION_V1,
            .source = append_text(writer, item.source),
            .crop_quality = probability_float(item.crop_quality, "evidence crop quality"),
            .latency_ms = latency_to_float(item.latency_ms),
            .candidate_count = to_u32(item.candidates.size(), "evidence candidate count"),
            .candidates_offset = candidates_offset};
        writer.write(element_offset(offset, index, sizeof(fac_lpr_evidence_v1)), wire);
    }
    return offset;
}

[[nodiscard]] std::uint32_t append_reasons(
    BufferWriter& writer,
    const std::span<const domain::RecognitionDecisionReason> reasons) {
    if (reasons.empty()) {
        return 0U;
    }
    const auto bytes = checked_array_bytes(
        reasons.size(), sizeof(fac_lpr_decision_reason_v1), "decision reason array size");
    const auto offset = writer.reserve(bytes);
    for (std::size_t index = 0U; index < reasons.size(); ++index) {
        const auto wire = map_reason(reasons[index]);
        writer.write(element_offset(offset, index, sizeof(fac_lpr_decision_reason_v1)), wire);
    }
    return offset;
}

[[nodiscard]] fac_lpr_plate_result_v1 serialize_plate(
    BufferWriter& writer,
    const domain::PlateRecognitionResult& result) {
    fac_lpr_plate_result_v1 wire{};
    wire.struct_size = static_cast<std::uint32_t>(sizeof(fac_lpr_plate_result_v1));
    wire.abi_version = FAC_LPR_ABI_VERSION_V1;
    wire.status = map_status(result.status);
    wire.degraded = result.degraded ? 1U : 0U;
    wire.plate = append_text(writer, result.plate);
    wire.confidence = probability_float(result.confidence, "recognition confidence");
    wire.detector_confidence = probability_float(
        result.detector_confidence,
        "detector confidence");
    wire.geometry_score = probability_float(result.geometry_score, "geometry score");
    wire.crop_quality = probability_float(result.crop_quality, "crop quality");
    wire.total_latency_ms = latency_to_float(result.total_latency_ms);

    if (result.bbox.has_value()) {
        wire.has_bbox = 1U;
        wire.bbox = {
            finite_float(result.bbox->x, "bbox x"),
            finite_float(result.bbox->y, "bbox y"),
            finite_float(result.bbox->width, "bbox width"),
            finite_float(result.bbox->height, "bbox height")};
    }
    if (result.quadrilateral.has_value()) {
        wire.has_quadrilateral = 1U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            wire.quadrilateral.points[index] = {
                finite_float(result.quadrilateral->points[index].x, "quadrilateral x"),
                finite_float(result.quadrilateral->points[index].y, "quadrilateral y")};
            wire.quadrilateral.confidences[index] = probability_float(
                result.quadrilateral->confidences[index],
                "quadrilateral confidence");
        }
    }

    wire.evidence_count = to_u32(result.evidence.size(), "evidence count");
    wire.evidence_offset = append_evidence(writer, result.evidence);
    wire.alternatives_count = to_u32(result.alternatives.size(), "alternative count");
    wire.alternatives_offset = append_candidates(writer, result.alternatives);
    wire.decision_reason_count = to_u32(result.decision_reasons.size(), "decision reason count");
    wire.decision_reasons_offset = append_reasons(writer, result.decision_reasons);
    return wire;
}

[[nodiscard]] std::size_t serialize_impl(
    const application::LprPipelineResult& result,
    void* output_buffer,
    const std::size_t output_capacity) {
    BufferWriter writer{output_buffer, output_capacity};
    const auto root_offset = writer.reserve(sizeof(fac_lpr_result_v1));
    if (root_offset != 0U) {
        throw application::InternalError("v1 result root must start at buffer offset zero");
    }

    std::uint32_t recognitions_offset = 0U;
    if (!result.recognitions.empty()) {
        const auto bytes = checked_array_bytes(
            result.recognitions.size(),
            sizeof(fac_lpr_plate_result_v1),
            "recognition array size");
        recognitions_offset = writer.reserve(bytes);
        for (std::size_t index = 0U; index < result.recognitions.size(); ++index) {
            const auto plate = serialize_plate(writer, result.recognitions[index]);
            writer.write(
                element_offset(recognitions_offset, index, sizeof(fac_lpr_plate_result_v1)),
                plate);
        }
    }

    const fac_lpr_result_v1 root{
        .struct_size = static_cast<std::uint32_t>(sizeof(fac_lpr_result_v1)),
        .abi_version = FAC_LPR_ABI_VERSION_V1,
        .recognition_count = to_u32(result.recognitions.size(), "recognition count"),
        .recognitions_offset = recognitions_offset,
        .total_latency_ms = latency_to_float(result.total_latency_ms),
        .degraded = result.degraded ? 1U : 0U,
        .provider_failure_count = to_u32(result.provider_failure_count, "provider failure count"),
        .reserved_zero = 0U};
    writer.write(root_offset, root);
    return writer.size();
}

} // namespace

fac_lpr_status serialize_result_v1(
    const application::LprPipelineResult& result,
    void* output_buffer,
    const std::size_t output_capacity,
    std::size_t* required_output_size) {
    if (required_output_size == nullptr) {
        return FAC_LPR_STATUS_CONFIGURATION_ERROR;
    }
    *required_output_size = 0U;

    try {
        const auto required = serialize_impl(result, nullptr, 0U);
        *required_output_size = required;
        if (output_buffer == nullptr || output_capacity < required) {
            return FAC_LPR_STATUS_BUFFER_TOO_SMALL;
        }
        const auto written = serialize_impl(result, output_buffer, output_capacity);
        return written == required ? FAC_LPR_STATUS_OK : FAC_LPR_STATUS_INTERNAL_ERROR;
    } catch (const application::EngineError& error) {
        return to_c_status(error.code());
    } catch (const std::bad_alloc&) {
        return FAC_LPR_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return FAC_LPR_STATUS_INTERNAL_ERROR;
    }
}

} // namespace fac_lpr::c_api
