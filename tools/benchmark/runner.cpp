#include "../lpr-cli/cli_engine_factory.hpp"

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/application/operation_context.hpp>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options final {
    std::filesystem::path image{};
    std::filesystem::path model_dir{};
    std::filesystem::path config{};
    std::filesystem::path report{"benchmark.json"};
    std::size_t warmup{10U};
    std::size_t iterations{100U};
};

struct Distribution final {
    std::size_t count{0U};
    double minimum{0.0};
    double mean{0.0};
    double p50{0.0};
    double p95{0.0};
    double p99{0.0};
    double maximum{0.0};
};

class TimingCollector final : public fac_lpr::application::IStageTimingSink {
public:
    void record(const std::string_view stage, const double latency_ms) override {
        if (!std::isfinite(latency_ms) || latency_ms < 0.0) {
            throw fac_lpr::application::InternalError("benchmark received invalid stage latency");
        }
        values_[std::string{stage}].push_back(latency_ms);
    }

    void record_pipeline(const fac_lpr::application::LprPipelineResult& result) {
        for (const auto& timing : result.stage_timings) {
            record(timing.stage, timing.latency_ms);
        }
        record("total", result.total_latency_ms);
        recognition_count_ += result.recognitions.size();
    }

    [[nodiscard]] const std::map<std::string, std::vector<double>>& values() const noexcept {
        return values_;
    }

    [[nodiscard]] std::size_t recognition_count() const noexcept {
        return recognition_count_;
    }

private:
    std::map<std::string, std::vector<double>> values_{};
    std::size_t recognition_count_{0U};
};

[[nodiscard]] std::optional<std::size_t> parse_size(const std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::size_t value = 0U;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

[[nodiscard]] std::optional<Options> parse_options(const int argc, char** argv) {
    if (argc < 2) return std::nullopt;
    Options options{};
    options.image = argv[1];
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
        } else if (key == "--warmup") {
            const auto parsed = parse_size(value);
            if (!parsed) return std::nullopt;
            options.warmup = *parsed;
        } else if (key == "--iterations") {
            const auto parsed = parse_size(value);
            if (!parsed || *parsed == 0U) return std::nullopt;
            options.iterations = *parsed;
        } else {
            return std::nullopt;
        }
    }
    if (!have_model_dir || !have_config) return std::nullopt;
    return options;
}

void print_usage() {
    std::cerr
        << "Usage: fac-lpr-benchmark <image> --model-dir <dir> --config <contract> [options]\n"
        << "  --warmup N\n"
        << "  --iterations N\n"
        << "  --report <path>\n";
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

[[nodiscard]] double percentile_nearest_rank(
    const std::vector<double>& sorted,
    const double percentile) {
    if (sorted.empty()) return 0.0;
    const auto rank = std::ceil(percentile * static_cast<double>(sorted.size()));
    const auto one_based = std::max<std::size_t>(1U, static_cast<std::size_t>(rank));
    const auto index = std::min(one_based - 1U, sorted.size() - 1U);
    return sorted[index];
}

[[nodiscard]] Distribution summarize(std::vector<double> values) {
    if (values.empty()) return {};
    std::sort(values.begin(), values.end());
    const auto sum = std::accumulate(values.begin(), values.end(), 0.0);
    return Distribution{
        .count = values.size(),
        .minimum = values.front(),
        .mean = sum / static_cast<double>(values.size()),
        .p50 = percentile_nearest_rank(values, 0.50),
        .p95 = percentile_nearest_rank(values, 0.95),
        .p99 = percentile_nearest_rank(values, 0.99),
        .maximum = values.back()};
}

void write_distribution(std::ostream& output, const Distribution& distribution) {
    output << "{\"count\":" << distribution.count
           << ",\"minMs\":" << distribution.minimum
           << ",\"meanMs\":" << distribution.mean
           << ",\"p50Ms\":" << distribution.p50
           << ",\"p95Ms\":" << distribution.p95
           << ",\"p99Ms\":" << distribution.p99
           << ",\"maxMs\":" << distribution.maximum << '}';
}

void write_report(
    const Options& options,
    const TimingCollector& collector,
    const double measured_wall_seconds) {
    if (!options.report.parent_path().empty()) {
        std::filesystem::create_directories(options.report.parent_path());
    }
    std::ofstream output{options.report};
    if (!output) {
        throw fac_lpr::application::InternalError("cannot create benchmark JSON report");
    }

    const auto frames_per_second =
        measured_wall_seconds > 0.0
            ? static_cast<double>(options.iterations) / measured_wall_seconds
            : 0.0;
    const auto recognitions_per_second =
        measured_wall_seconds > 0.0
            ? static_cast<double>(collector.recognition_count()) / measured_wall_seconds
            : 0.0;

    output << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schemaVersion\": 1,\n"
           << "  \"warmupIterations\": " << options.warmup << ",\n"
           << "  \"measuredIterations\": " << options.iterations << ",\n"
           << "  \"measuredWallSeconds\": " << measured_wall_seconds << ",\n"
           << "  \"recognitionCount\": " << collector.recognition_count() << ",\n"
           << "  \"framesPerSecond\": " << frames_per_second << ",\n"
           << "  \"recognitionsPerSecond\": " << recognitions_per_second << ",\n"
           << "  \"percentileMethod\": \"nearest-rank\",\n"
           << "  \"stages\": {";

    bool first = true;
    for (const auto& [stage, values] : collector.values()) {
        if (!first) output << ',';
        first = false;
        output << "\n    \"" << stage << "\": ";
        write_distribution(output, summarize(values));
    }
    if (!collector.values().empty()) output << '\n';
    output << "  }\n}\n";
}

} // namespace

int main(const int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    if (!options) {
        print_usage();
        return 2;
    }

    try {
        const cv::Mat image = cv::imread(options->image.string(), cv::IMREAD_COLOR);
        if (image.empty() || image.data == nullptr || image.cols <= 0 || image.rows <= 0) {
            std::cerr << "Cannot decode benchmark image\n";
            return 3;
        }
        const auto view = make_view(image);
        const auto pipeline = fac_lpr::cli::build_pipeline_from_contract(
            options->model_dir, options->config, "error");

        for (std::size_t iteration = 0U; iteration < options->warmup; ++iteration) {
            (void)pipeline->recognize(view);
        }

        TimingCollector collector{};
        fac_lpr::application::OperationContext context{};
        context.timing_sink = &collector;

        const auto measured_started = Clock::now();
        for (std::size_t iteration = 0U; iteration < options->iterations; ++iteration) {
            const auto result = pipeline->recognize(view, context);
            collector.record_pipeline(result);
        }
        const auto measured_wall_seconds = std::chrono::duration<double>(
            Clock::now() - measured_started).count();

        write_report(*options, collector, measured_wall_seconds);
        const auto total_it = collector.values().find("total");
        const auto total = total_it != collector.values().end()
            ? summarize(total_it->second)
            : Distribution{};

        std::cout << std::fixed << std::setprecision(3)
                  << "FAC LPR benchmark: warmup=" << options->warmup
                  << " measured=" << options->iterations
                  << " totalP50=" << total.p50 << "ms"
                  << " totalP95=" << total.p95 << "ms"
                  << " totalP99=" << total.p99 << "ms"
                  << " fps=" << (measured_wall_seconds > 0.0
                      ? static_cast<double>(options->iterations) / measured_wall_seconds
                      : 0.0)
                  << " report=" << options->report << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 5;
    }
}
