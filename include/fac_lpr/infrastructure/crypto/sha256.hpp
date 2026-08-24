#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace fac_lpr::infrastructure::crypto {

using Sha256Digest = std::array<std::uint8_t, 32U>;

[[nodiscard]] Sha256Digest sha256(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] std::string sha256_hex(std::span<const std::byte> bytes);

} // namespace fac_lpr::infrastructure::crypto
