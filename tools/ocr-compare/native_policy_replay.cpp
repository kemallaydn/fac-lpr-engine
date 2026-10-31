// Actual compiled C++ fusion + decision policy applied to real LPRNet and
// PaddleOCR predictions on known-label SYNTHETIC PLATE CROPS.
// Explicit limitations: LPRNet greedy CTC (NOT production constrained beam),
// fixed synthetic detector/crop evidence (NOT YOLO/camera E2E), uncalibrated
// raw model scores (NOT measured field probabilities).
#include <fac_lpr/application/candidate_fusion.hpp>
#include <fac_lpr/application/safe_decision_policy.hpp>
#include <fac_lpr/application/turkish_plate_grammar.hpp>
#include <fac_lpr/domain/recognition.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace application = fac_lpr::application;
namespace domain = fac_lpr::domain;

struct Counters {
    int total{0};
    int accepted_correct{0};
    int accepted_wrong{0};
    int review_correct{0};
    int review_wrong{0};
    int rejected{0};
    int review_suggestion_correct{0};
    int review_suggestion_wrong{0};
    int primary_correct{0};
    int paddle_correct{0};
};

std::vector<std::string> split(const std::string& text, const char delimiter) {
    std::vector<std::string> fields{};
    std::stringstream input{text};
    std::string item;
    while (std::getline(input, item, delimiter)) {
        fields.push_back(item);
    }
    // Preserve an empty final TSV column if needed.
    if (!text.empty() && text.back() == delimiter) fields.emplace_back();
    return fields;
}

float probability(const std::string& text) {
    if (text.empty()) throw std::runtime_error("missing probability");
    std::size_t consumed = 0U;
    const float value = std::stof(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value) || value < 0.0F || value > 1.0F) {
        throw std::runtime_error("invalid probability: " + text);
    }
    return value;
}

std::string json_quote(const std::string& raw) {
    std::string out{"\""};
    for (const char c : raw) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20U) {
                    throw std::runtime_error("invalid control in JSON string");
                }
                out.push_back(c);
        }
    }
    out += "\"";
    return out;
}
const char* status_name(const domain::RecognitionStatus value) {
    switch (value) {
        case domain::RecognitionStatus::accepted: return "ACCEPTED";
        case domain::RecognitionStatus::review: return "REVIEW";
        case domain::RecognitionStatus::rejected: return "REJECTED";
    }
    throw std::runtime_error("invalid status");
}

