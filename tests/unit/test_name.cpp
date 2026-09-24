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
 * UAX #44 character names, forward and back.
 *
 * The exhaustive part is the round trip: every codepoint that has a name is
 * named, and that name resolves back to it. 40,470 of them, plus the 11,172
 * Hangul syllables and the 100,000-odd CJK ideographs whose names are computed
 * rather than stored - which is where a round trip is worth more than a sample,
 * because the computation is the thing that can be wrong.
 */

#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/name.h>
#include <ghoti.io/unicode/unicode.h>

#include "test_helpers.h"

namespace {

std::string name_of(uint32_t cp) {
  char buffer[GUNI_NAME_MAX_LENGTH + 1];
  size_t length = 0;
  GUNI_Result result = guni_name(cp, buffer, sizeof(buffer), &length);
  if (result != GUNI_OK) {
    return std::string();
  }
  EXPECT_EQ(length, std::strlen(buffer));
  return std::string(buffer, length);
}

uint32_t by_name(const std::string & name) {
  uint32_t cp = 0;
  EXPECT_EQ(guni_codepoint_by_name(name.data(), name.size(), &cp), GUNI_OK)
      << "no codepoint named " << name;
  return cp;
}

TEST(Name, NamesAreDecodedFromTheWordDictionary) {
  EXPECT_EQ(name_of('A'), "LATIN CAPITAL LETTER A");
  EXPECT_EQ(name_of(0x00DF), "LATIN SMALL LETTER SHARP S");
  EXPECT_EQ(name_of(0x1F600), "GRINNING FACE");
  /* A name with a hyphen in it, which is a separator the encoding has to keep
   * apart from a space. */
  EXPECT_EQ(name_of(0x0F00), "TIBETAN SYLLABLE OM");
  EXPECT_EQ(name_of(0x11C88), "MARCHEN LETTER -A");
  EXPECT_EQ(name_of(0x1180), "HANGUL JUNGSEONG O-E");
}

TEST(Name, CodepointsWithoutNamesSayNoRatherThanGuessing) {
  /* A control character has no name of its own: UnicodeData.txt gives it as
   * <control>, and its names are aliases. */
  EXPECT_EQ(name_of(0x0000), "");
  EXPECT_FALSE(guni_has_name(0x0000));
  EXPECT_FALSE(guni_has_name(0xD800)) << "a surrogate";
  EXPECT_FALSE(guni_has_name(0xE000)) << "private use";
  EXPECT_FALSE(guni_has_name(0x0378)) << "unassigned";
  EXPECT_FALSE(guni_has_name(0x110000)) << "not a codepoint";
  EXPECT_TRUE(guni_has_name('A'));
}

TEST(Name, TheAlgorithmicFamiliesAreComputed) {
  /* HANGUL SYLLABLE GAG alone would cost 11,172 rows, and the CJK ideographs
   * a hundred thousand more. */
  EXPECT_EQ(name_of(0x4E00), "CJK UNIFIED IDEOGRAPH-4E00");
  EXPECT_EQ(name_of(0x20000), "CJK UNIFIED IDEOGRAPH-20000");
  EXPECT_EQ(name_of(0x17000), "TANGUT IDEOGRAPH-17000");
  EXPECT_EQ(name_of(0xAC00), "HANGUL SYLLABLE GA");
  EXPECT_EQ(name_of(0xAC01), "HANGUL SYLLABLE GAG");
  EXPECT_EQ(name_of(0xD7A3), "HANGUL SYLLABLE HIH");

  /* And they resolve back, which is the half that needs the parse. */
  EXPECT_EQ(by_name("CJK UNIFIED IDEOGRAPH-4E00"), UINT32_C(0x4E00));
  EXPECT_EQ(by_name("HANGUL SYLLABLE GAG"), UINT32_C(0xAC01));
  EXPECT_EQ(by_name("hangul syllable hih"), UINT32_C(0xD7A3));
  EXPECT_EQ(by_name("TANGUT IDEOGRAPH-17000"), UINT32_C(0x17000));

  /* A name that is the prefix plus something that is not hex, or hex outside
   * the range, resolves to nothing rather than to a partial parse. */
  uint32_t cp = 0;
  EXPECT_EQ(guni_codepoint_by_name("CJK UNIFIED IDEOGRAPH-4E00ZZ", 28, &cp),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_codepoint_by_name("CJK UNIFIED IDEOGRAPH-0041", 26, &cp),
      GUNI_ERR_INVALID)
      << "U+0041 is not a CJK ideograph";
  EXPECT_EQ(guni_codepoint_by_name("HANGUL SYLLABLE QQ", 18, &cp),
      GUNI_ERR_INVALID);
}

TEST(Name, EveryNameRoundTrips) {
  /* The exhaustive half. Every codepoint that has a name is named, the name
   * resolves back to it, and no two codepoints share a name - which is what
   * makes the reverse lookup a function. */
  size_t named = 0;
  size_t algorithmic = 0;
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    const std::string name = name_of(cp);
    if (name.empty()) {
      continue;
    }
    ++named;
    uint32_t back = 0;
    ASSERT_EQ(guni_codepoint_by_name(name.data(), name.size(), &back), GUNI_OK)
        << "U+" << std::hex << cp << " is named " << name
        << " and that name resolves to nothing";
    ASSERT_EQ(back, cp) << name;
    /* The loose forms resolve too: lower case, no spaces, underscores for
     * spaces - every spelling a pattern might use. */
    std::string lowered = name;
    for (char & character : lowered) {
      if (character >= 'A' && character <= 'Z') {
        character = static_cast<char>(character - 'A' + 'a');
      }
    }
    ASSERT_EQ(guni_codepoint_by_name(lowered.data(), lowered.size(), &back),
        GUNI_OK) << lowered;
    ASSERT_EQ(back, cp);
    std::string underscored;
    for (char character : name) {
      underscored += (character == ' ') ? '_' : character;
    }
    ASSERT_EQ(guni_codepoint_by_name(underscored.data(), underscored.size(),
                  &back),
        GUNI_OK) << underscored;
    ASSERT_EQ(back, cp);
    if (cp >= 0x3400) {
      ++algorithmic;
    }
  }
  /* The denominators: 40,470 stored names and more than a hundred thousand
   * computed ones. A test that named nothing would otherwise pass. */
  EXPECT_GT(named, static_cast<size_t>(140000)) << "only " << named << " named";
  EXPECT_GT(algorithmic, static_cast<size_t>(100000));
}

