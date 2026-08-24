#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/resource_budget.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
    if (size < sizeof(std::size_t) * 3U) {
        return 0;
    }
    std::size_t left = 0U;
    std::size_t right = 0U;
    std::size_t maximum = 0U;
    std::memcpy(&left, data, sizeof(left));
    std::memcpy(&right, data + sizeof(left), sizeof(right));
    std::memcpy(&maximum, data + (sizeof(left) * 2U), sizeof(maximum));
    maximum = maximum == 0U ? 1U : maximum;

    try {
        const auto value = fac_lpr::application::checked_resource_multiply(
            left, right, maximum, "fuzz resource");
        if (value > maximum) {
            __builtin_trap();
        }
    } catch (const fac_lpr::application::ResourceExhaustedError&) {
        // Expected fail-closed result for overflow or budget excess.
    }
    return 0;
}
