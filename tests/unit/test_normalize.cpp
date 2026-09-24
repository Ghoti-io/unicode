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
 * The normalisation API's own contract: the buffer protocol, the quick
 * checks, the stream-safe transform, Hangul, and the bounds.
 *
 * What the four forms actually produce is tests/conformance/test_norm.cpp's
 * job, against the Consortium's file. What is here is everything that file
 * cannot say: that the expansion constants are the real bounds, that the
 * composing forms report a sufficient length when the buffer is too small,
 * and that Hangul - which has no lines in the file because it is arithmetic -
 * decomposes and composes.
 */

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

#include "test_helpers.h"

namespace {

using Codepoints = std::vector<uint32_t>;

Codepoints normalize(GUNI_NormForm form, const Codepoints & input,
    GUNI_Result * result_out = nullptr) {
  size_t needed = 0;
  GUNI_Result first = guni_normalize(form, input.data(), input.size(), nullptr,
      nullptr, 0, &needed);
  if (result_out != nullptr) {
    *result_out = first;
  }
  Codepoints out(needed);
  size_t written = 0;
  GUNI_Result second = guni_normalize(form, input.data(), input.size(), nullptr,
      out.data(), out.size(), &written);
  EXPECT_EQ(second, GUNI_OK);
  out.resize(written);
  return out;
}

TEST(Normalize, DecomposeIsFullyExpanded) {
  /* The generator took the fixed point, so nothing the library returns is
   * decomposable again. U+1E0A is D with dot above; its canonical
   * decomposition is D plus the mark, and U+1E14's is three deep in the
   * source data. */
  uint32_t out[GUNI_NORM_MAX_EXPANSION_NFKD];
  size_t written = 0;
  ASSERT_EQ(guni_decompose(0x1E0A, false, out, 18, &written), GUNI_OK);
  ASSERT_EQ(written, static_cast<size_t>(2));
  EXPECT_EQ(out[0], UINT32_C(0x0044));
  EXPECT_EQ(out[1], UINT32_C(0x0307));

  /* U+1E14 LATIN CAPITAL LETTER E WITH MACRON AND GRAVE decomposes through
   * U+0112, and the answer here is the fixed point, not one step. */
  ASSERT_EQ(guni_decompose(0x1E14, false, out, 18, &written), GUNI_OK);
  ASSERT_EQ(written, static_cast<size_t>(3));
  EXPECT_EQ(out[0], UINT32_C(0x0045));
  EXPECT_EQ(out[1], UINT32_C(0x0304));
  EXPECT_EQ(out[2], UINT32_C(0x0300));

  /* A codepoint with no decomposition is itself, not an error. */
  ASSERT_EQ(guni_decompose('A', true, out, 18, &written), GUNI_OK);
  EXPECT_EQ(written, static_cast<size_t>(1));
  EXPECT_EQ(out[0], static_cast<uint32_t>('A'));
  ASSERT_EQ(guni_decompose(0x110000, true, out, 18, &written), GUNI_OK);
  EXPECT_EQ(written, static_cast<size_t>(1));
  EXPECT_EQ(out[0], UINT32_C(0x110000));
}

TEST(Normalize, CompatibilityDecompositionIsSeparate) {
  /* U+FB01 LATIN SMALL LIGATURE FI has a compatibility decomposition and no
   * canonical one, which is the distinction NFD and NFKD are about. */
  uint32_t out[GUNI_NORM_MAX_EXPANSION_NFKD];
  size_t written = 0;
  ASSERT_EQ(guni_decompose(0xFB01, false, out, 18, &written), GUNI_OK);
  EXPECT_EQ(written, static_cast<size_t>(1));
  EXPECT_EQ(out[0], UINT32_C(0xFB01));
  ASSERT_EQ(guni_decompose(0xFB01, true, out, 18, &written), GUNI_OK);
  ASSERT_EQ(written, static_cast<size_t>(2));
  EXPECT_EQ(out[0], static_cast<uint32_t>('f'));
  EXPECT_EQ(out[1], static_cast<uint32_t>('i'));
}

TEST(Normalize, TheExpansionBoundsAreTheRealBounds) {
  /* design.md section 6.3: the constants are generated from the data so that
   * a caller can size a buffer without a preflight. This is the check that
   * they are not merely plausible - every codepoint is decomposed and
   * measured, and U+FDFA is the witness for 18. */
  size_t worst_nfd = 0;
  size_t worst_nfkd = 0;
  uint32_t worst_nfkd_cp = 0;
  uint32_t out[64];
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    size_t written = 0;
    ASSERT_EQ(guni_decompose(cp, false, out, 64, &written), GUNI_OK);
    if (written > worst_nfd) {
      worst_nfd = written;
    }
    ASSERT_EQ(guni_decompose(cp, true, out, 64, &written), GUNI_OK);
    if (written > worst_nfkd) {
      worst_nfkd = written;
      worst_nfkd_cp = cp;
    }
  }
  EXPECT_EQ(worst_nfd, static_cast<size_t>(GUNI_NORM_MAX_EXPANSION_NFD));
  EXPECT_EQ(worst_nfkd, static_cast<size_t>(GUNI_NORM_MAX_EXPANSION_NFKD));
  EXPECT_EQ(worst_nfkd_cp, UINT32_C(0xFDFA))
      << "the witness for the 18-codepoint bound changed";
}

