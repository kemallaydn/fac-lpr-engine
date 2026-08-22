#include <fac_lpr/fac_lpr_engine.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

extern "C" int fac_lpr_c_header_smoke(void);

namespace {

TEST(CApiV1, PublicHeaderCompilesAsC) {
    EXPECT_EQ(fac_lpr_c_header_smoke(), 0);
}

TEST(CApiV1, CreateAndDestroyOpaqueHandleSafely) {
    fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
    fac_lpr_engine_handle* handle = nullptr;

    EXPECT_EQ(
        fac_lpr_engine_create_v1(&config, &handle),
        FAC_LPR_STATUS_OK);
    ASSERT_NE(handle, nullptr);

    auto* stale_copy = handle;
    EXPECT_EQ(fac_lpr_engine_destroy_v1(&handle), FAC_LPR_STATUS_OK);
    EXPECT_EQ(handle, nullptr);

    EXPECT_EQ(fac_lpr_engine_destroy_v1(&handle), FAC_LPR_STATUS_OK);
    EXPECT_EQ(fac_lpr_engine_destroy_v1(&stale_copy), FAC_LPR_STATUS_OK);
    EXPECT_EQ(stale_copy, nullptr);
    EXPECT_EQ(fac_lpr_engine_destroy_v1(nullptr), FAC_LPR_STATUS_OK);
}

TEST(CApiV1, NullConfigUsesV1DefaultsForLifecycleShell) {
    fac_lpr_engine_handle* handle = nullptr;
    EXPECT_EQ(fac_lpr_engine_create_v1(nullptr, &handle), FAC_LPR_STATUS_OK);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(fac_lpr_engine_destroy_v1(&handle), FAC_LPR_STATUS_OK);
}

TEST(CApiV1, InvalidConfigVersionAndSizeAreRejectedWithoutLeakingHandle) {
    fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
    fac_lpr_engine_handle* handle = reinterpret_cast<fac_lpr_engine_handle*>(0x1);

    config.abi_version = 999U;
    EXPECT_EQ(
        fac_lpr_engine_create_v1(&config, &handle),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);
    EXPECT_EQ(handle, nullptr);

    config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
    config.struct_size = sizeof(std::uint32_t);
    handle = reinterpret_cast<fac_lpr_engine_handle*>(0x1);
    EXPECT_EQ(
        fac_lpr_engine_create_v1(&config, &handle),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);
    EXPECT_EQ(handle, nullptr);
}

TEST(CApiV1, RecognizeRejectsNullOrMalformedInputsWithoutThrowing) {
    std::size_t required = 123U;
    EXPECT_EQ(
        fac_lpr_engine_recognize_v1(nullptr, nullptr, nullptr, 0U, &required),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);
    EXPECT_EQ(required, 0U);

    fac_lpr_engine_handle* handle = nullptr;
    ASSERT_EQ(fac_lpr_engine_create_v1(nullptr, &handle), FAC_LPR_STATUS_OK);

    fac_lpr_image_view_v1 image = FAC_LPR_IMAGE_VIEW_V1_INIT;
    EXPECT_EQ(
        fac_lpr_engine_recognize_v1(handle, &image, nullptr, 0U, &required),
        FAC_LPR_STATUS_INVALID_IMAGE);

    std::array<std::uint8_t, 12U> pixels{};
    image.data = pixels.data();
    image.data_size = pixels.size();
    image.width = 2U;
    image.height = 2U;
    image.stride_bytes = 6U;
    image.pixel_format = FAC_LPR_PIXEL_FORMAT_BGR8;

    EXPECT_EQ(
        fac_lpr_engine_recognize_v1(handle, &image, nullptr, 0U, &required),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);
    EXPECT_EQ(required, 0U);

    EXPECT_EQ(
        fac_lpr_engine_recognize_v1(handle, &image, nullptr, 4U, &required),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);

    EXPECT_EQ(
        fac_lpr_engine_recognize_v1(handle, &image, nullptr, 0U, nullptr),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);

    EXPECT_EQ(fac_lpr_engine_destroy_v1(&handle), FAC_LPR_STATUS_OK);
}

TEST(CApiV1, ImageStrideAndVersionValidationMapToInvalidImage) {
    fac_lpr_engine_handle* handle = nullptr;
    ASSERT_EQ(fac_lpr_engine_create_v1(nullptr, &handle), FAC_LPR_STATUS_OK);

    std::array<std::uint8_t, 12U> pixels{};
    fac_lpr_image_view_v1 image = FAC_LPR_IMAGE_VIEW_V1_INIT;
    image.data = pixels.data();
    image.data_size = pixels.size();
    image.width = 2U;
    image.height = 2U;
    image.stride_bytes = 5U;
    image.pixel_format = FAC_LPR_PIXEL_FORMAT_BGR8;
    std::size_t required = 0U;

    EXPECT_EQ(
        fac_lpr_engine_recognize_v1(handle, &image, nullptr, 0U, &required),
        FAC_LPR_STATUS_INVALID_IMAGE);

    image.stride_bytes = 6U;
    image.abi_version = 99U;
    EXPECT_EQ(
        fac_lpr_engine_recognize_v1(handle, &image, nullptr, 0U, &required),
        FAC_LPR_STATUS_INVALID_IMAGE);

    EXPECT_EQ(fac_lpr_engine_destroy_v1(&handle), FAC_LPR_STATUS_OK);
}

} // namespace