TEST(Name, LooseMatchingHasTwoSubtletiesAndBothCostACodepoint) {
  /* UAX #44-LM2's stated exception: U+116C is HANGUL JUNGSEONG OE and U+1180 is
   * HANGUL JUNGSEONG O-E, so that hyphen is the difference between two
   * characters and is the one hyphen the rule keeps. */
  EXPECT_EQ(by_name("HANGUL JUNGSEONG OE"), UINT32_C(0x116C));
  EXPECT_EQ(by_name("HANGUL JUNGSEONG O-E"), UINT32_C(0x1180));
  EXPECT_EQ(by_name("hanguljungseongo-e"), UINT32_C(0x1180));
  EXPECT_EQ(by_name("hangul jungseong oe"), UINT32_C(0x116C));

  /* And the one the Standard leaves to the word "medial", which the generator's
   * collision check found before any test did: a hyphen after a space is not
   * medial, so MARCHEN LETTER -A and MARCHEN LETTER A stay apart. */
  EXPECT_EQ(by_name("MARCHEN LETTER -A"), UINT32_C(0x11C88));
  EXPECT_EQ(by_name("MARCHEN LETTER A"), UINT32_C(0x11C8F));
  EXPECT_NE(by_name("MARCHEN LETTER -A"), by_name("MARCHEN LETTER A"));

  /* An ordinary medial hyphen is ignored, which is the rule's main clause. */
  EXPECT_EQ(by_name("CJK UNIFIED IDEOGRAPH 4E00"), UINT32_C(0x4E00))
      << "the hyphen in the algorithmic name is optional";
  EXPECT_EQ(by_name("LATIN-SMALL-LETTER-A"), static_cast<uint32_t>('a'));
}

