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

struct ImageRegion final {
    std::size_t x{0U};
    std::size_t y{0U};
    std::size_t width{0U};
    std::size_t height{0U};

    [[nodiscard]] bool empty() const noexcept {
        return width == 0U || height == 0U;
    }
};

struct ImageView final {
    std::span<const std::byte> bytes{};
    std::size_t width{0U};
    std::size_t height{0U};
    std::size_t stride_bytes{0U};
    PixelFormat format{PixelFormat::bgr8};
};

struct MutableImageView final {
    std::span<std::byte> bytes{};
    std::size_t width{0U};
    std::size_t height{0U};
    std::size_t stride_bytes{0U};
    PixelFormat format{PixelFormat::bgr8};

    [[nodiscard]] ImageView as_const() const noexcept {
        return ImageView{bytes, width, height, stride_bytes, format};
    }
};

struct ImageBuffer final {
    std::vector<std::byte> bytes{};
    std::size_t width{0U};
    std::size_t height{0U};
    std::size_t stride_bytes{0U};
    PixelFormat format{PixelFormat::bgr8};

    [[nodiscard]] ImageView view() const noexcept {
        return ImageView{bytes, width, height, stride_bytes, format};
    }

    [[nodiscard]] MutableImageView mutable_view() noexcept {
        return MutableImageView{bytes, width, height, stride_bytes, format};
    }
};

struct CropHypothesis final {
    ImageBuffer image{};
    std::string type{"unknown"};
};

} // namespace fac_lpr::application
