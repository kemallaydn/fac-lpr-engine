#pragma once

#include <fac_lpr/application/lpr_pipeline.hpp>
#include <fac_lpr/fac_lpr_engine.h>

#include <cstddef>

namespace fac_lpr::c_api {

[[nodiscard]] fac_lpr_status serialize_result_v1(
    const application::LprPipelineResult& result,
    void* output_buffer,
    std::size_t output_capacity,
    std::size_t* required_output_size);

} // namespace fac_lpr::c_api
