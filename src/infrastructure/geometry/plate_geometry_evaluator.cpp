#include <fac_lpr/infrastructure/geometry/plate_geometry_evaluator.hpp>

#include <fac_lpr/application/error.hpp>

namespace fac_lpr::infrastructure::geometry {

PlateGeometryEvaluatorAdapter::PlateGeometryEvaluatorAdapter(GeometryConfig config)
    : validator_(config) {}

application::GeometryEvidence PlateGeometryEvaluatorAdapter::evaluate(
    const domain::Detection& detection,
    const application::OperationContext& context) {
    if (context.cancellation_requested()) {
        throw application::CancelledError("geometry evaluation cancelled");
    }
    if (context.deadline_exceeded()) {
        throw application::TimeoutError("geometry evaluation deadline exceeded");
    }

    const auto evaluation = validator_.evaluate(detection);
    return application::GeometryEvidence{
        .valid = evaluation.valid,
        .score = evaluation.score};
}

} // namespace fac_lpr::infrastructure::geometry
