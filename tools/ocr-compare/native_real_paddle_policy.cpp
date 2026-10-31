// Replay 3 labelled real vehicle photo crops through verified LPRNet ONNX
// logits, the production native C++ beam, fusion and review-only Paddle policy.
// This is NOT full production C++ camera/perspective/multi-crop E2E.
#include <fac_lpr/application/candidate_fusion.hpp>
#include <fac_lpr/application/safe_decision_policy.hpp>
#include <fac_lpr/application/turkish_plate_grammar.hpp>
#include <fac_lpr/infrastructure/lprnet/constrained_ctc_beam_search.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace app = fac_lpr::application;
namespace domain = fac_lpr::domain;
namespace lpr = fac_lpr::infrastructure::lprnet;

std::vector<std::string> split(const std::string& row) {
    std::stringstream in{row};
    std::vector<std::string> result{};
    std::string field;
    while (std::getline(in, field, '\t')) result.push_back(field);
    if (!row.empty() && row.back() == '\t') result.emplace_back();
    return result;
}
std::map<std::string, std::string> read_contract(const std::string& file) {
    std::ifstream in{file};
    if (!in) throw std::runtime_error("model contract missing");
    std::map<std::string,std::string> result{};
    std::string row;
    while (std::getline(in,row)) {
        if (row.empty() || row[0]=='#') continue;
        const auto eq=row.find('=');
        if (eq==std::string::npos) throw std::runtime_error("bad model contract line");
        result[row.substr(0,eq)]=row.substr(eq+1U);
    }
    return result;
}
lpr::ConstrainedCtcBeamSearch beam(const std::map<std::string,std::string>& c) {
    lpr::ConstrainedCtcBeamSearchConfig conf{};
    const auto& charset=c.at("ocr.charset");
    conf.ctc.charset.assign(charset.begin(),charset.end());
    conf.ctc.blank_index=std::stoul(c.at("ocr.blank_index"));
    conf.ctc.maximum_timesteps=std::stoul(c.at("ocr.maximum_timesteps"));
    conf.ctc.maximum_classes=std::stoul(c.at("ocr.maximum_classes"));
    conf.beam_width=std::stoul(c.at("ocr.beam_width"));
    conf.result_limit=std::stoul(c.at("ocr.result_limit"));
    conf.classes_per_step=std::stoul(c.at("ocr.classes_per_step"));
    conf.confusion_weight=std::stof(c.at("ocr.confusion_weight"));
    if (conf.ctc.charset.size()!=33U || conf.ctc.blank_index!=33U ||
        conf.ctc.maximum_timesteps!=24U || conf.ctc.maximum_classes!=34U ||
        conf.beam_width!=16U || conf.result_limit!=5U || conf.classes_per_step!=6U)
        throw std::runtime_error("active model contract changed");
    return lpr::ConstrainedCtcBeamSearch{conf};
}
float probability(const std::string& s) {
    std::size_t consumed=0U;
    const float f=std::stof(s,&consumed);
    if (consumed!=s.size() || !std::isfinite(f) || f<0.0F || f>1.0F)
        throw std::runtime_error("bad OCR confidence");
    return f;
}
std::vector<float> logits(const std::string& path) {
    constexpr std::size_t count=24U*34U;
    if (!std::filesystem::is_regular_file(path) ||
        std::filesystem::file_size(path)!=count*sizeof(float))
        throw std::runtime_error("missing ONNX output/shape mismatch");
    std::ifstream in{path,std::ios::binary};
    std::vector<float> result(count);
    in.read(reinterpret_cast<char*>(result.data()),
            static_cast<std::streamsize>(count*sizeof(float)));
    if (!in || !std::all_of(result.begin(),result.end(),
                           [](float v){return std::isfinite(v);}))
        throw std::runtime_error("invalid ONNX logits");
    return result;
}
bool suggested(const domain::PlateRecognitionResult& result) {
    return std::find(result.decision_reasons.begin(),result.decision_reasons.end(),
        domain::RecognitionDecisionReason::secondary_ocr_review_suggestion)
        !=result.decision_reasons.end();
}
const char* status(domain::RecognitionStatus s) {
    switch (s) {
        case domain::RecognitionStatus::accepted:return "ACCEPTED";
        case domain::RecognitionStatus::review:return "REVIEW";
        case domain::RecognitionStatus::rejected:return "REJECTED";
    }
    throw std::runtime_error("bad recognition status");
}
} // namespace

