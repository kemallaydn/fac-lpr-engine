#include <fac_lpr/c_api/error_boundary.hpp>

#include <gtest/gtest.h>

#include <new>
#include <stdexcept>

namespace {

TEST(ErrorBoundary, ReturnsOkWhenCallableSucceeds) {
    const auto status = fac_lpr::c_api::invoke_noexcept([] {});
    EXPECT_EQ(status, FAC_LPR_STATUS_OK);
}

TEST(ErrorBoundary, MapsTypedEngineErrorsToStableCStatuses) {
    EXPECT_EQ(
        fac_lpr::c_api::invoke_noexcept([] {
            throw fac_lpr::application::ConfigurationError{"invalid configuration"};
        }),
        FAC_LPR_STATUS_CONFIGURATION_ERROR);
    EXPECT_EQ(
        fac_lpr::c_api::invoke_noexcept([] {
            throw fac_lpr::application::ModelLoadError{"model load failed"};
        }),
        FAC_LPR_STATUS_MODEL_LOAD_ERROR);
    EXPECT_EQ(
        fac_lpr::c_api::invoke_noexcept([] {
            throw fac_lpr::application::InvalidImageError{"invalid image"};
        }),
        FAC_LPR_STATUS_INVALID_IMAGE);
    EXPECT_EQ(
        fac_lpr::c_api::invoke_noexcept([] {
            throw fac_lpr::application::TimeoutError{"deadline exceeded"};
        }),
        FAC_LPR_STATUS_TIMEOUT);
}

TEST(ErrorBoundary, MapsAllocationFailureToResourceExhausted) {
    const auto status = fac_lpr::c_api::invoke_noexcept([] {
        throw std::bad_alloc{};
    });
    EXPECT_EQ(status, FAC_LPR_STATUS_RESOURCE_EXHAUSTED);
}

TEST(ErrorBoundary, MapsUnknownExceptionToInternalErrorWithoutEscaping) {
    const auto status = fac_lpr::c_api::invoke_noexcept([] {
        throw std::runtime_error{"implementation detail must not cross the C ABI"};
    });
    EXPECT_EQ(status, FAC_LPR_STATUS_INTERNAL_ERROR);
}

static_assert(noexcept(fac_lpr::c_api::invoke_noexcept([] {})));

} // namespace
