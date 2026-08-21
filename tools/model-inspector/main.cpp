#include <onnxruntime_cxx_api.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct OpsetImport final {
    std::string domain{};
    std::uint64_t version{0};
};

[[nodiscard]] std::uint64_t read_varint(
    std::span<const std::uint8_t> data,
    std::size_t& offset) {
    std::uint64_t value = 0;
    unsigned shift = 0;

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

void skip_field(
    std::span<const std::uint8_t> data,
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
        case 2U: {
            const auto length = read_varint(data, offset);
            if (length > static_cast<std::uint64_t>(data.size() - offset)) {
                throw std::runtime_error("truncated protobuf length-delimited field");
            }
            offset += static_cast<std::size_t>(length);
            return;
        }
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

[[nodiscard]] OpsetImport parse_opset_import(std::span<const std::uint8_t> bytes) {
    OpsetImport result{};
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        const auto tag = read_varint(bytes, offset);
        const auto field_number = static_cast<std::uint32_t>(tag >> 3U);
        const auto wire_type = static_cast<std::uint32_t>(tag & 0x07U);

        if (field_number == 1U && wire_type == 2U) {
            const auto length = read_varint(bytes, offset);
            if (length > static_cast<std::uint64_t>(bytes.size() - offset)) {
                throw std::runtime_error("truncated opset domain");
            }
            const auto count = static_cast<std::size_t>(length);
            result.domain.assign(
                reinterpret_cast<const char*>(bytes.data() + offset),
                count);
            offset += count;
            continue;
        }

        if (field_number == 2U && wire_type == 0U) {
            result.version = read_varint(bytes, offset);
            continue;
        }

        skip_field(bytes, offset, wire_type);
    }

    return result;
}

[[nodiscard]] std::vector<OpsetImport> read_opsets(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open model file");
    }

    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot determine model file size");
    }

    constexpr std::uint64_t maximum_inspector_model_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
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

    std::vector<OpsetImport> imports{};
    std::size_t offset = 0;
    const std::span<const std::uint8_t> data{bytes};

    while (offset < data.size()) {
        const auto tag = read_varint(data, offset);
        const auto field_number = static_cast<std::uint32_t>(tag >> 3U);
        const auto wire_type = static_cast<std::uint32_t>(tag & 0x07U);

        // ONNX ModelProto.opset_import is field 8 and is length-delimited.
        if (field_number == 8U && wire_type == 2U) {
            const auto length = read_varint(data, offset);
            if (length > static_cast<std::uint64_t>(data.size() - offset)) {
                throw std::runtime_error("truncated ONNX opset_import");
            }
            const auto count = static_cast<std::size_t>(length);
            imports.push_back(parse_opset_import(data.subspan(offset, count)));
            offset += count;
            continue;
        }

        skip_field(data, offset, wire_type);
    }

    return imports;
}

[[nodiscard]] std::string_view element_type_name(const ONNXTensorElementDataType type) noexcept {
    switch (type) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED: return "undefined";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: return "float32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8: return "uint8";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8: return "int8";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16: return "uint16";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16: return "int16";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: return "int32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: return "int64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING: return "string";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL: return "bool";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16: return "float16";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE: return "float64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32: return "uint32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64: return "uint64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64: return "complex64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128: return "complex128";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16: return "bfloat16";
        default: return "other";
    }
}

void print_shape(const std::vector<std::int64_t>& shape) {
    std::cout << '[';
    for (std::size_t index = 0; index < shape.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        if (shape[index] < 0) {
            std::cout << '?';
        } else {
            std::cout << shape[index];
        }
    }
    std::cout << ']';
}

void print_value_info(
    const Ort::Session& session,
    Ort::AllocatorWithDefaultOptions& allocator,
    const bool input) {
    const auto count = input ? session.GetInputCount() : session.GetOutputCount();
    std::cout << (input ? "Inputs" : "Outputs") << ":\n";

    for (std::size_t index = 0; index < count; ++index) {
        auto name = input
            ? session.GetInputNameAllocated(index, allocator)
            : session.GetOutputNameAllocated(index, allocator);
        const auto type_info = input
            ? session.GetInputTypeInfo(index)
            : session.GetOutputTypeInfo(index);

        std::cout << "  - name: " << name.get() << '\n';
        if (type_info.GetONNXType() != ONNX_TYPE_TENSOR) {
            std::cout << "    type: non-tensor\n";
            continue;
        }

        const auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
        std::cout << "    element_type: "
                  << element_type_name(tensor_info.GetElementType()) << '\n';
        std::cout << "    shape: ";
        print_shape(tensor_info.GetShape());
        std::cout << '\n';
    }
}

[[nodiscard]] std::string allocated_or_empty(Ort::AllocatedStringPtr value) {
    return value ? std::string{value.get()} : std::string{};
}

int inspect_model(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        std::cerr << "error: model file does not exist: " << path.string() << '\n';
        return 2;
    }

    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "fac-lpr-model-info"};
    Ort::SessionOptions options{};
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    Ort::Session session{environment, path.c_str(), options};
    Ort::AllocatorWithDefaultOptions allocator{};

    std::cout << "Model: " << path.filename().string() << '\n';
    std::cout << "Path: " << path.string() << '\n';

    const auto metadata = session.GetModelMetadata();
    std::cout << "Producer: "
              << allocated_or_empty(metadata.GetProducerNameAllocated(allocator)) << '\n';
    std::cout << "Graph: "
              << allocated_or_empty(metadata.GetGraphNameAllocated(allocator)) << '\n';
    std::cout << "Domain: "
              << allocated_or_empty(metadata.GetDomainAllocated(allocator)) << '\n';
    std::cout << "Model version: " << metadata.GetVersion() << '\n';
    std::cout << "Description: "
              << allocated_or_empty(metadata.GetDescriptionAllocated(allocator)) << '\n';

    const auto opsets = read_opsets(path);
    std::cout << "Opsets:\n";
    if (opsets.empty()) {
        std::cout << "  - none found\n";
    } else {
        for (const auto& opset : opsets) {
            std::cout << "  - domain: "
                      << (opset.domain.empty() ? "ai.onnx" : opset.domain)
                      << ", version: " << opset.version << '\n';
        }
    }

    print_value_info(session, allocator, true);
    print_value_info(session, allocator, false);
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
    } catch (const Ort::Exception& exception) {
        std::cerr << "onnxruntime error: " << exception.what() << '\n';
        return 3;
    } catch (const std::exception& exception) {
        std::cerr << "error: " << exception.what() << '\n';
        return 4;
    }
}
