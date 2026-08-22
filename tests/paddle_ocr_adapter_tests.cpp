#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/crypto/sha256.hpp>
#include <fac_lpr/infrastructure/paddle/paddle_ocr_adapter.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <thread>
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
    bool malformed_calibrated_confidence{false};
    std::chrono::milliseconds delay{0};
    std::size_t calls{0U};

    bool available() const noexcept override { return is_available; }

    infrastructure::paddle::PaddleOcrResponse recognize(
        const infrastructure::paddle::PaddleOcrRequest& request,
        const application::OperationContext&) override {
        ++calls;
        EXPECT_EQ(request.content_sha256.size(), 64U);
        if (delay > std::chrono::milliseconds::zero()) {
            std::this_thread::sleep_for(delay);
        }
        domain::PlateCandidate candidate{};
        candidate.text = malformed ? "" : "34ABC123";
        candidate.confidence = 0.91F;
        candidate.calibrated_confidence = malformed_calibrated_confidence ? 1.5F : 0.91F;
        candidate.format_valid = true;
        return {{candidate}};
    }
};

application::ImageView fixture(std::vector<std::byte>& bytes, const std::byte value = std::byte{0x2a}) {
    bytes.assign(12U, value);
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

TEST(PaddleOcrAdapter, CacheCapacityEvictsLeastRecentlyUsedPayload) {
    auto encoder = std::make_shared<FakeEncoder>();
    auto worker = std::make_shared<FakeWorker>();
    infrastructure::paddle::PaddleOcrProviderConfig config{};
    config.cache_capacity = 1U;
    infrastructure::paddle::PaddleOcrAdapter adapter{config, encoder, worker};

    std::vector<std::byte> first_bytes;
    std::vector<std::byte> second_bytes;
    const auto first_image = fixture(first_bytes, std::byte{0x2a});
    const auto second_image = fixture(second_bytes, std::byte{0x3b});

    (void)adapter.recognize(first_image, {});
    (void)adapter.recognize(second_image, {});
    (void)adapter.recognize(first_image, {});

    EXPECT_EQ(worker->calls, 3U);
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

TEST(PaddleOcrAdapter, InvalidCalibratedConfidenceIsMalformedResponse) {
    auto encoder = std::make_shared<FakeEncoder>();
    auto worker = std::make_shared<FakeWorker>();
    worker->malformed_calibrated_confidence = true;
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

TEST(PaddleOcrAdapter, ProviderTimeoutFailsSafelyAfterSlowWorkerReturns) {
    auto encoder = std::make_shared<FakeEncoder>();
    auto worker = std::make_shared<FakeWorker>();
    worker->delay = std::chrono::milliseconds{20};
    infrastructure::paddle::PaddleOcrProviderConfig config{};
    config.timeout = std::chrono::milliseconds{1};
    infrastructure::paddle::PaddleOcrAdapter adapter{config, encoder, worker};
    std::vector<std::byte> bytes;
    const auto image = fixture(bytes);

    EXPECT_THROW(adapter.recognize(image, {}), application::TimeoutError);
    EXPECT_EQ(worker->calls, 1U);
}

} // namespace