TEST(Normalize, CompositionExcludesWhatTheUcdExcludes) {
  EXPECT_EQ(guni_compose('A', 0x0301), UINT32_C(0x00C1));
  EXPECT_EQ(guni_compose(0x0041, 0x0300), UINT32_C(0x00C0));
  /* A pair that does not compose. */
  EXPECT_EQ(guni_compose('A', 'B'), 0u);
  EXPECT_EQ(guni_compose(0x0301, 0x0301), 0u);
  /* U+212B ANGSTROM SIGN decomposes to U+00C5 but is a composition exclusion,
   * so composing A with the ring gives U+00C5 and never U+212B. */
  EXPECT_EQ(guni_compose(0x0041, 0x030A), UINT32_C(0x00C5));
  /* U+0344 is a singleton-ish case: it decomposes to two marks, and marks
   * compose with nothing. */
  EXPECT_EQ(guni_compose(0x0308, 0x0301), 0u);
}

TEST(Normalize, HangulIsArithmeticInBothDirections) {
  /* 11,172 syllables, no table: Standard section 3.12. The round trip over
   * every one of them is the check that the arithmetic is right, and it is
   * cheap enough to do exhaustively. */
  size_t checked = 0;
  for (uint32_t syllable = 0xAC00; syllable <= 0xD7A3; ++syllable) {
    uint32_t parts[GUNI_NORM_MAX_EXPANSION_NFKD];
    size_t written = 0;
    ASSERT_EQ(guni_decompose(syllable, false, parts, 18, &written), GUNI_OK);
    ASSERT_TRUE(written == 2 || written == 3) << std::hex << syllable;
    uint32_t composed = guni_compose(parts[0], parts[1]);
    ASSERT_NE(composed, 0u) << std::hex << syllable;
    if (written == 3) {
      composed = guni_compose(composed, parts[2]);
      ASSERT_NE(composed, 0u) << std::hex << syllable;
    }
    ASSERT_EQ(composed, syllable);
    ++checked;
  }
  EXPECT_EQ(checked, static_cast<size_t>(11172));

  /* And through the normaliser, where the interesting part is that a Jamo
   * sequence composes across what would be three starters. */
  const Codepoints jamo = {0x1100, 0x1161, 0x11A8};
  EXPECT_EQ(normalize(GUNI_NFC, jamo), Codepoints({0xAC01}));
  EXPECT_EQ(normalize(GUNI_NFD, Codepoints({0xAC01})), jamo);
}

