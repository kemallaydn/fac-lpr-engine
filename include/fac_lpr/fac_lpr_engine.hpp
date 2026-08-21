#pragma once

#include <string_view>

namespace fac_lpr {

class Engine final {
public:
    Engine() = default;

    [[nodiscard]] static constexpr std::string_view name() noexcept {
        return "fac-lpr-engine";
    }
};

} // namespace fac_lpr
