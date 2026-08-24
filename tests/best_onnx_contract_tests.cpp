#include <fac_lpr/infrastructure/crypto/sha256.hpp>
#include <fac_lpr/infrastructure/onnx/onnx_session.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace {
using fac_lpr::infrastructure::onnx::OnnxRuntimeEnvironment;
using fac_lpr::infrastructure::onnx::OnnxSession;

TEST(BestOnnxContract, ActiveProductionArtifactMatchesPinnedContract) {
#ifdef FAC_LPR_TEST_BEST_MODEL_PATH
    const std::filesystem::path model_path{FAC_LPR_TEST_BEST_MODEL_PATH};
    if (!std::filesystem::is_regular_file(model_path)) {
        GTEST_SKIP() << "configured best.onnx artifact is missing";
    }

    EXPECT_EQ(std::filesystem::file_size(model_path), 12972328U);

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
        "22a9a65f6b0ef12baaba5e96d41b25fb9e475332229bd99b1c387f3345aea53d");

    auto environment = std::make_shared<OnnxRuntimeEnvironment>();
    const OnnxSession session{environment, model_path};

    ASSERT_EQ(session.inputs().size(), 1U);
    EXPECT_EQ(session.inputs().front().name, "images");
    EXPECT_EQ(session.inputs().front().element_type, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    EXPECT_EQ(session.inputs().front().shape, (std::vector<std::int64_t>{1, 3, 960, 960}));

    ASSERT_EQ(session.outputs().size(), 1U);
    EXPECT_EQ(session.outputs().front().name, "output0");
    EXPECT_EQ(session.outputs().front().element_type, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    EXPECT_EQ(session.outputs().front().shape, (std::vector<std::int64_t>{1, 17, 18900}));

    // The 17-feature output is pinned by the associated production hash/documented
    // parser contract: cx,cy,w,h + one plate class score + 4*(x,y,confidence).
    EXPECT_EQ(session.outputs().front().shape[1], 4 + 1 + (4 * 3));
#else
    GTEST_SKIP() << "best.onnx path was not provisioned at configure time";
#endif
}

} // namespace
