#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct OpsetImport final {
    std::string domain{};
    std::uint64_t version{0U};
};

struct TensorInfo final {
    std::string name{};
    std::uint64_t element_type{0U};
    std::vector<std::string> dimensions{};
};

struct MetadataEntry final {
    std::string key{};
    std::string value{};
};

struct ModelInfo final {
    std::uint64_t ir_version{0U};
    std::string producer_name{};
    std::string producer_version{};
    std::string domain{};
    std::uint64_t model_version{0U};
    std::string doc_string{};
    std::string graph_name{};
    std::string graph_doc_string{};
    std::vector<OpsetImport> opsets{};
    std::vector<MetadataEntry> metadata{};
    std::vector<TensorInfo> inputs{};
    std::vector<TensorInfo> outputs{};
};

[[nodiscard]] std::uint64_t read_varint(
    const std::span<const std::uint8_t> data,
    std::size_t& offset) {
    std::uint64_t value = 0U;
    unsigned shift = 0U;

    while (offset < data.size() && shift < 64U) {
        const auto byte = data[offset++];
        value |= static_cast<std::uint64_t>(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0U) {
            return value;
        }
        shift += 7U;
    }

    throw std::runtime_error("invalid protobuf varint");
}

[[nodiscard]] std::span<const std::uint8_t> read_bytes(
    const std::span<const std::uint8_t> data,
    std::size_t& offset,
    const std::string_view field) {
    const auto length = read_varint(data, offset);
    if (length > static_cast<std::uint64_t>(data.size() - offset)) {
        throw std::runtime_error("truncated protobuf field: " + std::string{field});
    }

    const auto count = static_cast<std::size_t>(length);
    const auto result = data.subspan(offset, count);
    offset += count;
    return result;
}

