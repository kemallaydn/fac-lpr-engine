#pragma once

#include <fac_lpr/domain/geometry.hpp>

#include <optional>
#include <string>

namespace fac_lpr::domain {

struct Detection final {
    BoundingBox bbox{};
    std::optional<PlateQuadrilateral> quadrilateral{};
    float confidence{0.0F};
    float geometry_score{0.0F};
    std::string provider{};
};

} // namespace fac_lpr::domain
