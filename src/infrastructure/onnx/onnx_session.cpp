#include <fac_lpr/infrastructure/onnx/onnx_session.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace fac_lpr::infrastructure::onnx {
namespace {

[[nodiscard]] std::size_t static_tensor_elements(
    const std::vector<std::int64_t>& shape,
    const std::size_t maximum,
    const std::string_view resource) {
    std::size_t total = 1U;
    for (const auto dimension : shape) {
        if (dimension <= 0) {
            return 0U; // dynamic shape is checked again against the concrete input tensor at run time.
        }
        const auto value = static_cast<std::size_t>(dimension);
        total = application::checked_resource_multiply(total, value, maximum, resource);
    }
    return total;
}

void validate_descriptor_budget(
    const std::vector<TensorDescriptor>& descriptors,
    const std::size_t maximum,
    const std::string_view direction) {
    for (const auto& descriptor : descriptors) {
        const auto resource = std::string{"ONNX "} + std::string{direction} + " tensor '" + descriptor.name + "'";
        static_cast<void>(static_tensor_elements(descriptor.shape, maximum, resource));
    }
}

void validate_runtime_input_budget(
    const std::span<const Ort::Value> values,
    const std::size_t maximum) {
    for (const auto& value : values) {
        if (!value.IsTensor()) {
            throw application::InferenceError("ONNX runtime input is not a tensor");
        }
        const auto info = value.GetTensorTypeAndShapeInfo();
        const auto count = info.GetElementCount();
        application::require_resource_count(count, maximum, "ONNX runtime input tensor elements");
    }
}

[[nodiscard]] std::vector<TensorDescriptor> inspect_descriptors(
    Ort::Session& session,
    const bool inputs) {
    Ort::AllocatorWithDefaultOptions allocator{};
    const auto count = inputs ? session.GetInputCount() : session.GetOutputCount();
    std::vector<TensorDescriptor> descriptors{};
    descriptors.reserve(count);

    for (std::size_t index = 0; index < count; ++index) {
        auto name = inputs
            ? session.GetInputNameAllocated(index, allocator)
            : session.GetOutputNameAllocated(index, allocator);
        const auto type_info = inputs
            ? session.GetInputTypeInfo(index)
            : session.GetOutputTypeInfo(index);

        if (type_info.GetONNXType() != ONNX_TYPE_TENSOR) {
            throw application::ModelLoadError(
                std::string{inputs ? "input" : "output"} +
                " node is not a tensor: " + name.get());
        }

        const auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
        descriptors.push_back(TensorDescriptor{
            .name = name.get(),
            .element_type = tensor_info.GetElementType(),
            .shape = tensor_info.GetShape(),
        });
    }

    return descriptors;
}

[[nodiscard]] std::string redact_path(
    std::string detail,
    const std::filesystem::path& path) {
    const auto replacement = path.filename().string();
    const auto replace_all = [&detail, &replacement](const std::string& value) {
        if (value.empty()) {
            return;
        }
        std::size_t offset = 0U;
        while ((offset = detail.find(value, offset)) != std::string::npos) {
            detail.replace(offset, value.size(), replacement);
            offset += replacement.size();
        }
    };

    replace_all(path.string());
    try {
        replace_all(std::filesystem::absolute(path).string());
    } catch (...) {
    }
    return detail;
}

[[nodiscard]] std::string model_load_message(
    const std::filesystem::path& path,
    const std::string_view detail) {
    return "failed to load ONNX model '" + path.filename().string() +
           "': " + redact_path(std::string{detail}, path);
}

} // namespace

OnnxRuntimeEnvironment::OnnxRuntimeEnvironment(
    const OrtLoggingLevel level,
    const char* log_id)
    : environment_(level, log_id) {}

