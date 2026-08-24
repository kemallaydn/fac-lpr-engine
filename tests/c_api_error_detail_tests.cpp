#include <fac_lpr/fac_lpr_engine.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

namespace {

TEST(CApiErrorDetailV1, FailureMessageUsesCallerOwnedBuffer) {
    fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
    config.abi_version = 999U;
    fac_lpr_engine_handle* handle = nullptr;

    ASSERT_EQ(
        fac_lpr_engine_create_v1(&config, &handle),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);
    ASSERT_EQ(handle, nullptr);

    std::size_t required = 0U;
    EXPECT_EQ(
        fac_lpr_get_last_error_v1(nullptr, 0U, &required),
        FAC_LPR_STATUS_BUFFER_TOO_SMALL);
    ASSERT_GT(required, 1U);

    std::vector<char> buffer(required);
    std::size_t reported = 0U;
    EXPECT_EQ(
        fac_lpr_get_last_error_v1(buffer.data(), buffer.size(), &reported),
        FAC_LPR_STATUS_OK);
    EXPECT_EQ(reported, required);
    EXPECT_EQ(buffer.back(), '\0');
    EXPECT_NE(std::string{buffer.data()}.find("unsupported C ABI version"), std::string::npos);
}

TEST(CApiErrorDetailV1, TooSmallErrorBufferReportsExactRequiredSize) {
    fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
    config.reserved_flags = 1U;
    fac_lpr_engine_handle* handle = nullptr;
    ASSERT_EQ(
        fac_lpr_engine_create_v1(&config, &handle),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);

    std::size_t required = 0U;
    ASSERT_EQ(
        fac_lpr_get_last_error_v1(nullptr, 0U, &required),
        FAC_LPR_STATUS_BUFFER_TOO_SMALL);
    ASSERT_GT(required, 1U);

    std::vector<char> short_buffer(required - 1U);
    std::size_t reported = 0U;
    EXPECT_EQ(
        fac_lpr_get_last_error_v1(short_buffer.data(), short_buffer.size(), &reported),
        FAC_LPR_STATUS_BUFFER_TOO_SMALL);
    EXPECT_EQ(reported, required);
}

TEST(CApiErrorDetailV1, SuccessfulApiCallClearsPreviousError) {
    fac_lpr_engine_config_v1 invalid = FAC_LPR_ENGINE_CONFIG_V1_INIT;
    invalid.abi_version = 999U;
    fac_lpr_engine_handle* handle = nullptr;
    ASSERT_EQ(
        fac_lpr_engine_create_v1(&invalid, &handle),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);

    fac_lpr_engine_config_v1 valid = FAC_LPR_ENGINE_CONFIG_V1_INIT;
    ASSERT_EQ(fac_lpr_engine_create_v1(&valid, &handle), FAC_LPR_STATUS_OK);
    ASSERT_NE(handle, nullptr);

    std::size_t required = 0U;
    EXPECT_EQ(
        fac_lpr_get_last_error_v1(nullptr, 0U, &required),
        FAC_LPR_STATUS_BUFFER_TOO_SMALL);
    EXPECT_EQ(required, 1U);

    char terminator = 'x';
    EXPECT_EQ(
        fac_lpr_get_last_error_v1(&terminator, 1U, &required),
        FAC_LPR_STATUS_OK);
    EXPECT_EQ(terminator, '\0');

    EXPECT_EQ(fac_lpr_engine_destroy_v1(&handle), FAC_LPR_STATUS_OK);
}

TEST(CApiErrorDetailV1, NullRequiredSizeIsRejectedWithoutThrowing) {
    EXPECT_EQ(
        fac_lpr_get_last_error_v1(nullptr, 0U, nullptr),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);
}

} // namespace
