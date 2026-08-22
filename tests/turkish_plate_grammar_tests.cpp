#include <fac_lpr/application/turkish_plate_grammar.hpp>

#include <gtest/gtest.h>

namespace {
using fac_lpr::application::TurkishPlateGrammar;
using fac_lpr::application::TurkishPlateGrammarConfig;

TEST(TurkishPlateGrammar, NormalizesAsciiCaseWhitespaceAndHyphen) {
    const TurkishPlateGrammar grammar{};
    EXPECT_EQ(grammar.normalize("34 abc-123"), "34ABC123");
    EXPECT_EQ(grammar.normalize(" 06\tab\r\n1234 "), "06AB1234");
}

TEST(TurkishPlateGrammar, RejectsNonAsciiInputDuringNormalization) {
    const TurkishPlateGrammar grammar{};
    EXPECT_TRUE(grammar.normalize("34 Ç 1234").empty());
    EXPECT_FALSE(grammar.is_valid("34 Ç 1234"));
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

TEST(TurkishPlateGrammar, RejectsUnsupportedGroupLengthsAndOrdering) {
    const TurkishPlateGrammar grammar{};
    EXPECT_FALSE(grammar.is_valid("34 A 123"));
    EXPECT_FALSE(grammar.is_valid("34 A 123456"));
    EXPECT_FALSE(grammar.is_valid("34 AB 12"));
    EXPECT_FALSE(grammar.is_valid("34 AB 12345"));
    EXPECT_FALSE(grammar.is_valid("34 ABC 1"));
    EXPECT_FALSE(grammar.is_valid("34 ABC 1234"));
    EXPECT_FALSE(grammar.is_valid("34 1234 A"));
    EXPECT_FALSE(grammar.is_valid("34 ABCD 12"));
}

TEST(TurkishPlateGrammar, RejectsProvinceCodesOutsideOneToEightyOne) {
    const TurkishPlateGrammar grammar{};
    EXPECT_FALSE(grammar.is_valid("00 A 1234"));
    EXPECT_FALSE(grammar.is_valid("82 A 1234"));
    EXPECT_FALSE(grammar.province_code("00A1234").has_value());
    EXPECT_FALSE(grammar.province_code("82A1234").has_value());
}

TEST(TurkishPlateGrammar, ExtractsValidProvinceCodesAfterNormalization) {
    const TurkishPlateGrammar grammar{};
    ASSERT_TRUE(grammar.province_code("34 abc-123").has_value());
    EXPECT_EQ(*grammar.province_code("34 abc-123"), 34U);
    ASSERT_TRUE(grammar.province_code("81 ABC 123").has_value());
    EXPECT_EQ(*grammar.province_code("81 ABC 123"), 81U);
}

TEST(TurkishPlateGrammar, RejectsLettersOutsideConfiguredTurkishPlateSet) {
    const TurkishPlateGrammar grammar{};
    EXPECT_FALSE(grammar.is_valid("34 Q 1234"));
    EXPECT_FALSE(grammar.is_valid("34 W 1234"));
    EXPECT_FALSE(grammar.is_valid("34 X 1234"));
    EXPECT_TRUE(grammar.is_valid("34 IO 1234"));
}

TEST(TurkishPlateGrammar, HonorsCustomAllowedLetterConfiguration) {
    TurkishPlateGrammarConfig config{};
    config.allowed_letters = "AB";
    const TurkishPlateGrammar grammar{config};

    EXPECT_TRUE(grammar.is_valid("34 A 1234"));
    EXPECT_TRUE(grammar.is_valid("34 AB 1234"));
    EXPECT_FALSE(grammar.is_valid("34 C 1234"));
}

TEST(TurkishPlateGrammar, PrefixValidationSupportsBeamSearchPruning) {
    const TurkishPlateGrammar grammar{};
    EXPECT_TRUE(grammar.is_valid_prefix(""));
    EXPECT_TRUE(grammar.is_valid_prefix("0"));
    EXPECT_TRUE(grammar.is_valid_prefix("3"));
    EXPECT_TRUE(grammar.is_valid_prefix("8"));
    EXPECT_TRUE(grammar.is_valid_prefix("01"));
    EXPECT_TRUE(grammar.is_valid_prefix("34"));
    EXPECT_TRUE(grammar.is_valid_prefix("81"));
    EXPECT_TRUE(grammar.is_valid_prefix("34A"));
    EXPECT_TRUE(grammar.is_valid_prefix("34A1"));
    EXPECT_TRUE(grammar.is_valid_prefix("34ABC12"));
    EXPECT_FALSE(grammar.is_valid_prefix("9"));
    EXPECT_FALSE(grammar.is_valid_prefix("00"));
    EXPECT_FALSE(grammar.is_valid_prefix("82"));
    EXPECT_FALSE(grammar.is_valid_prefix("34Q"));
    EXPECT_FALSE(grammar.is_valid_prefix("34A1B"));
    EXPECT_FALSE(grammar.is_valid_prefix("34A123456"));
    EXPECT_FALSE(grammar.is_valid_prefix("34AB12345"));
    EXPECT_FALSE(grammar.is_valid_prefix("34ABC1234"));
}

TEST(TurkishPlateGrammar, InvalidAllowedLetterConfigurationFailsFast) {
    TurkishPlateGrammarConfig duplicate_config{};
    duplicate_config.allowed_letters = "AABC";
    EXPECT_THROW(
        TurkishPlateGrammar{duplicate_config},
        fac_lpr::application::ConfigurationError);

    TurkishPlateGrammarConfig non_ascii_config{};
    non_ascii_config.allowed_letters = "ABÇ";
    EXPECT_THROW(
        TurkishPlateGrammar{non_ascii_config},
        fac_lpr::application::ConfigurationError);
}

TEST(TurkishPlateGrammar, MaximumInputLengthIsEnforced) {
    TurkishPlateGrammarConfig config{};
    config.maximum_input_length = 8U;
    const TurkishPlateGrammar grammar{config};

    EXPECT_TRUE(grammar.is_valid("34A12345"));
    EXPECT_TRUE(grammar.normalize("34 A 12345").empty());
}

} // namespace
