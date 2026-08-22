#include "cli_engine_factory.hpp"

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/domain/recognition.hpp>

#include <opencv2/imgcodecs.hpp>

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

struct CliOptions final {
    std::filesystem::path image{};
    std::filesystem::path model_dir{};
    std::filesystem::path config{};
    std::string log_level{"info"};
    bool json{false};
    bool debug_evidence{false};
};

[[nodiscard]] bool valid_log_level(const std::string_view value) {
    return value == "trace" || value == "debug" || value == "info" ||
           value == "warn" || value == "error";
}

void print_usage() {
    std::cerr
        << "Usage: fac-lpr-cli <image.jpg|image.png> --model-dir <path> --config <contract> [options]\n"
        << "Options:\n"
        << "  --json                 Emit JSON output\n"
        << "  --debug-evidence       Print provider evidence/alternatives/reasons\n"
        << "  --model-dir <path>     Directory containing runtime ONNX models\n"
        << "  --config <path>        Explicit model-contract key=value file\n"
        << "  --log-level <level>    trace|debug|info|warn|error\n";
}

[[nodiscard]] std::optional<CliOptions> parse_options(const int argc, char** argv) {
    if (argc < 2) {
        return std::nullopt;
    }

    CliOptions options{};
    options.image = argv[1];
    bool have_model_dir = false;
    bool have_config = false;
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--json") {
            options.json = true;
        } else if (argument == "--debug-evidence") {
            options.debug_evidence = true;
        } else if (argument == "--model-dir" || argument == "--config" || argument == "--log-level") {
            if (index + 1 >= argc) {
                return std::nullopt;
            }
            const std::string value{argv[++index]};
            if (argument == "--model-dir") {
                options.model_dir = value;
                have_model_dir = true;
            } else if (argument == "--config") {
                options.config = value;
                have_config = true;
            } else {
                if (!valid_log_level(value)) {
                    return std::nullopt;
                }
                options.log_level = value;
            }
        } else {
            return std::nullopt;
        }
    }
    if (!have_model_dir || !have_config) {
        return std::nullopt;
    }
    return options;
}

[[nodiscard]] std::string escape_json(const std::string_view value) {
    std::string escaped{};
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += ch; break;
        }
    }
    return escaped;
}

[[nodiscard]] const char* status_name(const fac_lpr::domain::RecognitionStatus status) {
    switch (status) {
        case fac_lpr::domain::RecognitionStatus::accepted: return "ACCEPTED";
        case fac_lpr::domain::RecognitionStatus::review: return "REVIEW";
        case fac_lpr::domain::RecognitionStatus::rejected: return "REJECTED";
    }
    return "UNKNOWN";
}

[[nodiscard]] const char* reason_name(const fac_lpr::domain::RecognitionDecisionReason reason) {
    using Reason = fac_lpr::domain::RecognitionDecisionReason;
    switch (reason) {
        case Reason::accepted_consensus: return "accepted_consensus";
        case Reason::fatal_provider_failure: return "fatal_provider_failure";
        case Reason::degraded_provider_set: return "degraded_provider_set";
        case Reason::detector_confidence_below_minimum: return "detector_confidence_below_minimum";
        case Reason::geometry_below_minimum: return "geometry_below_minimum";
        case Reason::crop_quality_below_minimum: return "crop_quality_below_minimum";
        case Reason::no_valid_candidate: return "no_valid_candidate";
        case Reason::candidate_confidence_below_review: return "candidate_confidence_below_review";
        case Reason::candidate_confidence_below_accept: return "candidate_confidence_below_accept";
        case Reason::conflicting_strong_candidates: return "conflicting_strong_candidates";
    }
    return "unknown";
}