int main(int argc,char** argv) {
    if (argc!=4) {
        std::cerr<<"usage: native-real-paddle rows.tsv contract.ini summary.md\n";
        return 2;
    }
    try {
        auto decoder=beam(read_contract(argv[2]));
        app::CandidateFusionConfig fusion_config{};
        fusion_config.source_weights["paddleocr"]=0.0F;
        const app::WeightedMultiCropCandidateFusion fusion{fusion_config};
        app::DecisionConfig dc{};
        dc.enable_paddle_review_suggestions=true;
        const app::SafeRecognitionDecisionPolicy policy{dc};
        const app::TurkishPlateGrammar grammar{};
        std::ifstream rows{argv[1]};
        std::ofstream report{argv[3]};
        if (!rows || !report) throw std::runtime_error("input/report unavailable");
        report<<"# Real photos: production C++ beam + Paddle REVIEW corroboration\n\n"
              <<"Real YOLO box crops + ONNX model + native beam and policy. "
              <<"Not production perspective/multi-crop or confidence calibration.\n\n"
              <<"| File | Expected | Greedy | Beam top-1 | Paddle | Decision | Status | Suggestion |\n"
              <<"|---|---|---|---|---|---|---|---|\n";
        unsigned n=0U,greedy=0U,top1=0U,final_correct=0U,
                 accepted_correct=0U,accepted_wrong=0U,review_correct=0U,
                 review_wrong=0U,rejected=0U,secondary_correct=0U;
        std::string row;
        while (std::getline(rows,row)) {
            if (row.empty()) continue;
            const auto fields=split(row);
            if (fields.size()!=6U) throw std::runtime_error("expected 6 fields");
            const auto& filename=fields[0];
            const auto& truth=fields[1];
            if (!grammar.is_valid_normalized(truth))
                throw std::runtime_error("invalid truth");
            const auto decoded=decoder.decode(logits(fields[2]),24U,34U);
            domain::RecognitionEvidence primary{};
            primary.source="lprnet_onnx";
            primary.crop_quality=0.9F; // fixed diagnostic crop quality, not production
            primary.candidates=decoded.candidates;
            primary.greedy_text=decoded.greedy.text;
            primary.greedy_character_confidences=decoded.greedy.character_confidences;
            domain::RecognitionEvidence paddle{};
            paddle.source="paddleocr";
            paddle.crop_quality=0.9F;
            const auto pscore=probability(fields[4]);
            if (!fields[3].empty())
                paddle.candidates.push_back({fields[3],pscore,pscore,
                                              grammar.is_valid_normalized(fields[3])});
            const std::vector evidence{primary,paddle};
            const auto fused=fusion.fuse(evidence,{});
            domain::Detection detection{};
            detection.bbox={0.0F,0.0F,160.0F,40.0F};
            detection.confidence=probability(fields[5]);
            detection.geometry_score=0.9F; // diagnostic assumption
            detection.provider="real_photo_yolo_box";
            const auto result=policy.decide(detection,evidence,fused,{});
            const auto first=decoded.candidates.empty()?"":decoded.candidates.front().text;
            const bool correct=result.plate==truth;
            const bool is_suggestion=suggested(result);
            ++n;
            greedy+=unsigned(decoded.greedy.text==truth);
            top1+=unsigned(first==truth);
            final_correct+=unsigned(correct);
            if (result.status==domain::RecognitionStatus::accepted) {
                accepted_correct+=unsigned(correct);
                accepted_wrong+=unsigned(!correct);
            } else if (result.status==domain::RecognitionStatus::review) {
                review_correct+=unsigned(correct);
                review_wrong+=unsigned(!correct);
            } else ++rejected;
            secondary_correct+=unsigned(is_suggestion && correct);
            report<<"| "<<filename<<" | "<<truth<<" | "<<decoded.greedy.text
                  <<" | "<<first<<" | "<<fields[3]<<" | "<<result.plate
                  <<" | "<<status(result.status)<<" | "
                  <<(is_suggestion?"yes":"no")<<" |\n";
            std::cout<<"REAL_POLICY_CASE file="<<filename
                     <<" expected="<<truth<<" beam="<<first
                     <<" paddle="<<fields[3]<<" decision="<<result.plate
                     <<" status="<<status(result.status)
                     <<" suggested="<<(is_suggestion?"yes":"no")<<"\n";
        }
        if (n!=3U) throw std::runtime_error("expected three labelled real photos");
        report<<"\nRaw greedy: "<<greedy<<"/3; beam top-1: "<<top1
              <<"/3; selected plate correct: "<<final_correct
              <<"/3; accepted correct: "<<accepted_correct
              <<"; **accepted wrong: "<<accepted_wrong
              <<"**; review correct: "<<review_correct
              <<"; review wrong: "<<review_wrong
              <<"; rejected: "<<rejected
              <<"; correct Paddle REVIEW suggestions: "<<secondary_correct<<".\n";
        std::cout<<"REAL_POLICY_RESULT total="<<n
                 <<" greedy="<<greedy<<" beam_top1="<<top1
                 <<" selected_correct="<<final_correct
                 <<" accepted_correct="<<accepted_correct
                 <<" accepted_wrong="<<accepted_wrong
                 <<" review_correct="<<review_correct
                 <<" review_wrong="<<review_wrong
                 <<" rejected="<<rejected
                 <<" suggestion_correct="<<secondary_correct<<"\n";
        if (!report) throw std::runtime_error("report write error");
        return accepted_wrong==0U?0:1;
    } catch (const std::exception& e) {
        std::cerr<<"REAL_POLICY_FATAL "<<e.what()<<"\n";
        return 2;
    }
}