[[nodiscard]] std::string read_string(
    const std::span<const std::uint8_t> data,
    std::size_t& offset,
    const std::string_view field) {
    const auto bytes = read_bytes(data, offset, field);
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

void skip_field(
    const std::span<const std::uint8_t> data,
    std::size_t& offset,
    const std::uint32_t wire_type) {
    switch (wire_type) {
        case 0U:
            static_cast<void>(read_varint(data, offset));
            return;
        case 1U:
            if (data.size() - offset < 8U) {
                throw std::runtime_error("truncated protobuf fixed64 field");
            }
            offset += 8U;
            return;
        case 2U:
            static_cast<void>(read_bytes(data, offset, "length-delimited"));
            return;
        case 5U:
            if (data.size() - offset < 4U) {
                throw std::runtime_error("truncated protobuf fixed32 field");
            }
            offset += 4U;
            return;
        default:
            throw std::runtime_error("unsupported protobuf wire type");
    }
}

[[nodiscard]] OpsetImport parse_opset(
    const std::span<const std::uint8_t> bytes) {
    OpsetImport result{};
    std::size_t offset = 0U;

    while (offset < bytes.size()) {
        const auto tag = read_varint(bytes, offset);
        const auto field = static_cast<std::uint32_t>(tag >> 3U);
        const auto wire = static_cast<std::uint32_t>(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            result.domain = read_string(bytes, offset, "opset domain");
        } else if (field == 2U && wire == 0U) {
            result.version = read_varint(bytes, offset);
        } else {
            skip_field(bytes, offset, wire);
        }
    }

    return result;
}

[[nodiscard]] MetadataEntry parse_metadata(
    const std::span<const std::uint8_t> bytes) {
    MetadataEntry result{};
    std::size_t offset = 0U;

    while (offset < bytes.size()) {
        const auto tag = read_varint(bytes, offset);
        const auto field = static_cast<std::uint32_t>(tag >> 3U);
        const auto wire = static_cast<std::uint32_t>(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            result.key = read_string(bytes, offset, "metadata key");
        } else if (field == 2U && wire == 2U) {
            result.value = read_string(bytes, offset, "metadata value");
        } else {
            skip_field(bytes, offset, wire);
        }
    }

    return result;
}

[[nodiscard]] std::vector<std::string> parse_shape(
    const std::span<const std::uint8_t> bytes) {
    std::vector<std::string> dimensions{};
    std::size_t offset = 0U;

    while (offset < bytes.size()) {
        const auto tag = read_varint(bytes, offset);
        const auto field = static_cast<std::uint32_t>(tag >> 3U);
        const auto wire = static_cast<std::uint32_t>(tag & 0x07U);

        if (field != 1U || wire != 2U) {
            skip_field(bytes, offset, wire);
            continue;
        }

        const auto dimension_bytes = read_bytes(bytes, offset, "tensor dimension");
        std::size_t dimension_offset = 0U;
        std::string dimension{"?"};

        while (dimension_offset < dimension_bytes.size()) {
            const auto dimension_tag = read_varint(dimension_bytes, dimension_offset);
            const auto dimension_field = static_cast<std::uint32_t>(dimension_tag >> 3U);
            const auto dimension_wire = static_cast<std::uint32_t>(dimension_tag & 0x07U);

            if (dimension_field == 1U && dimension_wire == 0U) {
                dimension = std::to_string(read_varint(dimension_bytes, dimension_offset));
            } else if (dimension_field == 2U && dimension_wire == 2U) {
                dimension = read_string(
                    dimension_bytes,
                    dimension_offset,
                    "symbolic dimension");
            } else {
                skip_field(dimension_bytes, dimension_offset, dimension_wire);
            }
        }

        dimensions.push_back(std::move(dimension));
    }

    return dimensions;
}

[[nodiscard]] std::pair<std::uint64_t, std::vector<std::string>> parse_tensor_type(
    const std::span<const std::uint8_t> bytes) {
    std::uint64_t element_type = 0U;
    std::vector<std::string> shape{};
    std::size_t offset = 0U;

    while (offset < bytes.size()) {
        const auto tag = read_varint(bytes, offset);
        const auto field = static_cast<std::uint32_t>(tag >> 3U);
        const auto wire = static_cast<std::uint32_t>(tag & 0x07U);

        if (field == 1U && wire == 0U) {
            element_type = read_varint(bytes, offset);
        } else if (field == 2U && wire == 2U) {
            shape = parse_shape(read_bytes(bytes, offset, "tensor shape"));
        } else {
            skip_field(bytes, offset, wire);
        }
    }

    return {element_type, std::move(shape)};
}

[[nodiscard]] std::pair<std::uint64_t, std::vector<std::string>> parse_type(
    const std::span<const std::uint8_t> bytes) {
    std::size_t offset = 0U;

    while (offset < bytes.size()) {
        const auto tag = read_varint(bytes, offset);
        const auto field = static_cast<std::uint32_t>(tag >> 3U);
        const auto wire = static_cast<std::uint32_t>(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            return parse_tensor_type(read_bytes(bytes, offset, "tensor type"));
        }
        skip_field(bytes, offset, wire);
    }

    return {};
}

[[nodiscard]] TensorInfo parse_value_info(
    const std::span<const std::uint8_t> bytes) {
    TensorInfo result{};
    std::size_t offset = 0U;

    while (offset < bytes.size()) {
        const auto tag = read_varint(bytes, offset);
        const auto field = static_cast<std::uint32_t>(tag >> 3U);
        const auto wire = static_cast<std::uint32_t>(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            result.name = read_string(bytes, offset, "value name");
        } else if (field == 2U && wire == 2U) {
            auto [element_type, shape] = parse_type(
                read_bytes(bytes, offset, "value type"));
            result.element_type = element_type;
            result.dimensions = std::move(shape);
        } else {
            skip_field(bytes, offset, wire);
        }
    }

    return result;
}

void parse_graph(
    const std::span<const std::uint8_t> bytes,
    ModelInfo& model) {
    std::size_t offset = 0U;

    while (offset < bytes.size()) {
        const auto tag = read_varint(bytes, offset);
        const auto field = static_cast<std::uint32_t>(tag >> 3U);
        const auto wire = static_cast<std::uint32_t>(tag & 0x07U);

        if (field == 2U && wire == 2U) {
            model.graph_name = read_string(bytes, offset, "graph name");
        } else if (field == 10U && wire == 2U) {
            model.graph_doc_string = read_string(bytes, offset, "graph doc string");
        } else if (field == 11U && wire == 2U) {
            model.inputs.push_back(parse_value_info(
                read_bytes(bytes, offset, "graph input")));
        } else if (field == 12U && wire == 2U) {
            model.outputs.push_back(parse_value_info(
                read_bytes(bytes, offset, "graph output")));
        } else {
            skip_field(bytes, offset, wire);
        }
    }
}

[[nodiscard]] std::vector<std::uint8_t> read_model_bytes(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open model file");
    }

    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot determine model file size");
    }

    constexpr std::uint64_t maximum_inspector_model_bytes =
        2ULL * 1024ULL * 1024ULL * 1024ULL;
    const auto file_size = static_cast<std::uint64_t>(end);
    if (file_size > maximum_inspector_model_bytes ||
        file_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("model is too large for inspector safety limit");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            throw std::runtime_error("cannot read complete model file");
        }
    }
    return bytes;
}

