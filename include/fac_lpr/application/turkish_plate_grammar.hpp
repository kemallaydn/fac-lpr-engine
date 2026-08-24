#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace fac_lpr::application {

struct TurkishPlateGrammarConfig final {
    std::string allowed_letters{"ABCDEFGHIJKLMNOPRSTUVYZ"};
    bool strip_ascii_whitespace{true};
    bool strip_hyphen{true};
    std::size_t maximum_input_length{32U};
};

class TurkishPlateGrammar final {
public:
    explicit TurkishPlateGrammar(TurkishPlateGrammarConfig config = {});

    [[nodiscard]] const TurkishPlateGrammarConfig& config() const noexcept {
        return config_;
    }

    [[nodiscard]] std::string normalize(std::string_view text) const;
    [[nodiscard]] bool is_valid(std::string_view text) const;
    [[nodiscard]] bool is_valid_normalized(std::string_view normalized) const noexcept;
    [[nodiscard]] bool is_valid_prefix(std::string_view prefix) const;
    [[nodiscard]] std::optional<unsigned int> province_code(
        std::string_view text) const;

private:
    [[nodiscard]] bool is_allowed_letter(char value) const noexcept;

    TurkishPlateGrammarConfig config_{};
    std::array<bool, 128U> allowed_letters_{};
};

} // namespace fac_lpr::application