TEST(Normalize, TheComposingFormsReportASufficientLength) {
  /* The buffer has to hold the intermediate decomposition, so ERR_LIMIT
   * reports a length that is enough rather than the exact one, and asking
   * again with it succeeds and returns the exact one. The header says so; this
   * is the check that it does. */
  const Codepoints input = {0x1E14, 0x1E14}; /* each decomposes to three */
  size_t needed = 0;
  ASSERT_EQ(guni_normalize(GUNI_NFC, input.data(), input.size(), nullptr,
                nullptr, 0, &needed),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, static_cast<size_t>(6)) << "the decomposed length";
  std::vector<uint32_t> out(needed);
  size_t written = 0;
  ASSERT_EQ(guni_normalize(GUNI_NFC, input.data(), input.size(), nullptr,
                out.data(), out.size(), &written),
      GUNI_OK);
  EXPECT_EQ(written, static_cast<size_t>(2)) << "the exact length";
  EXPECT_EQ(out[0], UINT32_C(0x1E14));

  /* A buffer that would hold the result but not the intermediate is still
   * refused, which is the price of not allocating. */
  size_t again = 0;
  EXPECT_EQ(guni_normalize(GUNI_NFC, input.data(), input.size(), nullptr,
                out.data(), 2, &again),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(again, static_cast<size_t>(6));
}

TEST(Normalize, QuickCheckDismissesMostTextAndNeverGuesses) {
  const Codepoints ascii = {'H', 'e', 'l', 'l', 'o'};
  EXPECT_EQ(guni_quick_check_text(GUNI_NFC, ascii.data(), ascii.size()),
      GUNI_QC_YES);
  EXPECT_EQ(guni_quick_check_text(GUNI_NFD, ascii.data(), ascii.size()),
      GUNI_QC_YES);

  /* A combining mark can compose with what precedes it, so the answer for
   * text containing one is MAYBE and only normalising can settle it. */
  const Codepoints maybe = {'A', 0x0301};
  EXPECT_EQ(guni_quick_check(GUNI_NFC, 0x0301), GUNI_QC_MAYBE);
  EXPECT_EQ(guni_quick_check_text(GUNI_NFC, maybe.data(), maybe.size()),
      GUNI_QC_MAYBE);
  bool normalised = true;
  ASSERT_EQ(guni_is_normalized(GUNI_NFC, maybe.data(), maybe.size(), nullptr,
                &normalised),
      GUNI_OK);
  EXPECT_FALSE(normalised);

  /* A precomposed character is not in NFD at all, which the quick check can
   * say outright. */
  const Codepoints no = {0x00C1};
  EXPECT_EQ(guni_quick_check(GUNI_NFD, 0x00C1), GUNI_QC_NO);
  EXPECT_EQ(guni_quick_check_text(GUNI_NFD, no.data(), no.size()), GUNI_QC_NO);

  /* Marks out of canonical order are not normalised in any form, whatever
   * their own quick-check properties are: UAX #15 section 9's first rule. */
  const Codepoints disordered = {'a', 0x0301, 0x0316}; /* class 230 then 220 */
  EXPECT_EQ(guni_quick_check_text(GUNI_NFD, disordered.data(),
                disordered.size()),
      GUNI_QC_NO);
  ASSERT_EQ(guni_is_normalized(GUNI_NFD, disordered.data(), disordered.size(),
                nullptr, &normalised),
      GUNI_OK);
  EXPECT_FALSE(normalised);
  EXPECT_EQ(normalize(GUNI_NFD, disordered), Codepoints({'a', 0x0316, 0x0301}));
}

TEST(Normalize, CanonicalOrderingIsStable) {
  /* Two marks of the same class keep the text's order, which is what makes
   * ordering idempotent. An unstable sort would change text that is already
   * normalised - the worst kind of bug here, because it would only show on
   * the second pass. */
  Codepoints text = {'a', 0x0301, 0x0300}; /* both class 230 */
  guni_canonical_order(text.data(), text.size());
  EXPECT_EQ(text, Codepoints({'a', 0x0301, 0x0300}));

  text = {'a', 0x0301, 0x0316, 0x0300}; /* 230, 220, 230 */
  guni_canonical_order(text.data(), text.size());
  EXPECT_EQ(text, Codepoints({'a', 0x0316, 0x0301, 0x0300}));

  /* Nothing moves across a starter. */
  text = {0x0301, 'b', 0x0316};
  guni_canonical_order(text.data(), text.size());
  EXPECT_EQ(text, Codepoints({0x0301, 'b', 0x0316}));

  guni_canonical_order(nullptr, 3);
}

TEST(Normalize, StreamSafeInsertsJoinersAndSaysWhenItNeedTo) {
  Codepoints many;
  many.push_back('a');
  for (int index = 0; index < 40; ++index) {
    many.push_back(0x0301);
  }
  EXPECT_FALSE(guni_is_stream_safe(many.data(), many.size(), nullptr));
  size_t needed = 0;
  ASSERT_EQ(guni_stream_safe(many.data(), many.size(), nullptr, nullptr, 0,
                &needed),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, many.size() + 1) << "one joiner for 40 marks";
  Codepoints safe(needed);
  size_t written = 0;
  ASSERT_EQ(guni_stream_safe(many.data(), many.size(), nullptr, safe.data(),
                safe.size(), &written),
      GUNI_OK);
  EXPECT_EQ(written, needed);
  EXPECT_TRUE(guni_is_stream_safe(safe.data(), written, nullptr));
  EXPECT_EQ(safe[31], UINT32_C(0x034F)) << "U+034F COMBINING GRAPHEME JOINER";

  /* Text that is already stream-safe passes through unchanged. */
  const Codepoints ordinary = {'a', 0x0301, 'b'};
  EXPECT_TRUE(guni_is_stream_safe(ordinary.data(), ordinary.size(), nullptr));
  Codepoints copy(8);
  ASSERT_EQ(guni_stream_safe(ordinary.data(), ordinary.size(), nullptr,
                copy.data(), copy.size(), &written),
      GUNI_OK);
  copy.resize(written);
  EXPECT_EQ(copy, ordinary);

  /* The bound is a limit the caller owns. */
  GUNI_Limits limits;
  guni_limits_default(&limits);
  EXPECT_EQ(limits.max_nonstarters, static_cast<size_t>(30));
  limits.max_nonstarters = 2;
  EXPECT_FALSE(guni_is_stream_safe(ordinary.data(), ordinary.size(), &limits)
      == false)
      << "one mark is within a bound of two";
  limits.max_nonstarters = 0;
  EXPECT_EQ(guni_stream_safe(ordinary.data(), ordinary.size(), &limits,
                copy.data(), copy.size(), &written),
      GUNI_ERR_INVALID)
      << "a bound of zero has no answer, so it is refused rather than looped";
}

TEST(Normalize, Utf8EntryPointsReportByteLengths) {
  const std::string composed = "\xC3\x81";          /* U+00C1 */
  const std::string decomposed = "A\xCC\x81";       /* A + U+0301 */
  size_t needed = 0;
  ASSERT_EQ(guni_normalize_utf8(GUNI_NFD, composed.data(), composed.size(),
                GUNI_INVALID_REFUSE, nullptr, nullptr, 0, &needed),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, decomposed.size());
  std::string out(needed, '\0');
  size_t written = 0;
  ASSERT_EQ(guni_normalize_utf8(GUNI_NFD, composed.data(), composed.size(),
                GUNI_INVALID_REFUSE, nullptr, out.data(), out.size(), &written),
      GUNI_OK);
  EXPECT_EQ(out, decomposed);
  EXPECT_EQ(written, decomposed.size());

  std::string back(8, '\0');
  ASSERT_EQ(guni_normalize_utf8(GUNI_NFC, decomposed.data(), decomposed.size(),
                GUNI_INVALID_REFUSE, nullptr, back.data(), back.size(),
                &written),
      GUNI_OK);
  back.resize(written);
  EXPECT_EQ(back, composed);

  bool normalised = false;
  ASSERT_EQ(guni_is_normalized_utf8(GUNI_NFC, composed.data(), composed.size(),
                nullptr, &normalised),
      GUNI_OK);
  EXPECT_TRUE(normalised);
  ASSERT_EQ(guni_is_normalized_utf8(GUNI_NFC, decomposed.data(),
                decomposed.size(), nullptr, &normalised),
      GUNI_OK);
  EXPECT_FALSE(normalised);
  EXPECT_EQ(guni_quick_check_utf8(GUNI_NFD, composed.data(), composed.size()),
      GUNI_QC_NO);
}

TEST(Normalize, Utf8RefusesIllFormedBytesAndCanReplaceThem) {
  const std::string broken = "a\xFF" "b";
  char out[16];
  size_t written = 0;
  EXPECT_EQ(guni_normalize_utf8(GUNI_NFC, broken.data(), broken.size(),
                GUNI_INVALID_REFUSE, nullptr, out, sizeof(out), &written),
      GUNI_ERR_INVALID);
  ASSERT_EQ(guni_normalize_utf8(GUNI_NFC, broken.data(), broken.size(),
                GUNI_INVALID_REPLACE, nullptr, out, sizeof(out), &written),
      GUNI_OK);
  EXPECT_EQ(std::string(out, written), std::string("a\xEF\xBF\xBD" "b"));
  ASSERT_EQ(guni_normalize_utf8(GUNI_NFC, broken.data(), broken.size(),
                GUNI_INVALID_SKIP, nullptr, out, sizeof(out), &written),
      GUNI_OK);
  EXPECT_EQ(std::string(out, written), std::string("ab"));
  /* Ill-formed bytes are in no normalisation form: saying MAYBE would send a
   * caller off to normalise something that cannot be normalised. */
  EXPECT_EQ(guni_quick_check_utf8(GUNI_NFC, broken.data(), broken.size()),
      GUNI_QC_NO);
  bool normalised = true;
  ASSERT_EQ(guni_is_normalized_utf8(GUNI_NFC, broken.data(), broken.size(),
                nullptr, &normalised),
      GUNI_OK);
  EXPECT_FALSE(normalised);
}

TEST(Normalize, LongRunsWithoutABoundaryAreRefusedRatherThanTruncated) {
  /* The UTF-8 path works between normalisation boundaries so that its working
   * buffer is fixed. A run with no boundary in it - which takes hundreds of
   * combining marks on one base, and which no natural text contains - is
   * refused, and guni_stream_safe() is the documented way through. */
  std::string attack = "a";
  for (int index = 0; index < 2000; ++index) {
    attack += "\xCC\x81"; /* U+0301, never a boundary */
  }
  std::vector<char> out(attack.size() * 2);
  size_t written = 0;
  EXPECT_EQ(guni_normalize_utf8(GUNI_NFD, attack.data(), attack.size(),
                GUNI_INVALID_REFUSE, nullptr, out.data(), out.size(), &written),
      GUNI_ERR_LIMIT);
  /* The codepoint entry point has no such limit: it works in the caller's
   * buffer, so there is nothing to overflow. */
  Codepoints codepoints;
  codepoints.push_back('a');
  for (int index = 0; index < 2000; ++index) {
    codepoints.push_back(0x0301);
  }
  size_t needed = 0;
  ASSERT_EQ(guni_normalize(GUNI_NFD, codepoints.data(), codepoints.size(),
                nullptr, nullptr, 0, &needed),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, codepoints.size());
  Codepoints result(needed);
  ASSERT_EQ(guni_normalize(GUNI_NFD, codepoints.data(), codepoints.size(),
                nullptr, result.data(), result.size(), &written),
      GUNI_OK);
  EXPECT_EQ(written, codepoints.size());
}

TEST(Normalize, LimitsAndErrorPathsAreRefusedRatherThanGuessed) {
  const Codepoints text = {'a', 0x0301};
  uint32_t out[8];
  size_t written = 0;
  EXPECT_EQ(guni_normalize(static_cast<GUNI_NormForm>(GUNI_NORM_FORM_COUNT),
                text.data(), text.size(), nullptr, out, 8, &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_normalize(GUNI_NFC, nullptr, 3, nullptr, out, 8, &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_normalize(GUNI_NFC, text.data(), text.size(), nullptr, nullptr,
                8, &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_normalize(GUNI_NFC, text.data(), text.size(), nullptr, out, 8,
                nullptr),
      GUNI_ERR_INVALID);

  /* max_text_bytes caps the input, counted as four bytes per codepoint. */
  GUNI_Limits limits;
  guni_limits_default(&limits);
  limits.max_text_bytes = 4;
  EXPECT_EQ(guni_normalize(GUNI_NFC, text.data(), text.size(), &limits, out, 8,
                &written),
      GUNI_ERR_LIMIT);
  limits.max_text_bytes = 8;
  EXPECT_EQ(guni_normalize(GUNI_NFC, text.data(), text.size(), &limits, out, 8,
                &written),
      GUNI_OK);

  EXPECT_EQ(guni_quick_check(static_cast<GUNI_NormForm>(99), 'a'), GUNI_QC_NO);
  EXPECT_EQ(guni_quick_check_text(GUNI_NFC, nullptr, 3), GUNI_QC_NO);
  EXPECT_EQ(guni_quick_check_utf8(GUNI_NFC, nullptr, 3), GUNI_QC_NO);
  EXPECT_EQ(guni_decompose('a', false, nullptr, 4, &written), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_decompose('a', false, out, 8, nullptr), GUNI_ERR_INVALID);
  bool normalised = false;
  EXPECT_EQ(guni_is_normalized(GUNI_NFC, text.data(), text.size(), nullptr,
                nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_is_normalized_utf8(GUNI_NFC, nullptr, 3, nullptr, &normalised),
      GUNI_ERR_INVALID);
  EXPECT_FALSE(guni_is_stream_safe(nullptr, 3, nullptr));
  EXPECT_EQ(guni_stream_safe(text.data(), text.size(), nullptr, nullptr, 4,
                &written),
      GUNI_ERR_INVALID);

  /* An empty input is empty output, in every form, and not an error. */
  size_t empty = 99;
  for (int form = 0; form < GUNI_NORM_FORM_COUNT; ++form) {
    ASSERT_EQ(guni_normalize(static_cast<GUNI_NormForm>(form), nullptr, 0,
                  nullptr, out, 8, &empty),
        GUNI_OK);
    EXPECT_EQ(empty, static_cast<size_t>(0));
  }
}


TEST(Normalize, TheChunkedPathHandlesTextLongerThanItsWindow) {
  /* guni_normalize_utf8() works between normalisation boundaries in a
   * 512-codepoint window, so text longer than that exercises a path nothing
   * else reaches: finding the last boundary in a full window and resuming
   * from it. Real text is nearly all boundaries, which is the point. */
  std::string text;
  for (int index = 0; index < 900; ++index) {
    text += "a\xCC\x81"; /* a + U+0301, which composes to U+00E1 */
  }
  std::string out(text.size() * 2, '\0');
  size_t written = 0;
  ASSERT_EQ(guni_normalize_utf8(GUNI_NFC, text.data(), text.size(),
                GUNI_INVALID_REFUSE, nullptr, out.data(), out.size(), &written),
      GUNI_OK);
  out.resize(written);
  std::string expected;
  for (int index = 0; index < 900; ++index) {
    expected += "\xC3\xA1"; /* U+00E1 */
  }
  EXPECT_EQ(out, expected);

  /* And the same text through the codepoint entry point, which has no window,
   * gives the same answer: the two paths are one algorithm. */
  std::vector<uint32_t> codepoints;
  for (int index = 0; index < 900; ++index) {
    codepoints.push_back('a');
    codepoints.push_back(0x0301);
  }
  Codepoints composed = normalize(GUNI_NFC, codepoints);
  EXPECT_EQ(composed.size(), static_cast<size_t>(900));
  EXPECT_EQ(composed[0], UINT32_C(0x00E1));

  /* is_normalized() over the same length takes its own chunked path. */
  bool normalised = true;
  ASSERT_EQ(guni_is_normalized(GUNI_NFC, codepoints.data(), codepoints.size(),
                nullptr, &normalised),
      GUNI_OK);
  EXPECT_FALSE(normalised);
  ASSERT_EQ(guni_is_normalized(GUNI_NFC, composed.data(), composed.size(),
                nullptr, &normalised),
      GUNI_OK);
  EXPECT_TRUE(normalised);
  std::string composed_utf8(composed.size() * 4, '\0');
  size_t bytes = 0;
  ASSERT_EQ(guni_utf8_from_codepoints(composed.data(), composed.size(),
                GUNI_INVALID_REFUSE, composed_utf8.data(), composed_utf8.size(),
                &bytes),
      GUNI_OK);
  composed_utf8.resize(bytes);
  ASSERT_EQ(guni_is_normalized_utf8(GUNI_NFC, composed_utf8.data(),
                composed_utf8.size(), nullptr, &normalised),
      GUNI_OK);
  EXPECT_TRUE(normalised);
}

TEST(Normalize, AMaybeCharacterThatComposesWithNothingIsNormalised) {
  /* A combining mark at the start of the text: the quick check says MAYBE,
   * because the mark could have composed with something, and the full check
   * has to run and find that it did not. */
  const Codepoints text = {0x0301, 'x'};
  EXPECT_EQ(guni_quick_check_text(GUNI_NFC, text.data(), text.size()),
      GUNI_QC_MAYBE);
  bool normalised = false;
  ASSERT_EQ(guni_is_normalized(GUNI_NFC, text.data(), text.size(), nullptr,
                &normalised),
      GUNI_OK);
  EXPECT_TRUE(normalised);
  const std::string utf8 = "\xCC\x81x";
  EXPECT_EQ(guni_quick_check_utf8(GUNI_NFC, utf8.data(), utf8.size()),
      GUNI_QC_MAYBE);
  ASSERT_EQ(guni_is_normalized_utf8(GUNI_NFC, utf8.data(), utf8.size(), nullptr,
                &normalised),
      GUNI_OK);
  EXPECT_TRUE(normalised);
}

TEST(Normalize, Utf8QuickCheckSeesDisorderedMarks) {
  /* The out-of-order rule is in both quick checks, and the UTF-8 one had no
   * test of its own: a class that decreases without an intervening starter is
   * not normalised whatever the quick-check properties say. */
  const std::string disordered = "a\xCC\x81\xCC\x96"; /* 230 then 220 */
  EXPECT_EQ(guni_quick_check_utf8(GUNI_NFD, disordered.data(),
                disordered.size()),
      GUNI_QC_NO);
  bool normalised = true;
  ASSERT_EQ(guni_is_normalized_utf8(GUNI_NFD, disordered.data(),
                disordered.size(), nullptr, &normalised),
      GUNI_OK);
  EXPECT_FALSE(normalised);
}

TEST(Normalize, ABoundaryFreeRunThatExpandsTooFarIsRefused) {
  /* The window is 512 codepoints in and 1,024 out, and the second bound is
   * reachable on its own: U+FDFA expands to 18 and its NFKD quick check is No,
   * so a run of them has no boundary to split at and 60 of them overflow the
   * output window. Refused, with guni_stream_safe() and the codepoint entry
   * point as the ways through - and the codepoint one has no window at all. */
  std::string text = "a";
  for (int index = 0; index < 60; ++index) {
    text += "\xEF\xB7\xBA"; /* U+FDFA */
  }
  std::vector<char> out(text.size() * 20);
  size_t written = 0;
  EXPECT_EQ(guni_normalize_utf8(GUNI_NFKD, text.data(), text.size(),
                GUNI_INVALID_REFUSE, nullptr, out.data(), out.size(), &written),
      GUNI_ERR_LIMIT);
  Codepoints codepoints;
  codepoints.push_back('a');
  for (int index = 0; index < 60; ++index) {
    codepoints.push_back(0xFDFA);
  }
  Codepoints decomposed = normalize(GUNI_NFKD, codepoints);
  EXPECT_EQ(decomposed.size(), static_cast<size_t>(1 + 60 * 18));
}

TEST(Normalize, Utf8ErrorPathsAndLimits) {
  const std::string text = "a\xCC\x81";
  char out[32];
  size_t written = 0;
  EXPECT_EQ(guni_normalize_utf8(GUNI_NFC, text.data(), text.size(),
                GUNI_INVALID_REFUSE, nullptr, out, sizeof(out), nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_normalize_utf8(GUNI_NFC, text.data(), text.size(),
                GUNI_INVALID_REFUSE, nullptr, nullptr, sizeof(out), &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_normalize_utf8(GUNI_NFC, nullptr, 3, GUNI_INVALID_REFUSE,
                nullptr, out, sizeof(out), &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_normalize_utf8(static_cast<GUNI_NormForm>(9), text.data(),
                text.size(), GUNI_INVALID_REFUSE, nullptr, out, sizeof(out),
                &written),
      GUNI_ERR_INVALID);
  GUNI_Limits limits;
  guni_limits_default(&limits);
  limits.max_text_bytes = 1;
  EXPECT_EQ(guni_normalize_utf8(GUNI_NFC, text.data(), text.size(),
                GUNI_INVALID_REFUSE, &limits, out, sizeof(out), &written),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(guni_is_normalized_utf8(GUNI_NFC, text.data(), text.size(), &limits,
                nullptr),
      GUNI_ERR_INVALID);
  bool normalised = false;
  EXPECT_EQ(guni_is_normalized_utf8(GUNI_NFC, text.data(), text.size(), &limits,
                &normalised),
      GUNI_ERR_LIMIT);
  const Codepoints empty;
  EXPECT_EQ(guni_is_normalized(GUNI_NFC, empty.data(), 0, &limits, &normalised),
      GUNI_OK);
  EXPECT_TRUE(normalised) << "empty text is normalised in every form";

  /* Every byte ill-formed and the policy is SKIP: the result is empty and not
   * an error, and the loop has to notice that it consumed everything. */
  const std::string rubbish = "\xFF\xFE\xFF";
  ASSERT_EQ(guni_normalize_utf8(GUNI_NFC, rubbish.data(), rubbish.size(),
                GUNI_INVALID_SKIP, nullptr, out, sizeof(out), &written),
      GUNI_OK);
  EXPECT_EQ(written, static_cast<size_t>(0));

  /* And guni_decompose reports the length it needs rather than truncating. */
  uint32_t one[1];
  size_t needed = 0;
  EXPECT_EQ(guni_decompose(0xFDFA, true, one, 1, &needed), GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, static_cast<size_t>(GUNI_NORM_MAX_EXPANSION_NFKD));
}


TEST(Normalize, IsNormalizedHonoursItsLimitsAndItsWindow) {
  /* The full check has the same two bounds the normaliser has, and reaching
   * them needs text the quick check cannot dismiss: a combining mark is
   * MAYBE for NFC, so a long run of them gets past the quick check and into
   * the chunked comparison. */
  Codepoints text;
  text.push_back('a');
  for (int index = 0; index < 600; ++index) {
    text.push_back(0x0301);
  }
  EXPECT_EQ(guni_quick_check_text(GUNI_NFC, text.data(), text.size()),
      GUNI_QC_MAYBE);
  bool normalised = true;
  GUNI_Limits limits;
  guni_limits_default(&limits);
  limits.max_text_bytes = 8;
  EXPECT_EQ(guni_is_normalized(GUNI_NFC, text.data(), text.size(), &limits,
                &normalised),
      GUNI_ERR_LIMIT);

  /* And over UTF-8, a run of more than the window with no normalisation
   * boundary in it is refused rather than answered from half the text. */
  std::string utf8;
  utf8 += 'a';
  for (int index = 0; index < 600; ++index) {
    utf8 += "\xCC\x81";
  }
  EXPECT_EQ(guni_quick_check_utf8(GUNI_NFC, utf8.data(), utf8.size()),
      GUNI_QC_MAYBE);
  EXPECT_EQ(guni_is_normalized_utf8(GUNI_NFC, utf8.data(), utf8.size(), nullptr,
                &normalised),
      GUNI_ERR_LIMIT);
  /* guni_is_normalized() has the same window, and for the same reason: it has
   * to normalise in order to compare, so it needs a working buffer where
   * guni_normalize() has the caller's. Both refuse, and both name
   * guni_stream_safe() as the way through. */
  EXPECT_EQ(guni_is_normalized(GUNI_NFC, text.data(), text.size(), nullptr,
                &normalised),
      GUNI_ERR_LIMIT);

  /* The normaliser itself has no window, because it writes into the caller's
   * buffer, and it answers this text. */
  Codepoints composed = normalize(GUNI_NFC, text);
  EXPECT_EQ(composed.size(), text.size() - 1)
      << "the first mark composed with the a";

  /* And the documented remedy works: the stream-safe transform inserts
   * U+034F, which is a starter whose NFC quick check is Yes - a normalisation
   * boundary - so the windowed entry points can take the text afterwards. */
  Codepoints safe(text.size() + 32);
  size_t written = 0;
  ASSERT_EQ(guni_stream_safe(text.data(), text.size(), nullptr, safe.data(),
                safe.size(), &written),
      GUNI_OK);
  safe.resize(written);
  ASSERT_EQ(guni_is_normalized(GUNI_NFC, safe.data(), safe.size(), nullptr,
                &normalised),
      GUNI_OK);
  EXPECT_FALSE(normalised);
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
