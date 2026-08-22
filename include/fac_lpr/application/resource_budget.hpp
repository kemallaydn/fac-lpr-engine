#pragma once

#include <fac_lpr/application/error.hpp>

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

namespace fac_lpr::application {

struct ResourceBudget final {
    std::size_t max_image_bytes{128U * 1024U * 1024U};
    std::size_t max_tile_count{256U};
    std::size_t max_crop_count{256U};
    std::size_t max_tensor_elements{64U * 1024U * 1024U};
    std::size_t max_result_bytes{64U * 1024U * 1024U};
    std::size_t max_queue_memory_bytes{512U * 1024U * 1024U};
};

inline constexpr ResourceBudget default_resource_budget{};

[[nodiscard]] inline std::size_t checked_resource_multiply(
    const std::size_t left,
    const std::size_t right,
    const std::size_t maximum,
    const std::string_view resource) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        throw ResourceExhaustedError(std::string{resource} + " size overflows size_t");
    }
    const auto value = left * right;
    if (value > maximum) {
        throw ResourceExhaustedError(std::string{resource} + " exceeds configured resource budget");
    }
    return value;
}

inline void require_resource_count(
    const std::size_t value,
    const std::size_t maximum,
    const std::string_view resource) {
    if (value > maximum) {
        throw ResourceExhaustedError(std::string{resource} + " exceeds configured resource budget");
    }
}

inline void validate_resource_budget(const ResourceBudget& budget) {
    if (budget.max_image_bytes == 0U || budget.max_image_bytes > (1024ULL * 1024ULL * 1024ULL)) {
        throw ConfigurationError("resource_budget.max_image_bytes must be in (0, 1 GiB]");
    }
    if (budget.max_tile_count == 0U || budget.max_tile_count > 4096U) {
        throw ConfigurationError("resource_budget.max_tile_count must be in [1, 4096]");
    }
    if (budget.max_crop_count == 0U || budget.max_crop_count > 4096U) {
        throw ConfigurationError("resource_budget.max_crop_count must be in [1, 4096]");
    }
    if (budget.max_tensor_elements == 0U || budget.max_tensor_elements > (512ULL * 1024ULL * 1024ULL)) {
        throw ConfigurationError("resource_budget.max_tensor_elements must be in (0, 512M]");
    }
    if (budget.max_result_bytes == 0U || budget.max_result_bytes > (1024ULL * 1024ULL * 1024ULL)) {
        throw ConfigurationError("resource_budget.max_result_bytes must be in (0, 1 GiB]");
    }
    if (budget.max_queue_memory_bytes == 0U || budget.max_queue_memory_bytes > (8ULL * 1024ULL * 1024ULL * 1024ULL)) {
        throw ConfigurationError("resource_budget.max_queue_memory_bytes must be in (0, 8 GiB]");
    }
}

} // namespace fac_lpr::application
