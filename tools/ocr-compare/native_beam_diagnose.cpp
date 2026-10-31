// Actual FAC LPR C++ constrained beam-search diagnostic using pinned ONNX
// logits (not Python reimplementations). Cropped synthetics, not real-camera E2E.
#include <fac_lpr/infrastructure/lprnet/constrained_ctc_beam_search.hpp>
#include <fac_lpr/application/turkish_plate_grammar.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace lpr = fac_lpr::infrastructure::lprnet;

struct Aggregate {
    unsigned count{0};
    unsigned greedy_correct{0};
    unsigned beam_top1_correct{0};
    unsigned beam_topk_contains_truth{0};
    unsigned beam_no_valid_candidate{0};
    unsigned native_fallback{0};
    unsigned pure_deletion{0};
    unsigned high_confidence_wrong{0};
    unsigned blank_steps_sum{0};
};

std::vector<std::string> fields(const std::string& text, const char separator) {
    std::vector<std::string> items{};
    std::stringstream input{text};
    std::string item;
    while (std::getline(input, item, separator)) items.push_back(item);
    if (!text.empty() && text.back() == separator) items.emplace_back();
    return items;
}

std::map<std::string, std::string> read_contract(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("missing production OCR contract");
    std::map<std::string, std::string> config{};
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) throw std::runtime_error("invalid OCR contract");
        config[line.substr(0, equal)] = line.substr(equal + 1U);
    }
    return config;
}

std::string json_quote(const std::string& raw) {
    std::string result{"\""};
    for (const char value : raw) {
        if (value == '"' || value == '\\') result.push_back('\\');
        if (static_cast<unsigned char>(value) < 32U) {
            throw std::runtime_error("unexpected control in JSON field");
        }
        result.push_back(value);
    }
    result.push_back('"');
    return result;
}

bool subsequence(const std::string& shorter, const std::string& longer) {
    if (shorter.size() >= longer.size()) return false;
    std::size_t at = 0U;
    for (const char value : longer) {
        if (at < shorter.size() && shorter[at] == value) ++at;
    }
    return at == shorter.size();
}

std::vector<float> read_logits(const std::string& file) {
    constexpr std::size_t count = 24U * 34U;
    if (!std::filesystem::is_regular_file(file) ||
        std::filesystem::file_size(file) != count * sizeof(float)) {
        throw std::runtime_error("CTC file not found or wrong shape: " + file);
    }
    std::ifstream in(file, std::ios::binary);
    std::vector<float> values(count);
    in.read(reinterpret_cast<char*>(values.data()),
            static_cast<std::streamsize>(values.size() * sizeof(float)));
    if (!in) throw std::runtime_error("CTC logits read failed");
    for (float v : values) if (!std::isfinite(v)) throw std::runtime_error("Nonfinite logits");
    return values;
}

