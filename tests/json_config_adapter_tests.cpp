#include <fac_lpr/infrastructure/config/json_config_adapter.hpp>
#include <fac_lpr/application/error.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

namespace {

using fac_lpr::application::ConfigurationError;
using fac_lpr::infrastructure::JsonConfigLoadOptions;
using fac_lpr::infrastructure::UnknownFieldPolicy;
using fac_lpr::infrastructure::load_engine_config_json;
using fac_lpr::infrastructure::load_engine_config_json_file;

TEST(JsonConfigAdapter, LoadsVersionedConfigAndKeepsDefaults) {
    const auto config = load_engine_config_json(R"json({
        "schema_version": 1,
        "detector": {"confidence_threshold": 0.61, "max_detections": 12},
        "performance": {"worker_count": 3, "recognition_timeout_ms": 1500}
    })json");

    EXPECT_FLOAT_EQ(config.detector.confidence_threshold, 0.61F);
    EXPECT_EQ(config.detector.max_detections, 12U);
    EXPECT_EQ(config.performance.worker_count, 3U);
    EXPECT_EQ(config.performance.recognition_timeout, std::chrono::milliseconds{1500});
    EXPECT_EQ(config.recognition.beam_width, 16U);
}

TEST(JsonConfigAdapter, RequiresSupportedSchemaVersion) {
    EXPECT_THROW((void)load_engine_config_json(R"json({})json"), ConfigurationError);
    EXPECT_THROW((void)load_engine_config_json(R"json({"schema_version": 2})json"), ConfigurationError);
}

TEST(JsonConfigAdapter, RejectsUnknownFieldsByDefault) {
    EXPECT_THROW(
        (void)load_engine_config_json(R"json({"schema_version": 1, "mystery": true})json"),
        ConfigurationError);
    EXPECT_THROW(
        (void)load_engine_config_json(R"json({"schema_version": 1, "detector": {"mystery": 1}})json"),
        ConfigurationError);
}

TEST(JsonConfigAdapter, CanExplicitlyIgnoreUnknownFields) {
    JsonConfigLoadOptions options{};
    options.unknown_fields = UnknownFieldPolicy::ignore;
    const auto config = load_engine_config_json(
        R"json({"schema_version": 1, "mystery": true, "detector": {"mystery": 1, "confidence_threshold": 0.7}})json",
        options);
    EXPECT_FLOAT_EQ(config.detector.confidence_threshold, 0.7F);
}

TEST(JsonConfigAdapter, FailsFastOnInvalidTypesAndRanges) {
    EXPECT_THROW(
        (void)load_engine_config_json(R"json({"schema_version": 1, "performance": {"worker_count": -1}})json"),
        ConfigurationError);
    EXPECT_THROW(
        (void)load_engine_config_json(R"json({"schema_version": 1, "detector": {"confidence_threshold": "high"}})json"),
        ConfigurationError);
    EXPECT_THROW(
        (void)load_engine_config_json(R"json({"schema_version": 1, "decision": {"review_confidence_threshold": 0.9, "accepted_confidence_threshold": 0.8}})json"),
        ConfigurationError);
}

TEST(JsonConfigAdapter, RejectsMalformedJsonAndUnreadableFile) {
    EXPECT_THROW((void)load_engine_config_json("{"), ConfigurationError);
    EXPECT_THROW(
        (void)load_engine_config_json_file(std::filesystem::path{"definitely-not-a-real-fac-lpr-config.json"}),
        ConfigurationError);
}

} // namespace
