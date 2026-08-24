#include <fac_lpr/infrastructure/crypto/sha256.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace fac_lpr::infrastructure::crypto {
namespace {

constexpr std::array<std::uint32_t, 64U> kConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

[[nodiscard]] constexpr std::uint32_t rotate_right(const std::uint32_t value, const unsigned int bits) noexcept {
    return (value >> bits) | (value << (32U - bits));
}

void transform(std::array<std::uint32_t, 8U>& state, const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64U> words{};
    for (std::size_t index = 0U; index < 16U; ++index) {
        const auto offset = index * 4U;
        words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                       (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                       static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
        const auto s0 = rotate_right(words[index - 15U], 7U) ^ rotate_right(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
        const auto s1 = rotate_right(words[index - 2U], 17U) ^ rotate_right(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    auto a = state[0]; auto b = state[1]; auto c = state[2]; auto d = state[3];
    auto e = state[4]; auto f = state[5]; auto g = state[6]; auto h = state[7];
    for (std::size_t index = 0U; index < 64U; ++index) {
        const auto sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
        const auto choice = (e & f) ^ ((~e) & g);
        const auto temp1 = h + sum1 + choice + kConstants[index] + words[index];
        const auto sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temp2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

} // namespace

Sha256Digest sha256(const std::span<const std::byte> bytes) noexcept {
    std::array<std::uint32_t, 8U> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

    std::size_t offset = 0U;
    while (bytes.size() - offset >= 64U) {
        transform(state, reinterpret_cast<const std::uint8_t*>(bytes.data() + offset));
        offset += 64U;
    }

    std::array<std::uint8_t, 128U> tail{};
    const auto remainder = bytes.size() - offset;
    for (std::size_t index = 0U; index < remainder; ++index) {
        tail[index] = std::to_integer<std::uint8_t>(bytes[offset + index]);
    }
    tail[remainder] = 0x80U;

    const std::size_t padded_size = remainder < 56U ? 64U : 128U;
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8ULL;
    for (std::size_t index = 0U; index < 8U; ++index) {
        tail[padded_size - 1U - index] = static_cast<std::uint8_t>(bit_length >> (index * 8U));
    }
    transform(state, tail.data());
    if (padded_size == 128U) {
        transform(state, tail.data() + 64U);
    }

    Sha256Digest digest{};
    for (std::size_t index = 0U; index < state.size(); ++index) {
        digest[index * 4U] = static_cast<std::uint8_t>(state[index] >> 24U);
        digest[index * 4U + 1U] = static_cast<std::uint8_t>(state[index] >> 16U);
        digest[index * 4U + 2U] = static_cast<std::uint8_t>(state[index] >> 8U);
        digest[index * 4U + 3U] = static_cast<std::uint8_t>(state[index]);
    }
    return digest;
}

std::string sha256_hex(const std::span<const std::byte> bytes) {
    const auto digest = sha256(bytes);
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto value : digest) {
        stream << std::setw(2) << static_cast<unsigned int>(value);
    }
    return stream.str();
}

} // namespace fac_lpr::infrastructure::crypto
