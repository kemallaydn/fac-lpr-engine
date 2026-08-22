#pragma once

#include <onnxruntime_cxx_api.h>

#include <string>
#include <utility>
#include <vector>

namespace fac_lpr::infrastructure::onnx {

enum class OnnxExecutionProvider {
    cpu,
    cuda,
    directml,
    tensorrt,
};

enum class OnnxProviderFallbackPolicy {
    fail_fast,
    fallback_to_cpu,
};

struct OnnxExecutionProviderConfig final {
    OnnxExecutionProvider provider{OnnxExecutionProvider::cpu};
    OnnxProviderFallbackPolicy fallback_policy{OnnxProviderFallbackPolicy::fail_fast};
    std::vector<std::pair<std::string, std::string>> options{};
};

struct OnnxExecutionProviderDiagnostics final {
    OnnxExecutionProvider requested{OnnxExecutionProvider::cpu};
    OnnxExecutionProvider active{OnnxExecutionProvider::cpu};
    bool fallback_used{false};
    std::vector<std::string> available_providers{};
};

[[nodiscard]] const char* to_string(OnnxExecutionProvider provider) noexcept;

class OnnxExecutionProviderStrategy final {
public:
    [[nodiscard]] static std::vector<std::string> available_providers();
    [[nodiscard]] static OnnxExecutionProviderDiagnostics configure(
        Ort::SessionOptions& options,
        const OnnxExecutionProviderConfig& config);
};

} // namespace fac_lpr::infrastructure::onnx
