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

LprNetInputSpec make_unit_rgb_spec(
    const std::size_t width,
    const std::size_t height,
    const TensorLayout layout = TensorLayout::nchw,
    const InputColorOrder color_order = InputColorOrder::rgb) {
    LprNetInputSpec spec{};
    spec.width = width;
    spec.height = height;
    spec.channels = 3U;
    spec.layout = layout;
    spec.color_order = color_order;
    spec.input_scale = 1.0F / 255.0F;
    spec.mean = {0.0F, 0.0F, 0.0F};
    spec.standard_deviation = {1.0F, 1.0F, 1.0F};
    return spec;
}

TEST(LprNetPreprocessor, SyntheticGoldenRgbNchwTensorIsDeterministic) {
    auto bytes = make_bgr_fixture();
    const ImageView image{bytes, 2U, 2U, 6U, PixelFormat::bgr8};
    const auto validated = validate_image(image, PerformanceConfig{});
    const LprNetPreprocessor preprocessor{make_unit_rgb_spec(2U, 2U)};

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

TEST(LprNetPreprocessor, BilinearResizeGoldenFixtureUsesPixelCenterMapping) {
    auto bytes = make_bgr_fixture();
    const ImageView image{bytes, 2U, 2U, 6U, PixelFormat::bgr8};
    const auto validated = validate_image(image, PerformanceConfig{});
    const LprNetPreprocessor preprocessor{make_unit_rgb_spec(3U, 3U)};

    const auto tensor = preprocessor.preprocess(validated);
    ASSERT_EQ(tensor.values.size(), 27U);

    constexpr std::size_t plane_size = 9U;
    constexpr std::size_t center = 4U;
    EXPECT_NEAR(tensor.values[center], 0.5F, 1.0e-6F);
    EXPECT_NEAR(tensor.values[plane_size + center], 0.5F, 1.0e-6F);
    EXPECT_NEAR(tensor.values[(2U * plane_size) + center], 0.5F, 1.0e-6F);

    EXPECT_NEAR(tensor.values[0U], 1.0F, 1.0e-6F);
    EXPECT_NEAR(tensor.values[plane_size], 0.0F, 1.0e-6F);
    EXPECT_NEAR(tensor.values[2U * plane_size], 0.0F, 1.0e-6F);
}

TEST(LprNetPreprocessor, SupportsNhwcAndConfiguredBgrOrder) {
    auto bytes = make_bgr_fixture();
    const ImageView image{bytes, 2U, 2U, 6U, PixelFormat::bgr8};
    const auto validated = validate_image(image, PerformanceConfig{});
    const LprNetPreprocessor preprocessor{
        make_unit_rgb_spec(2U, 2U, TensorLayout::nhwc, InputColorOrder::bgr)};

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

TEST(LprNetPreprocessor, ReusesNativeTensorWorkspaceWithoutReallocation) {
    auto bytes = make_bgr_fixture();
    const ImageView image{bytes, 2U, 2U, 6U, PixelFormat::bgr8};
    const auto validated = validate_image(image, PerformanceConfig{});
    const LprNetPreprocessor preprocessor{make_unit_rgb_spec(4U, 2U)};
    NativeImageWorkspace workspace{};

    const auto first = preprocessor.preprocess(validated, workspace);
    const auto* first_data = first.values.data();
    const auto capacity = workspace.tensor_capacity();
    const auto second = preprocessor.preprocess(validated, workspace);

    EXPECT_EQ(first.values.size(), second.values.size());
    EXPECT_EQ(workspace.tensor_capacity(), capacity);
    EXPECT_EQ(second.values.data(), first_data);
}

TEST(LprNetPreprocessor, InvalidChannelContractFailsFast) {
    auto spec = make_unit_rgb_spec(94U, 24U);
    spec.channels = 2U;
    EXPECT_THROW(
        LprNetPreprocessor{spec},
        fac_lpr::application::ConfigurationError);
}

} // namespace
