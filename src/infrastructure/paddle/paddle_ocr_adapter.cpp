#include <fac_lpr/infrastructure/paddle/paddle_ocr_adapter.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/crypto/sha256.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace fac_lpr::infrastructure::paddle {
namespace {

void check_context(const application::OperationContext& context) {
    if (context.cancellation_requested()) {
        throw application::CancelledError("PaddleOCR recognition cancelled");
    }
    if (context.deadline_exceeded()) {
        throw application::TimeoutError("PaddleOCR recognition deadline exceeded");
    }
}

} // namespace

PaddleOcrAdapter::PaddleOcrAdapter(
    PaddleOcrProviderConfig config,
    std::shared_ptr<IImageEncoder> encoder,
    std::shared_ptr<IPaddleOcrWorker> worker)
    : config_(std::move(config)), encoder_(std::move(encoder)), worker_(std::move(worker)) {
    if (config_.provider_name.empty()) {
        throw application::ConfigurationError("PaddleOCR provider name cannot be empty");
    }
    if (config_.timeout <= std::chrono::milliseconds::zero()) {
        throw application::ConfigurationError("PaddleOCR timeout must be positive");
    }
    if (config_.maximum_payload_bytes == 0U || config_.maximum_candidates == 0U) {
        throw application::ConfigurationError("PaddleOCR bounds must be greater than zero");
    }
    if (!encoder_ || !worker_) {
        throw application::ConfigurationError("PaddleOCR encoder and worker are required");
    }
}

std::string_view PaddleOcrAdapter::name() const noexcept {
    return config_.provider_name;
}

application::OperationContext PaddleOcrAdapter::child_context(
    const application::OperationContext& parent) const {
    application::OperationContext child{parent};
    const auto local_deadline = std::chrono::steady_clock::now() + config_.timeout;
    if (!child.deadline.has_value() || local_deadline < *child.deadline) {
        child.deadline = local_deadline;
    }
    return child;
}

void PaddleOcrAdapter::validate_response(PaddleOcrResponse& response) const {
    if (response.candidates.size() > config_.maximum_candidates) {
        throw application::ProviderError("PaddleOCR response contains too many candidates");
    }
    for (auto& candidate : response.candidates) {
        if (candidate.text.empty() || !std::isfinite(candidate.confidence) ||
            candidate.confidence < 0.0F || candidate.confidence > 1.0F) {
            throw application::ProviderError("PaddleOCR worker returned a malformed candidate");
        }
        if (!std::isfinite(candidate.calibrated_confidence) ||
            candidate.calibrated_confidence < 0.0F || candidate.calibrated_confidence > 1.0F) {
            candidate.calibrated_confidence = candidate.confidence;
        }
    }
}

bool PaddleOcrAdapter::try_get_cached(
    const std::string& key,
    domain::RecognitionEvidence& evidence) const {
    if (config_.cache_capacity == 0U) {
        return false;
    }
    std::scoped_lock lock{cache_mutex_};
    const auto iterator = cache_.find(key);
    if (iterator == cache_.end()) {
        return false;
    }
    lru_.splice(lru_.begin(), lru_, iterator->second.lru_iterator);
    evidence = iterator->second.evidence;
    return true;
}

void PaddleOcrAdapter::store_cached(
    const std::string& key,
    const domain::RecognitionEvidence& evidence) const {
    if (config_.cache_capacity == 0U) {
        return;
    }
    std::scoped_lock lock{cache_mutex_};
    if (const auto existing = cache_.find(key); existing != cache_.end()) {
        existing->second.evidence = evidence;
        lru_.splice(lru_.begin(), lru_, existing->second.lru_iterator);
        return;
    }
    lru_.push_front(key);
    cache_.emplace(key, CacheEntry{evidence, lru_.begin()});
    while (cache_.size() > config_.cache_capacity) {
        const auto expired = lru_.back();
        cache_.erase(expired);
        lru_.pop_back();
    }
}

domain::RecognitionEvidence PaddleOcrAdapter::recognize(
    const application::ImageView& plate,
    const application::OperationContext& context) {
    check_context(context);
    if (!worker_->available()) {
        throw application::ProviderError("PaddleOCR worker is unavailable");
    }

    auto encoded = encoder_->encode(plate, context);
    check_context(context);
    if (encoded.bytes.empty() || encoded.mime_type.empty()) {
        throw application::ProviderError("PaddleOCR encoder returned an empty payload");
    }
    if (encoded.bytes.size() > config_.maximum_payload_bytes) {
        throw application::ResourceExhaustedError("PaddleOCR payload exceeds configured limit");
    }

    const auto key = crypto::sha256_hex(encoded.bytes);
    domain::RecognitionEvidence cached{};
    if (try_get_cached(key, cached)) {
        return cached;
    }

    const auto started = std::chrono::steady_clock::now();
    auto bounded_context = child_context(context);
    check_context(bounded_context);
    auto response = worker_->recognize(
        PaddleOcrRequest{encoded.bytes, encoded.mime_type, key},
        bounded_context);
    check_context(bounded_context);
    validate_response(response);

    domain::RecognitionEvidence evidence{};
    evidence.source = config_.provider_name;
    evidence.candidates = std::move(response.candidates);
    evidence.latency_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    store_cached(key, evidence);
    return evidence;
}

} // namespace fac_lpr::infrastructure::paddle
