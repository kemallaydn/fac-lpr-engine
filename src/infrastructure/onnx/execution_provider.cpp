#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/onnx/execution_provider.hpp>

#include <algorithm>
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

struct OptionPointers final {
    std::vector<const char*> keys{};
    std::vector<const char*> values{};
};

[[nodiscard]] OptionPointers option_pointers(const OnnxExecutionProviderConfig& config) {
    OptionPointers pointers{};
    pointers.keys.reserve(config.options.size());
    pointers.values.reserve(config.options.size());
    for (const auto& [key, value] : config.options) {
        if (key.empty() || value.empty()) {
            throw application::ConfigurationError("ONNX execution provider option key/value cannot be empty");
        }
        pointers.keys.push_back(key.c_str());
        pointers.values.push_back(value.c_str());
    }
    return pointers;
}

void append_cuda(
    Ort::SessionOptions& options,
    const OptionPointers& pointers) {
    const auto& api = Ort::GetApi();
    OrtCUDAProviderOptionsV2* provider_options = nullptr;
    Ort::ThrowOnError(api.CreateCUDAProviderOptions(&provider_options));
    try {
        if (!pointers.keys.empty()) {
            Ort::ThrowOnError(api.UpdateCUDAProviderOptions(
                provider_options,
                pointers.keys.data(),
                pointers.values.data(),
                pointers.keys.size()));
        }
        Ort::ThrowOnError(api.SessionOptionsAppendExecutionProvider_CUDA_V2(options, provider_options));
    } catch (...) {
        api.ReleaseCUDAProviderOptions(provider_options);
        throw;
    }
    api.ReleaseCUDAProviderOptions(provider_options);
}

void append_tensorrt(
    Ort::SessionOptions& options,
    const OptionPointers& pointers) {
    const auto& api = Ort::GetApi();
    OrtTensorRTProviderOptionsV2* provider_options = nullptr;
    Ort::ThrowOnError(api.CreateTensorRTProviderOptions(&provider_options));
    try {
        if (!pointers.keys.empty()) {
            Ort::ThrowOnError(api.UpdateTensorRTProviderOptions(
                provider_options,
                pointers.keys.data(),
                pointers.values.data(),
                pointers.keys.size()));
        }
        Ort::ThrowOnError(api.SessionOptionsAppendExecutionProvider_TensorRT_V2(options, provider_options));
    } catch (...) {
        api.ReleaseTensorRTProviderOptions(provider_options);
        throw;
    }
    api.ReleaseTensorRTProviderOptions(provider_options);
}

void append_directml(
    Ort::SessionOptions& options,
    const OptionPointers& pointers) {
    Ort::ThrowOnError(Ort::GetApi().SessionOptionsAppendExecutionProvider(
        options,
        "DML",
        pointers.keys.empty() ? nullptr : pointers.keys.data(),
        pointers.values.empty() ? nullptr : pointers.values.data(),
        pointers.keys.size()));
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

    try {
        const auto pointers = option_pointers(config);
        switch (config.provider) {
        case OnnxExecutionProvider::cpu:
            break;
        case OnnxExecutionProvider::cuda:
            append_cuda(options, pointers);
            break;
        case OnnxExecutionProvider::directml:
            append_directml(options, pointers);
            break;
        case OnnxExecutionProvider::tensorrt:
            append_tensorrt(options, pointers);
            break;
        }
    } catch (const application::EngineError&) {
        throw;
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
