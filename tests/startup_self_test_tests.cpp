#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/startup_self_test.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {
using namespace fac_lpr;

TEST(StartupSelfTest, RequiredFailureMarksEngineFailed) {
    auto diagnostics = std::make_shared<application::EngineDiagnostics>();
    application::StartupSelfTestRunner runner{diagnostics};

    const auto report = runner.run({
        {.name = "detector", .required = true, .execute = [] { throw application::ModelLoadError("bad detector"); }},
        {.name = "optional-ocr", .required = false, .execute = [] {}}});

    EXPECT_EQ(report.readiness, application::ReadinessState::failed);
    EXPECT_FALSE(report.ready());
    const auto snapshot = diagnostics->snapshot();
    EXPECT_EQ(snapshot.readiness, application::ReadinessState::failed);
    ASSERT_EQ(snapshot.startup_checks.size(), 2U);
    EXPECT_FALSE(snapshot.startup_checks[0].passed);
}

TEST(StartupSelfTest, OptionalFailureMarksEngineDegradedButReady) {
    auto diagnostics = std::make_shared<application::EngineDiagnostics>();
    application::StartupSelfTestRunner runner{diagnostics};

    const auto report = runner.run({
        {.name = "detector", .required = true, .execute = [] {}},
        {.name = "optional-ocr", .required = false, .execute = [] { throw application::InferenceError("optional failed"); }}});

    EXPECT_EQ(report.readiness, application::ReadinessState::degraded);
    EXPECT_TRUE(report.ready());
    EXPECT_EQ(diagnostics->snapshot().readiness, application::ReadinessState::degraded);
}

TEST(StartupSelfTest, AllPassingChecksMarkEngineReady) {
    auto diagnostics = std::make_shared<application::EngineDiagnostics>();
    application::StartupSelfTestRunner runner{diagnostics};

    const auto report = runner.run({
        {.name = "manifest", .required = true, .execute = [] {}},
        {.name = "detector", .required = true, .execute = [] {}},
        {.name = "recognizer", .required = true, .execute = [] {}}});

    EXPECT_EQ(report.readiness, application::ReadinessState::ready);
    EXPECT_TRUE(report.ready());
    for (const auto& check : report.checks) EXPECT_TRUE(check.passed);
}

} // namespace
