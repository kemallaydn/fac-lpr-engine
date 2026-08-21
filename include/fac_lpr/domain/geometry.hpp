#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace fac_lpr::domain {

struct Point2f final {
    float x{0.0F};
    float y{0.0F};

    [[nodiscard]] constexpr bool is_finite() const noexcept {
        return std::isfinite(x) && std::isfinite(y);
    }
};

struct BoundingBox final {
    float x{0.0F};
    float y{0.0F};
    float width{0.0F};
    float height{0.0F};

    [[nodiscard]] constexpr float area() const noexcept {
        return width > 0.0F && height > 0.0F ? width * height : 0.0F;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) &&
               std::isfinite(height) && width > 0.0F && height > 0.0F;
    }
};

struct PlateQuadrilateral final {
    static constexpr std::size_t point_count = 4;

    std::array<Point2f, point_count> points{};
    std::array<float, point_count> confidences{};
};

} // namespace fac_lpr::domain
