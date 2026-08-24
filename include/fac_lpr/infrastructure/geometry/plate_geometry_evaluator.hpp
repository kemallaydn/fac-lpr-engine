#pragma once

#include <fac_lpr/application/providers.hpp>
#include <fac_lpr/infrastructure/geometry/plate_geometry_validator.hpp>

namespace fac_lpr::infrastructure::geometry {

class PlateGeometryEvaluatorAdapter final
    : public application::IPlateGeometryEvaluator {
public:
    explicit PlateGeometryEvaluatorAdapter(GeometryConfig config = {});

    [[nodiscard]] application::GeometryEvidence evaluate(
        const domain::Detection& detection,
        const application::OperationContext& context) override;

private:
    PlateGeometryValidator validator_;
};

} // namespace fac_lpr::infrastructure::geometry
