#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace fac_lpr::application {

enum class PixelFormat {
    gray8,
    bgr8,
    rgb8
};

struct ImageView final {
    std::span<const std::byte> bytes{};
    std::size_t width{0};
    std::size_t height{0};
    std::size_t stride_bytes{0};
    PixelFormat format{PixelFormat::bgr8};
};

struct ImageBuffer final {
    std::vector<std::byte> bytes{};
    std::size_t width{0};
    std::size_t height{0};
    std::size_t stride_bytes{0};
    PixelFormat format{PixelFormat::bgr8};

    [[nodiscard]] ImageView view() const noexcept {
        return ImageView{bytes, width, height, stride_bytes, format};
    }
};

struct CropHypothesis final {
    ImageBuffer image{};
    std::string type{"unknown"};
};

} // namespace fac_lpr::application