[[nodiscard]] ModelInfo parse_model(const std::filesystem::path& path) {
    const auto storage = read_model_bytes(path);
    const std::span<const std::uint8_t> bytes{storage};
    ModelInfo model{};
    std::size_t offset = 0U;

    while (offset < bytes.size()) {
        const auto tag = read_varint(bytes, offset);
        const auto field = static_cast<std::uint32_t>(tag >> 3U);
        const auto wire = static_cast<std::uint32_t>(tag & 0x07U);

        if (field == 1U && wire == 0U) {
            model.ir_version = read_varint(bytes, offset);
        } else if (field == 2U && wire == 2U) {
            model.producer_name = read_string(bytes, offset, "producer name");
        } else if (field == 3U && wire == 2U) {
            model.producer_version = read_string(bytes, offset, "producer version");
        } else if (field == 4U && wire == 2U) {
            model.domain = read_string(bytes, offset, "domain");
        } else if (field == 5U && wire == 0U) {
            model.model_version = read_varint(bytes, offset);
        } else if (field == 6U && wire == 2U) {
            model.doc_string = read_string(bytes, offset, "model doc string");
        } else if (field == 7U && wire == 2U) {
            parse_graph(read_bytes(bytes, offset, "graph"), model);
        } else if (field == 8U && wire == 2U) {
            model.opsets.push_back(parse_opset(
                read_bytes(bytes, offset, "opset import")));
        } else if (field == 14U && wire == 2U) {
            model.metadata.push_back(parse_metadata(
                read_bytes(bytes, offset, "metadata entry")));
        } else {
            skip_field(bytes, offset, wire);
        }
    }

    if (model.inputs.empty() || model.outputs.empty()) {
        throw std::runtime_error("model has no tensor inputs or outputs");
    }
    return model;
}

[[nodiscard]] std::string_view element_type_name(
    const std::uint64_t type) noexcept {
    switch (type) {
        case 0U: return "undefined";
        case 1U: return "float32";
        case 2U: return "uint8";
        case 3U: return "int8";
        case 4U: return "uint16";
        case 5U: return "int16";
        case 6U: return "int32";
        case 7U: return "int64";
        case 8U: return "string";
        case 9U: return "bool";
        case 10U: return "float16";
        case 11U: return "float64";
        case 12U: return "uint32";
        case 13U: return "uint64";
        case 14U: return "complex64";
        case 15U: return "complex128";
        case 16U: return "bfloat16";
        default: return "other";
    }
}

void print_shape(const std::vector<std::string>& shape) {
    std::cout << '[';
    for (std::size_t index = 0U; index < shape.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        std::cout << shape[index];
    }
    std::cout << ']';
}

void print_values(
    const std::string_view title,
    const std::vector<TensorInfo>& values) {
    std::cout << title << ":\n";
    for (const auto& value : values) {
        std::cout << "  - name: " << value.name << '\n';
        std::cout << "    element_type: "
                  << element_type_name(value.element_type) << '\n';
        std::cout << "    shape: ";
        print_shape(value.dimensions);
        std::cout << '\n';
    }
}

int inspect_model(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        std::cerr << "error: model file does not exist: "
                  << path.filename().string() << '\n';
        return 2;
    }

    const auto model = parse_model(path);
    std::cout << "Model: " << path.filename().string() << '\n';
    std::cout << "IR version: " << model.ir_version << '\n';
    std::cout << "Producer: " << model.producer_name << '\n';
    std::cout << "Producer version: " << model.producer_version << '\n';
    std::cout << "Graph: " << model.graph_name << '\n';
    std::cout << "Domain: " << model.domain << '\n';
    std::cout << "Model version: " << model.model_version << '\n';
    std::cout << "Description: " << model.doc_string << '\n';

    std::cout << "Opsets:\n";
    if (model.opsets.empty()) {
        std::cout << "  - none found\n";
    } else {
        for (const auto& opset : model.opsets) {
            std::cout << "  - domain: "
                      << (opset.domain.empty() ? "ai.onnx" : opset.domain)
                      << ", version: " << opset.version << '\n';
        }
    }

    std::cout << "Metadata:\n";
    if (model.metadata.empty()) {
        std::cout << "  - none\n";
    } else {
        for (const auto& entry : model.metadata) {
            std::cout << "  - " << entry.key << ": " << entry.value << '\n';
        }
    }

    print_values("Inputs", model.inputs);
    print_values("Outputs", model.outputs);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: fac-lpr-model-info <model.onnx>\n";
        return 64;
    }

    try {
        return inspect_model(std::filesystem::path{argv[1]});
    } catch (const std::exception& exception) {
        std::cerr << "error: " << exception.what() << '\n';
        return 4;
    }
}
