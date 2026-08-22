#include "../lpr-cli/cli_engine_factory.hpp"

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/fac_lpr_engine.h>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <unistd.h>
#endif

namespace {

constexpr std::uint64_t MIB = 1024ULL * 1024ULL;

struct Options final {
    std::filesystem::path image{};
    std::filesystem::path model_dir{};
    std::filesystem::path config{};
    std::filesystem::path report{"memory-stress.json"};
    std::size_t iterations{10000U};
    std::size_t warmup{100U};
    std::size_t sample_every{1000U};
    std::size_t lifecycle_cycles{100U};
    std::size_t handle_cycles{10000U};
    std::uint64_t max_loop_growth_bytes{128ULL * MIB};
    std::uint64_t max_lifecycle_growth_bytes{256ULL * MIB};
};

struct RssSample final {
    std::size_t iteration{0U};
    std::uint64_t bytes{0U};
};

[[nodiscard]] std::optional<std::size_t> parse_size(const std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::size_t value = 0U;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

[[nodiscard]] std::optional<double> parse_nonnegative_double(const std::string_view text) {
    try {
        const std::string owned{text};
        std::size_t consumed = 0U;
        const auto value = std::stod(owned, &consumed);
        if (consumed != owned.size() || value < 0.0 || value > 1048576.0) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::size_t profile_iterations(const std::string_view value) noexcept {
    if (value == "10k") return 10000U;
    if (value == "100k") return 100000U;
    if (value == "1m") return 1000000U;
    return 0U;
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
        } else if (key == "--profile") {
            options.iterations = profile_iterations(value);
            if (options.iterations == 0U) return std::nullopt;
        } else if (key == "--iterations") {
            const auto parsed = parse_size(value);
            if (!parsed || *parsed == 0U) return std::nullopt;
            options.iterations = *parsed;
        } else if (key == "--warmup") {
            const auto parsed = parse_size(value);
            if (!parsed) return std::nullopt;
            options.warmup = *parsed;
        } else if (key == "--sample-every") {
            const auto parsed = parse_size(value);
            if (!parsed || *parsed == 0U) return std::nullopt;
            options.sample_every = *parsed;
        } else if (key == "--lifecycle-cycles") {
            const auto parsed = parse_size(value);
            if (!parsed) return std::nullopt;
            options.lifecycle_cycles = *parsed;
        } else if (key == "--handle-cycles") {
            const auto parsed = parse_size(value);
            if (!parsed) return std::nullopt;
            options.handle_cycles = *parsed;
        } else if (key == "--max-loop-growth-mib") {
            const auto parsed = parse_nonnegative_double(value);
            if (!parsed) return std::nullopt;
            options.max_loop_growth_bytes = static_cast<std::uint64_t>(*parsed * static_cast<double>(MIB));
        } else if (key == "--max-lifecycle-growth-mib") {
            const auto parsed = parse_nonnegative_double(value);
            if (!parsed) return std::nullopt;
            options.max_lifecycle_growth_bytes = static_cast<std::uint64_t>(*parsed * static_cast<double>(MIB));
        } else {
            return std::nullopt;
        }
    }

    if (!have_model_dir || !have_config) return std::nullopt;
    return options;
}

void print_usage() {
    std::cerr
        << "Usage: fac-lpr-memory-stress <image> --model-dir <dir> --config <contract> [options]\n"
        << "  --profile 10k|100k|1m\n"
        << "  --iterations N\n"
        << "  --warmup N\n"
        << "  --sample-every N\n"
        << "  --lifecycle-cycles N\n"
        << "  --handle-cycles N\n"
        << "  --max-loop-growth-mib N\n"
        << "  --max-lifecycle-growth-mib N\n"
        << "  --report <path>\n";
}

[[nodiscard]] std::uint64_t current_rss_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters)) == 0) {
        throw fac_lpr::application::InternalError("GetProcessMemoryInfo failed");
    }
    return static_cast<std::uint64_t>(counters.WorkingSetSize);
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(
            mach_task_self(),
            MACH_TASK_BASIC_INFO,
            reinterpret_cast<task_info_t>(&info),
            &count) != KERN_SUCCESS) {
        throw fac_lpr::application::InternalError("mach task_info failed");
    }
    return static_cast<std::uint64_t>(info.resident_size);
