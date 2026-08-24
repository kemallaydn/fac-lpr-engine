#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/infrastructure/native_image/native_image.hpp>
#include <fac_lpr/infrastructure/yolo/yolo_pose_preprocessor.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace {
using fac_lpr::application::ImageView;
using fac_lpr::application::PerformanceConfig;
using fac_lpr::application::PixelFormat;
using fac_lpr::application::validate_image;
using fac_lpr::infrastructure::native_image::NativeImageWorkspace;
using fac_lpr::infrastructure::yolo::YoloInputSpec;
using fac_lpr::infrastructure::yolo::YoloPosePreprocessor;

TEST(YoloPreprocessor, UsesInspectedProductionInputContract) {
    // best.onnx: images float32 [1, 3, 960, 960]
    const YoloPosePreprocessor preprocessor{
        YoloInputSpec{960U, 960U, 3U, 1.0F / 255.0F, 114.0F}};
    EXPECT_EQ(preprocessor.spec().width, 960U);
    EXPECT_EQ(preprocessor.spec().height, 960U);
    EXPECT_EQ(preprocessor.spec().channels, 3U);
    EXPECT_EQ(preprocessor.tensor_elements(), 3U * 960U * 960U);
}

TEST(YoloPreprocessor, ConvertsBgrDirectlyToRgbChw) {
    std::vector<std::byte> bytes{std::byte{10}, std::byte{20}, std::byte{30}};
    const auto image = validate_image(
        ImageView{bytes, 1U, 1U, 3U, PixelFormat::bgr8},
        PerformanceConfig{});
    const YoloPosePreprocessor preprocessor{
        YoloInputSpec{1U, 1U, 3U, 1.0F / 255.0F, 114.0F}};
    const auto output = preprocessor.preprocess(image);
    ASSERT_EQ(output.chw.size(), 3U);
    EXPECT_NEAR(output.chw[0], 30.0F / 255.0F, 1.0e-6F);
    EXPECT_NEAR(output.chw[1], 20.0F / 255.0F, 1.0e-6F);
    EXPECT_NEAR(output.chw[2], 10.0F / 255.0F, 1.0e-6F);
}

TEST(YoloPreprocessor, ProducesDeterministicLetterboxMetadata) {
    std::vector<std::byte> bytes(4U * 2U * 3U, std::byte{64});
    const auto image = validate_image(
        ImageView{bytes, 4U, 2U, 12U, PixelFormat::rgb8},
        PerformanceConfig{});
    const YoloPosePreprocessor preprocessor{
        YoloInputSpec{4U, 4U, 3U, 1.0F / 255.0F, 114.0F}};
    NativeImageWorkspace workspace{};
    const auto first = preprocessor.preprocess(image, workspace);
    std::vector<float> snapshot(first.chw.begin(), first.chw.end());
    const auto second = preprocessor.preprocess(image, workspace);
    EXPECT_EQ(snapshot.size(), second.chw.size());
    EXPECT_TRUE(std::equal(snapshot.begin(), snapshot.end(), second.chw.begin()));
    EXPECT_EQ(second.letterbox.resized_width, 4U);
    EXPECT_EQ(second.letterbox.resized_height, 2U);
    EXPECT_EQ(second.letterbox.pad_left, 0U);
    EXPECT_EQ(second.letterbox.pad_top, 1U);
}

TEST(YoloPreprocessor, ReusesCallerProvidedTensorStorage) {
    std::vector<std::byte> bytes(8U * 8U * 3U, std::byte{128});
    const auto image = validate_image(
        ImageView{bytes, 8U, 8U, 24U, PixelFormat::bgr8},
        PerformanceConfig{});
    const YoloPosePreprocessor preprocessor{
        YoloInputSpec{16U, 16U, 3U, 1.0F / 255.0F, 114.0F}};
    NativeImageWorkspace workspace{};
    const auto first = preprocessor.preprocess(image, workspace);
    const auto* first_address = first.chw.data();
    const auto capacity = workspace.tensor_capacity();
    const auto second = preprocessor.preprocess(image, workspace);
    EXPECT_EQ(second.chw.data(), first_address);
    EXPECT_EQ(workspace.tensor_capacity(), capacity);
}

} // namespace
