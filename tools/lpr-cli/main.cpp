#include <fac_lpr/fac_lpr_engine.h>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CliOptions final {
    std::filesystem::path image{};
    std::optional<std::filesystem::path> model_dir{};
    std::optional<std::filesystem::path> config{};
    std::string log_level{"info"};
    bool json{false};
    bool debug_evidence{false};
};

[[nodiscard]] bool valid_log_level(const std::string_view value) {
    return value == "trace" || value == "debug" || value == "info" ||
           value == "warn" || value == "error" || value == "off";
}

void print_usage() {
    std::cerr
        << "Usage: fac-lpr-cli <image.jpg|image.png> [options]\n"
        << "Options:\n"
        << "  --json                 Emit JSON output\n"
        << "  --debug-evidence       Include recognition evidence in human output\n"
        << "  --model-dir <path>     Runtime model directory (composition wiring pending)\n"
        << "  --config <path>        Runtime config path (composition wiring pending)\n"
        << "  --log-level <level>    trace|debug|info|warn|error|off\n";
}

[[nodiscard]] std::optional<CliOptions> parse_options(const int argc, char** argv) {
    if (argc < 2) {
        return std::nullopt;
    }

    CliOptions options{};
    options.image = argv[1];
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
            } else if (argument == "--config") {
                options.config = value;
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

[[nodiscard]] std::string last_error() {
    std::size_t required = 0U;
    const auto query = fac_lpr_get_last_error_v1(nullptr, 0U, &required);
    if (query != FAC_LPR_STATUS_BUFFER_TOO_SMALL || required == 0U) {
        return {};
    }
    std::vector<char> buffer(required);
    if (fac_lpr_get_last_error_v1(buffer.data(), buffer.size(), &required) != FAC_LPR_STATUS_OK) {
        return {};
    }
    return std::string{buffer.data()};
}

[[nodiscard]] bool range_is_valid(
    const std::size_t total,
    const std::uint32_t offset,
    const std::size_t bytes) {
    const auto begin = static_cast<std::size_t>(offset);
    return begin <= total && bytes <= total - begin;
}

template <typename T>
[[nodiscard]] std::optional<T> read_record(
    const std::byte* bytes,
    const std::size_t total,
    const std::uint32_t offset) {
    if (!range_is_valid(total, offset, sizeof(T))) {
        return std::nullopt;
    }
    T value{};
    std::memcpy(&value, bytes + offset, sizeof(T));
    return value;
}

[[nodiscard]] std::optional<std::string_view> read_text(
    const std::byte* bytes,
    const std::size_t total,
    const fac_lpr_text_ref_v1 ref) {
    if (ref.length == 0U) {
        return std::string_view{};
    }
    if (!range_is_valid(total, ref.offset, ref.length)) {
        return std::nullopt;
    }
    return std::string_view{
        reinterpret_cast<const char*>(bytes + ref.offset),
        ref.length};
}

[[nodiscard]] const char* status_name(const fac_lpr_recognition_status_v1 status) {
    if (status == FAC_LPR_RECOGNITION_ACCEPTED_V1) return "ACCEPTED";
    if (status == FAC_LPR_RECOGNITION_REVIEW_V1) return "REVIEW";
    if (status == FAC_LPR_RECOGNITION_REJECTED_V1) return "REJECTED";
    return "UNKNOWN";
}

[[nodiscard]] int emit_result(
    const std::vector<std::uint32_t>& storage,
    const std::size_t byte_size,
    const CliOptions& options) {
    const auto* bytes = reinterpret_cast<const std::byte*>(storage.data());
    const auto root = read_record<fac_lpr_result_v1>(bytes, byte_size, 0U);
    if (!root || root->abi_version != FAC_LPR_ABI_VERSION_V1) {
        std::cerr << "Invalid C ABI result buffer\n";
        return 5;
    }

    if (options.json) {
        std::cout << "{\"totalLatencyMs\":" << root->total_latency_ms
                  << ",\"degraded\":" << (root->degraded != 0U ? "true" : "false")
                  << ",\"recognitions\":[";
    } else {
        std::cout << "FAC LPR result: " << root->recognition_count
                  << " recognition(s), total=" << root->total_latency_ms << " ms\n";
    }

    for (std::uint32_t index = 0U; index < root->recognition_count; ++index) {
        const auto relative = static_cast<std::size_t>(index) * sizeof(fac_lpr_plate_result_v1);
        if (relative > std::numeric_limits<std::uint32_t>::max() - root->recognitions_offset) {
            return 5;
        }
        const auto offset = root->recognitions_offset + static_cast<std::uint32_t>(relative);
        const auto plate = read_record<fac_lpr_plate_result_v1>(bytes, byte_size, offset);
        if (!plate) {
            return 5;
        }
        const auto text = read_text(bytes, byte_size, plate->plate);
        if (!text) {
            return 5;
        }

        if (options.json) {
            if (index != 0U) std::cout << ',';
            std::cout << "{\"status\":\"" << status_name(plate->status)
                      << "\",\"plate\":\"" << escape_json(*text)
                      << "\",\"confidence\":" << plate->confidence
                      << ",\"degraded\":" << (plate->degraded != 0U ? "true" : "false")
                      << '}';
        } else {
            std::cout << "[" << status_name(plate->status) << "] " << *text
                      << " confidence=" << plate->confidence << '\n';
            if (options.debug_evidence) {
                std::cout << "  evidence=" << plate->evidence_count
                          << " alternatives=" << plate->alternatives_count
                          << " reasons=" << plate->decision_reason_count << '\n';
            }
        }
    }

    if (options.json) {
        std::cout << "]}\n";
    }
    return 0;
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
    if (options->model_dir && !std::filesystem::is_directory(*options->model_dir)) {
        std::cerr << "Model directory not found: " << *options->model_dir << '\n';
        return 2;
    }
    if (options->config && !std::filesystem::is_regular_file(*options->config)) {
        std::cerr << "Config file not found: " << *options->config << '\n';
        return 2;
    }

    const cv::Mat image = cv::imread(options->image.string(), cv::IMREAD_COLOR);
    if (image.empty() || image.cols <= 0 || image.rows <= 0) {
        std::cerr << "Cannot decode JPG/PNG image: " << options->image << '\n';
        return 3;
    }

    fac_lpr_engine_handle* engine = nullptr;
    fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
    auto status = fac_lpr_engine_create_v1(&config, &engine);
    if (status != FAC_LPR_STATUS_OK) {
        std::cerr << "Engine create failed (" << status << "): " << last_error() << '\n';
        return 4;
    }

    fac_lpr_image_view_v1 view = FAC_LPR_IMAGE_VIEW_V1_INIT;
    view.data = image.ptr<std::uint8_t>();
    view.data_size = image.total() * image.elemSize();
    view.width = static_cast<std::uint32_t>(image.cols);
    view.height = static_cast<std::uint32_t>(image.rows);
    view.stride_bytes = static_cast<std::uint32_t>(image.step);
    view.pixel_format = FAC_LPR_PIXEL_FORMAT_BGR8;

    std::size_t required = 0U;
    status = fac_lpr_engine_recognize_v1(engine, &view, nullptr, 0U, &required);
    if (status != FAC_LPR_STATUS_BUFFER_TOO_SMALL) {
        std::cerr << "Recognition failed (" << status << "): " << last_error() << '\n';
        (void)fac_lpr_engine_destroy_v1(&engine);
        return 4;
    }

    const auto word_count = (required + sizeof(std::uint32_t) - 1U) / sizeof(std::uint32_t);
    std::vector<std::uint32_t> storage(word_count);
    status = fac_lpr_engine_recognize_v1(
        engine,
        &view,
        storage.data(),
        storage.size() * sizeof(std::uint32_t),
        &required);
    (void)fac_lpr_engine_destroy_v1(&engine);
    if (status != FAC_LPR_STATUS_OK) {
        std::cerr << "Recognition failed (" << status << "): " << last_error() << '\n';
        return 4;
    }

    return emit_result(storage, required, *options);
}
