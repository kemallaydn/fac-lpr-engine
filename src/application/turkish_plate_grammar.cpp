#include <fac_lpr/application/turkish_plate_grammar.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

namespace fac_lpr::application {
namespace {

[[nodiscard]] bool is_ascii_digit(const char value) noexcept {
    return value >= '0' && value <= '9';
}

[[nodiscard]] bool is_ascii_space(const char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

[[nodiscard]] char ascii_upper(const char value) noexcept {
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - ('a' - 'A'));
    }
    return value;
}

[[nodiscard]] bool allowed_digit_count(
    const std::size_t letter_count,
    const std::size_t digit_count) noexcept {
    switch (letter_count) {
        case 1U:
            return digit_count == 4U || digit_count == 5U;
        case 2U:
            return digit_count == 3U || digit_count == 4U;
        case 3U:
            return digit_count == 2U || digit_count == 3U;
        default:
            return false;
    }
}

[[nodiscard]] std::size_t maximum_digit_count(
    const std::size_t letter_count) noexcept {
    switch (letter_count) {
        case 1U: return 5U;
        case 2U: return 4U;
        case 3U: return 3U;
        default: return 0U;
    }
}

} // namespace

TurkishPlateGrammar::TurkishPlateGrammar(TurkishPlateGrammarConfig config)
    : config_(std::move(config)) {
    if (config_.allowed_letters.empty() || config_.allowed_letters.size() > 26U ||
        config_.maximum_input_length == 0U || config_.maximum_input_length > 256U) {
        throw ConfigurationError("Turkish plate grammar configuration is invalid");
    }

    for (const auto raw : config_.allowed_letters) {
        const auto value = ascii_upper(raw);
        const auto byte = static_cast<unsigned char>(value);
        if (byte >= allowed_letters_.size() || value < 'A' || value > 'Z' ||
            allowed_letters_[byte]) {
            throw ConfigurationError(
                "Turkish plate allowed letter set must contain unique ASCII letters");
        }
        allowed_letters_[byte] = true;
    }
}

std::string TurkishPlateGrammar::normalize(const std::string_view text) const {
    if (text.size() > config_.maximum_input_length) {
        return {};
    }

    std::string normalized{};
    normalized.reserve(text.size());
    for (const auto raw : text) {
        const auto byte = static_cast<unsigned char>(raw);
        if (byte >= 128U) {
            return {};
        }
        if (config_.strip_ascii_whitespace && is_ascii_space(raw)) {
            continue;
        }
        if (config_.strip_hyphen && raw == '-') {
            continue;
        }
        normalized.push_back(ascii_upper(raw));
    }
    return normalized;
}

std::optional<unsigned int> TurkishPlateGrammar::province_code(
    const std::string_view text) const {
    const auto normalized = normalize(text);
    if (normalized.size() < 2U || !is_ascii_digit(normalized[0]) ||
        !is_ascii_digit(normalized[1])) {
        return std::nullopt;
    }

    const auto code = static_cast<unsigned int>(normalized[0] - '0') * 10U +
                      static_cast<unsigned int>(normalized[1] - '0');
    if (code < 1U || code > 81U) {
        return std::nullopt;
    }
    return code;
}

bool TurkishPlateGrammar::is_allowed_letter(const char value) const noexcept {
    const auto byte = static_cast<unsigned char>(value);
    return byte < allowed_letters_.size() && allowed_letters_[byte];
}

bool TurkishPlateGrammar::is_valid_normalized(
    const std::string_view normalized) const noexcept {
    if (normalized.size() < 6U || normalized.size() > 8U ||
        !is_ascii_digit(normalized[0]) || !is_ascii_digit(normalized[1])) {
        return false;
    }

    const auto code = static_cast<unsigned int>(normalized[0] - '0') * 10U +
                      static_cast<unsigned int>(normalized[1] - '0');
    if (code < 1U || code > 81U) {
        return false;
    }

    std::size_t position = 2U;
    std::size_t letter_count = 0U;
    while (position < normalized.size() && is_allowed_letter(normalized[position])) {
        ++position;
        ++letter_count;
        if (letter_count > 3U) {
            return false;
        }
    }
    if (letter_count == 0U) {
        return false;
    }

    const auto digit_start = position;
    while (position < normalized.size() && is_ascii_digit(normalized[position])) {
        ++position;
    }
    if (position != normalized.size()) {
        return false;
    }

    const auto digit_count = normalized.size() - digit_start;
    return allowed_digit_count(letter_count, digit_count);
}

bool TurkishPlateGrammar::is_valid(const std::string_view text) const {
    const auto normalized = normalize(text);
    return !normalized.empty() && is_valid_normalized(normalized);
}

bool TurkishPlateGrammar::is_valid_prefix(const std::string_view prefix) const {
    const auto normalized = normalize(prefix);
    if (prefix.empty()) {
        return true;
    }
    if (normalized.empty() || normalized.size() > 8U) {
        return false;
    }

    if (!is_ascii_digit(normalized[0])) {
        return false;
    }
    if (normalized.size() == 1U) {
        return normalized[0] >= '0' && normalized[0] <= '8';
    }
    if (!is_ascii_digit(normalized[1])) {
        return false;
    }

    const auto code = static_cast<unsigned int>(normalized[0] - '0') * 10U +
                      static_cast<unsigned int>(normalized[1] - '0');
    if (code < 1U || code > 81U) {
        return false;
    }
    if (normalized.size() == 2U) {
        return true;
    }

    std::size_t position = 2U;
    std::size_t letter_count = 0U;
    while (position < normalized.size() && is_allowed_letter(normalized[position])) {
        ++position;
        ++letter_count;
        if (letter_count > 3U) {
            return false;
        }
    }
    if (letter_count == 0U) {
        return false;
    }
    if (position == normalized.size()) {
        return true;
    }

    const auto digit_start = position;
    while (position < normalized.size() && is_ascii_digit(normalized[position])) {
        ++position;
    }
    if (position != normalized.size()) {
        return false;
    }

    const auto digit_count = normalized.size() - digit_start;
    return digit_count > 0U && digit_count <= maximum_digit_count(letter_count);
}

} // namespace fac_lpr::application
