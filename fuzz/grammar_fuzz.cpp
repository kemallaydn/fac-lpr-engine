#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/turkish_plate_grammar.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
    if (data == nullptr) {
        return 0;
    }

    constexpr std::size_t max_input = 256U;
    const auto bounded_size = std::min(size, max_input);
    const std::string input{reinterpret_cast<const char*>(data), bounded_size};

    try {
        fac_lpr::application::TurkishPlateGrammarConfig config{};
        config.maximum_input_length = 64U;
        const fac_lpr::application::TurkishPlateGrammar grammar{config};
        (void)grammar.normalize(input);
        (void)grammar.is_valid(input);
        (void)grammar.is_valid_prefix(input);
        (void)grammar.province_code(input);
    } catch (const fac_lpr::application::EngineError&) {
    }
    return 0;
}
