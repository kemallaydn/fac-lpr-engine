#include <fac_lpr/application/turkish_plate_grammar.hpp>

#include <gtest/gtest.h>

namespace {
using fac_lpr::application::TurkishPlateGrammar;
using fac_lpr::application::TurkishPlateGrammarConfig;

TEST(TurkishPlateGrammar, NormalizesAsciiCaseWhitespaceAndHyphen) {
    const TurkishPlateGrammar grammar{};
    EXPECT_EQ(grammar.normalize("34 abc-123"), "34ABC123");
}

TEST(TurkishPlateGrammar, AcceptsSupportedCivilPlateShapes) {
    const TurkishPlateGrammar grammar{};
    EXPECT_TRUE(grammar.is_valid("34 A 1234"));
    EXPECT_TRUE(grammar.is_valid("34 A 12345"));
    EXPECT_TRUE(grammar.is_valid("06 AB 123"));
    EXPECT_TRUE(grammar.is_valid("35 AB 1234"));
    EXPECT_TRUE(grammar.is_valid("01 ABC 12"));
    EXPECT_TRUE(grammar.is_valid("81 ABC 123"));
}

TEST(TurkishPlateGrammar, RejectsProvinceCodesOutsideOneToEightyOne) {
    const TurkishPlateGrammar grammar{};
    EXPECT_FALSE(grammar.is_valid("00 A 1234"));
    EXPECT_FALSE(grammar.is_valid("82 A 1234"));
    EXPECT_FALSE(grammar.province_code("00A1234").has_value());
    EXPECT_FALSE(grammar.province_code("82A1234").has_value());
}

TEST(TurkishPlateGrammar, RejectsLettersOutsideConfiguredTurkishPlateSet) {
    const TurkishPlateGrammar grammar{};
    EXPECT_FALSE(grammar.is_valid("34 Q 1234"));
    EXPECT_FALSE(grammar.is_valid("34 W 1234"));
    EXPECT_FALSE(grammar.is_valid("34 X 1234"));
    EXPECT_TRUE(grammar.is_valid("34 IO 1234"));
}

TEST(TurkishPlateGrammar, PrefixValidationSupportsBeamSearchPruning) {
    const TurkishPlateGrammar grammar{};
    EXPECT_TRUE(grammar.is_valid_prefix(""));
    EXPECT_TRUE(grammar.is_valid_prefix("3"));
    EXPECT_TRUE(grammar.is_valid_prefix("34"));
    EXPECT_TRUE(grammar.is_valid_prefix("34A"));
    EXPECT_TRUE(grammar.is_valid_prefix("34A1"));
    EXPECT_TRUE(grammar.is_valid_prefix("34ABC12"));
    EXPECT_FALSE(grammar.is_valid_prefix("9"));
    EXPECT_FALSE(grammar.is_valid_prefix("82"));
    EXPECT_FALSE(grammar.is_valid_prefix("34Q"));
    EXPECT_FALSE(grammar.is_valid_prefix("34A1B"));
    EXPECT_FALSE(grammar.is_valid_prefix("34A123456"));
}

TEST(TurkishPlateGrammar, InvalidAllowedLetterConfigurationFailsFast) {
    TurkishPlateGrammarConfig config{};
    config.allowed_letters = "AABC";
    EXPECT_THROW(
        TurkishPlateGrammar{config},
        fac_lpr::application::ConfigurationError);
}

} // namespace
