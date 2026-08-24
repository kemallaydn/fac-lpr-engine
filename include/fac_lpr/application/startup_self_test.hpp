#pragma once

#include <fac_lpr/application/engine_diagnostics.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fac_lpr::application {

struct StartupSelfTestCheck final {
    std::string name{};
    bool required{true};
    std::function<void()> execute{};
};

struct StartupSelfTestReport final {
    ReadinessState readiness{ReadinessState::failed};
    std::vector<StartupCheckSnapshot> checks{};

    [[nodiscard]] bool ready() const noexcept {
        return readiness == ReadinessState::ready || readiness == ReadinessState::degraded;
    }
};

class StartupSelfTestRunner final {
public:
    explicit StartupSelfTestRunner(std::shared_ptr<EngineDiagnostics> diagnostics);

    [[nodiscard]] StartupSelfTestReport run(
        const std::vector<StartupSelfTestCheck>& checks) const;

private:
    std::shared_ptr<EngineDiagnostics> diagnostics_{};
};

} // namespace fac_lpr::application
