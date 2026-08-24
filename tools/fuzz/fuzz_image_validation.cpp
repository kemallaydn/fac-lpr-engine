#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace {

std::uint16_t read_u16(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
    if (data == nullptr || size < 10U) return 0;

    const auto width = static_cast<std::size_t>(read_u16(data));
    const auto height = static_cast<std::size_t>(read_u16(data + 2U));
    const auto stride = static_cast<std::size_t>(read_u32(data + 4U));
    const auto format_index = data[8U] % 3U;
    const auto format = static_cast<fac_lpr::application::PixelFormat>(format_index);
    const auto* payload = reinterpret_cast<const std::byte*>(data + 9U);
    const auto payload_size = size - 9U;

    const fac_lpr::application::ImageView image{
        std::span<const std::byte>{payload, payload_size},
        width,
        height,
        stride,
        format};

    fac_lpr::application::PerformanceConfig limits{};
    limits.max_image_width = 8192U;
    limits.max_image_height = 8192U;
    limits.max_image_bytes = 128U * 1024U * 1024U;

    try {
        (void)fac_lpr::application::validate_image(image, limits);
    } catch (const fac_lpr::application::EngineError&) {
    }
    return 0;
}
