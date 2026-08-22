#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/lprnet/ctc_decoder.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::uint32_t read_u32(const std::uint8_t* data) noexcept {
    std::uint32_t value = 0U;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

float read_float(const std::uint8_t* data) noexcept {
    return std::bit_cast<float>(read_u32(data));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
    if (data == nullptr || size < 8U) {
        return 0;
    }

    constexpr char charset_text[] = "0123456789ABCDEFGHIJKLMNOPRSTUVYZ";
    fac_lpr::infrastructure::lprnet::GreedyCtcDecoderConfig config{};
    config.charset.assign(std::begin(charset_text), std::end(charset_text) - 1);
    config.blank_index = 33U;
    config.maximum_timesteps = 64U;
    config.maximum_classes = 64U;

    const auto timesteps = 1U + (static_cast<std::size_t>(read_u32(data)) % 64U);
    const auto classes = 1U + (static_cast<std::size_t>(read_u32(data + 4U)) % 64U);
    const auto element_count = timesteps * classes;
    std::vector<float> logits(element_count, 0.0F);

    if (size > 8U) {
        for (std::size_t index = 0U; index < element_count; ++index) {
            const auto byte_index = 8U + ((index * sizeof(std::uint32_t)) % (size - 8U));
            if (byte_index + sizeof(std::uint32_t) <= size) {
                logits[index] = read_float(data + byte_index);
            }
        }
    }

    try {
        const fac_lpr::infrastructure::lprnet::GreedyCtcDecoder decoder{config};
        (void)decoder.decode(logits, timesteps, classes);
    } catch (const fac_lpr::application::EngineError&) {
    }
    return 0;
}
