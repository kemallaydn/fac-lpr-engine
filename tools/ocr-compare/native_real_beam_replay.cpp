// Replay actual ONNX logits from three real vehicle photos through the
// production C++ ConstrainedCtcBeamSearch. Crop is Python YOLO bounding box,
// not C++ perspective/multi-crop pipeline. These are diagnostic OCR results.
#include <fac_lpr/application/turkish_plate_grammar.hpp>
#include <fac_lpr/infrastructure/lprnet/constrained_ctc_beam_search.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace lpr = fac_lpr::infrastructure::lprnet;
namespace domain = fac_lpr::domain;

std::vector<std::string> split(const std::string& raw, char separator) {
    std::stringstream input{raw};
    std::vector<std::string> fields{};
    std::string cell;
    while (std::getline(input, cell, separator)) fields.push_back(cell);
    if (!raw.empty() && raw.back() == separator) fields.emplace_back();
    return fields;
}

std::map<std::string, std::string> contract_file(const std::string& filename) {
    std::ifstream input{filename};
    if (!input) throw std::runtime_error("Cannot load pinned LPR contract");
    std::map<std::string, std::string> contract{};
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) throw std::runtime_error("Invalid contract line");
        contract[line.substr(0, equal)] = line.substr(equal + 1U);
    }
    return contract;
}

std::vector<float> load_logits(const std::string& filename) {
    constexpr std::size_t count = 24U * 34U;
    constexpr std::size_t size = count * sizeof(float);
    if (!std::filesystem::is_regular_file(filename) ||
        std::filesystem::file_size(filename) != size) {
        throw std::runtime_error("Missing or incorrect shape logits " + filename);
    }
    std::ifstream input{filename, std::ios::binary};
    std::vector<float> values(count);
    input.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(size));
    if (!input) throw std::runtime_error("Could not read native logits");
    for (const float v : values)
        if (!std::isfinite(v)) throw std::runtime_error("Non-finite model output");
    return values;
}

std::string json_quote(const std::string& raw) {
    std::string answer{"\""};
    for (const char v : raw) {
        if (v == '"' || v == '\\') answer.push_back('\\');
        if (static_cast<unsigned char>(v) < 0x20U)
            throw std::runtime_error("Unexpected control character");
        answer.push_back(v);
    }
    answer.push_back('"');
    return answer;
}

