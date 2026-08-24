#include <fac_lpr/application/lpr_pipeline.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace fac_lpr::application {
namespace {

[[nodiscard]] ImageBuffer synthetic_bgr_image(
    const std::size_t width,
    const std::size_t height) {
    ImageBuffer image{};
    image.width = width;
    image.height = height;
    image.stride_bytes = width * 3U;
    image.format = PixelFormat::bgr8;
    image.bytes.assign(image.stride_bytes * height, std::byte{0});
    return image;
}

} // namespace

StartupSelfTestReport LprPipeline::startup_self_test() const {
    StartupSelfTestRunner runner{dependencies_.diagnostics};
    std::vector<StartupSelfTestCheck> checks{};

    checks.push_back(StartupSelfTestCheck{
        .name = "model_integrity",
        .required = true,
        .execute = [diagnostics = dependencies_.diagnostics]() {
            const auto snapshot = diagnostics->snapshot();
            if (snapshot.models.empty()) {
                throw ModelLoadError("no active model integrity metadata registered");
            }
            const auto invalid = std::find_if(
                snapshot.models.begin(), snapshot.models.end(),
                [](const DiagnosticModelInfo& model) {
                    return model.name.empty() || model.sha256.empty() || !model.integrity_verified;
                });
            if (invalid != snapshot.models.end()) {
                throw ModelLoadError("active model integrity verification is incomplete");
            }
        }});

    checks.push_back(StartupSelfTestCheck{
        .name = std::string{"detector:"} + std::string{dependencies_.detector->name()},
        .required = true,
        .execute = [detector = dependencies_.detector]() {
            const auto image = synthetic_bgr_image(64U, 64U);
            (void)detector->detect(image.view(), {});
        }});

    bool has_enabled_recognizer = false;
    for (const auto& registration : dependencies_.recognition_ensemble->recognizers()) {
        if (!registration.enabled || !registration.provider) {
            continue;
        }
        has_enabled_recognizer = true;
        checks.push_back(StartupSelfTestCheck{
            .name = std::string{"recognizer:"} + std::string{registration.provider->name()},
            .required = registration.required,
            .execute = [provider = registration.provider]() {
                const auto image = synthetic_bgr_image(160U, 40U);
                (void)provider->recognize(image.view(), {});
            }});
    }

    if (!has_enabled_recognizer) {
        checks.push_back(StartupSelfTestCheck{
            .name = "recognizer_set",
            .required = true,
            .execute = [] {
                throw ConfigurationError("no enabled recognizer provider is configured");
            }});
    }

    return runner.run(checks);
}

} // namespace fac_lpr::application
