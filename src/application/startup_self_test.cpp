#include <fac_lpr/application/startup_self_test.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

namespace fac_lpr::application {

StartupSelfTestRunner::StartupSelfTestRunner(std::shared_ptr<EngineDiagnostics> diagnostics)
    : diagnostics_(std::move(diagnostics)) {
    if (!diagnostics_) {
        throw ConfigurationError("startup self-test requires diagnostics");
    }
}

StartupSelfTestReport StartupSelfTestRunner::run(
    const std::vector<StartupSelfTestCheck>& checks) const {
    StartupSelfTestReport report{};
    bool required_failed = false;
    bool optional_failed = false;
    report.checks.reserve(checks.size());

    for (const auto& check : checks) {
        StartupCheckSnapshot snapshot{};
        snapshot.name = check.name;
        snapshot.required = check.required;

        if (check.name.empty() || !check.execute) {
            snapshot.passed = false;
            snapshot.summary = "invalid startup self-test registration";
        } else {
            try {
                check.execute();
                snapshot.passed = true;
                snapshot.summary = "ok";
            } catch (const EngineError& error) {
                snapshot.passed = false;
                snapshot.summary = error.what();
            } catch (const std::exception& error) {
                snapshot.passed = false;
                snapshot.summary = error.what();
            } catch (...) {
                snapshot.passed = false;
                snapshot.summary = "unknown startup self-test failure";
            }
        }

        if (!snapshot.passed) {
            if (snapshot.required) {
                required_failed = true;
            } else {
                optional_failed = true;
            }
        }
        report.checks.push_back(std::move(snapshot));
    }

    report.readiness = required_failed
        ? ReadinessState::failed
        : optional_failed
            ? ReadinessState::degraded
            : ReadinessState::ready;
    diagnostics_->set_readiness(report.readiness, report.checks);
    return report;
}

} // namespace fac_lpr::application