#else
    std::ifstream input{"/proc/self/statm"};
    std::uint64_t ignored_total_pages = 0U;
    std::uint64_t resident_pages = 0U;
    if (!(input >> ignored_total_pages >> resident_pages)) {
        throw fac_lpr::application::InternalError("cannot read /proc/self/statm");
    }
    (void)ignored_total_pages;
    const auto page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        throw fac_lpr::application::InternalError("cannot resolve system page size");
    }
    return resident_pages * static_cast<std::uint64_t>(page_size);
#endif
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

[[nodiscard]] std::uint64_t growth(
    const std::uint64_t baseline,
    const std::uint64_t current) noexcept {
    return current > baseline ? current - baseline : 0U;
}

[[nodiscard]] std::uint64_t tail_average(const std::vector<RssSample>& samples) {
    if (samples.empty()) return 0U;
    const auto count = std::max<std::size_t>(1U, samples.size() / 4U);
    const auto start = samples.size() - count;
    long double sum = 0.0L;
    for (std::size_t index = start; index < samples.size(); ++index) {
        sum += static_cast<long double>(samples[index].bytes);
    }
    return static_cast<std::uint64_t>(sum / static_cast<long double>(count));
}

void run_c_handle_cycles(const std::size_t cycles) {
    for (std::size_t index = 0U; index < cycles; ++index) {
        fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
        fac_lpr_engine_handle* handle = nullptr;
        if (fac_lpr_engine_create_v1(&config, &handle) != FAC_LPR_STATUS_OK || handle == nullptr) {
            throw fac_lpr::application::InternalError("C ABI create failed during memory stress");
        }
        if (fac_lpr_engine_destroy_v1(&handle) != FAC_LPR_STATUS_OK || handle != nullptr) {
            throw fac_lpr::application::InternalError("C ABI destroy failed during memory stress");
        }
        if (fac_lpr_engine_destroy_v1(&handle) != FAC_LPR_STATUS_OK) {
            throw fac_lpr::application::InternalError("C ABI repeated destroy failed during memory stress");
        }
    }
}

