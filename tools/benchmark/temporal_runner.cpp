#include "../lpr-cli/cli_engine_factory.hpp"

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/application/recognition_stream_session.hpp>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct Options final {
    std::filesystem::path manifest{};
    std::filesystem::path model_dir{};
    std::filesystem::path config{};
    std::filesystem::path report{"temporal-benchmark.json"};
};

struct FrameSpec final {
    std::chrono::milliseconds timestamp{};
    std::string expected_plate{};
    std::filesystem::path image{};
};

struct Metrics final {
    std::size_t frames{0U};
    std::size_t expected_plate_frames{0U};
    std::size_t per_frame_correct{0U};
    std::size_t stable_emitted{0U};
    std::size_t stable_correct{0U};
    std::size_t false_stable{0U};
    std::size_t duplicate_suppressed{0U};
    std::size_t ambiguous_frames{0U};
    std::size_t first_correct_stable_frame{0U};
};

[[nodiscard]] std::optional<Options> parse_options(const int argc, char** argv) {
    if (argc < 6) return std::nullopt;

    Options options{};
    options.manifest = argv[1];
    bool have_model_dir = false;
    bool have_config = false;

    for (int index = 2; index < argc; ++index) {
        if (index + 1 >= argc) return std::nullopt;
        const std::string_view key{argv[index]};
        const std::string_view value{argv[++index]};
        if (key == "--model-dir") {
            options.model_dir = value;
            have_model_dir = true;
        } else if (key == "--config") {
            options.config = value;
            have_config = true;
        } else if (key == "--report") {
            options.report = value;
        } else {
            return std::nullopt;
        }
    }

    if (!have_model_dir || !have_config) return std::nullopt;
    return options;
}

void print_usage() {
    std::cerr
        << "Usage: fac-lpr-temporal-benchmark <manifest.tsv> --model-dir <dir> --config <contract> [--report <path>]\n"
        << "Manifest columns: timestamp_ms<TAB>expected_plate_or_-<TAB>image_path\n";
}

[[nodiscard]] std::optional<std::chrono::milliseconds> parse_timestamp(const std::string_view text) {
    if (text.empty()) return std::nullopt;
    long long value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 0) {
        return std::nullopt;
    }
    return std::chrono::milliseconds{value};
}

[[nodiscard]] std::vector<FrameSpec> load_manifest(const std::filesystem::path& manifest) {
    std::ifstream input{manifest};
    if (!input) {
        throw fac_lpr::application::ConfigurationError("cannot open temporal benchmark manifest");
    }

    std::vector<FrameSpec> frames{};
    std::string line{};
    std::size_t line_number = 0U;
    std::optional<std::chrono::milliseconds> previous_timestamp{};
    const auto base = manifest.parent_path();

    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty() || line.front() == '#') continue;

        const auto first_tab = line.find('\t');
        const auto second_tab = first_tab == std::string::npos
            ? std::string::npos
            : line.find('\t', first_tab + 1U);
        if (first_tab == std::string::npos || second_tab == std::string::npos) {
            throw fac_lpr::application::ConfigurationError(
                "invalid temporal manifest row at line " + std::to_string(line_number));
        }

        const std::string_view timestamp_text{line.data(), first_tab};
        const auto timestamp = parse_timestamp(timestamp_text);
        if (!timestamp.has_value()) {
            throw fac_lpr::application::ConfigurationError(
                "invalid temporal manifest timestamp at line " + std::to_string(line_number));
        }
        if (previous_timestamp.has_value() && *timestamp < *previous_timestamp) {
            throw fac_lpr::application::ConfigurationError(
                "temporal manifest timestamps must be monotonic");
        }
        previous_timestamp = timestamp;

        auto expected = line.substr(first_tab + 1U, second_tab - first_tab - 1U);
        if (expected == "-") expected.clear();
        auto image = std::filesystem::path{line.substr(second_tab + 1U)};
        if (image.empty()) {
            throw fac_lpr::application::ConfigurationError(
                "temporal manifest image path is empty at line " + std::to_string(line_number));
        }
        if (image.is_relative()) image = base / image;

        frames.push_back(FrameSpec{*timestamp, std::move(expected), std::move(image)});
    }

    if (frames.empty()) {
        throw fac_lpr::application::ConfigurationError("temporal benchmark manifest has no frames");
    }
    return frames;
}

[[nodiscard]] fac_lpr::application::ImageView make_view(const cv::Mat& image) {
    const auto packed_row = static_cast<std::size_t>(image.cols) * image.elemSize();
    const auto required_bytes =
        (static_cast<std::size_t>(image.rows) - 1U) * image.step + packed_row;
    return fac_lpr::application::make_image_view(
        image.data,
        required_bytes,
        static_cast<std::size_t>(image.cols),
        static_cast<std::size_t>(image.rows),
        image.step,
        fac_lpr::application::PixelFormat::bgr8);
}

