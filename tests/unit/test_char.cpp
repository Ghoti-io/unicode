/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Unicode.
 *
 * Ghoti.io Unicode is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Unicode is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * The property accessors: the contract at the edges of the codepoint space,
 * and a handful of values that a reader can check against the UCD by hand.
 *
 * The exhaustive comparison against the data is test_sweep.cpp's job. What is
 * here is what a sweep cannot do: the behaviour above U+10FFFF, which is not
 * in the UCD and so not in any oracle; and the few values whose being right
 * is the point of a decision - U+30FC's Line_Break is CJ and not NS because
 * LB1 is the caller's (design.md section 7.3), and U+0E01's is SA and not AL
 * for the same reason.
 */

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

#include "test_helpers.h"

namespace {

TEST(Char, VersionIsQueryableAtRunTime) {
  /* "Which Unicode version classified this string" is an audit question, so
   * it is answerable from the linked library and not only from the header
   * the caller compiled against (design.md section 5.4). */
  EXPECT_STREQ(guni_ucd_version(), GUNI_UCD_VERSION_STRING);
  EXPECT_EQ(guni_ucd_version_number(), GUNI_UCD_VERSION_NUMBER);
  EXPECT_EQ(guni_ucd_version_number(),
      GUNI_MAKE_VERSION(GUNI_UCD_VERSION_MAJOR, GUNI_UCD_VERSION_MINOR,
          GUNI_UCD_VERSION_PATCH));
}

TEST(Char, LatinCapitalA) {
  EXPECT_EQ(guni_general_category('A'), GUNI_GC_LU);
  EXPECT_EQ(guni_script('A'), GUNI_SCRIPT_LATIN);
  EXPECT_EQ(guni_combining_class('A'), 0);
  EXPECT_EQ(guni_bidi_class('A'), GUNI_BIDI_L);
  EXPECT_EQ(guni_east_asian_width('A'), GUNI_EAW_NA);
  EXPECT_EQ(guni_line_break('A'), GUNI_LB_AL);
  EXPECT_EQ(guni_word_break('A'), GUNI_WB_ALETTER);
  EXPECT_EQ(guni_grapheme_cluster_break('A'), GUNI_GCB_OTHER);
  EXPECT_EQ(guni_joining_type('A'), GUNI_JT_U);
  EXPECT_EQ(guni_block('A'), GUNI_BLOCK_BASIC_LATIN);
  EXPECT_TRUE(guni_has_property('A', GUNI_PROP_ALPHABETIC));
  EXPECT_TRUE(guni_has_property('A', GUNI_PROP_UPPERCASE));
  EXPECT_TRUE(guni_has_property('A', GUNI_PROP_ASCII));
  EXPECT_TRUE(guni_has_property('A', GUNI_PROP_ANY));
  EXPECT_TRUE(guni_has_property('A', GUNI_PROP_ASSIGNED));
  EXPECT_FALSE(guni_has_property('A', GUNI_PROP_LOWERCASE));
  EXPECT_TRUE(guni_gc_mask_contains(GUNI_GC_MASK_L, 'A'));
  EXPECT_TRUE(guni_gc_mask_contains(GUNI_GC_MASK_LC, 'A'));
  EXPECT_FALSE(guni_gc_mask_contains(GUNI_GC_MASK_N, 'A'));
}

TEST(Char, CombiningAcuteAccent) {
  const uint32_t cp = 0x0301;
  EXPECT_EQ(guni_general_category(cp), GUNI_GC_MN);
  EXPECT_EQ(guni_combining_class(cp), 230);
  EXPECT_EQ(guni_grapheme_cluster_break(cp), GUNI_GCB_EXTEND);
  EXPECT_EQ(guni_script(cp), GUNI_SCRIPT_INHERITED);
  EXPECT_EQ(guni_line_break(cp), GUNI_LB_CM);
  EXPECT_TRUE(guni_has_property(cp, GUNI_PROP_GRAPHEME_EXTEND));
}

TEST(Char, ArabicLetterBeh) {
  const uint32_t cp = 0x0628;
  EXPECT_EQ(guni_script(cp), GUNI_SCRIPT_ARABIC);
  EXPECT_EQ(guni_joining_type(cp), GUNI_JT_D);
  EXPECT_EQ(guni_joining_group(cp), GUNI_JG_BEH);
  EXPECT_EQ(guni_bidi_class(cp), GUNI_BIDI_AL);
}

TEST(Char, HangulSyllablesCarryTheirType) {
  EXPECT_EQ(guni_hangul_syllable_type(0xAC00), GUNI_HST_LV);
  EXPECT_EQ(guni_hangul_syllable_type(0xAC01), GUNI_HST_LVT);
  EXPECT_EQ(guni_hangul_syllable_type('A'), GUNI_HST_NA);
  EXPECT_EQ(guni_grapheme_cluster_break(0xAC00), GUNI_GCB_LV);
  EXPECT_EQ(guni_grapheme_cluster_break(0xAC01), GUNI_GCB_LVT);
}

TEST(Char, LineBreakClassesAreUnresolved) {
  /* This is M2 and it is the reason this library exists rather than regex's
   * tables being installed. LB1 resolves CJ to NS *or* ID at the
   * implementation's choice, which is what CSS exposes as line-break: strict
   * and normal; a table that resolved it would have made one of the three
   * unreachable. The same for SA, which LB1 turns into AL - leaving Thai with
   * no interior break opportunity at all. */
  EXPECT_EQ(guni_line_break(0x30FC), GUNI_LB_CJ);
  EXPECT_NE(guni_line_break(0x30FC), GUNI_LB_NS);
  EXPECT_EQ(guni_line_break(0x0E01), GUNI_LB_SA);
  EXPECT_NE(guni_line_break(0x0E01), GUNI_LB_AL);
  EXPECT_EQ(guni_line_break(0x00BD), GUNI_LB_AI);
  /* And XX really is XX, not AL: unassigned codepoints keep the class the
   * table gives them. */
  EXPECT_EQ(guni_line_break(0x0378), GUNI_LB_XX);
}

TEST(Char, VerticalOrientationForVerticalLayout) {
  /* UAX #50. A Latin letter rotates when the line runs top to bottom; a CJK
   * ideograph does not. font's vertical layout reads this and nothing else
   * in the suite does yet, which is why it had no caller and coverage said so. */
  EXPECT_EQ(guni_vertical_orientation('A'), GUNI_VO_R);
  EXPECT_EQ(guni_vertical_orientation(0x3042), GUNI_VO_U);
  EXPECT_EQ(guni_vertical_orientation(0x5146), GUNI_VO_U);
}

TEST(Char, EmojiProperties) {
  const uint32_t cp = 0x1F600;
  EXPECT_EQ(guni_general_category(cp), GUNI_GC_SO);
  EXPECT_EQ(guni_east_asian_width(cp), GUNI_EAW_W);
  EXPECT_TRUE(guni_has_property(cp, GUNI_PROP_EMOJI));
  EXPECT_TRUE(guni_has_property(cp, GUNI_PROP_EMOJI_PRESENTATION));
  EXPECT_TRUE(guni_has_property(cp, GUNI_PROP_EXTENDED_PICTOGRAPHIC));
  EXPECT_FALSE(guni_has_property(cp, GUNI_PROP_EMOJI_MODIFIER));
  /* The regional indicators, which UAX #29 pairs. */
  EXPECT_TRUE(guni_has_property(0x1F1E6, GUNI_PROP_REGIONAL_INDICATOR));
  EXPECT_EQ(guni_grapheme_cluster_break(0x1F1E6), GUNI_GCB_REGIONAL_INDICATOR);
}

TEST(Char, NumericValuesAreExactRationals) {
  int64_t numerator = 0;
  uint32_t denominator = 0;
  ASSERT_TRUE(guni_numeric_value('7', &numerator, &denominator));
  EXPECT_EQ(numerator, 7);
  EXPECT_EQ(denominator, 1u);
  EXPECT_EQ(guni_numeric_type('7'), GUNI_NT_DECIMAL);

  ASSERT_TRUE(guni_numeric_value(0x00BD, &numerator, &denominator));
  EXPECT_EQ(numerator, 1);
  EXPECT_EQ(denominator, 2u);

  /* U+0F33 TIBETAN DIGIT HALF ZERO is negative, which a uint64 numerator
   * would have wrapped. */
  ASSERT_TRUE(guni_numeric_value(0x0F33, &numerator, &denominator));
  EXPECT_EQ(numerator, -1);
  EXPECT_EQ(denominator, 2u);

  /* U+5146 is 10^12, which a double would have rounded and a 32-bit
   * numerator would have truncated. */
  ASSERT_TRUE(guni_numeric_value(0x5146, &numerator, &denominator));
  EXPECT_EQ(numerator, INT64_C(1000000000000));
  EXPECT_EQ(denominator, 1u);

  EXPECT_FALSE(guni_numeric_value('A', &numerator, &denominator));
  EXPECT_EQ(guni_numeric_type('A'), GUNI_NT_NONE);
  EXPECT_FALSE(guni_numeric_value(0x110000, &numerator, &denominator));
  EXPECT_FALSE(guni_numeric_value('7', nullptr, &denominator));
  EXPECT_FALSE(guni_numeric_value('7', &numerator, nullptr));
}

TEST(Char, ScriptExtensionsIsNeverEmpty) {
  /* UAX #24's default is the codepoint's own Script, so no caller has to
   * special-case the absence of a set. */
  GUNI_Script buffer[32];
  size_t count = 0;
  ASSERT_EQ(guni_script_extensions('A', buffer, 32, &count), GUNI_OK);
  EXPECT_EQ(count, static_cast<size_t>(1));
  EXPECT_EQ(buffer[0], GUNI_SCRIPT_LATIN);
  EXPECT_TRUE(guni_script_extensions_contains('A', GUNI_SCRIPT_LATIN));
  EXPECT_FALSE(guni_script_extensions_contains('A', GUNI_SCRIPT_GREEK));

  /* U+0964 DEVANAGARI DANDA is shared by twenty-one scripts, and is the
   * codepoint that makes a fixed-size answer buffer a bad idea. */
  ASSERT_EQ(guni_script_extensions(0x0964, buffer, 32, &count), GUNI_OK);
  EXPECT_GT(count, static_cast<size_t>(15));
  EXPECT_TRUE(guni_script_extensions_contains(0x0964, GUNI_SCRIPT_DEVANAGARI));
  EXPECT_TRUE(guni_script_extensions_contains(0x0964, GUNI_SCRIPT_BENGALI));
  EXPECT_FALSE(guni_script_extensions_contains(0x0964, GUNI_SCRIPT_LATIN));
  /* Its own Script is Common, and Common is not in the set. */
  EXPECT_EQ(guni_script(0x0964), GUNI_SCRIPT_COMMON);

  /* The output contract: too small reports the requirement. */
  GUNI_Script one[1];
  size_t needed = 0;
  EXPECT_EQ(guni_script_extensions(0x0964, one, 1, &needed), GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, count);
  EXPECT_EQ(guni_script_extensions(0x0964, nullptr, 0, &needed), GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, count);
  EXPECT_EQ(guni_script_extensions('A', nullptr, 0, nullptr), GUNI_ERR_INVALID);
}

TEST(Char, BlocksIncludeTheGapsBetweenThem) {
  EXPECT_EQ(guni_block('A'), GUNI_BLOCK_BASIC_LATIN);
  EXPECT_EQ(guni_block(0x0378), GUNI_BLOCK_GREEK_AND_COPTIC);
  EXPECT_EQ(guni_block(0xE0000), GUNI_BLOCK_TAGS);
  /* A codepoint in no block at all, which is a value Blocks.txt does not
   * list and the library has to have an answer for. */
  EXPECT_EQ(guni_block(0x1FC00), GUNI_BLOCK_NO_BLOCK);
  EXPECT_EQ(guni_block(0x110000), GUNI_BLOCK_NO_BLOCK);
  EXPECT_STREQ(guni_value_name(GUNI_PROPERTY_BLOCK, guni_block('A')),
      "Basic Latin");
}

/**
 * design.md section 4.4: every function answers for every uint32_t. This is
 * the test that says so, and it is here rather than in the sweep because the
 * UCD has nothing to say about 0x110000 and so no oracle can.
 */
TEST(Char, EveryFunctionAnswersAboveTheLastCodepoint) {
  const uint32_t beyond[] = {0x110000, 0x110001, 0x7FFFFFFF, 0x80000000,
      0xFFFFFFFF};
  for (uint32_t cp : beyond) {
    EXPECT_FALSE(guni_is_codepoint(cp));
    EXPECT_EQ(guni_general_category(cp), GUNI_GC_CN) << std::hex << cp;
    EXPECT_EQ(guni_script(cp), GUNI_SCRIPT_UNKNOWN);
    EXPECT_EQ(guni_combining_class(cp), 0);
    EXPECT_EQ(guni_line_break(cp), GUNI_LB_XX);
    EXPECT_EQ(guni_grapheme_cluster_break(cp), GUNI_GCB_OTHER);
    EXPECT_EQ(guni_word_break(cp), GUNI_WB_OTHER);
    EXPECT_EQ(guni_sentence_break(cp), GUNI_SB_OTHER);
    EXPECT_EQ(guni_east_asian_width(cp), GUNI_EAW_N);
    EXPECT_EQ(guni_joining_type(cp), GUNI_JT_U);
    EXPECT_EQ(guni_joining_group(cp), GUNI_JG_NO_JOINING_GROUP);
    EXPECT_EQ(guni_indic_syllabic_category(cp), GUNI_INSC_OTHER);
    EXPECT_EQ(guni_indic_positional_category(cp), GUNI_INPC_NA);
    EXPECT_EQ(guni_indic_conjunct_break(cp), GUNI_INCB_NONE);
    EXPECT_EQ(guni_vertical_orientation(cp), GUNI_VO_R);
    EXPECT_EQ(guni_hangul_syllable_type(cp), GUNI_HST_NA);
    EXPECT_EQ(guni_decomposition_type(cp), GUNI_DT_NONE);
    EXPECT_EQ(guni_numeric_type(cp), GUNI_NT_NONE);
    EXPECT_EQ(guni_block(cp), GUNI_BLOCK_NO_BLOCK);
    /* Not even Any: a value that is not a codepoint is not in the codepoint
     * space, which is the one place this differs from "answer as unassigned". */
    EXPECT_FALSE(guni_has_property(cp, GUNI_PROP_ANY));
    EXPECT_FALSE(guni_has_property(cp, GUNI_PROP_ASSIGNED));
    EXPECT_FALSE(guni_has_property(cp, GUNI_PROP_ALPHABETIC));
    GUNI_Script buffer[4];
    size_t count = 0;
    EXPECT_EQ(guni_script_extensions(cp, buffer, 4, &count), GUNI_OK);
    EXPECT_EQ(count, static_cast<size_t>(1));
    EXPECT_EQ(buffer[0], GUNI_SCRIPT_UNKNOWN);
  }
}

TEST(Char, SurrogatesAndNoncharactersHaveProperties) {
  /* A surrogate is a codepoint with properties, even though no UTF-8 encodes
   * it, and a library that refused to classify one would make a caller
   * special-case its own data. */
  EXPECT_EQ(guni_general_category(0xD800), GUNI_GC_CS);
  EXPECT_EQ(guni_general_category(0xDFFF), GUNI_GC_CS);
  EXPECT_TRUE(guni_has_property(0xD800, GUNI_PROP_ANY));
  EXPECT_EQ(guni_general_category(0xFFFE), GUNI_GC_CN);
  EXPECT_TRUE(guni_has_property(0xFDD0, GUNI_PROP_NONCHARACTER_CODE_POINT));
  EXPECT_TRUE(guni_has_property(0x10FFFF, GUNI_PROP_NONCHARACTER_CODE_POINT));
  EXPECT_EQ(guni_general_category(0xE000), GUNI_GC_CO);
}

TEST(Char, OutOfRangeBinaryPropertyIsFalseRatherThanARead) {
  EXPECT_FALSE(guni_has_property('A',
      static_cast<GUNI_BinaryProperty>(GUNI_PROP_COUNT)));
  EXPECT_FALSE(guni_has_property('A',
      static_cast<GUNI_BinaryProperty>(1000)));
  EXPECT_EQ(guni_property_value('A',
                static_cast<GUNI_Property>(GUNI_PROPERTY_COUNT)),
      0u);
}

TEST(Char, GenericAccessorAgreesWithTheTypedOnes) {
  /* guni_property_value() is what a regex compiler and the sweep use. If it
   * disagreed with the typed accessors, the sweep would be checking something
   * no consumer calls. */
  const uint32_t samples[] = {0, 'A', 0x301, 0x628, 0x1F600, 0xAC01, 0x0E01,
      0xD800, 0x10FFFF, 0x110000};
  for (uint32_t cp : samples) {
    EXPECT_EQ(guni_property_value(cp, GUNI_PROPERTY_GENERAL_CATEGORY),
        static_cast<uint32_t>(guni_general_category(cp)));
    EXPECT_EQ(guni_property_value(cp, GUNI_PROPERTY_SCRIPT),
        static_cast<uint32_t>(guni_script(cp)));
    EXPECT_EQ(guni_property_value(cp, GUNI_PROPERTY_LINE_BREAK),
        static_cast<uint32_t>(guni_line_break(cp)));
    EXPECT_EQ(guni_property_value(cp, GUNI_PROPERTY_CANONICAL_COMBINING_CLASS),
        static_cast<uint32_t>(guni_combining_class(cp)));
    EXPECT_EQ(guni_property_value(cp, GUNI_PROPERTY_BLOCK),
        static_cast<uint32_t>(guni_block(cp)));
    EXPECT_EQ(guni_property_value(cp, GUNI_PROPERTY_ALPHABETIC),
        guni_has_property(cp, GUNI_PROP_ALPHABETIC) ? 1u : 0u);
    /* Script_Extensions is set-valued and says so rather than guessing. */
    EXPECT_EQ(guni_property_kind(GUNI_PROPERTY_SCRIPT_EXTENSIONS),
        GUNI_PROP_KIND_SCX);
    EXPECT_EQ(guni_property_value(cp, GUNI_PROPERTY_SCRIPT_EXTENSIONS), 0u);
  }
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
