#include <fac_lpr/infrastructure/onnx/onnx_session.hpp>

#include <filesystem>
#include <sstream>
#include <utility>

namespace fac_lpr::infrastructure::onnx {
namespace {

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

[[nodiscard]] std::string model_load_message(
    const std::filesystem::path& path,
    const std::string_view detail) {
    std::ostringstream stream{};
    stream << "failed to load ONNX model '" << path.string() << "': " << detail;
    return stream.str();
}

} // namespace

OnnxRuntimeEnvironment::OnnxRuntimeEnvironment(
    const OrtLoggingLevel level,
    const char* log_id)
    : environment_(level, log_id) {}

Ort::SessionOptions OnnxSession::build_options(const OnnxSessionConfig& config) {
    if (config.intra_op_threads < 0 || config.inter_op_threads < 0) {
        throw application::ConfigurationError("ONNX Runtime thread counts cannot be negative");
    }

    Ort::SessionOptions options{};
    options.SetGraphOptimizationLevel(config.optimization);
    if (config.intra_op_threads > 0) {
        options.SetIntraOpNumThreads(config.intra_op_threads);
    }
    if (config.inter_op_threads > 0) {
        options.SetInterOpNumThreads(config.inter_op_threads);
    }
    return options;
}

OnnxSession::OnnxSession(
    std::shared_ptr<OnnxRuntimeEnvironment> environment,
    const std::filesystem::path& model_path,
    const OnnxSessionConfig& config)
    : environment_(std::move(environment)),
      model_path_(model_path) {
    if (!environment_) {
        throw application::ModelLoadError("ONNX Runtime environment is null");
    }
    if (!std::filesystem::is_regular_file(model_path_)) {
        throw application::ModelLoadError(
            model_load_message(model_path_, "model file does not exist or is not a regular file"));
    }

    try {
        auto options = build_options(config);
        session_ = Ort::Session(environment_->native(), model_path_.c_str(), options);
        inputs_ = inspect_inputs(session_);
        outputs_ = inspect_outputs(session_);

        if (inputs_.empty() || outputs_.empty()) {
            throw application::ModelLoadError(
                model_load_message(model_path_, "model has no tensor inputs or outputs"));
        }
    } catch (const application::EngineError&) {
        throw;
    } catch (const Ort::Exception& exception) {
        throw application::ModelLoadError(model_load_message(model_path_, exception.what()));
    } catch (const std::exception& exception) {
        throw application::ModelLoadError(model_load_message(model_path_, exception.what()));
    }
}

std::vector<TensorDescriptor> OnnxSession::inspect_inputs(Ort::Session& session) {
    return inspect_descriptors(session, true);
}

std::vector<TensorDescriptor> OnnxSession::inspect_outputs(Ort::Session& session) {
    return inspect_descriptors(session, false);
}

} // namespace fac_lpr::infrastructure::onnx