[[nodiscard]] std::optional<std::string_view> single_frame_plate(
    const fac_lpr::application::LprPipelineResult& result) {
    if (result.recognitions.size() != 1U) return std::nullopt;
    const auto& recognition = result.recognitions.front();
    if (recognition.status == fac_lpr::domain::RecognitionStatus::rejected || recognition.plate.empty()) {
        return std::nullopt;
    }
    return recognition.plate;
}

void write_report(
    const Options& options,
    const Metrics& metrics,
    const std::size_t max_history_observed) {
    if (!options.report.parent_path().empty()) {
        std::filesystem::create_directories(options.report.parent_path());
    }
    std::ofstream output{options.report};
    if (!output) {
        throw fac_lpr::application::InternalError("cannot create temporal benchmark report");
    }

    const auto per_frame_accuracy = metrics.expected_plate_frames > 0U
        ? static_cast<double>(metrics.per_frame_correct) /
              static_cast<double>(metrics.expected_plate_frames)
        : 0.0;
    const auto stable_precision = metrics.stable_emitted > 0U
        ? static_cast<double>(metrics.stable_correct) /
              static_cast<double>(metrics.stable_emitted)
        : 0.0;

    output << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schemaVersion\": 1,\n"
           << "  \"frames\": " << metrics.frames << ",\n"
           << "  \"expectedPlateFrames\": " << metrics.expected_plate_frames << ",\n"
           << "  \"perFrameCorrect\": " << metrics.per_frame_correct << ",\n"
           << "  \"perFrameAccuracy\": " << per_frame_accuracy << ",\n"
           << "  \"stableEmitted\": " << metrics.stable_emitted << ",\n"
           << "  \"stableCorrect\": " << metrics.stable_correct << ",\n"
           << "  \"falseStable\": " << metrics.false_stable << ",\n"
           << "  \"stablePrecision\": " << stable_precision << ",\n"
           << "  \"duplicateSuppressed\": " << metrics.duplicate_suppressed << ",\n"
           << "  \"ambiguousFrames\": " << metrics.ambiguous_frames << ",\n"
           << "  \"firstCorrectStableFrame\": " << metrics.first_correct_stable_frame << ",\n"
           << "  \"maxHistoryObserved\": " << max_history_observed << "\n"
           << "}\n";
}

} // namespace

int main(const int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    if (!options.has_value()) {
        print_usage();
        return 2;
    }

    try {
        const auto frames = load_manifest(options->manifest);
        const auto pipeline = fac_lpr::cli::build_pipeline_from_contract(
            options->model_dir, options->config, "error");
        fac_lpr::application::RecognitionStreamSession session{pipeline};

        Metrics metrics{};
        std::size_t max_history_observed = 0U;
        const auto origin = fac_lpr::application::RecognitionStreamSession::Clock::now();

        for (const auto& frame : frames) {
            const cv::Mat image = cv::imread(frame.image.string(), cv::IMREAD_COLOR);
            if (image.empty() || image.data == nullptr || image.cols <= 0 || image.rows <= 0) {
                throw fac_lpr::application::InvalidImageError(
                    "cannot decode temporal benchmark frame: " + frame.image.string());
            }

            const auto timestamp = origin + frame.timestamp;
            const auto result = session.recognize_at(make_view(image), timestamp);
            ++metrics.frames;
            if (!frame.expected_plate.empty()) {
                ++metrics.expected_plate_frames;
                const auto per_frame = single_frame_plate(result.frame_result);
                if (per_frame.has_value() && *per_frame == frame.expected_plate) {
                    ++metrics.per_frame_correct;
                }
            }
            if (result.ambiguous_frame) ++metrics.ambiguous_frames;
            max_history_observed = std::max(max_history_observed, result.temporal.history_size);

            if (result.emission.duplicate_suppressed) ++metrics.duplicate_suppressed;
            if (!result.emission.emitted_result.has_value()) continue;

            ++metrics.stable_emitted;
            const bool correct = !frame.expected_plate.empty() &&
                                 result.emission.emitted_result->plate == frame.expected_plate;
            if (correct) {
                ++metrics.stable_correct;
                if (metrics.first_correct_stable_frame == 0U) {
                    metrics.first_correct_stable_frame = metrics.frames;
                }
            } else {
                ++metrics.false_stable;
            }
        }

        write_report(*options, metrics, max_history_observed);
        std::cout << "FAC LPR temporal benchmark: frames=" << metrics.frames
                  << " perFrameCorrect=" << metrics.per_frame_correct
                  << " stableEmitted=" << metrics.stable_emitted
                  << " stableCorrect=" << metrics.stable_correct
                  << " falseStable=" << metrics.false_stable
                  << " firstCorrectStableFrame=" << metrics.first_correct_stable_frame
                  << " report=" << options->report << '\n';
        return metrics.false_stable == 0U ? 0 : 6;
    } catch (const std::exception& error) {
        std::cerr << "Temporal benchmark failed: " << error.what() << '\n';
        return 5;
    }
}
