#pragma once

#include <fac_lpr/infrastructure/paddle/paddle_ocr_adapter.hpp>

#include <cstdint>

namespace fac_lpr::infrastructure::paddle {

// Calls the locally hosted Python PaddleOCR text recognition worker.
// The worker must bind to 127.0.0.1 and accept raw JPEG/PNG POST /recognize.
// No remote endpoints or credentials are supported.
class CurlPaddleOcrWorker final : public IPaddleOcrWorker {
public:
    explicit CurlPaddleOcrWorker(std::uint16_t port);

    [[nodiscard]] bool available() const noexcept override;
    [[nodiscard]] PaddleOcrResponse recognize(
        const PaddleOcrRequest& request,
        const application::OperationContext& context) override;

private:
    std::uint16_t port_{8765U};
};

} // namespace fac_lpr::infrastructure::paddle
