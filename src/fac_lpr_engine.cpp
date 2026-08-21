#include <fac_lpr/domain/recognition.hpp>
#include <fac_lpr/fac_lpr_engine.hpp>

#include <type_traits>

namespace fac_lpr {

static_assert(!Engine::name().empty());
static_assert(std::is_copy_constructible_v<domain::BoundingBox>);
static_assert(std::is_copy_constructible_v<domain::Detection>);
static_assert(std::is_copy_constructible_v<domain::PlateCandidate>);
static_assert(std::is_move_constructible_v<domain::PlateRecognitionResult>);

} // namespace fac_lpr
