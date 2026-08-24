#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace {

std::uint32_t read_u32(const std::uint8_t* data) noexcept {
    std::uint32_t value = 0U;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
    if (data == nullptr || size < 13U) {
        return 0;
    }

    const auto width = static_cast<std::size_t>(read_u32(data));
    const auto height = static_cast<std::size_t>(read_u32(data + 4U));
    const auto stride = static_cast<std::size_t>(read_u32(data + 8U));
    const auto format_raw = data[12U];
    const auto payload = std::span<const std::uint8_t>{data + 13U, size - 13U};

    fac_lpr::application::PixelFormat format{};
    switch (format_raw % 4U) {
        case 0U: format = fac_lpr::application::PixelFormat::gray8; break;
        case 1U: format = fac_lpr::application::PixelFormat::bgr8; break;
        case 2U: format = fac_lpr::application::PixelFormat::rgb8; break;
        default: format = static_cast<fac_lpr::application::PixelFormat>(255U); break;
    }

    try {
        const auto view = fac_lpr::application::make_image_view(
            payload.data(), payload.size(), width, height, stride, format);
        fac_lpr::application::PerformanceConfig limits{};
        limits.max_image_width = 8192U;
        limits.max_image_height = 8192U;
        limits.max_image_bytes = 64U * 1024U * 1024U;
        (void)fac_lpr::application::validate_image(view, limits);
    } catch (const fac_lpr::application::EngineError&) {
    }
    return 0;
}
