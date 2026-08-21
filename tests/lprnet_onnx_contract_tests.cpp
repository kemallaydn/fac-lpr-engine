#include <fac_lpr/infrastructure/lprnet/lprnet_onnx_contract.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

namespace {
using fac_lpr::infrastructure::lprnet::InputColorOrder;
using fac_lpr::infrastructure::lprnet::LprNetPreprocessSemantics;
using fac_lpr::infrastructure::lprnet::LprNetPreprocessor;
using fac_lpr::infrastructure::lprnet::TensorLayout;
using fac_lpr::infrastructure::lprnet::inspect_lprnet_input_spec;
using fac_lpr::infrastructure::lprnet::make_lprnet_input_spec;
using fac_lpr::infrastructure::onnx::OnnxRuntimeEnvironment;
using fac_lpr::infrastructure::onnx::OnnxSession;
using fac_lpr::infrastructure::onnx::TensorDescriptor;

LprNetPreprocessSemantics rgb_unit_semantics() {
    LprNetPreprocessSemantics semantics{};
    semantics.color_order = InputColorOrder::rgb;
    semantics.input_scale = 1.0F / 255.0F;
    semantics.standard_deviation = {1.0F, 1.0F, 1.0F};
    return semantics;
}

TEST(LprNetOnnxContract, DerivesNchwShapeWithoutHardCodedSpatialDimensions) {
    TensorDescriptor input{};
    input.name = "input";
    input.element_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    input.shape = {1, 3, 24, 94};

    const auto spec = make_lprnet_input_spec(input, rgb_unit_semantics());
    EXPECT_EQ(spec.layout, TensorLayout::nchw);
    EXPECT_EQ(spec.channels, 3U);
    EXPECT_EQ(spec.height, 24U);
    EXPECT_EQ(spec.width, 94U);
}

TEST(LprNetOnnxContract, DerivesNhwcShape) {
    TensorDescriptor input{};
    input.name = "image";
    input.element_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    input.shape = {1, 24, 94, 3};

    const auto spec = make_lprnet_input_spec(input, rgb_unit_semantics());
    EXPECT_EQ(spec.layout, TensorLayout::nhwc);
    EXPECT_EQ(spec.channels, 3U);
    EXPECT_EQ(spec.height, 24U);
    EXPECT_EQ(spec.width, 94U);
}

TEST(LprNetOnnxContract, RejectsUnsupportedOrAmbiguousContracts) {
    TensorDescriptor wrong_type{};
    wrong_type.element_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8;
    wrong_type.shape = {1, 3, 24, 94};
    EXPECT_THROW(
        make_lprnet_input_spec(wrong_type, rgb_unit_semantics()),
        fac_lpr::application::ModelLoadError);

    TensorDescriptor ambiguous{};
    ambiguous.element_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    ambiguous.shape = {1, 3, 24, 3};
    EXPECT_THROW(
        make_lprnet_input_spec(ambiguous, rgb_unit_semantics()),
        fac_lpr::application::ModelLoadError);
}

TEST(LprNetOnnxContract, RequiresExplicitValidPreprocessSemantics) {
    TensorDescriptor input{};
    input.element_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    input.shape = {1, 3, 24, 94};
    LprNetPreprocessSemantics unspecified{};
    EXPECT_THROW(
        make_lprnet_input_spec(input, unspecified),
        fac_lpr::application::ConfigurationError);
}

TEST(LprNetOnnxContract, RealModelMetadataDrivesPreprocessorWhenArtifactIsAvailable) {
#ifdef FAC_LPR_TEST_LPRNET_MODEL_PATH
    const std::filesystem::path model_path{FAC_LPR_TEST_LPRNET_MODEL_PATH};
    if (!std::filesystem::exists(model_path)) {
        GTEST_SKIP() << "configured LPRNet model artifact is missing";
    }

    auto environment = std::make_shared<OnnxRuntimeEnvironment>();
    const OnnxSession session{environment, model_path};
    const auto spec = inspect_lprnet_input_spec(
        session,
        rgb_unit_semantics());
    EXPECT_GT(spec.width, 0U);
    EXPECT_GT(spec.height, 0U);
    EXPECT_TRUE(spec.channels == 1U || spec.channels == 3U);
    EXPECT_NO_THROW(LprNetPreprocessor{spec});
#else
    GTEST_SKIP() << "real LPRNet model path was not provisioned at configure time";
#endif
}

} // namespace
