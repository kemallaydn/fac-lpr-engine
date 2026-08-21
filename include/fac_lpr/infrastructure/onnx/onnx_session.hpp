#pragma once

#include <fac_lpr/application/error.hpp>

#include <onnxruntime_cxx_api.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fac_lpr::infrastructure::onnx {

struct TensorDescriptor final {
    std::string name{};
    ONNXTensorElementDataType element_type{ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED};
    std::vector<std::int64_t> shape{};
};

class OnnxRuntimeEnvironment final {
public:
    explicit OnnxRuntimeEnvironment(
        OrtLoggingLevel level = ORT_LOGGING_LEVEL_WARNING,
        const char* log_id = "fac-lpr-engine");

    OnnxRuntimeEnvironment(const OnnxRuntimeEnvironment&) = delete;
    OnnxRuntimeEnvironment& operator=(const OnnxRuntimeEnvironment&) = delete;
    OnnxRuntimeEnvironment(OnnxRuntimeEnvironment&&) = delete;
    OnnxRuntimeEnvironment& operator=(OnnxRuntimeEnvironment&&) = delete;

    [[nodiscard]] Ort::Env& native() noexcept { return environment_; }

private:
    Ort::Env environment_;
};

struct OnnxSessionConfig final {
    int intra_op_threads{0};
    int inter_op_threads{0};
    GraphOptimizationLevel optimization{GraphOptimizationLevel::ORT_ENABLE_ALL};
};

class OnnxSession final {
public:
    OnnxSession(
        std::shared_ptr<OnnxRuntimeEnvironment> environment,
        const std::filesystem::path& model_path,
        const OnnxSessionConfig& config = {});

    OnnxSession(const OnnxSession&) = delete;
    OnnxSession& operator=(const OnnxSession&) = delete;
    OnnxSession(OnnxSession&&) noexcept = default;
    OnnxSession& operator=(OnnxSession&&) noexcept = default;
    ~OnnxSession() = default;

    [[nodiscard]] Ort::Session& native() noexcept { return session_; }
    [[nodiscard]] const Ort::Session& native() const noexcept { return session_; }

    [[nodiscard]] const std::vector<TensorDescriptor>& inputs() const noexcept { return inputs_; }
    [[nodiscard]] const std::vector<TensorDescriptor>& outputs() const noexcept { return outputs_; }
    [[nodiscard]] const std::filesystem::path& model_path() const noexcept { return model_path_; }

private:
    [[nodiscard]] static Ort::SessionOptions build_options(const OnnxSessionConfig& config);
    [[nodiscard]] static std::vector<TensorDescriptor> inspect_inputs(Ort::Session& session);
    [[nodiscard]] static std::vector<TensorDescriptor> inspect_outputs(Ort::Session& session);

    std::shared_ptr<OnnxRuntimeEnvironment> environment_;
    std::filesystem::path model_path_;
    Ort::Session session_{nullptr};
    std::vector<TensorDescriptor> inputs_{};
    std::vector<TensorDescriptor> outputs_{};
};

} // namespace fac_lpr::infrastructure::onnx
