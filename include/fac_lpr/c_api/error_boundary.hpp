#pragma once

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/fac_lpr_error.h>

#include <new>
#include <utility>

namespace fac_lpr::c_api {

[[nodiscard]] constexpr fac_lpr_status to_c_status(
    const application::EngineErrorCode code) noexcept {
    switch (code) {
        case application::EngineErrorCode::configuration:
            return FAC_LPR_STATUS_CONFIGURATION_ERROR;
        case application::EngineErrorCode::model_load:
            return FAC_LPR_STATUS_MODEL_LOAD_ERROR;
        case application::EngineErrorCode::inference:
            return FAC_LPR_STATUS_INFERENCE_ERROR;
        case application::EngineErrorCode::invalid_image:
            return FAC_LPR_STATUS_INVALID_IMAGE;
        case application::EngineErrorCode::provider:
            return FAC_LPR_STATUS_PROVIDER_ERROR;
        case application::EngineErrorCode::cancelled:
            return FAC_LPR_STATUS_CANCELLED;
        case application::EngineErrorCode::timeout:
            return FAC_LPR_STATUS_TIMEOUT;
        case application::EngineErrorCode::resource_exhausted:
            return FAC_LPR_STATUS_RESOURCE_EXHAUSTED;
        case application::EngineErrorCode::internal:
            return FAC_LPR_STATUS_INTERNAL_ERROR;
    }
    return FAC_LPR_STATUS_INTERNAL_ERROR;
}

template <typename Function>
[[nodiscard]] fac_lpr_status invoke_noexcept(Function&& function) noexcept {
    try {
        std::forward<Function>(function)();
        return FAC_LPR_STATUS_OK;
    } catch (const application::EngineError& error) {
        return to_c_status(error.code());
    } catch (const std::bad_alloc&) {
        return FAC_LPR_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return FAC_LPR_STATUS_INTERNAL_ERROR;
    }
}

static_assert(to_c_status(application::EngineErrorCode::configuration) ==
              FAC_LPR_STATUS_CONFIGURATION_ERROR);
static_assert(to_c_status(application::EngineErrorCode::model_load) ==
              FAC_LPR_STATUS_MODEL_LOAD_ERROR);
static_assert(to_c_status(application::EngineErrorCode::inference) ==
              FAC_LPR_STATUS_INFERENCE_ERROR);
static_assert(to_c_status(application::EngineErrorCode::invalid_image) ==
              FAC_LPR_STATUS_INVALID_IMAGE);
static_assert(to_c_status(application::EngineErrorCode::provider) ==
              FAC_LPR_STATUS_PROVIDER_ERROR);
static_assert(to_c_status(application::EngineErrorCode::cancelled) ==
              FAC_LPR_STATUS_CANCELLED);
static_assert(to_c_status(application::EngineErrorCode::timeout) ==
              FAC_LPR_STATUS_TIMEOUT);
static_assert(to_c_status(application::EngineErrorCode::resource_exhausted) ==
              FAC_LPR_STATUS_RESOURCE_EXHAUSTED);
static_assert(to_c_status(application::EngineErrorCode::internal) ==
              FAC_LPR_STATUS_INTERNAL_ERROR);

} // namespace fac_lpr::c_api
