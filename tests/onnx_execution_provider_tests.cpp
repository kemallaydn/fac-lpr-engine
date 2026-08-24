#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/onnx/execution_provider.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace {
using namespace fac_lpr::infrastructure::onnx;

TEST(OnnxExecutionProviderStrategy, CpuIsDefaultAndNeverFallsBack) {
    Ort::SessionOptions options{};
    const auto diagnostics = OnnxExecutionProviderStrategy::configure(options, {});

    EXPECT_EQ(diagnostics.requested, OnnxExecutionProvider::cpu);
    EXPECT_EQ(diagnostics.active, OnnxExecutionProvider::cpu);
    EXPECT_FALSE(diagnostics.fallback_used);
}

TEST(OnnxExecutionProviderStrategy, UnavailableProviderCanFallBackToCpu) {
    const auto available = OnnxExecutionProviderStrategy::available_providers();
    const OnnxExecutionProvider candidates[] = {
        OnnxExecutionProvider::cuda,
        OnnxExecutionProvider::directml,
        OnnxExecutionProvider::tensorrt,
    };

    for (const auto candidate : candidates) {
        const std::string runtime_name = candidate == OnnxExecutionProvider::cuda
            ? "CUDAExecutionProvider"
            : candidate == OnnxExecutionProvider::directml
                ? "DmlExecutionProvider"
                : "TensorrtExecutionProvider";
        if (std::find(available.begin(), available.end(), runtime_name) != available.end()) {
            continue;
        }

        Ort::SessionOptions options{};
        OnnxExecutionProviderConfig config{};
        config.provider = candidate;
        config.fallback_policy = OnnxProviderFallbackPolicy::fallback_to_cpu;
        const auto diagnostics = OnnxExecutionProviderStrategy::configure(options, config);
        EXPECT_EQ(diagnostics.requested, candidate);
        EXPECT_EQ(diagnostics.active, OnnxExecutionProvider::cpu);
        EXPECT_TRUE(diagnostics.fallback_used);
        return;
    }

    GTEST_SKIP() << "All optional execution providers are available on this host";
}

TEST(OnnxExecutionProviderStrategy, UnavailableProviderFailsFast) {
    const auto available = OnnxExecutionProviderStrategy::available_providers();
    const OnnxExecutionProvider candidates[] = {
        OnnxExecutionProvider::cuda,
        OnnxExecutionProvider::directml,
        OnnxExecutionProvider::tensorrt,
    };

    for (const auto candidate : candidates) {
        const std::string runtime_name = candidate == OnnxExecutionProvider::cuda
            ? "CUDAExecutionProvider"
            : candidate == OnnxExecutionProvider::directml
                ? "DmlExecutionProvider"
                : "TensorrtExecutionProvider";
        if (std::find(available.begin(), available.end(), runtime_name) != available.end()) {
            continue;
        }

        Ort::SessionOptions options{};
        OnnxExecutionProviderConfig config{};
        config.provider = candidate;
        config.fallback_policy = OnnxProviderFallbackPolicy::fail_fast;
        EXPECT_THROW(
            OnnxExecutionProviderStrategy::configure(options, config),
            fac_lpr::application::ConfigurationError);
        return;
    }

    GTEST_SKIP() << "All optional execution providers are available on this host";
}

} // namespace