Ort::SessionOptions OnnxSession::build_options(
    const OnnxSessionConfig& config,
    OnnxExecutionProviderDiagnostics& diagnostics) {
    if (config.intra_op_threads < 0 || config.inter_op_threads < 0) {
        throw application::ConfigurationError("ONNX Runtime thread counts cannot be negative");
    }
    if (config.max_tensor_elements == 0U ||
        config.max_tensor_elements > application::default_resource_budget.max_tensor_elements * 8U) {
        throw application::ConfigurationError("ONNX max_tensor_elements is outside safe limits");
    }

    Ort::SessionOptions options{};
    options.SetGraphOptimizationLevel(config.optimization);
    if (config.intra_op_threads > 0) {
        options.SetIntraOpNumThreads(config.intra_op_threads);
    }
    if (config.inter_op_threads > 0) {
        options.SetInterOpNumThreads(config.inter_op_threads);
    }
    diagnostics = OnnxExecutionProviderStrategy::configure(options, config.execution_provider);
    return options;
}

OnnxSession::OnnxSession(
    std::shared_ptr<OnnxRuntimeEnvironment> environment,
    const std::filesystem::path& model_path,
    const OnnxSessionConfig& config)
    : environment_(std::move(environment)),
      model_path_(model_path),
      max_tensor_elements_(config.max_tensor_elements) {
    if (!environment_) {
        throw application::ModelLoadError("ONNX Runtime environment is null");
    }
    if (!std::filesystem::is_regular_file(model_path_)) {
        throw application::ModelLoadError(
            model_load_message(model_path_, "model file does not exist or is not a regular file"));
    }

    try {
        auto options = build_options(config, execution_provider_diagnostics_);
        session_ = Ort::Session(environment_->native(), model_path_.c_str(), options);
        inputs_ = inspect_inputs(session_);
        outputs_ = inspect_outputs(session_);

        if (inputs_.empty() || outputs_.empty()) {
            throw application::ModelLoadError(
                model_load_message(model_path_, "model has no tensor inputs or outputs"));
        }
        validate_descriptor_budget(inputs_, max_tensor_elements_, "input");
        validate_descriptor_budget(outputs_, max_tensor_elements_, "output");
    } catch (const application::EngineError&) {
        throw;
    } catch (const Ort::Exception& exception) {
        throw application::ModelLoadError(model_load_message(model_path_, exception.what()));
    } catch (const std::bad_alloc&) {
        throw application::ResourceExhaustedError("ONNX session allocation exceeded memory budget");
    } catch (const std::exception& exception) {
        throw application::ModelLoadError(model_load_message(model_path_, exception.what()));
    }
}

std::vector<Ort::Value> OnnxSession::run(
    const std::span<const char* const> input_names,
    const std::span<const Ort::Value> input_values,
    const std::span<const char* const> output_names) {
    if (input_names.size() != input_values.size()) {
        throw application::InferenceError("ONNX input name/value counts do not match");
    }
    if (input_names.empty() || output_names.empty()) {
        throw application::InferenceError("ONNX inference requires at least one input and one output");
    }

    try {
        validate_runtime_input_budget(input_values, max_tensor_elements_);
        auto outputs = session_.Run(
            Ort::RunOptions{nullptr},
            input_names.data(),
            input_values.data(),
            input_values.size(),
            output_names.data(),
            output_names.size());
        for (const auto& output : outputs) {
            if (!output.IsTensor()) {
                throw application::InferenceError("ONNX runtime output is not a tensor");
            }
            application::require_resource_count(
                output.GetTensorTypeAndShapeInfo().GetElementCount(),
                max_tensor_elements_,
                "ONNX runtime output tensor elements");
        }
        return outputs;
    } catch (const application::EngineError&) {
        throw;
    } catch (const std::bad_alloc&) {
        throw application::ResourceExhaustedError("ONNX inference allocation exceeded memory budget");
    } catch (const Ort::Exception&) {
        throw application::InferenceError("ONNX Runtime inference failed");
    } catch (const std::exception&) {
        throw application::InferenceError("ONNX inference failed");
    }
}

std::vector<TensorDescriptor> OnnxSession::inspect_inputs(Ort::Session& session) {
    return inspect_descriptors(session, true);
}

std::vector<TensorDescriptor> OnnxSession::inspect_outputs(Ort::Session& session) {
    return inspect_descriptors(session, false);
}

} // namespace fac_lpr::infrastructure::onnx
