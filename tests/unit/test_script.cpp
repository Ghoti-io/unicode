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
 * Script runs and script itemisation.
 *
 * The cases that matter are the ones where "one script" is not "one Script
 * property value": the confusable-domain attack the rule exists to catch, the
 * three-way Han mix that the augmented sets exist for, and the digit rule that
 * is independent of the script rule.
 */

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

#include "test_helpers.h"

namespace {

using Codepoints = std::vector<uint32_t>;

std::vector<GUNI_ScriptItem> items(const Codepoints & text) {
  size_t needed = 0;
  GUNI_Result first = guni_script_items(text.data(), text.size(), nullptr, 0,
      &needed);
  EXPECT_TRUE(first == GUNI_OK || first == GUNI_ERR_LIMIT);
  std::vector<GUNI_ScriptItem> out(needed);
  size_t written = 0;
  EXPECT_EQ(guni_script_items(text.data(), text.size(), out.data(), out.size(),
                &written),
      GUNI_OK);
  EXPECT_EQ(written, needed);
  return out;
}

TEST(Script, TheConfusableDomainIsNotAScriptRun) {
  /* The question the rule exists to answer. "paypal" in Latin is a run;
   * "paypal" with a Cyrillic а looks identical and is not. */
  const Codepoints latin = {'p', 'a', 'y', 'p', 'a', 'l'};
  const Codepoints spoofed = {'p', 0x0430, 'y', 'p', 'a', 'l'};
  EXPECT_TRUE(guni_script_run(latin.data(), latin.size()));
  EXPECT_FALSE(guni_script_run(spoofed.data(), spoofed.size()));
  EXPECT_TRUE(guni_script_run_utf8("paypal", 6));
  EXPECT_FALSE(guni_script_run_utf8("p\xD0\xB0ypal", 7));
}

TEST(Script, CommonAndInheritedGoWithAnything) {
  /* Punctuation is Common and a diacritic is Inherited: neither narrows what
   * script the run could be. */
  const Codepoints dotted = {'a', '.', 'b'};
  EXPECT_TRUE(guni_script_run(dotted.data(), dotted.size()));
  const Codepoints accented = {0x03B1, 0x0301}; /* alpha, combining acute */
  EXPECT_TRUE(guni_script_run(accented.data(), accented.size()));
  /* A run beginning with punctuation constrains nothing, which is the case
   * that made regex's first version fail: the first character must not write
   * its own set into the intersection. */
  const Codepoints leading = {'.', 0x03B1};
  EXPECT_TRUE(guni_script_run(leading.data(), leading.size()));
  const Codepoints leading_mixed = {'.', 0x03B1, 0x0430};
  EXPECT_FALSE(guni_script_run(leading_mixed.data(), leading_mixed.size()));
}

TEST(Script, TheThreeWayHanMix) {
  /* The reason the tables carry augmented sets. Han goes with Hiragana in
   * Japanese, with Hangul in Korean and with Bopomofo in Taiwanese Mandarin,
   * so each pair is a run - and all three together are none of those
   * languages and must not be. A plain pairwise intersection would accept it. */
  const uint32_t han = 0x6F22;      /* CJK ideograph */
  const uint32_t hiragana = 0x3042; /* hiragana a */
  const uint32_t hangul = 0xAC00;   /* hangul syllable */
  const uint32_t bopomofo = 0x3105; /* bopomofo b */

  const Codepoints japanese = {han, hiragana};
  const Codepoints korean = {han, hangul};
  const Codepoints mandarin = {han, bopomofo};
  EXPECT_TRUE(guni_script_run(japanese.data(), japanese.size()));
  EXPECT_TRUE(guni_script_run(korean.data(), korean.size()));
  EXPECT_TRUE(guni_script_run(mandarin.data(), mandarin.size()));

  const Codepoints all_three = {han, hangul, bopomofo};
  EXPECT_FALSE(guni_script_run(all_three.data(), all_three.size()))
      << "Hangul and Bopomofo together are no language's script set";
  const Codepoints kana_and_hangul = {hiragana, hangul};
  EXPECT_FALSE(guni_script_run(kana_and_hangul.data(), kana_and_hangul.size()));
}

TEST(Script, TheDigitRuleIsIndependentOfTheScriptRule) {
  /* Every ASCII digit is Common, so the script half accepts any mix of digit
   * systems; the digit rule refuses them, because "1" and the Arabic-Indic "١"
   * are two different tens. */
  const Codepoints ascii = {'1', '2'};
  EXPECT_TRUE(guni_script_run(ascii.data(), ascii.size()));
  const Codepoints arabic_indic = {0x0661, 0x0662};
  EXPECT_TRUE(guni_script_run(arabic_indic.data(), arabic_indic.size()));
  const Codepoints mixed = {'1', 0x0662};
  EXPECT_FALSE(guni_script_run(mixed.data(), mixed.size()))
      << "two digit systems in one run";
  /* And it applies across the script rule: Devanagari digits with Devanagari
   * letters are fine, ASCII digits with them are fine too - both are one digit
   * system each - but the two digit systems together are not. */
  const Codepoints devanagari = {0x0915, 0x0966, 0x0967};
  EXPECT_TRUE(guni_script_run(devanagari.data(), devanagari.size()));
  const Codepoints two_systems = {0x0915, 0x0966, '1'};
  EXPECT_FALSE(guni_script_run(two_systems.data(), two_systems.size()));
}

TEST(Script, AShortStringIsAlwaysARun) {
  /* "A string that is less than two characters long is a script run. This is
   * the only case in which an Unknown character can be part of one." A rule,
   * not a consequence: an unassigned codepoint's script is Unknown, which
   * intersects nothing. */
  const Codepoints unassigned = {0xE0000};
  EXPECT_TRUE(guni_script_run(unassigned.data(), unassigned.size()));
  EXPECT_TRUE(guni_script_run(nullptr, 0));
  const Codepoints two_unknown = {0xE0000, 0xE0001};
  EXPECT_FALSE(guni_script_run(two_unknown.data(), two_unknown.size()));
  /* And an Unknown character anywhere in a longer string ends it, even when
   * the character that makes the string long enough arrives later. */
  const Codepoints late = {0xE0000, 'a'};
  EXPECT_FALSE(guni_script_run(late.data(), late.size()));
  /* Ill-formed bytes are not a string, so they are not a run. */
  EXPECT_FALSE(guni_script_run_utf8("a\xFF", 2));
  EXPECT_TRUE(guni_script_run_utf8(nullptr, 0));
  EXPECT_FALSE(guni_script_run(nullptr, 3)) << "a null buffer with a length";
}

TEST(Script, TheIncrementalFormMatchesTheWholeString) {
  /* The rule is not decomposable - {A,B}, {B,C} and {C,A} intersect pairwise
   * and not together - so the incremental form exists and has to agree with
   * the batch one. */
  const std::vector<Codepoints> cases = {
    {'a', 'b', 'c'},
    {'a', 0x0430},
    {0x6F22, 0xAC00, 0x3105},
    {'.', 0x03B1, 0x0301},
    {'1', 0x0662},
    {0xE0000},
  };
  for (const Codepoints & text : cases) {
    GUNI_ScriptRun state;
    guni_script_run_begin(&state);
    bool incremental = true;
    for (uint32_t cp : text) {
      if (!guni_script_run_add(&state, cp)) {
        incremental = false;
        break;
      }
    }
    EXPECT_EQ(incremental, guni_script_run(text.data(), text.size()));
  }
  /* The null-state paths refuse rather than crash. */
  guni_script_run_begin(nullptr);
  EXPECT_FALSE(guni_script_run_add(nullptr, 'a'));
}

TEST(Script, ItemisationCutsAtScriptChanges) {
  /* The shaper's question. Greek, then Latin, then Greek again: three runs,
   * and the space between them joins the run before it. */
  const Codepoints text = {0x03B1, 0x03B2, ' ', 'a', 'b', ' ', 0x03B3};
  std::vector<GUNI_ScriptItem> found = items(text);
  ASSERT_EQ(found.size(), static_cast<size_t>(3));
  EXPECT_EQ(found[0].start, static_cast<size_t>(0));
  EXPECT_EQ(found[0].length, static_cast<size_t>(3)) << "the space joins Greek";
  EXPECT_EQ(found[0].script, GUNI_SCRIPT_GREEK);
  EXPECT_EQ(found[1].start, static_cast<size_t>(3));
  EXPECT_EQ(found[1].length, static_cast<size_t>(3));
  EXPECT_EQ(found[1].script, GUNI_SCRIPT_LATIN);
  EXPECT_EQ(found[2].script, GUNI_SCRIPT_GREEK);

  /* The runs tile the text: a shaper has to account for every character. */
  size_t total = 0;
  for (const GUNI_ScriptItem & item : found) {
    EXPECT_EQ(item.start, total);
    total += item.length;
  }
  EXPECT_EQ(total, text.size());
}

TEST(Script, ALeadingNeutralRunTakesTheScriptThatFollowsIt) {
  /* There is no run before it to join, so the decision is deferred - which is
   * why the itemiser keeps a "still neutral" flag rather than deciding when the
   * run starts. */
  const Codepoints text = {'"', ' ', 0x05D0, 0x05D1};
  std::vector<GUNI_ScriptItem> found = items(text);
  ASSERT_EQ(found.size(), static_cast<size_t>(1));
  EXPECT_EQ(found[0].script, GUNI_SCRIPT_HEBREW);
  EXPECT_EQ(found[0].length, text.size());

  /* Text that is nothing but neutrals is one run of Common: there is nothing
   * to take a script from, and a shaper still has to draw it. */
  const Codepoints neutral = {' ', '.', '1'};
  found = items(neutral);
  ASSERT_EQ(found.size(), static_cast<size_t>(1));
  EXPECT_EQ(found[0].script, GUNI_SCRIPT_COMMON);
}

TEST(Script, ScriptExtensionsKeepSharedCharactersWithTheirRun) {
  /* U+0964 DEVANAGARI DANDA has Script=Common and a Script_Extensions set of
   * twenty-one scripts. In Devanagari text it belongs to the Devanagari run;
   * it would be a run of its own if the itemiser looked only at Script. */
  const Codepoints devanagari = {0x0915, 0x0964, 0x0916};
  std::vector<GUNI_ScriptItem> found = items(devanagari);
  ASSERT_EQ(found.size(), static_cast<size_t>(1));
  EXPECT_EQ(found[0].script, GUNI_SCRIPT_DEVANAGARI);
  EXPECT_EQ(found[0].length, devanagari.size());

  /* And a character whose Script_Extensions does not include the run's script
   * starts a new run. */
  const Codepoints mixed = {0x0915, 0x05D0};
  found = items(mixed);
  EXPECT_EQ(found.size(), static_cast<size_t>(2));
}

TEST(Script, ItemisationOverUtf8UsesByteOffsets) {
  const std::string text = "\xCE\xB1\xCE\xB2 ab"; /* alpha beta space a b */
  size_t needed = 0;
  ASSERT_EQ(guni_script_items_utf8(text.data(), text.size(),
                GUNI_INVALID_REFUSE, nullptr, 0, &needed),
      GUNI_ERR_LIMIT);
  std::vector<GUNI_ScriptItem> found(needed);
  size_t written = 0;
  ASSERT_EQ(guni_script_items_utf8(text.data(), text.size(),
                GUNI_INVALID_REFUSE, found.data(), found.size(), &written),
      GUNI_OK);
  ASSERT_EQ(written, static_cast<size_t>(2));
  EXPECT_EQ(found[0].start, static_cast<size_t>(0));
  EXPECT_EQ(found[0].length, static_cast<size_t>(5))
      << "two two-byte Greek letters and the space";
  EXPECT_EQ(found[0].script, GUNI_SCRIPT_GREEK);
  EXPECT_EQ(found[1].start, static_cast<size_t>(5));
  EXPECT_EQ(found[1].length, static_cast<size_t>(2));
  EXPECT_EQ(found[1].script, GUNI_SCRIPT_LATIN);

  /* Ill-formed bytes follow utf.h's policy. */
  EXPECT_EQ(guni_script_items_utf8("a\xFF", 2, GUNI_INVALID_REFUSE,
                found.data(), found.size(), &written),
      GUNI_ERR_INVALID);
  ASSERT_EQ(guni_script_items_utf8("a\xFF", 2, GUNI_INVALID_SKIP, found.data(),
                found.size(), &written),
      GUNI_OK);
  EXPECT_EQ(written, static_cast<size_t>(1));
  ASSERT_EQ(guni_script_items_utf8("a\xFF", 2, GUNI_INVALID_REPLACE,
                found.data(), found.size(), &written),
      GUNI_OK);
  EXPECT_EQ(written, static_cast<size_t>(1))
      << "U+FFFD is Common, so it joins the Latin run";
}

TEST(Script, ItemisationErrorPaths) {
  const Codepoints text = {'a'};
  GUNI_ScriptItem out[4];
  size_t written = 0;
  EXPECT_EQ(guni_script_items(nullptr, 3, out, 4, &written), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_script_items(text.data(), 1, nullptr, 4, &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_script_items(text.data(), 1, out, 4, nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_script_items_utf8(nullptr, 3, GUNI_INVALID_REFUSE, out, 4,
                &written),
      GUNI_ERR_INVALID);
  /* Empty text is zero runs and not an error. */
  size_t empty = 99;
  ASSERT_EQ(guni_script_items(nullptr, 0, out, 4, &empty), GUNI_OK);
  EXPECT_EQ(empty, static_cast<size_t>(0));
  /* A buffer too small reports the requirement and what fitted is the first
   * runs, so a caller that grows and retries gets a superset. */
  const Codepoints three = {0x03B1, 'a', 0x05D0};
  size_t needed = 0;
  ASSERT_EQ(guni_script_items(three.data(), three.size(), nullptr, 0, &needed),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, static_cast<size_t>(3));
  EXPECT_EQ(guni_script_items(three.data(), three.size(), out, 1, &written),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(written, static_cast<size_t>(3));
  EXPECT_EQ(out[0].script, GUNI_SCRIPT_GREEK);
}


TEST(Script, ANonNeutralCharacterCanStillJoinAnotherScriptsRun) {
  /* The case that needs Script_Extensions rather than Script, and that the
   * danda does not reach: U+0375 GREEK LOWER NUMERAL SIGN has Script=Greek -
   * not Common, so it is not neutral - and its Script_Extensions set names
   * Coptic as well. In Coptic text it joins the Coptic run. */
  EXPECT_EQ(guni_script(0x0375), GUNI_SCRIPT_GREEK);
  EXPECT_TRUE(guni_script_extensions_contains(0x0375, GUNI_SCRIPT_COPTIC));
  const Codepoints coptic = {0x2C80, 0x0375, 0x2C81};
  std::vector<GUNI_ScriptItem> found = items(coptic);
  ASSERT_EQ(found.size(), static_cast<size_t>(1));
  EXPECT_EQ(found[0].script, GUNI_SCRIPT_COPTIC);
  EXPECT_EQ(found[0].length, coptic.size());
  /* In Greek text it is Greek, which is the same rule reaching the other
   * answer. */
  const Codepoints greek = {0x03B1, 0x0375, 0x03B2};
  found = items(greek);
  ASSERT_EQ(found.size(), static_cast<size_t>(1));
  EXPECT_EQ(found[0].script, GUNI_SCRIPT_GREEK);
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
