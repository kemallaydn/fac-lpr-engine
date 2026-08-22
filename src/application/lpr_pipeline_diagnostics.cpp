#include <fac_lpr/application/lpr_pipeline.hpp>

namespace fac_lpr::application {

EngineDiagnosticsSnapshot LprPipeline::diagnostics_snapshot() const {
    return dependencies_.diagnostics
        ? dependencies_.diagnostics->snapshot()
        : EngineDiagnosticsSnapshot{};
}

std::shared_ptr<EngineDiagnostics> LprPipeline::diagnostics() const noexcept {
    return dependencies_.diagnostics;
}

} // namespace fac_lpr::application