void emit_human(
    const fac_lpr::application::LprPipelineResult& result,
    const bool debug_evidence) {
    std::cout << "FAC LPR result: " << result.recognitions.size()
              << " recognition(s), total=" << result.total_latency_ms << " ms"
              << (result.degraded ? " degraded" : "") << '\n';

    for (std::size_t index = 0U; index < result.recognitions.size(); ++index) {
        const auto& recognition = result.recognitions[index];
        std::cout << "#" << index << " [" << status_name(recognition.status) << "] "
                  << recognition.plate << " confidence=" << recognition.confidence
                  << " detector=" << recognition.detector_confidence
                  << " geometry=" << recognition.geometry_score
                  << " crop=" << recognition.crop_quality << '\n';

        if (!debug_evidence) {
            continue;
        }
        for (const auto& evidence : recognition.evidence) {
            std::cout << "  provider=" << evidence.source
                      << " cropQuality=" << evidence.crop_quality
                      << " latencyMs=" << evidence.latency_ms << '\n';
            for (const auto& candidate : evidence.candidates) {
                std::cout << "    candidate=" << candidate.text
                          << " raw=" << candidate.confidence
                          << " calibrated=" << candidate.calibrated_confidence
                          << " format=" << (candidate.format_valid ? "valid" : "invalid") << '\n';
            }
        }
        if (!recognition.alternatives.empty()) {
            std::cout << "  alternatives:";
            for (const auto& candidate : recognition.alternatives) {
                std::cout << ' ' << candidate.text << '(' << candidate.calibrated_confidence << ')';
            }
            std::cout << '\n';
        }
        if (!recognition.decision_reasons.empty()) {
            std::cout << "  reasons:";
            for (const auto reason : recognition.decision_reasons) {
                std::cout << ' ' << reason_name(reason);
            }
            std::cout << '\n';
        }
    }
}

void emit_json(const fac_lpr::application::LprPipelineResult& result) {
    std::cout << "{\"totalLatencyMs\":" << result.total_latency_ms
              << ",\"degraded\":" << (result.degraded ? "true" : "false")
              << ",\"providerFailureCount\":" << result.provider_failure_count
              << ",\"recognitions\":[";
    for (std::size_t index = 0U; index < result.recognitions.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        const auto& recognition = result.recognitions[index];
        std::cout << "{\"status\":\"" << status_name(recognition.status)
                  << "\",\"plate\":\"" << escape_json(recognition.plate)
                  << "\",\"confidence\":" << recognition.confidence
                  << ",\"detectorConfidence\":" << recognition.detector_confidence
                  << ",\"geometryScore\":" << recognition.geometry_score
                  << ",\"cropQuality\":" << recognition.crop_quality
                  << ",\"degraded\":" << (recognition.degraded ? "true" : "false")
                  << ",\"reasons\":[";
        for (std::size_t reason_index = 0U;
             reason_index < recognition.decision_reasons.size(); ++reason_index) {
            if (reason_index != 0U) {
                std::cout << ',';
            }
            std::cout << '"' << reason_name(recognition.decision_reasons[reason_index]) << '"';
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}

} // namespace

int main(const int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    if (!options) {
        print_usage();
        return 2;
    }
    if (!std::filesystem::is_regular_file(options->image)) {
        std::cerr << "Image file not found: " << options->image << '\n';
        return 2;
    }
    if (!std::filesystem::is_directory(options->model_dir)) {
        std::cerr << "Model directory not found: " << options->model_dir << '\n';
        return 2;
    }
    if (!std::filesystem::is_regular_file(options->config)) {
        std::cerr << "Contract file not found: " << options->config << '\n';
        return 2;
    }

    try {
        const cv::Mat image = cv::imread(options->image.string(), cv::IMREAD_COLOR);
        if (image.empty() || image.cols <= 0 || image.rows <= 0 || image.data == nullptr) {
            std::cerr << "Cannot decode JPG/PNG image: " << options->image << '\n';
            return 3;
        }

        const auto packed_row = static_cast<std::size_t>(image.cols) * image.elemSize();
        const auto required_bytes =
            (static_cast<std::size_t>(image.rows) - 1U) * image.step + packed_row;
        const auto view = fac_lpr::application::make_image_view(
            image.data,
            required_bytes,
            static_cast<std::size_t>(image.cols),
            static_cast<std::size_t>(image.rows),
            image.step,
            fac_lpr::application::PixelFormat::bgr8);

        const auto pipeline = fac_lpr::cli::build_pipeline_from_contract(
            options->model_dir,
            options->config,
            options->log_level);
        const auto result = pipeline->recognize(view);
        if (options->json) {
            emit_json(result);
        } else {
            emit_human(result, options->debug_evidence);
        }
        return 0;
    } catch (const fac_lpr::application::EngineError& error) {
        std::cerr << "FAC LPR error: " << error.what() << '\n';
        return 4;
    } catch (const std::exception& error) {
        std::cerr << "Unexpected error: " << error.what() << '\n';
        return 5;
    }
}