TEST(Name, AliasesAreWhereAControlCharactersNameIs) {
  /* U+0000 has no name; it has two aliases, and the kinds tell a caller which
   * to show a user. */
  ASSERT_EQ(guni_name_alias_count(0x0000), static_cast<size_t>(2));
  char buffer[GUNI_NAME_MAX_LENGTH + 1];
  size_t length = 0;
  GUNI_NameAliasKind kind = GUNI_NAME_ALIAS_FIGMENT;
  ASSERT_EQ(guni_name_alias(0x0000, 0, &kind, buffer, sizeof(buffer), &length),
      GUNI_OK);
  EXPECT_EQ(std::string(buffer, length), "NULL");
  EXPECT_EQ(kind, GUNI_NAME_ALIAS_CONTROL);
  ASSERT_EQ(guni_name_alias(0x0000, 1, &kind, buffer, sizeof(buffer), &length),
      GUNI_OK);
  EXPECT_EQ(std::string(buffer, length), "NUL");
  EXPECT_EQ(kind, GUNI_NAME_ALIAS_ABBREVIATION);
  EXPECT_EQ(guni_name_alias(0x0000, 2, &kind, buffer, sizeof(buffer), &length),
      GUNI_ERR_INVALID);

  /* Both resolve, which is what a pattern like \N{NUL} needs. */
  EXPECT_EQ(by_name("NULL"), 0u);
  EXPECT_EQ(by_name("NUL"), 0u);

  /* A correction: U+01A2 is named LATIN CAPITAL LETTER OI today and GHA is the
   * correction alias, so both resolve to it. Checked against the data rather
   * than from memory - the first version of this test said OU. */
  EXPECT_EQ(by_name("LATIN CAPITAL LETTER GHA"), UINT32_C(0x01A2));
  EXPECT_EQ(name_of(0x01A2), "LATIN CAPITAL LETTER OI");
  bool found_correction = false;
  for (size_t index = 0; index < guni_name_alias_count(0x01A2); ++index) {
    ASSERT_EQ(guni_name_alias(0x01A2, index, &kind, buffer, sizeof(buffer),
                  &length),
        GUNI_OK);
    if (kind == GUNI_NAME_ALIAS_CORRECTION) {
      found_correction = true;
      EXPECT_EQ(std::string(buffer, length), "LATIN CAPITAL LETTER GHA");
    }
  }
  EXPECT_TRUE(found_correction);

  /* A figment - a name for a character that was never encoded as described -
   * and an alternate, both of which Perl resolves and so does this. */
  EXPECT_EQ(by_name("WEIERSTRASS ELLIPTIC FUNCTION"), UINT32_C(0x2118));
  EXPECT_EQ(by_name("BYTE ORDER MARK"), UINT32_C(0xFEFF));

  /* Every alias resolves to the codepoint it belongs to. */
  size_t checked = 0;
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    size_t count = guni_name_alias_count(cp);
    for (size_t index = 0; index < count; ++index) {
      ASSERT_EQ(guni_name_alias(cp, index, &kind, buffer, sizeof(buffer),
                    &length),
          GUNI_OK);
      uint32_t back = 0;
      ASSERT_EQ(guni_codepoint_by_name(buffer, length, &back), GUNI_OK)
          << buffer;
      EXPECT_EQ(back, cp) << buffer;
      ++checked;
    }
  }
  EXPECT_EQ(checked, static_cast<size_t>(481)) << "NameAliases.txt's rows";
}

