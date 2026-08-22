#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/onnx/execution_provider.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace fac_lpr::infrastructure::onnx {
namespace {

[[nodiscard]] const char* runtime_provider_name(const OnnxExecutionProvider provider) noexcept {
    switch (provider) {
    case OnnxExecutionProvider::cpu:
        return "CPUExecutionProvider";
    case OnnxExecutionProvider::cuda:
        return "CUDAExecutionProvider";
    case OnnxExecutionProvider::directml:
        return "DmlExecutionProvider";
    case OnnxExecutionProvider::tensorrt:
        return "TensorrtExecutionProvider";
    }
    return "CPUExecutionProvider";
}

[[nodiscard]] bool contains_provider(
    const std::vector<std::string>& providers,
    const OnnxExecutionProvider provider) {
    const std::string expected{runtime_provider_name(provider)};
    return std::find(providers.begin(), providers.end(), expected) != providers.end();
}

} // namespace

const char* to_string(const OnnxExecutionProvider provider) noexcept {
    switch (provider) {
    case OnnxExecutionProvider::cpu:
        return "cpu";
    case OnnxExecutionProvider::cuda:
        return "cuda";
    case OnnxExecutionProvider::directml:
        return "directml";
    case OnnxExecutionProvider::tensorrt:
        return "tensorrt";
    }
    return "cpu";
}

std::vector<std::string> OnnxExecutionProviderStrategy::available_providers() {
    try {
        return Ort::GetAvailableProviders();
    } catch (const Ort::Exception& exception) {
        throw application::ConfigurationError(
            std::string{"failed to enumerate ONNX Runtime execution providers: "} + exception.what());
    }
}

OnnxExecutionProviderDiagnostics OnnxExecutionProviderStrategy::configure(
    Ort::SessionOptions& options,
    const OnnxExecutionProviderConfig& config) {
    auto available = available_providers();
    OnnxExecutionProviderDiagnostics diagnostics{
        .requested = config.provider,
        .active = config.provider,
        .fallback_used = false,
        .available_providers = available,
    };

    if (config.provider == OnnxExecutionProvider::cpu) {
        diagnostics.active = OnnxExecutionProvider::cpu;
        return diagnostics;
    }

    if (!contains_provider(available, config.provider)) {
        if (config.fallback_policy == OnnxProviderFallbackPolicy::fallback_to_cpu) {
            diagnostics.active = OnnxExecutionProvider::cpu;
            diagnostics.fallback_used = true;
            return diagnostics;
        }
        throw application::ConfigurationError(
            std::string{"requested ONNX execution provider is unavailable: "} + to_string(config.provider));
    }

    std::vector<const char*> keys{};
    std::vector<const char*> values{};
    keys.reserve(config.options.size());
    values.reserve(config.options.size());
    for (const auto& [key, value] : config.options) {
        keys.push_back(key.c_str());
        values.push_back(value.c_str());
    }

    try {
        Ort::ThrowOnError(Ort::GetApi().SessionOptionsAppendExecutionProvider(
            options,
            runtime_provider_name(config.provider),
            keys.empty() ? nullptr : keys.data(),
            values.empty() ? nullptr : values.data(),
            keys.size()));
    } catch (const Ort::Exception& exception) {
        if (config.fallback_policy == OnnxProviderFallbackPolicy::fallback_to_cpu) {
            diagnostics.active = OnnxExecutionProvider::cpu;
            diagnostics.fallback_used = true;
            return diagnostics;
        }
        throw application::ConfigurationError(
            std::string{"failed to configure ONNX execution provider '"} +
            to_string(config.provider) + "': " + exception.what());
    }

    return diagnostics;
}

} // namespace fac_lpr::infrastructure::onnx
