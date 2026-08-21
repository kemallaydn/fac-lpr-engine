#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/infrastructure/lprnet/lprnet_preprocessor.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace {
using fac_lpr::application::ImageView;
using fac_lpr::application::PerformanceConfig;
using fac_lpr::application::PixelFormat;
using fac_lpr::application::validate_image;
using fac_lpr::infrastructure::lprnet::InputColorOrder;
using fac_lpr::infrastructure::lprnet::LprNetInputSpec;
using fac_lpr::infrastructure::lprnet::LprNetPreprocessor;
using fac_lpr::infrastructure::lprnet::TensorLayout;
using fac_lpr::infrastructure::native_image::NativeImageWorkspace;

std::vector<std::byte> make_bgr_fixture() {
    return {
        std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{0}, std::byte{255}, std::byte{0},
        std::byte{255}, std::byte{0}, std::byte{0},
        std::byte{255}, std::byte{255}, std::byte{255}};
}

TEST(LprNetPreprocessor, SyntheticGoldenRgbNchwTensorIsDeterministic) {
    auto bytes = make_bgr_fixture();
    const ImageView image{bytes, 2U, 2U, 6U, PixelFormat::bgr8};
    const auto validated = validate_image(image, PerformanceConfig{});

    LprNetInputSpec spec{};
    spec.width = 2U;
    spec.height = 2U;
    spec.channels = 3U;
    spec.layout = TensorLayout::nchw;
    spec.color_order = InputColorOrder::rgb;
    const LprNetPreprocessor preprocessor{spec};

    const auto tensor = preprocessor.preprocess(validated);
    ASSERT_EQ(tensor.values.size(), 12U);
    EXPECT_EQ(tensor.shape[0], 1);
    EXPECT_EQ(tensor.shape[1], 3);
    EXPECT_EQ(tensor.shape[2], 2);
    EXPECT_EQ(tensor.shape[3], 2);

    const std::vector<float> expected{
        1.0F, 0.0F, 0.0F, 1.0F,
        0.0F, 1.0F, 0.0F, 1.0F,
        0.0F, 0.0F, 1.0F, 1.0F};
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        EXPECT_NEAR(tensor.values[index], expected[index], 1.0e-6F);
    }
}

TEST(LprNetPreprocessor, SupportsNhwcAndConfiguredBgrOrder) {
    auto bytes = make_bgr_fixture();
    const ImageView image{bytes, 2U, 2U, 6U, PixelFormat::bgr8};
    const auto validated = validate_image(image, PerformanceConfig{});

    LprNetInputSpec spec{};
    spec.width = 2U;
    spec.height = 2U;
    spec.channels = 3U;
    spec.layout = TensorLayout::nhwc;
    spec.color_order = InputColorOrder::bgr;
    const LprNetPreprocessor preprocessor{spec};

    const auto tensor = preprocessor.preprocess(validated);
    EXPECT_EQ(tensor.shape[0], 1);
    EXPECT_EQ(tensor.shape[1], 2);
    EXPECT_EQ(tensor.shape[2], 2);
    EXPECT_EQ(tensor.shape[3], 3);
    ASSERT_GE(tensor.values.size(), 3U);
    EXPECT_NEAR(tensor.values[0], 0.0F, 1.0e-6F);
    EXPECT_NEAR(tensor.values[1], 0.0F, 1.0e-6F);
    EXPECT_NEAR(tensor.values[2], 1.0F, 1.0e-6F);
}

TEST(LprNetPreprocessor, ReusesNativeTensorWorkspace) {
    auto bytes = make_bgr_fixture();
    const ImageView image{bytes, 2U, 2U, 6U, PixelFormat::bgr8};
    const auto validated = validate_image(image, PerformanceConfig{});

    LprNetInputSpec spec{};
    spec.width = 4U;
    spec.height = 2U;
    spec.channels = 3U;
    const LprNetPreprocessor preprocessor{spec};
    NativeImageWorkspace workspace{};

    const auto first = preprocessor.preprocess(validated, workspace);
    const auto capacity = workspace.tensor_capacity();
    const auto second = preprocessor.preprocess(validated, workspace);
    EXPECT_EQ(first.values.size(), second.values.size());
    EXPECT_EQ(workspace.tensor_capacity(), capacity);
}

TEST(LprNetPreprocessor, InvalidChannelContractFailsFast) {
    LprNetInputSpec spec{};
    spec.width = 94U;
    spec.height = 24U;
    spec.channels = 2U;
    EXPECT_THROW(
        LprNetPreprocessor{spec},
        fac_lpr::application::ConfigurationError);
}

} // namespace
