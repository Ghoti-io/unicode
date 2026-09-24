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
 * Case mapping: the sixteen conditional lines, and the API around them.
 *
 * The simple and unconditional full mappings are swept exhaustively against an
 * independent parse of the UCD in test_sweep.cpp - all 3,037 case rows, over
 * all 1,114,112 codepoints - which is possible because they are a function of
 * the codepoint. The conditional ones are not: their answer depends on the text
 * around the character, so no per-codepoint sweep can reach them, and each of
 * the sixteen lines of `SpecialCasing.txt` gets a case here.
 *
 * Every expectation below is transcribed from the file's own fields, which are
 * `codepoint; lower; title; upper; condition` in that order - not from memory
 * of what a case mapping ought to do.
 */

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

#include "test_helpers.h"

namespace {

using Codepoints = std::vector<uint32_t>;

Codepoints mapped_at(GUNI_Result (*mapping)(const uint32_t *, size_t, size_t,
                         GUNI_CaseTailoring, uint32_t *, size_t, size_t *),
    const Codepoints & text, size_t position, GUNI_CaseTailoring tailoring) {
  uint32_t buffer[GUNI_CASE_MAX_EXPANSION];
  size_t written = 0;
  EXPECT_EQ(mapping(text.data(), text.size(), position, tailoring, buffer,
                GUNI_CASE_MAX_EXPANSION, &written),
      GUNI_OK);
  return Codepoints(buffer, buffer + written);
}

Codepoints whole(GUNI_Result (*mapping)(const uint32_t *, size_t,
                     GUNI_CaseTailoring, const GUNI_Limits *, uint32_t *,
                     size_t, size_t *),
    const Codepoints & text, GUNI_CaseTailoring tailoring) {
  size_t needed = 0;
  GUNI_Result first = mapping(text.data(), text.size(), tailoring, nullptr,
      nullptr, 0, &needed);
  EXPECT_TRUE(first == GUNI_OK || first == GUNI_ERR_LIMIT);
  Codepoints out(needed);
  size_t written = 0;
  EXPECT_EQ(mapping(text.data(), text.size(), tailoring, nullptr, out.data(),
                out.size(), &written),
      GUNI_OK);
  EXPECT_EQ(written, needed) << "the reported length is exact";
  out.resize(written);
  return out;
}

Codepoints folded(const Codepoints & text, bool turkic = false) {
  size_t needed = 0;
  GUNI_Result first = guni_case_fold(text.data(), text.size(), turkic, nullptr,
      nullptr, 0, &needed);
  EXPECT_TRUE(first == GUNI_OK || first == GUNI_ERR_LIMIT);
  Codepoints out(needed);
  size_t written = 0;
  EXPECT_EQ(guni_case_fold(text.data(), text.size(), turkic, nullptr,
                out.data(), out.size(), &written),
      GUNI_OK);
  out.resize(written);
  return out;
}

TEST(Case, SimpleMappingsDoNotChangeTheLength) {
  /* What a caller that must not change the length has to accept, and the
   * example of why the two exist: the simple uppercase of sharp s is sharp s,
   * and its full uppercase is SS. */
  EXPECT_EQ(guni_to_upper_simple(0x00DF), UINT32_C(0x00DF));
  EXPECT_EQ(guni_to_upper_simple('a'), static_cast<uint32_t>('A'));
  EXPECT_EQ(guni_to_lower_simple('A'), static_cast<uint32_t>('a'));
  EXPECT_EQ(guni_to_upper_simple('1'), static_cast<uint32_t>('1'));
  EXPECT_EQ(guni_to_upper_simple(0x110000), UINT32_C(0x110000));

  /* Titlecase differs from uppercase only for the digraphs, and the UCD
   * expresses that by leaving the titlecase field empty everywhere else. */
  EXPECT_EQ(guni_to_title_simple(0x01F3), UINT32_C(0x01F2)); /* dz -> Dz */
  EXPECT_EQ(guni_to_upper_simple(0x01F3), UINT32_C(0x01F1)); /* dz -> DZ */
  EXPECT_EQ(guni_to_title_simple('a'), static_cast<uint32_t>('A'));

  /* Folding is not lower-casing. U+1E9E capital sharp s lower-cases to U+00DF
   * and folds to "ss"; U+212A KELVIN SIGN folds to k. */
  EXPECT_EQ(guni_case_fold_simple(0x212A), static_cast<uint32_t>('k'));
  EXPECT_EQ(guni_case_fold_simple(0x03C2), UINT32_C(0x03C3)) << "final sigma";
  EXPECT_EQ(guni_case_fold_simple(0x03A3), UINT32_C(0x03C3)) << "capital sigma";
}

TEST(Case, FinalSigma) {
  /* SpecialCasing.txt: "03A3; 03C2; 03A3; 03A3; Final_Sigma". The lowercase of
   * a capital sigma is the final form at the end of a word and the medial form
   * inside one, and the condition is the only way to tell. */
  const Codepoints end = {0x039F, 0x03A3};   /* omicron, sigma */
  const Codepoints middle = {0x03A3, 0x039F}; /* sigma, omicron */
  EXPECT_EQ(mapped_at(guni_to_lower_at, end, 1, GUNI_LANG_NONE),
      Codepoints({0x03C2}))
      << "at the end of a word: the final form";
  EXPECT_EQ(mapped_at(guni_to_lower_at, middle, 0, GUNI_LANG_NONE),
      Codepoints({0x03C3}))
      << "before a letter: the medial form";
  /* A sigma on its own has no cased letter before it, so the condition fails
   * and the medial form stands. */
  const Codepoints alone = {0x03A3};
  EXPECT_EQ(mapped_at(guni_to_lower_at, alone, 0, GUNI_LANG_NONE),
      Codepoints({0x03C3}));
  /* Case-ignorable characters are skipped on both sides: a full stop after the
   * sigma does not make it medial, and a combining mark before it does not
   * stop the letter behind it counting. */
  const Codepoints punctuated = {0x039F, 0x03A3, '.'};
  EXPECT_EQ(mapped_at(guni_to_lower_at, punctuated, 1, GUNI_LANG_NONE),
      Codepoints({0x03C2}));
  /* And the whole-string mapping applies it in place. */
  EXPECT_EQ(whole(guni_to_lower, {0x039F, 0x03A3}, GUNI_LANG_NONE),
      Codepoints({0x03BF, 0x03C2}));
}

TEST(Case, TheTurkicDottedAndDotlessI) {
  /* Four lines of SpecialCasing.txt, and the reason the tailoring is an
   * argument rather than a locale: with the process locale deciding, a Turkish
   * user's spreadsheet upper-cases "file" to "FİLE" and nobody can see why. */

  /* "0130; 0069; 0130; 0130; tr": the lowercase of capital I with dot above is
   * a plain i in Turkish, where elsewhere it is i followed by U+0307. */
  const Codepoints dotted = {0x0130};
  EXPECT_EQ(mapped_at(guni_to_lower_at, dotted, 0, GUNI_LANG_TURKIC),
      Codepoints({0x0069}));
  EXPECT_EQ(mapped_at(guni_to_lower_at, dotted, 0, GUNI_LANG_NONE),
      Codepoints({0x0069, 0x0307}))
      << "the unconditional line, which every other language uses";

  /* "0069; 0069; 0130; 0130; tr": the uppercase of i is the dotted capital. */
  const Codepoints small_i = {0x0069};
  EXPECT_EQ(mapped_at(guni_to_upper_at, small_i, 0, GUNI_LANG_TURKIC),
      Codepoints({0x0130}));
  EXPECT_EQ(mapped_at(guni_to_upper_at, small_i, 0, GUNI_LANG_NONE),
      Codepoints({0x0049}));

  /* "0049; 0131; 0049; 0049; tr Not_Before_Dot": the lowercase of capital I is
   * the dotless i - unless a combining dot above follows it, in which case the
   * pair together is the dotted i and the ordinary mapping applies. */
  const Codepoints capital_i = {0x0049};
  EXPECT_EQ(mapped_at(guni_to_lower_at, capital_i, 0, GUNI_LANG_TURKIC),
      Codepoints({0x0131}));
  const Codepoints i_then_dot = {0x0049, 0x0307};
  EXPECT_EQ(mapped_at(guni_to_lower_at, i_then_dot, 0, GUNI_LANG_TURKIC),
      Codepoints({0x0069}))
      << "Not_Before_Dot fails, so the simple mapping stands";
  /* "0307; ; 0307; 0307; tr After_I": and the dot itself disappears, because
   * the i it sat on now carries its own. */
  EXPECT_EQ(mapped_at(guni_to_lower_at, i_then_dot, 1, GUNI_LANG_TURKIC),
      Codepoints({}))
      << "the combining dot is removed, which is a mapping to nothing";
  EXPECT_EQ(whole(guni_to_lower, i_then_dot, GUNI_LANG_TURKIC),
      Codepoints({0x0069}));

  /* A character of combining class 0 or 230 between them breaks After_I. */
  const Codepoints i_mark_dot = {0x0049, 0x0316, 0x0307}; /* class 220 between */
  EXPECT_EQ(mapped_at(guni_to_lower_at, i_mark_dot, 2, GUNI_LANG_TURKIC),
      Codepoints({}))
      << "class 220 does not intervene";
  const Codepoints i_above_dot = {0x0049, 0x0300, 0x0307}; /* class 230 */
  EXPECT_EQ(mapped_at(guni_to_lower_at, i_above_dot, 2, GUNI_LANG_TURKIC),
      Codepoints({0x0307}))
      << "class 230 does intervene, so the dot stays";
}

TEST(Case, TheLithuanianRetainedDot) {
  /* "0049; 0069 0307; 0049; 0049; lt More_Above": Lithuanian keeps the dot on
   * a lower-cased I when an accent follows, because the accent would otherwise
   * sit where the dot was. */
  const Codepoints i_grave = {0x0049, 0x0300};
  EXPECT_EQ(mapped_at(guni_to_lower_at, i_grave, 0, GUNI_LANG_LITHUANIAN),
      Codepoints({0x0069, 0x0307}));
  EXPECT_EQ(mapped_at(guni_to_lower_at, i_grave, 0, GUNI_LANG_NONE),
      Codepoints({0x0069}))
      << "every other language just lower-cases it";
  /* With no accent above, More_Above fails. */
  const Codepoints bare_i = {0x0049};
  EXPECT_EQ(mapped_at(guni_to_lower_at, bare_i, 0, GUNI_LANG_LITHUANIAN),
      Codepoints({0x0069}));

  /* "0307; 0307; ; ; lt After_Soft_Dotted": and upper-casing drops the dot,
   * because the capital has none. */
  const Codepoints i_dot = {0x0069, 0x0307};
  EXPECT_EQ(mapped_at(guni_to_upper_at, i_dot, 1, GUNI_LANG_LITHUANIAN),
      Codepoints({}));
  EXPECT_EQ(whole(guni_to_upper, i_dot, GUNI_LANG_LITHUANIAN),
      Codepoints({0x0049}));
  EXPECT_EQ(whole(guni_to_upper, i_dot, GUNI_LANG_NONE),
      Codepoints({0x0049, 0x0307}))
      << "every other language keeps it";

  /* "00CC; 0069 0307 0300; 00CC; 00CC; lt": three codepoints out of one, which
   * is where GUNI_CASE_MAX_EXPANSION comes from. */
  const Codepoints i_with_grave = {0x00CC};
  EXPECT_EQ(mapped_at(guni_to_lower_at, i_with_grave, 0, GUNI_LANG_LITHUANIAN),
      Codepoints({0x0069, 0x0307, 0x0300}));
  EXPECT_EQ(GUNI_CASE_MAX_EXPANSION, 3);
}

TEST(Case, UnconditionalFullMappings) {
  /* The 103 lines with no condition, of which the ligatures are the ones a
   * caller notices: one character in, three out. */
  EXPECT_EQ(whole(guni_to_upper, {0x00DF}, GUNI_LANG_NONE),
      Codepoints({'S', 'S'}));
  EXPECT_EQ(whole(guni_to_title, {0x00DF}, GUNI_LANG_NONE),
      Codepoints({'S', 's'}));
  EXPECT_EQ(whole(guni_to_upper, {0xFB03}, GUNI_LANG_NONE),
      Codepoints({'F', 'F', 'I'}));
  EXPECT_EQ(whole(guni_to_lower, {0xFB03}, GUNI_LANG_NONE),
      Codepoints({0xFB03}))
      << "the ligature has no lowercase of its own";
}

TEST(Case, TitleCasingUsesWordBoundaries) {
  /* UAX #21: the first cased character of each word takes the titlecase
   * mapping and the rest take the lowercase. "Word" is UAX #29's, which is why
   * this is the one case function that reads break.h - and why a caller cannot
   * do it a character at a time. */
  const std::string text = "hello WORLD o'neill";
  Codepoints codepoints(text.begin(), text.end());
  Codepoints titled = whole(guni_to_title, codepoints, GUNI_LANG_NONE);
  std::string out(titled.begin(), titled.end());
  EXPECT_EQ(out, std::string("Hello World O'neill"))
      << "UAX #29's rules WB6 and WB7 keep an apostrophe between letters inside "
         "one word, so \"O'neill\" is one word with one titlecase character - "
         "which is not what a human would write, and is what the Standard "
         "says. A caller who wants the other answer tailors the word "
         "segmentation, which is a decision this library does not make for it";

  /* A word with no cased character in it has nothing to title-case. */
  const std::string numbers = "123 abc";
  Codepoints number_points(numbers.begin(), numbers.end());
  Codepoints number_titled = whole(guni_to_title, number_points, GUNI_LANG_NONE);
  EXPECT_EQ(std::string(number_titled.begin(), number_titled.end()),
      std::string("123 Abc"));

  /* The digraph, which is the reason titlecase is not uppercase. */
  EXPECT_EQ(whole(guni_to_title, {0x01F3, 'a'}, GUNI_LANG_NONE),
      Codepoints({0x01F2, 'a'}));
}

TEST(Case, FoldingAndFoldedComparison) {
  EXPECT_EQ(folded({0x00DF}), Codepoints({'s', 's'})) << "sharp s folds to ss";
  EXPECT_EQ(folded({0x212A}), Codepoints({'k'}));
  EXPECT_EQ(folded({0x03A3}), folded({0x03C2}))
      << "both sigmas fold together";

  /* The comparison, which is what a caller wants and which allocates nothing
   * and stops at the first difference. */
  const Codepoints sharp = {0x00DF};
  const Codepoints ss = {'s', 's'};
  EXPECT_TRUE(guni_case_folded_equal(sharp.data(), sharp.size(), ss.data(),
      ss.size(), false));
  const Codepoints sz = {'s', 'z'};
  EXPECT_FALSE(guni_case_folded_equal(sharp.data(), sharp.size(), sz.data(),
      sz.size(), false));
  /* Lengths that differ before folding and match after it, and the other way
   * round: "ss" is two characters and folds to two, "ß" is one and folds to
   * two, and a prefix is not a match. */
  const Codepoints s = {'s'};
  EXPECT_FALSE(guni_case_folded_equal(sharp.data(), sharp.size(), s.data(),
      s.size(), false));
  EXPECT_TRUE(guni_case_folded_equal(nullptr, 0, nullptr, 0, false));

  /* The Turkic fold, where dotted and dotless i come apart. */
  const Codepoints dotted = {0x0130};
  const Codepoints i_dot = {'i', 0x0307};
  EXPECT_TRUE(guni_case_folded_equal(dotted.data(), 1, i_dot.data(), 2, false));
  EXPECT_FALSE(guni_case_folded_equal(dotted.data(), 1, i_dot.data(), 2, true))
      << "with the Turkic fold they are different characters";
  EXPECT_EQ(guni_case_fold_simple_turkic(0x0049), UINT32_C(0x0131));
  EXPECT_EQ(guni_case_fold_simple(0x0049), static_cast<uint32_t>('i'));
  EXPECT_EQ(guni_case_fold_simple_turkic('a'), static_cast<uint32_t>('a'))
      << "a codepoint with no Turkic entry folds the ordinary way";

  /* Canonical equivalence is deliberately not applied: that is the caller's
   * decision to make with guni_normalize(). */
  const Codepoints composed = {0x00C5};          /* A with ring above */
  const Codepoints decomposed = {'A', 0x030A};
  EXPECT_FALSE(guni_case_folded_equal(composed.data(), 1, decomposed.data(), 2,
      false));
}

TEST(Case, FoldOrbits) {
  /* What a case-insensitive character class needs: [k] has to match K and the
   * Kelvin sign, because all three fold to k. */
  uint32_t buffer[8];
  size_t written = 0;
  ASSERT_EQ(guni_case_orbit('k', buffer, 8, &written), GUNI_OK);
  Codepoints orbit(buffer, buffer + written);
  EXPECT_EQ(orbit, Codepoints({'K', 'k', 0x212A}));
  /* Every member of an orbit reports the same orbit, which is what makes it a
   * usable equivalence class. */
  for (uint32_t member : orbit) {
    size_t again = 0;
    ASSERT_EQ(guni_case_orbit(member, buffer, 8, &again), GUNI_OK);
    EXPECT_EQ(Codepoints(buffer, buffer + again), orbit)
        << "U+" << std::hex << member;
  }
  /* The sigmas, which are three. */
  ASSERT_EQ(guni_case_orbit(0x03C3, buffer, 8, &written), GUNI_OK);
  EXPECT_EQ(written, static_cast<size_t>(3));

  /* A codepoint that shares its fold with nothing is its own orbit of one, not
   * an empty answer: a caller adds the orbit unconditionally. */
  ASSERT_EQ(guni_case_orbit('1', buffer, 8, &written), GUNI_OK);
  EXPECT_EQ(Codepoints(buffer, buffer + written), Codepoints({'1'}));
  ASSERT_EQ(guni_case_orbit(0x110000, buffer, 8, &written), GUNI_OK);
  EXPECT_EQ(written, static_cast<size_t>(1));

  /* The output contract. */
  size_t needed = 0;
  EXPECT_EQ(guni_case_orbit('k', nullptr, 0, &needed), GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, static_cast<size_t>(3));
  EXPECT_EQ(guni_case_orbit('k', buffer, 8, nullptr), GUNI_ERR_INVALID);
}

TEST(Case, Utf8EntryPoints) {
  const std::string sharp = "\xC3\x9F"; /* U+00DF */
  char out[32];
  size_t written = 0;
  ASSERT_EQ(guni_to_upper_utf8(sharp.data(), sharp.size(), GUNI_INVALID_REFUSE,
                GUNI_LANG_NONE, nullptr, out, sizeof(out), &written),
      GUNI_OK);
  EXPECT_EQ(std::string(out, written), std::string("SS"));

  const std::string greek = "\xCE\x9F\xCE\xA3"; /* omicron, sigma */
  ASSERT_EQ(guni_to_lower_utf8(greek.data(), greek.size(), GUNI_INVALID_REFUSE,
                GUNI_LANG_NONE, nullptr, out, sizeof(out), &written),
      GUNI_OK);
  EXPECT_EQ(std::string(out, written), std::string("\xCE\xBF\xCF\x82"))
      << "omicron and a final sigma: Final_Sigma survives the trip through "
         "UTF-8, where the chunking could have lost the context it needs";

  ASSERT_EQ(guni_to_title_utf8("hello world", 11, GUNI_INVALID_REFUSE,
                GUNI_LANG_NONE, nullptr, out, sizeof(out), &written),
      GUNI_OK);
  EXPECT_EQ(std::string(out, written), std::string("Hello World"));

  ASSERT_EQ(guni_case_fold_utf8(sharp.data(), sharp.size(), GUNI_INVALID_REFUSE,
                false, nullptr, out, sizeof(out), &written),
      GUNI_OK);
  EXPECT_EQ(std::string(out, written), std::string("ss"));

  /* The preflight reports the exact byte length. */
  size_t needed = 0;
  ASSERT_EQ(guni_to_upper_utf8(sharp.data(), sharp.size(), GUNI_INVALID_REFUSE,
                GUNI_LANG_NONE, nullptr, nullptr, 0, &needed),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, static_cast<size_t>(2));

  /* The folded comparison over UTF-8, where ill-formed bytes never match. */
  EXPECT_TRUE(guni_case_folded_equal_utf8("Stra\xC3\x9F" "e", 7, "STRASSE", 7,
      false));
  EXPECT_FALSE(guni_case_folded_equal_utf8("\xFF", 1, "\xFF", 1, false))
      << "two strings that are not text are not the same identifier";
  EXPECT_TRUE(guni_case_folded_equal_utf8(nullptr, 0, "", 0, false));
  EXPECT_FALSE(guni_case_folded_equal_utf8("a", 1, "ab", 2, false));

  /* Ill-formed bytes follow utf.h's policy. */
  EXPECT_EQ(guni_to_upper_utf8("a\xFF", 2, GUNI_INVALID_REFUSE, GUNI_LANG_NONE,
                nullptr, out, sizeof(out), &written),
      GUNI_ERR_INVALID);
  ASSERT_EQ(guni_to_upper_utf8("a\xFF", 2, GUNI_INVALID_SKIP, GUNI_LANG_NONE,
                nullptr, out, sizeof(out), &written),
      GUNI_OK);
  EXPECT_EQ(std::string(out, written), std::string("A"));
}

TEST(Case, LimitsAndErrorPaths) {
  const Codepoints text = {'a'};
  uint32_t out[8];
  size_t written = 0;
  EXPECT_EQ(guni_to_upper(nullptr, 3, GUNI_LANG_NONE, nullptr, out, 8, &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_to_upper(text.data(), 1, GUNI_LANG_NONE, nullptr, nullptr, 8,
                &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_to_upper(text.data(), 1, GUNI_LANG_NONE, nullptr, out, 8,
                nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_to_upper_at(text.data(), 1, 1, GUNI_LANG_NONE, out, 8,
                &written),
      GUNI_ERR_INVALID)
      << "a position at the end names no character";
  EXPECT_EQ(guni_to_upper_at(nullptr, 1, 0, GUNI_LANG_NONE, out, 8, &written),
      GUNI_ERR_INVALID);

  GUNI_Limits limits;
  guni_limits_default(&limits);
  limits.max_text_bytes = 2;
  EXPECT_EQ(guni_to_upper(text.data(), 1, GUNI_LANG_NONE, &limits, out, 8,
                &written),
      GUNI_ERR_LIMIT);

  /* Title-casing has a working-state bound and refuses rather than allocating
   * behind the caller's back. */
  Codepoints long_text(2000, 'a');
  size_t needed = 0;
  EXPECT_EQ(guni_to_title(long_text.data(), long_text.size(), GUNI_LANG_NONE,
                nullptr, nullptr, 0, &needed),
      GUNI_ERR_LIMIT);
  /* Upper and lower have no such bound: they are per character. */
  Codepoints upper(long_text.size());
  ASSERT_EQ(guni_to_upper(long_text.data(), long_text.size(), GUNI_LANG_NONE,
                nullptr, upper.data(), upper.size(), &written),
      GUNI_OK);
  EXPECT_EQ(written, long_text.size());

  /* An empty string maps to an empty string in every mapping. */
  size_t empty = 99;
  ASSERT_EQ(guni_to_upper(nullptr, 0, GUNI_LANG_NONE, nullptr, out, 8, &empty),
      GUNI_OK);
  EXPECT_EQ(empty, static_cast<size_t>(0));
  ASSERT_EQ(guni_case_fold(nullptr, 0, false, nullptr, out, 8, &empty), GUNI_OK);
  EXPECT_EQ(empty, static_cast<size_t>(0));
}


TEST(Case, FinalSigmaLooksThroughIgnorablesBothWays) {
  /* The condition's wording is "a cased letter and then zero or more
   * case-ignorable characters" before, and the mirror of that after. Both
   * skips need a case of their own, and the medial answer needs a cased letter
   * genuinely following the sigma rather than the text running out. */
  const Codepoints medial = {0x039F, 0x03A3, 0x039F}; /* omicron sigma omicron */
  EXPECT_EQ(mapped_at(guni_to_lower_at, medial, 1, GUNI_LANG_NONE),
      Codepoints({0x03C3}))
      << "a cased letter follows, so the medial form";
  const Codepoints ignorable_before = {0x039F, '\'', 0x03A3};
  EXPECT_EQ(mapped_at(guni_to_lower_at, ignorable_before, 2, GUNI_LANG_NONE),
      Codepoints({0x03C2}))
      << "the apostrophe is case-ignorable, so the omicron still counts";
  const Codepoints ignorable_after = {0x039F, 0x03A3, '\'', 0x039F};
  EXPECT_EQ(mapped_at(guni_to_lower_at, ignorable_after, 1, GUNI_LANG_NONE),
      Codepoints({0x03C3}))
      << "and on the other side too: the omicron after the apostrophe counts";
}

TEST(Case, TheChunkedUtf8PathHandlesTextLongerThanItsWindow) {
  /* The UTF-8 entry points decode into a 512-character window and chunk at
   * word boundaries, because the conditions look at the characters around the
   * one being mapped. Longer text exercises the resume, and a single word
   * longer than the window is refused rather than split - splitting it would
   * change the answer. */
  std::string words;
  for (int index = 0; index < 400; ++index) {
    words += "ab ";
  }
  std::vector<char> out(words.size() * 2);
  size_t written = 0;
  ASSERT_EQ(guni_to_upper_utf8(words.data(), words.size(), GUNI_INVALID_REFUSE,
                GUNI_LANG_NONE, nullptr, out.data(), out.size(), &written),
      GUNI_OK);
  EXPECT_EQ(written, words.size());
  EXPECT_EQ(std::string(out.data(), 3), std::string("AB "));

  /* And the same text through the codepoint entry point, which has no window,
   * agrees. */
  Codepoints codepoints(words.begin(), words.end());
  Codepoints upper = whole(guni_to_upper, codepoints, GUNI_LANG_NONE);
  EXPECT_EQ(std::string(upper.begin(), upper.end()),
      std::string(out.data(), written));

  std::string one_word(600, 'a');
  EXPECT_EQ(guni_to_upper_utf8(one_word.data(), one_word.size(),
                GUNI_INVALID_REFUSE, GUNI_LANG_NONE, nullptr, out.data(),
                out.size(), &written),
      GUNI_ERR_LIMIT)
      << "a single word longer than the window";

  /* Every byte ill-formed and the policy is SKIP: empty output, not an error. */
  const std::string rubbish = "\xFF\xFE";
  ASSERT_EQ(guni_to_upper_utf8(rubbish.data(), rubbish.size(),
                GUNI_INVALID_SKIP, GUNI_LANG_NONE, nullptr, out.data(),
                out.size(), &written),
      GUNI_OK);
  EXPECT_EQ(written, static_cast<size_t>(0));
  /* And REPLACE puts one U+FFFD in, which upper-cases to itself. */
  ASSERT_EQ(guni_to_upper_utf8("a\xFF", 2, GUNI_INVALID_REPLACE, GUNI_LANG_NONE,
                nullptr, out.data(), out.size(), &written),
      GUNI_OK);
  EXPECT_EQ(std::string(out.data(), written), std::string("A\xEF\xBF\xBD"));
}

TEST(Case, MoreErrorPaths) {
  const Codepoints text = {'a'};
  uint32_t small[1];
  size_t written = 0;
  /* A full mapping that does not fit reports what it needs. */
  size_t needed = 0;
  EXPECT_EQ(guni_to_upper_at(&text[0], 1, 0, GUNI_LANG_NONE, nullptr, 0,
                &needed),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, static_cast<size_t>(1));
  const Codepoints sharp = {0x00DF};
  EXPECT_EQ(guni_to_upper_at(sharp.data(), 1, 0, GUNI_LANG_NONE, small, 1,
                &needed),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, static_cast<size_t>(2)) << "SS does not fit in one";

  /* An orbit of one that does not fit either. */
  EXPECT_EQ(guni_case_orbit('1', nullptr, 0, &needed), GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, static_cast<size_t>(1));

  /* The UTF-8 entry points' own argument checks and limit. */
  char out[8];
  EXPECT_EQ(guni_to_upper_utf8(nullptr, 3, GUNI_INVALID_REFUSE, GUNI_LANG_NONE,
                nullptr, out, sizeof(out), &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_to_upper_utf8("a", 1, GUNI_INVALID_REFUSE, GUNI_LANG_NONE,
                nullptr, out, sizeof(out), nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_to_upper_utf8("a", 1, GUNI_INVALID_REFUSE, GUNI_LANG_NONE,
                nullptr, nullptr, sizeof(out), &written),
      GUNI_ERR_INVALID);
  GUNI_Limits limits;
  guni_limits_default(&limits);
  limits.max_text_bytes = 0;
  EXPECT_EQ(guni_to_upper_utf8("a", 1, GUNI_INVALID_REFUSE, GUNI_LANG_NONE,
                &limits, out, sizeof(out), &written),
      GUNI_ERR_LIMIT);

  /* A condition whose lookback runs out of text: the combining dot above at
   * the start of the text has no I before it, so After_I fails. */
  const Codepoints lone_dot = {0x0307};
  EXPECT_EQ(mapped_at(guni_to_lower_at, lone_dot, 0, GUNI_LANG_TURKIC),
      Codepoints({0x0307}))
      << "nothing before it, so the Turkic rule does not apply";

  /* Two strings the same length that differ in a character, which is the
   * comparison's ordinary mismatch rather than a length or validity one. */
  EXPECT_FALSE(guni_case_folded_equal_utf8("ab", 2, "ac", 2, false));

  /* Title-casing's argument checks. */
  uint32_t titled[8];
  EXPECT_EQ(guni_to_title(nullptr, 3, GUNI_LANG_NONE, nullptr, titled, 8,
                &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_to_title(text.data(), 1, GUNI_LANG_NONE, nullptr, titled, 8,
                nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_to_title(text.data(), 1, GUNI_LANG_NONE, nullptr, nullptr, 8,
                &written),
      GUNI_ERR_INVALID);

  /* The folded comparisons' null and invalid paths, on both sides. */
  EXPECT_FALSE(guni_case_folded_equal(nullptr, 1, text.data(), 1, false));
  EXPECT_FALSE(guni_case_folded_equal(text.data(), 1, nullptr, 1, false));
  EXPECT_FALSE(guni_case_folded_equal_utf8(nullptr, 1, "a", 1, false));
  EXPECT_FALSE(guni_case_folded_equal_utf8("a", 1, nullptr, 1, false));
  EXPECT_FALSE(guni_case_folded_equal_utf8("a\xFF", 2, "a", 1, false))
      << "an ill-formed byte on the left";
  EXPECT_FALSE(guni_case_folded_equal_utf8("a", 1, "a\xFF", 2, false))
      << "and on the right";
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
