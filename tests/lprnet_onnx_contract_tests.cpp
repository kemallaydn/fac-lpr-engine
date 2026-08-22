#include <fac_lpr/infrastructure/crypto/sha256.hpp>
#include <fac_lpr/infrastructure/lprnet/lprnet_onnx_contract.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

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

LprNetPreprocessSemantics active_v2_semantics() {
    LprNetPreprocessSemantics semantics{};
    semantics.color_order = InputColorOrder::bgr;
    semantics.input_scale = 1.0F;
    semantics.mean = {127.5F, 127.5F, 127.5F};
    semantics.standard_deviation = {128.0F, 128.0F, 128.0F};
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

TEST(LprNetOnnxContract, ActiveV2MixedEpoch7ArtifactMatchesPinnedContract) {
#ifdef FAC_LPR_TEST_LPRNET_MODEL_PATH
    const std::filesystem::path model_path{FAC_LPR_TEST_LPRNET_MODEL_PATH};
    if (!std::filesystem::is_regular_file(model_path)) {
        GTEST_SKIP() << "configured LPRNet model artifact is missing";
    }

    EXPECT_EQ(std::filesystem::file_size(model_path), 1308628U);

    std::ifstream input{model_path, std::ios::binary};
    ASSERT_TRUE(input.good());
    const std::vector<char> raw{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t index = 0U; index < raw.size(); ++index) {
        bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(raw[index]));
    }
    EXPECT_EQ(
        fac_lpr::infrastructure::crypto::sha256_hex(bytes),
        "2d8fa236f468615ccd8b9ad6748c3e71b3d19ea53affdf5d5fee5a59719e310d");

    auto environment = std::make_shared<OnnxRuntimeEnvironment>();
    const OnnxSession session{environment, model_path};

    ASSERT_EQ(session.inputs().size(), 1U);
    EXPECT_EQ(session.inputs().front().name, "input");
    EXPECT_EQ(session.inputs().front().element_type, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    EXPECT_EQ(session.inputs().front().shape, (std::vector<std::int64_t>{1, 3, 40, 160}));

    ASSERT_EQ(session.outputs().size(), 1U);
    EXPECT_EQ(session.outputs().front().name, "output");
    EXPECT_EQ(session.outputs().front().element_type, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    EXPECT_EQ(session.outputs().front().shape, (std::vector<std::int64_t>{1, 34, 24}));

    const auto spec = inspect_lprnet_input_spec(session, active_v2_semantics());
    EXPECT_EQ(spec.layout, TensorLayout::nchw);
    EXPECT_EQ(spec.color_order, InputColorOrder::bgr);
    EXPECT_EQ(spec.channels, 3U);
    EXPECT_EQ(spec.height, 40U);
    EXPECT_EQ(spec.width, 160U);
    EXPECT_FLOAT_EQ(spec.input_scale, 1.0F);
    EXPECT_FLOAT_EQ(spec.mean[0], 127.5F);
    EXPECT_FLOAT_EQ(spec.mean[1], 127.5F);
    EXPECT_FLOAT_EQ(spec.mean[2], 127.5F);
    EXPECT_FLOAT_EQ(spec.standard_deviation[0], 128.0F);
    EXPECT_FLOAT_EQ(spec.standard_deviation[1], 128.0F);
    EXPECT_FLOAT_EQ(spec.standard_deviation[2], 128.0F);
    EXPECT_NO_THROW(LprNetPreprocessor{spec});
#else
    GTEST_SKIP() << "real LPRNet model path was not provisioned at configure time";
#endif
}

} // namespace
