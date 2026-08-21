#pragma once

#include <fac_lpr/application/providers.hpp>

#include <chrono>
#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace fac_lpr::infrastructure::paddle {

struct EncodedImage final {
    std::vector<std::byte> bytes{};
    std::string mime_type{"image/jpeg"};
};

class IImageEncoder {
public:
    virtual ~IImageEncoder() = default;
    [[nodiscard]] virtual EncodedImage encode(
        const application::ImageView& image,
        const application::OperationContext& context) = 0;
};

struct PaddleOcrRequest final {
    std::span<const std::byte> payload{};
    std::string mime_type{};
    std::string content_sha256{};
};

struct PaddleOcrResponse final {
    std::vector<domain::PlateCandidate> candidates{};
};

class IPaddleOcrWorker {
public:
    virtual ~IPaddleOcrWorker() = default;
    [[nodiscard]] virtual bool available() const noexcept = 0;
    [[nodiscard]] virtual PaddleOcrResponse recognize(
        const PaddleOcrRequest& request,
        const application::OperationContext& context) = 0;
};

struct PaddleOcrProviderConfig final {
    std::string provider_name{"paddleocr"};
    std::chrono::milliseconds timeout{750};
    std::size_t maximum_payload_bytes{4U * 1024U * 1024U};
    std::size_t cache_capacity{128U};
    std::size_t maximum_candidates{8U};
};

class PaddleOcrAdapter final : public application::IPlateRecognizer {
public:
    PaddleOcrAdapter(
        PaddleOcrProviderConfig config,
        std::shared_ptr<IImageEncoder> encoder,
        std::shared_ptr<IPaddleOcrWorker> worker);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] domain::RecognitionEvidence recognize(
        const application::ImageView& plate,
        const application::OperationContext& context) override;

private:
    struct CacheEntry final {
        domain::RecognitionEvidence evidence{};
        std::list<std::string>::iterator lru_iterator{};
    };

    [[nodiscard]] application::OperationContext child_context(
        const application::OperationContext& parent) const;
    void validate_response(PaddleOcrResponse& response) const;
    [[nodiscard]] bool try_get_cached(
        const std::string& key,
        domain::RecognitionEvidence& evidence) const;
    void store_cached(
        const std::string& key,
        const domain::RecognitionEvidence& evidence) const;

    PaddleOcrProviderConfig config_{};
    std::shared_ptr<IImageEncoder> encoder_{};
    std::shared_ptr<IPaddleOcrWorker> worker_{};
    mutable std::mutex cache_mutex_{};
    mutable std::list<std::string> lru_{};
    mutable std::unordered_map<std::string, CacheEntry> cache_{};
};

} // namespace fac_lpr::infrastructure::paddle