void write_report(
    const Options& options,
    const std::vector<RssSample>& samples,
    const std::uint64_t loop_baseline,
    const std::uint64_t loop_final,
    const std::uint64_t loop_peak,
    const std::uint64_t loop_tail_average,
    const std::uint64_t handle_before,
    const std::uint64_t handle_after,
    const std::uint64_t lifecycle_before,
    const std::uint64_t lifecycle_after,
    const bool pass) {
    if (!options.report.parent_path().empty()) {
        std::filesystem::create_directories(options.report.parent_path());
    }
    std::ofstream output{options.report};
    if (!output) {
        throw fac_lpr::application::InternalError("cannot create memory stress JSON report");
    }

    output << "{\n"
           << "  \"schemaVersion\": 1,\n"
           << "  \"iterations\": " << options.iterations << ",\n"
           << "  \"warmup\": " << options.warmup << ",\n"
           << "  \"sampleEvery\": " << options.sample_every << ",\n"
           << "  \"handleCycles\": " << options.handle_cycles << ",\n"
           << "  \"pipelineLifecycleCycles\": " << options.lifecycle_cycles << ",\n"
           << "  \"loopBaselineRssBytes\": " << loop_baseline << ",\n"
           << "  \"loopFinalRssBytes\": " << loop_final << ",\n"
           << "  \"loopPeakRssBytes\": " << loop_peak << ",\n"
           << "  \"loopTailAverageRssBytes\": " << loop_tail_average << ",\n"
           << "  \"loopFinalGrowthBytes\": " << growth(loop_baseline, loop_final) << ",\n"
           << "  \"loopTailGrowthBytes\": " << growth(loop_baseline, loop_tail_average) << ",\n"
           << "  \"handleLifecycleGrowthBytes\": " << growth(handle_before, handle_after) << ",\n"
           << "  \"pipelineLifecycleGrowthBytes\": " << growth(lifecycle_before, lifecycle_after) << ",\n"
           << "  \"maxLoopGrowthBytes\": " << options.max_loop_growth_bytes << ",\n"
           << "  \"maxLifecycleGrowthBytes\": " << options.max_lifecycle_growth_bytes << ",\n"
           << "  \"pass\": " << (pass ? "true" : "false") << ",\n"
           << "  \"samples\": [";

    for (std::size_t index = 0U; index < samples.size(); ++index) {
        if (index != 0U) output << ',';
        output << "{\"iteration\":" << samples[index].iteration
               << ",\"rssBytes\":" << samples[index].bytes << '}';
    }
    output << "]\n}\n";
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
            std::cerr << "Cannot decode fixture image\n";
            return 3;
        }
        const auto view = make_view(image);

        auto pipeline = fac_lpr::cli::build_pipeline_from_contract(
            options->model_dir, options->config, "error");
        for (std::size_t index = 0U; index < options->warmup; ++index) {
            (void)pipeline->recognize(view);
        }

        const auto loop_baseline = current_rss_bytes();
        auto loop_peak = loop_baseline;
        std::vector<RssSample> samples{};
        samples.reserve((options->iterations / options->sample_every) + 2U);
        samples.push_back({0U, loop_baseline});

        for (std::size_t iteration = 1U; iteration <= options->iterations; ++iteration) {
            (void)pipeline->recognize(view);
            if ((iteration % options->sample_every) == 0U || iteration == options->iterations) {
                const auto rss = current_rss_bytes();
                loop_peak = std::max(loop_peak, rss);
                samples.push_back({iteration, rss});
            }
        }

        const auto loop_final = current_rss_bytes();
        loop_peak = std::max(loop_peak, loop_final);
        const auto loop_tail = tail_average(samples);
        pipeline.reset();

        const auto handle_before = current_rss_bytes();
        run_c_handle_cycles(options->handle_cycles);
        const auto handle_after = current_rss_bytes();

        const auto lifecycle_before = current_rss_bytes();
        for (std::size_t cycle = 0U; cycle < options->lifecycle_cycles; ++cycle) {
            auto cycle_pipeline = fac_lpr::cli::build_pipeline_from_contract(
                options->model_dir, options->config, "error");
            (void)cycle_pipeline->recognize(view);
            cycle_pipeline.reset();
        }
        const auto lifecycle_after = current_rss_bytes();

        const auto loop_final_growth = growth(loop_baseline, loop_final);
        const auto loop_tail_growth = growth(loop_baseline, loop_tail);
        const auto handle_growth = growth(handle_before, handle_after);
        const auto lifecycle_growth = growth(lifecycle_before, lifecycle_after);
        const bool pass =
            loop_final_growth <= options->max_loop_growth_bytes &&
            loop_tail_growth <= options->max_loop_growth_bytes &&
            handle_growth <= options->max_lifecycle_growth_bytes &&
            lifecycle_growth <= options->max_lifecycle_growth_bytes;

        write_report(
            *options,
            samples,
            loop_baseline,
            loop_final,
            loop_peak,
            loop_tail,
            handle_before,
            handle_after,
            lifecycle_before,
            lifecycle_after,
            pass);

        std::cout << "FAC LPR memory stress: iterations=" << options->iterations
                  << " finalGrowth=" << loop_final_growth
                  << " tailGrowth=" << loop_tail_growth
                  << " handleGrowth=" << handle_growth
                  << " pipelineLifecycleGrowth=" << lifecycle_growth
                  << " report=" << options->report << '\n';
        return pass ? 0 : 6;
    } catch (const std::exception& error) {
        std::cerr << "Memory stress failed: " << error.what() << '\n';
        return 5;
    }
}
