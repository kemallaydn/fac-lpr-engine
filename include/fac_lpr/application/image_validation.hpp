#pragma once

#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/image.hpp>

#include <cstddef>

namespace fac_lpr::application {

struct ValidatedImage final {
    ImageView view{};
    std::size_t channels{0U};
    std::size_t minimum_row_bytes{0U};
    std::size_t required_bytes{0U};
};

[[nodiscard]] std::size_t pixel_format_channels(PixelFormat format) noexcept;

// Safe boundary helper for caller-owned/C ABI buffers. A non-zero byte count
// with a null pointer is rejected before constructing std::span.
[[nodiscard]] ImageView make_image_view(
    const void* data,
    std::size_t byte_count,
    std::size_t width,
    std::size_t height,
    std::size_t stride_bytes,
    PixelFormat format);

[[nodiscard]] ValidatedImage validate_image(
    const ImageView& image,
    const PerformanceConfig& limits);

} // namespace fac_lpr::application