void write_summary(const std::string& path, const std::map<std::string, Aggregate>& totals) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Cannot write diagnostics");
    output << "# LPRNet root-cause diagnostic: real ONNX + native C++ beam\n\n"
           << "32 **synthetic plate crops** in four preprocessing conditions. "
           << "This is **not** camera/YOLO detection or calibrated accuracy. "
           << "Beam is the actual production decoder with configured beam-width/classes; "
           << "no PaddleOCR is used in this ablation.\n\n"
           << "| Variant | Native greedy exact | Native beam top-1 exact | Truth in beam top-5 | "
           << "No valid beam candidate | Fallback | Pure deletions in greedy | "
           << "Mean blank steps / 24 | Wrong beam top-1 with raw confidence >= 0.78 |\n"
           << "|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& [name, s] : totals) {
        if (s.count != 32U) throw std::runtime_error("Expected 32 cases per preprocessing variant");
        output << "| " << name << " | " << s.greedy_correct << "/32 | "
               << s.beam_top1_correct << "/32 | "
               << s.beam_topk_contains_truth << "/32 | "
               << s.beam_no_valid_candidate << "/32 | "
               << s.native_fallback << "/32 | "
               << s.pure_deletion << "/32 | "
               << std::fixed << std::setprecision(1)
               << (static_cast<double>(s.blank_steps_sum) / s.count)
               << " | " << s.high_confidence_wrong << "/32 |\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "Usage: lprnet_native_diagnose input.tsv contract.ini cases.json report.md\n";
        return 2;
    }
    try {
        const auto config = read_contract(argv[2]);
        lpr::ConstrainedCtcBeamSearchConfig beam_config{};
        const auto& charset = config.at("ocr.charset");
        beam_config.ctc.charset.assign(charset.begin(), charset.end());
        beam_config.ctc.blank_index = std::stoul(config.at("ocr.blank_index"));
        beam_config.ctc.maximum_classes = std::stoul(config.at("ocr.maximum_classes"));
        beam_config.ctc.maximum_timesteps = std::stoul(config.at("ocr.maximum_timesteps"));
        beam_config.beam_width = std::stoul(config.at("ocr.beam_width"));
        beam_config.result_limit = std::stoul(config.at("ocr.result_limit"));
        beam_config.classes_per_step = std::stoul(config.at("ocr.classes_per_step"));
        beam_config.confusion_weight = std::stof(config.at("ocr.confusion_weight"));
        if (beam_config.ctc.charset.size() != 33U ||
            beam_config.ctc.blank_index != 33U ||
            beam_config.ctc.maximum_timesteps != 24U) {
            throw std::runtime_error("Unsupported/potentially mismatched production model contract");
        }
        const lpr::ConstrainedCtcBeamSearch decoder(beam_config);
        std::ifstream input(argv[1]);
        std::ofstream out(argv[3]);
        if (!input || !out) throw std::runtime_error("Missing input or JSON output");
        std::map<std::string, Aggregate> aggregate{};
        std::string line;
        unsigned total = 0U;
        out << "{\n\"kind\":\"real-onnx-native-beam-root-cause-diagnostic\","
            << "\n\"syntheticCropsOnly\":true,\n\"cases\":[\n";
        while (std::getline(input, line)) {
            if (line.empty()) continue;
            const auto data = fields(line, '\t');
            if (data.size() != 7U) throw std::runtime_error("Expected seven TSV columns");
            const auto& variant = data[0];
            const auto& filename = data[1];
            const auto& truth = data[2];
            const auto& py_greedy = data[4];
            const auto blank_steps = std::stoul(data[5]);
            if (!fac_lpr::application::TurkishPlateGrammar{}.is_valid_normalized(truth)) {
                throw std::runtime_error("Known truth is not valid Turkish plate");
            }
            const auto logits = read_logits(data[3]);
            const auto decoded = decoder.decode(logits, 24U, 34U);
            if (decoded.greedy.text != py_greedy) {
                throw std::runtime_error("Python greedy vs C++ greedy disagree: " + filename);
            }
            const bool top1 = !decoded.candidates.empty() && decoded.candidates.front().text == truth &&
                              decoded.candidates.front().format_valid;
            const bool topk = std::any_of(decoded.candidates.begin(), decoded.candidates.end(),
                [&](const fac_lpr::domain::PlateCandidate& v) {return v.text == truth && v.format_valid;});
            const bool no_valid = std::none_of(decoded.candidates.begin(), decoded.candidates.end(),
                [](const fac_lpr::domain::PlateCandidate& v) {return v.format_valid;});
            const float top_conf = decoded.candidates.empty() ? 0.0F : decoded.candidates.front().confidence;
            auto& a = aggregate[variant];
            ++a.count;
            a.greedy_correct += unsigned(decoded.greedy.text == truth);
            a.beam_top1_correct += unsigned(top1);
            a.beam_topk_contains_truth += unsigned(topk);
            a.beam_no_valid_candidate += unsigned(no_valid);
            a.native_fallback += unsigned(decoded.fallback_used);
            a.pure_deletion += unsigned(subsequence(decoded.greedy.text, truth));
            a.high_confidence_wrong += unsigned(!top1 && !decoded.candidates.empty() && top_conf >= 0.78F);
            a.blank_steps_sum += static_cast<unsigned>(blank_steps);
            if (total++) out << ",\n";
            out << "{\"variant\":" << json_quote(variant)
                << ",\"file\":" << json_quote(filename)
                << ",\"truth\":" << json_quote(truth)
                << ",\"greedy\":" << json_quote(decoded.greedy.text)
                << ",\"greedyConfidence\":" << decoded.greedy.confidence
                << ",\"beamTop1\":" << json_quote(decoded.candidates.empty() ? "" : decoded.candidates.front().text)
                << ",\"beamTop1Confidence\":" << top_conf
                << ",\"beamTruthInTopK\":" << (topk ? "true":"false")
                << ",\"nativeFallback\":" << (decoded.fallback_used ? "true":"false")
                << ",\"blankSteps\":" << blank_steps
                << ",\"pureDeletion\":" << (subsequence(decoded.greedy.text,truth) ? "true":"false")
                << "}";
            std::cout << variant << " " << filename << " target=" << truth
                      << " greedy=" << decoded.greedy.text
                      << " beam=" << (decoded.candidates.empty() ? "" : decoded.candidates.front().text)
                      << " truth_in_topk=" << (topk ? "yes":"no")
                      << " blank=" << blank_steps << '\n';
        }
        out << "\n]}\n";
        if (total != 128U || aggregate.size() != 4U) {
            throw std::runtime_error("Expected exactly 32 crops * 4 variants");
        }
        write_summary(argv[4], aggregate);
        for (const auto& [variant, a] : aggregate) {
            std::cout << "ROOT_CAUSE_DIAG variant=" << variant
                      << " greedy=" << a.greedy_correct << "/32"
                      << " beam_top1=" << a.beam_top1_correct << "/32"
                      << " beam_truth_in_top5=" << a.beam_topk_contains_truth << "/32"
                      << " greedy_pure_deletion=" << a.pure_deletion << "/32"
                      << " blank_steps_mean=" << (double(a.blank_steps_sum)/a.count)
                      << " high_conf_wrong=" << a.high_confidence_wrong << "/32\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ROOT_CAUSE_FATAL " << e.what() << '\n';
        return 2;
    }
}