TEST(Name, NamedSequencesAreNamesForSequences) {
  uint32_t out[GUNI_SEQUENCE_MAX_LENGTH];
  size_t length = 0;
  ASSERT_EQ(guni_named_sequence("LATIN SMALL LETTER A WITH MACRON AND GRAVE",
                42, out, GUNI_SEQUENCE_MAX_LENGTH, &length),
      GUNI_OK);
  ASSERT_EQ(length, static_cast<size_t>(2));
  EXPECT_EQ(out[0], UINT32_C(0x0101));
  EXPECT_EQ(out[1], UINT32_C(0x0300));

  /* Matched loosely, like every other name. */
  ASSERT_EQ(guni_named_sequence("latinsmallletterawithmacronandgrave", 35, out,
                GUNI_SEQUENCE_MAX_LENGTH, &length),
      GUNI_OK);
  EXPECT_EQ(length, static_cast<size_t>(2));

  /* A named sequence is not a codepoint, so the codepoint lookup does not
   * resolve it - which is the distinction the two functions exist for. */
  uint32_t cp = 0;
  EXPECT_EQ(guni_codepoint_by_name("LATIN SMALL LETTER A WITH MACRON AND GRAVE",
                42, &cp),
      GUNI_ERR_INVALID);

  /* The output contract. */
  size_t needed = 0;
  EXPECT_EQ(guni_named_sequence("LATIN SMALL LETTER A WITH MACRON AND GRAVE",
                42, nullptr, 0, &needed),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, static_cast<size_t>(2));
  EXPECT_EQ(guni_named_sequence("NOT A SEQUENCE", 14, out,
                GUNI_SEQUENCE_MAX_LENGTH, &length),
      GUNI_ERR_INVALID);
}

TEST(Name, TheOutputContractAndTheErrorPaths) {
  /* The preflight reports the exact length, so a caller can size a buffer or
   * use GUNI_NAME_MAX_LENGTH and never ask. */
  size_t needed = 0;
  EXPECT_EQ(guni_name('A', nullptr, 0, &needed), GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, std::strlen("LATIN CAPITAL LETTER A"));
  char small[8];
  EXPECT_EQ(guni_name('A', small, sizeof(small), &needed), GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, std::strlen("LATIN CAPITAL LETTER A"));
  EXPECT_EQ(guni_name(0x4E00, nullptr, 0, &needed), GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, std::strlen("CJK UNIFIED IDEOGRAPH-4E00"));
  EXPECT_EQ(guni_name(0xAC01, nullptr, 0, &needed), GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, std::strlen("HANGUL SYLLABLE GAG"));

  /* The longest name fits the constant, which is the promise the constant
   * makes. */
  size_t longest = 0;
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    size_t length = 0;
    if (guni_name(cp, nullptr, 0, &length) == GUNI_ERR_LIMIT && length > longest) {
      longest = length;
    }
  }
  EXPECT_EQ(longest, static_cast<size_t>(GUNI_NAME_MAX_LENGTH));

  uint32_t cp = 0;
  EXPECT_EQ(guni_name('A', small, sizeof(small), nullptr), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_name('A', nullptr, 4, &needed), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_codepoint_by_name(nullptr, 4, &cp), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_codepoint_by_name("A", 1, nullptr), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_codepoint_by_name("", 0, &cp), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_codepoint_by_name("   ", 3, &cp), GUNI_ERR_INVALID)
      << "a name that normalises away to nothing";
  EXPECT_EQ(guni_codepoint_by_name("NOT A CHARACTER NAME", 20, &cp),
      GUNI_ERR_INVALID);
  const std::string huge(4096, 'X');
  EXPECT_EQ(guni_codepoint_by_name(huge.data(), huge.size(), &cp),
      GUNI_ERR_INVALID)
      << "longer than any name, and it must not overrun the fold buffer";
  EXPECT_EQ(guni_name_alias_count(0x110000), static_cast<size_t>(0));
  EXPECT_EQ(guni_name_alias('A', 0, nullptr, small, sizeof(small), &needed),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_name_alias(0x0000, 0, nullptr, small, sizeof(small), nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_named_sequence(nullptr, 4, nullptr, 0, &needed),
      GUNI_ERR_INVALID);
  uint32_t sequence[GUNI_SEQUENCE_MAX_LENGTH];
  EXPECT_EQ(guni_named_sequence("A", 1, sequence, GUNI_SEQUENCE_MAX_LENGTH,
                nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_named_sequence("A", 1, nullptr, 4, &needed), GUNI_ERR_INVALID);

  /* A hex tail that overflows the codepoint space, which the parser has to
   * refuse rather than wrap. */
  EXPECT_EQ(guni_codepoint_by_name("CJK UNIFIED IDEOGRAPH-FFFFFFFFFF", 32, &cp),
      GUNI_ERR_INVALID);
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