bool is_suggestion(const domain::PlateRecognitionResult& result) {
    const auto& reasons = result.decision_reasons;
    return std::find(reasons.begin(), reasons.end(),
                     domain::RecognitionDecisionReason::secondary_ocr_review_suggestion)
           != reasons.end();
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: policy_inference_report predictions.tsv report.json\n";
        return 2;
    }
    try {
        std::ifstream input{argv[1]};
        if (!input) throw std::runtime_error("missing actual-inference TSV");
        std::ofstream output{argv[2]};
        if (!output) throw std::runtime_error("cannot open JSON report");
        const application::TurkishPlateGrammar grammar{};
        application::CandidateFusionConfig fusion_config{};
        fusion_config.source_weights["paddleocr"] = 0.0F;
        const application::WeightedMultiCropCandidateFusion fusion{fusion_config};
        application::DecisionConfig policy_config{};
        policy_config.enable_paddle_review_suggestions = true;
        const application::SafeRecognitionDecisionPolicy policy{policy_config};

        domain::Detection detection{};
        detection.bbox = {0.0F, 0.0F, 520.0F, 132.0F};
        detection.confidence = 0.95F;
        detection.geometry_score = 0.90F;
        detection.provider = "synthetic_upstream_assumption";

        Counters counts{};
        std::ostringstream cases{};
        cases << "[";
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty()) continue;
            const auto fields = split(line, '\t');
            if (fields.size() != 7U) {
                throw std::runtime_error("expected 7 TSV fields, got " +
                                         std::to_string(fields.size()));
            }
            const auto& filename = fields[0];
            const auto& truth = fields[1];
            const auto& lpr_text = fields[2];
            const float lpr_confidence = probability(fields[3]);
            const auto& paddle_text = fields[5];
            const float paddle_confidence = probability(fields[6]);
            if (!grammar.is_valid_normalized(truth)) {
                throw std::runtime_error("invalid known-label plate");
            }

            domain::RecognitionEvidence lpr{};
            lpr.source = "lprnet_onnx";
            lpr.crop_quality = 0.90F;
            lpr.greedy_text = lpr_text;
            if (!fields[4].empty()) {
                for (const auto& score : split(fields[4], ';')) {
                    lpr.greedy_character_confidences.push_back(probability(score));
                }
            }
            if (lpr.greedy_character_confidences.size() != lpr_text.size()) {
                throw std::runtime_error("greedy scores/text alignment mismatch");
            }
            if (!lpr_text.empty()) {
                lpr.candidates.push_back(domain::PlateCandidate{
                    .text = lpr_text,
                    .confidence = lpr_confidence,
                    .calibrated_confidence = lpr_confidence,
                    .format_valid = grammar.is_valid_normalized(lpr_text)});
            }

            domain::RecognitionEvidence paddle{};
            paddle.source = "paddleocr";
            paddle.crop_quality = 0.90F;
            if (!paddle_text.empty()) {
                paddle.candidates.push_back(domain::PlateCandidate{
                    .text = paddle_text,
                    .confidence = paddle_confidence,
                    .calibrated_confidence = paddle_confidence,
                    .format_valid = grammar.is_valid_normalized(paddle_text)});
            }
            const std::vector<domain::RecognitionEvidence> evidence{lpr, paddle};
            const auto fused = fusion.fuse(evidence, {});
            const auto decision = policy.decide(detection, evidence, fused, {});

            ++counts.total;
            const bool correct = decision.plate == truth;
            if (lpr_text == truth) ++counts.primary_correct;
            if (paddle_text == truth) ++counts.paddle_correct;
            if (decision.status == domain::RecognitionStatus::accepted) {
                if (correct) ++counts.accepted_correct;
                else ++counts.accepted_wrong;
            } else if (decision.status == domain::RecognitionStatus::review) {
                if (correct) ++counts.review_correct;
                else ++counts.review_wrong;
            } else ++counts.rejected;
            if (is_suggestion(decision)) {
                if (correct) ++counts.review_suggestion_correct;
                else ++counts.review_suggestion_wrong;
            }
            if (counts.total > 1) cases << ",";
            cases << "{\"file\":" << json_quote(filename)
                  << ",\"expected\":" << json_quote(truth)
                  << ",\"lprnetGreedy\":" << json_quote(lpr_text)
                  << ",\"paddle\":" << json_quote(paddle_text)
                  << ",\"decisionPlate\":" << json_quote(decision.plate)
                  << ",\"status\":" << json_quote(status_name(decision.status))
                  << ",\"correct\":" << (correct ? "true" : "false")
                  << ",\"secondarySuggestion\":" << (is_suggestion(decision) ? "true" : "false")
                  << "}";
            std::cout << filename << " expected=" << truth
                      << " lprnet=" << lpr_text << " paddle=" << paddle_text
                      << " final=" << decision.plate
                      << " status=" << status_name(decision.status)
                      << " correct=" << (correct ? "true" : "false") << '\n';
        }
        cases << "]";
        if (counts.total != 32) {
            throw std::runtime_error("expected 32 real inference rows, got " +
                                     std::to_string(counts.total));
        }
        output << "{\n"
               << "\"kind\":\"real-model-crop-native-cpp-policy-replay\",\n"
               << "\"notRealCameraOrProductionBeam\":true,\n"
               << "\"total\":" << counts.total << ",\n"
               << "\"lprnetGreedyCorrect\":" << counts.primary_correct << ",\n"
               << "\"paddleCorrect\":" << counts.paddle_correct << ",\n"
               << "\"acceptedCorrect\":" << counts.accepted_correct << ",\n"
               << "\"acceptedWrong\":" << counts.accepted_wrong << ",\n"
               << "\"reviewCorrect\":" << counts.review_correct << ",\n"
               << "\"reviewWrong\":" << counts.review_wrong << ",\n"
               << "\"rejected\":" << counts.rejected << ",\n"
               << "\"reviewSuggestionCorrect\":" << counts.review_suggestion_correct << ",\n"
               << "\"reviewSuggestionWrong\":" << counts.review_suggestion_wrong << ",\n"
               << "\"cases\":" << cases.str() << "\n}\n";
        output.flush();
        if (!output) throw std::runtime_error("report write failed");
        std::cout << "NATIVE_POLICY_RESULT total=" << counts.total
                  << " lprnet=" << counts.primary_correct
                  << " paddle=" << counts.paddle_correct
                  << " accepted_correct=" << counts.accepted_correct
                  << " accepted_wrong=" << counts.accepted_wrong
                  << " review_correct=" << counts.review_correct
                  << " review_wrong=" << counts.review_wrong
                  << " rejected=" << counts.rejected
                  << " suggestion_correct=" << counts.review_suggestion_correct
                  << " suggestion_wrong=" << counts.review_suggestion_wrong << '\n';
        // Unsafe automatically accepted incorrect plates are always a hard gate.
        return counts.accepted_wrong == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "NATIVE_POLICY_FATAL " << error.what() << '\n';
        return 2;
    }
}
