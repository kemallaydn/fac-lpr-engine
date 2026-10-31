#include <fac_lpr/infrastructure/paddle/curl_paddle_ocr_worker.hpp>

#include <fac_lpr/application/error.hpp>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace fac_lpr::infrastructure::paddle {
namespace {

constexpr std::size_t maximum_response_bytes = 8192U;

struct ResponseBuffer final {
    std::string body{};
    bool exceeded{false};
};

std::size_t append_response(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto& response = *static_cast<ResponseBuffer*>(userdata);
    if (size != 0U && nmemb > std::numeric_limits<std::size_t>::max() / size) {
        response.exceeded = true;
        return 0U;
    }
    const auto bytes = size * nmemb;
    if (bytes > maximum_response_bytes - response.body.size()) {
        response.exceeded = true;
        return 0U;
    }
    response.body.append(ptr, bytes);
    return bytes;
}

void initialize_curl() {
    static std::once_flag once{};
    static CURLcode status = CURLE_FAILED_INIT;
    std::call_once(once, [] { status = curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (status != CURLE_OK) {
        throw application::ProviderError("PaddleOCR HTTP transport initialization failed");
    }
}

[[nodiscard]] long timeout_ms(const application::OperationContext& context) {
    const auto now = std::chrono::steady_clock::now();
    if (context.cancellation_requested()) {
        throw application::CancelledError("PaddleOCR request cancelled");
    }
    if (context.deadline_exceeded() || !context.deadline.has_value() ||
        *context.deadline <= now) {
        throw application::TimeoutError("PaddleOCR request deadline exceeded");
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        *context.deadline - now).count();
    return static_cast<long>(std::clamp<std::int64_t>(remaining, 1, 60000));
}

[[nodiscard]] PaddleOcrResponse parse_response(const std::string& body) {
    try {
        const auto json = nlohmann::json::parse(body);
        if (!json.is_object() || !json.contains("candidates") ||
            !json.at("candidates").is_array() || json.at("candidates").size() > 8U) {
            throw application::ProviderError("PaddleOCR worker response schema is invalid");
        }
        PaddleOcrResponse response{};
        for (const auto& item : json.at("candidates")) {
            if (!item.is_object() || !item.contains("text") ||
                !item.at("text").is_string() || !item.contains("confidence") ||
                !item.at("confidence").is_number()) {
                throw application::ProviderError("PaddleOCR worker candidate schema is invalid");
            }
            const auto value = item.at("text").get<std::string>();
            const auto confidence = item.at("confidence").get<float>();
            if (value.empty() || value.size() > 16U || !std::isfinite(confidence) ||
                confidence < 0.0F || confidence > 1.0F ||
                !std::all_of(value.begin(), value.end(), [](unsigned char character) {
                    return (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9');
                })) {
                throw application::ProviderError("PaddleOCR worker candidate is invalid");
            }
            // The Paddle text score is NOT calibrated to LPRNet confidence.
            // Without independent plate grammar validation it must not create
            // an ACCEPTED candidate on its own.
            response.candidates.push_back(domain::PlateCandidate{
                .text = value,
                .confidence = confidence,
                .calibrated_confidence = confidence,
                .format_valid = false});
        }
        return response;
    } catch (const nlohmann::json::exception&) {
        throw application::ProviderError("PaddleOCR worker returned invalid JSON");
    }
}

} // namespace

CurlPaddleOcrWorker::CurlPaddleOcrWorker(const std::uint16_t port)
    : port_(port) {
    if (port_ < 1024U) {
        throw application::ConfigurationError("PaddleOCR worker port must be >= 1024");
    }
}

bool CurlPaddleOcrWorker::available() const noexcept {
    return true; // Request errors are handled by the optional recognition provider.
}

PaddleOcrResponse CurlPaddleOcrWorker::recognize(
    const PaddleOcrRequest& request,
    const application::OperationContext& context) {
    if (request.payload.empty() || request.payload.size() > 4U * 1024U * 1024U ||
        (request.mime_type != "image/png" && request.mime_type != "image/jpeg")) {
        throw application::ProviderError("PaddleOCR worker request is invalid");
    }
    initialize_curl();

    // No alternate destination is configurable: avoid arbitrary outbound requests.
    const auto url = "http://127.0.0.1:" + std::to_string(port_) + "/recognize";
    using CurlPtr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlPtr curl{curl_easy_init(), &curl_easy_cleanup};
    if (!curl) {
        throw application::ProviderError("PaddleOCR HTTP transport could not initialize");
    }
    const auto ms = timeout_ms(context);
    ResponseBuffer response{};
    struct curl_slist* raw_headers = nullptr;
    const auto type = "Content-Type: " + request.mime_type;
    raw_headers = curl_slist_append(raw_headers, type.c_str());
    if (!raw_headers) {
        throw application::ProviderError("PaddleOCR HTTP header allocation failed");
    }
    using HeadersPtr = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;
    HeadersPtr headers{raw_headers, &curl_slist_free_all};

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request.payload.data());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(request.payload.size()));
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, ms);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, std::min(ms, 250L));
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &append_response);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_PROXY, "");
    const auto status = curl_easy_perform(curl.get());
    if (context.cancellation_requested()) {
        throw application::CancelledError("PaddleOCR request cancelled");
    }
    if (status == CURLE_OPERATION_TIMEDOUT || context.deadline_exceeded()) {
        throw application::TimeoutError("PaddleOCR worker timed out");
    }
    if (status != CURLE_OK || response.exceeded) {
        throw application::ProviderError("PaddleOCR worker HTTP request failed");
    }
    long http_status = 0L;
    if (curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_status) != CURLE_OK ||
        http_status != 200L) {
        throw application::ProviderError("PaddleOCR worker did not return success");
    }
    return parse_response(response.body);
}

} // namespace fac_lpr::infrastructure::paddle