lpr::ConstrainedCtcBeamSearch configured_decoder(const std::map<std::string,std::string>& cfg) {
    lpr::ConstrainedCtcBeamSearchConfig c{};
    for (char v : cfg.at("ocr.charset")) c.ctc.charset.push_back(v);
    c.ctc.blank_index = std::stoul(cfg.at("ocr.blank_index"));
    c.ctc.maximum_timesteps = std::stoul(cfg.at("ocr.maximum_timesteps"));
    c.ctc.maximum_classes = std::stoul(cfg.at("ocr.maximum_classes"));
    c.beam_width = std::stoul(cfg.at("ocr.beam_width"));
    c.result_limit = std::stoul(cfg.at("ocr.result_limit"));
    c.classes_per_step = std::stoul(cfg.at("ocr.classes_per_step"));
    c.confusion_weight = std::stof(cfg.at("ocr.confusion_weight"));
    if (c.ctc.charset.size() != 33U || c.ctc.blank_index != 33U ||
        c.ctc.maximum_timesteps != 24U || c.beam_width != 16U ||
        c.result_limit != 5U || c.classes_per_step != 6U)
        throw std::runtime_error("Unexpected active LPRNet model/decoder contract");
    return lpr::ConstrainedCtcBeamSearch{c};
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "Usage: native-real-beam input.tsv contract.ini result.json result.md\n";
        return 2;
    }
    try {
        const auto decoder = configured_decoder(contract_file(argv[2]));
        std::ifstream input{argv[1]};
        std::ofstream json{argv[3]};
        std::ofstream markdown{argv[4]};
        if (!input || !json || !markdown) throw std::runtime_error("Report input/output unavailable");
        const fac_lpr::application::TurkishPlateGrammar grammar{};
        unsigned total = 0;
        unsigned greedy_exact = 0;
        unsigned top1_exact = 0;
        unsigned top5_contains = 0;
        unsigned no_valid = 0;
        std::set<std::string> filenames{};
        json << "{\"kind\":\"real-photo-native-cpp-constrained-beam\","
             << "\"notProductionPerspectiveOrMultiCrop\":true,\"cases\":[";
        markdown << "# Three real car photos: native LPRNet C++ beam search\n\n"
                 << "YOLO bounding-box crop, real ONNX inference, **actual native C++ "
                    "production beam decoder**. This is NOT full C++ engine, "
                    "perspective alignment, multi-crop fusion, or final acceptance.\n\n"
                 << "| Photo | Expected | Greedy CTC | Native beam top-1 | "
                    "Native beam confidence (uncalibrated) | Expected in top-5 |\n"
                 << "|---|---|---|---|---:|---|\n";
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty()) continue;
            const auto row = split(line, '\t');
            if (row.size() != 6U) throw std::runtime_error("Expected six native input columns");
            const auto& filename = row[0];
            const auto& truth = row[1];
            if (!filenames.insert(filename).second)
                throw std::runtime_error("Duplicate real photo fixture");
            if (!grammar.is_valid_normalized(truth))
                throw std::runtime_error("Invalid ground-truth plate");
            const auto values = load_logits(row[2]);
            const auto decoded = decoder.decode(values, 24U, 34U);
            if (decoded.greedy.text != row[3])
                throw std::runtime_error("Python/native greedy mismatch");
            const auto top1 = decoded.candidates.empty() ? std::string{} : decoded.candidates.front().text;
            const float confidence = decoded.candidates.empty() ? 0.0F : decoded.candidates.front().confidence;
            const bool valid_top1 = !decoded.candidates.empty() && decoded.candidates.front().format_valid;
            const bool found = std::any_of(
                decoded.candidates.begin(), decoded.candidates.end(),
                [&truth](const domain::PlateCandidate& candidate) {
                    return candidate.format_valid && candidate.text == truth;
                });
            const bool any_valid = std::any_of(
                decoded.candidates.begin(), decoded.candidates.end(),
                [](const domain::PlateCandidate& candidate) {return candidate.format_valid;});
            if (total++) json << ",";
            json << "{\"file\":" << json_quote(filename)
                 << ",\"expected\":" << json_quote(truth)
                 << ",\"greedy\":" << json_quote(decoded.greedy.text)
                 << ",\"greedyConfidence\":" << decoded.greedy.confidence
                 << ",\"beamTop1\":" << json_quote(top1)
                 << ",\"beamTop1Confidence\":" << confidence
                 << ",\"beamTop1Valid\":" << (valid_top1 ? "true":"false")
                 << ",\"truthInTop5\":" << (found ? "true":"false")
                 << ",\"fallbackUsed\":" << (decoded.fallback_used ? "true":"false")
                 << ",\"candidates\":[";
            for (std::size_t j=0; j<decoded.candidates.size(); ++j) {
                const auto& candidate = decoded.candidates[j];
                if (j) json << ",";
                json << "{\"text\":" << json_quote(candidate.text)
                     << ",\"confidence\":" << candidate.confidence
                     << ",\"valid\":" << (candidate.format_valid?"true":"false") << "}";
            }
            json << "]}";
            greedy_exact += unsigned(decoded.greedy.text == truth);
            top1_exact += unsigned(valid_top1 && top1 == truth);
            top5_contains += unsigned(found);
            no_valid += unsigned(!any_valid);
            markdown << "| " << filename << " | " << truth
                     << " | " << decoded.greedy.text << " | " << top1
                     << " | " << std::fixed << std::setprecision(4) << confidence
                     << " | " << (found?"yes":"no") << " |\n";
            std::cout << "REAL_BEAM_CASE file=" << filename << " truth=" << truth
                      << " greedy=" << decoded.greedy.text
                      << " beam=" << top1 << " score=" << confidence
                      << " top5=" << (found?"yes":"no") << '\n';
        }
        if (total != 3U || filenames != std::set<std::string>{
                "plate_01.jpg", "plate_02.jpg", "plate_03.jpg"}) {
            throw std::runtime_error("Test did not cover all three real fixture photos");
        }
        json << "],\"summary\":{\"total\":" << total
             << ",\"greedyExact\":" << greedy_exact
             << ",\"beamTop1Exact\":" << top1_exact
             << ",\"beamTop5ContainsTruth\":" << top5_contains
             << ",\"noValidCandidates\":" << no_valid << "}}\n";
        markdown << "\nGreedy exact: **" << greedy_exact << "/3**, beam top-1 exact: **"
                 << top1_exact << "/3**, ground truth in beam top-5: **"
                 << top5_contains << "/3**, no valid candidate: **"
                 << no_valid << "/3**.\n";
        if (!json || !markdown) throw std::runtime_error("Could not write reports");
        std::cout << "REAL_BEAM_RESULT total=3 greedy=" << greedy_exact
                  << " beam_top1=" << top1_exact
                  << " truth_in_top5=" << top5_contains
                  << " no_valid=" << no_valid << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "REAL_BEAM_FATAL " << e.what() << '\n';
        return 2;
    }
}
