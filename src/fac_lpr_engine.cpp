#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/logging.hpp>
#include <fac_lpr/application/providers.hpp>
#include <fac_lpr/domain/recognition.hpp>
#include <fac_lpr/fac_lpr_engine.hpp>

#include <stdexcept>
#include <type_traits>

namespace fac_lpr {

static_assert(!Engine::name().empty());
static_assert(std::is_copy_constructible_v<domain::BoundingBox>);
static_assert(std::is_copy_constructible_v<domain::Detection>);
static_assert(std::is_copy_constructible_v<domain::PlateCandidate>);
static_assert(std::is_move_constructible_v<domain::PlateRecognitionResult>);
static_assert(std::has_virtual_destructor_v<application::IPlateDetector>);
static_assert(std::has_virtual_destructor_v<application::IPlateRecognizer>);
static_assert(std::has_virtual_destructor_v<application::IDecisionPolicy>);
static_assert(std::has_virtual_destructor_v<application::ILogger>);
static_assert(application::EngineConfig{}.decision.fail_closed);
static_assert(std::is_base_of_v<std::runtime_error, application::EngineError>);
static_assert(std::is_base_of_v<application::EngineError, application::ConfigurationError>);

} // namespace fac_lpr
