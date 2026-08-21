#include <fac_lpr/infrastructure/crypto/sha256.hpp>
#include <fac_lpr/infrastructure/paddle/paddle_ocr_adapter.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace fac_lpr;

class FakeEncoder final : public infrastructure::paddle::IImageEncoder {
public:
    infrastructure::paddle::EncodedImage encode(
        const application::ImageView& image,
        const application::OperationContext&) override {
        return {std::vector<std::byte>(image.bytes.begin(), image.bytes.end()), "image/jpeg"};
    }
};

class FakeWorker final : public infrastructure::paddle::IPaddleOcrWorker {
public:
    bool is_available{true};
    bool malformed{false};
    std::size_t calls{0U};

    bool available() const noexcept override { return is_available; }

    infrastructure::paddle::PaddleOcrResponse recognize(
        const infrastructure::paddle::PaddleOcrRequest& request,
        const application::OperationContext&) override {
        ++calls;
        EXPECT_EQ(request.content_sha256.size(), 64U);
        domain::PlateCandidate candidate{};
        candidate.text = malformed ? "" : "34ABC123";
        candidate.confidence = 0.91F;
        candidate.calibrated_confidence = 0.91F;
        candidate.format_valid = true;
        return {{candidate}};
    }
};

application::ImageView fixture(std::vector<std::byte>& bytes) {
    bytes.assign(12U, std::byte{0x2a});
    return {bytes, 2U, 2U, 6U, application::PixelFormat::bgr8};
}

TEST(Sha256, MatchesKnownAbcVector) {
    const char* text = "abc";
    const auto bytes = std::span{
        reinterpret_cast<const std::byte*>(text), std::size_t{3U}};
    EXPECT_EQ(
        infrastructure::crypto::sha256_hex(bytes),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(PaddleOcrAdapter, UsesBoundedSha256Cache) {
    auto encoder = std::make_shared<FakeEncoder>();
    auto worker = std::make_shared<FakeWorker>();
    infrastructure::paddle::PaddleOcrProviderConfig config{};
    config.cache_capacity = 1U;
    infrastructure::paddle::PaddleOcrAdapter adapter{config, encoder, worker};
    std::vector<std::byte> bytes;
    const auto image = fixture(bytes);

    const auto first = adapter.recognize(image, {});
    const auto second = adapter.recognize(image, {});
    EXPECT_EQ(first.candidates.front().text, "34ABC123");
    EXPECT_EQ(second.candidates.front().text, "34ABC123");
    EXPECT_EQ(worker->calls, 1U);
}

TEST(PaddleOcrAdapter, UnavailableWorkerFailsAsProviderErrorForEnsembleDegradation) {
    auto encoder = std::make_shared<FakeEncoder>();
    auto worker = std::make_shared<FakeWorker>();
    worker->is_available = false;
    infrastructure::paddle::PaddleOcrAdapter adapter{{}, encoder, worker};
    std::vector<std::byte> bytes;
    const auto image = fixture(bytes);
    EXPECT_THROW(adapter.recognize(image, {}), application::ProviderError);
}

TEST(PaddleOcrAdapter, MalformedResponseFailsSafely) {
    auto encoder = std::make_shared<FakeEncoder>();
    auto worker = std::make_shared<FakeWorker>();
    worker->malformed = true;
    infrastructure::paddle::PaddleOcrAdapter adapter{{}, encoder, worker};
    std::vector<std::byte> bytes;
    const auto image = fixture(bytes);
    EXPECT_THROW(adapter.recognize(image, {}), application::ProviderError);
}

TEST(PaddleOcrAdapter, ExpiredParentDeadlineFailsBeforeWorker) {
    auto encoder = std::make_shared<FakeEncoder>();
    auto worker = std::make_shared<FakeWorker>();
    infrastructure::paddle::PaddleOcrAdapter adapter{{}, encoder, worker};
    std::vector<std::byte> bytes;
    const auto image = fixture(bytes);
    application::OperationContext context{};
    context.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds{1};
    EXPECT_THROW(adapter.recognize(image, context), application::TimeoutError);
    EXPECT_EQ(worker->calls, 0U);
}

} // namespace
