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
 * UTF-8, against the Unicode Standard's Table 3-7 transcribed independently.
 *
 * The table is written out again here as nine rows of byte ranges rather than
 * reusing the decoder's own view of it, because the decoder is what is on
 * trial: a test that asks the implementation what the rules are agrees with
 * the implementation by construction. The sweep over byte-range boundaries is
 * what makes the agreement mean something - every legal and illegal boundary
 * of every byte position, which is where a decoder's arms are wrong.
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

#include "test_helpers.h"

namespace {

/**
 * Table 3-7, "Well-Formed UTF-8 Byte Sequences", of the Unicode Standard.
 * Nine rows; the ranges of the second byte differ by row and that is the whole
 * content of the table. A decoder that accepts 80..BF as any second byte
 * accepts overlong forms, UTF-16 surrogates and codepoints above U+10FFFF -
 * three classic vulnerabilities that are one missing table.
 */
struct Row {
  unsigned lead_low, lead_high;
  unsigned b2_low, b2_high;
  unsigned b3_low, b3_high;
  unsigned b4_low, b4_high;
  int length;
};

const Row kTable37[] = {
  {0x00, 0x7F, 0, 0, 0, 0, 0, 0, 1},
  {0xC2, 0xDF, 0x80, 0xBF, 0, 0, 0, 0, 2},
  {0xE0, 0xE0, 0xA0, 0xBF, 0x80, 0xBF, 0, 0, 3},
  {0xE1, 0xEC, 0x80, 0xBF, 0x80, 0xBF, 0, 0, 3},
  {0xED, 0xED, 0x80, 0x9F, 0x80, 0xBF, 0, 0, 3},
  {0xEE, 0xEF, 0x80, 0xBF, 0x80, 0xBF, 0, 0, 3},
  {0xF0, 0xF0, 0x90, 0xBF, 0x80, 0xBF, 0x80, 0xBF, 4},
  {0xF1, 0xF3, 0x80, 0xBF, 0x80, 0xBF, 0x80, 0xBF, 4},
  {0xF4, 0xF4, 0x80, 0x8F, 0x80, 0xBF, 0x80, 0xBF, 4},
};

/** Is this byte sequence well formed, according to the transcribed table? */
bool table_says_well_formed(const unsigned char * bytes, size_t length,
    int * sequence_length) {
  for (const Row & row : kTable37) {
    if (bytes[0] < row.lead_low || bytes[0] > row.lead_high) {
      continue;
    }
    *sequence_length = row.length;
    if (length < static_cast<size_t>(row.length)) {
      return false;
    }
    if (row.length >= 2 && (bytes[1] < row.b2_low || bytes[1] > row.b2_high)) {
      return false;
    }
    if (row.length >= 3 && (bytes[2] < row.b3_low || bytes[2] > row.b3_high)) {
      return false;
    }
    if (row.length >= 4 && (bytes[3] < row.b4_low || bytes[3] > row.b4_high)) {
      return false;
    }
    return true;
  }
  *sequence_length = 0;
  return false;
}

/** Every boundary of every range in Table 3-7, and one either side of it. */
const unsigned kBoundaryBytes[] = {
  0x00, 0x01, 0x7F, 0x80, 0x81, 0x8F, 0x90, 0x9F, 0xA0, 0xA1, 0xBF, 0xC0,
  0xC1, 0xC2, 0xC3, 0xDF, 0xE0, 0xE1, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0,
  0xF1, 0xF3, 0xF4, 0xF5, 0xFE, 0xFF,
};

TEST(Utf8, AgreesWithTable37AtEveryByteRangeBoundary) {
  size_t checked = 0;
  size_t well_formed = 0;
  for (unsigned b0 : kBoundaryBytes) {
    for (unsigned b1 : kBoundaryBytes) {
      for (unsigned b2 : kBoundaryBytes) {
        for (unsigned b3 : kBoundaryBytes) {
          unsigned char bytes[4] = {
            static_cast<unsigned char>(b0), static_cast<unsigned char>(b1),
            static_cast<unsigned char>(b2), static_cast<unsigned char>(b3)};
          int expected_length = 0;
          bool expected =
              table_says_well_formed(bytes, 4, &expected_length);
          uint32_t cp = 0;
          bool valid = false;
          size_t used = guni_utf8_decode(reinterpret_cast<const char *>(bytes),
              4, &cp, &valid);
          ASSERT_EQ(valid, expected)
              << "bytes " << std::hex << b0 << " " << b1 << " " << b2 << " "
              << b3;
          ASSERT_GE(used, static_cast<size_t>(1));
          if (expected) {
            ASSERT_EQ(used, static_cast<size_t>(expected_length));
            /* A well-formed sequence decodes to a codepoint, never to a
             * surrogate and never above U+10FFFF: the table's second-byte
             * ranges are exactly what excludes those. */
            EXPECT_TRUE(guni_is_codepoint(cp)) << std::hex << cp;
            EXPECT_EQ(guni_utf8_length(cp), used);
            ++well_formed;
          }
          else {
            EXPECT_EQ(cp, GUNI_REPLACEMENT_CHARACTER);
          }
          ++checked;
        }
      }
    }
  }
  /* The denominator, so that a loop that stopped early cannot read as a
   * clean sweep. */
  EXPECT_EQ(checked, sizeof(kBoundaryBytes) / sizeof(*kBoundaryBytes)
      * sizeof(kBoundaryBytes) / sizeof(*kBoundaryBytes)
      * sizeof(kBoundaryBytes) / sizeof(*kBoundaryBytes)
      * sizeof(kBoundaryBytes) / sizeof(*kBoundaryBytes));
  EXPECT_GT(well_formed, static_cast<size_t>(1000));
}

TEST(Utf8, EveryOneAndTwoByteSequenceAgreesWithTable37) {
  for (unsigned b0 = 0; b0 <= 0xFF; ++b0) {
    for (unsigned b1 = 0; b1 <= 0xFF; ++b1) {
      unsigned char bytes[2] = {static_cast<unsigned char>(b0),
          static_cast<unsigned char>(b1)};
      int expected_length = 0;
      bool expected = table_says_well_formed(bytes, 2, &expected_length);
      /* Only rows of length 1 and 2 can be well formed in two bytes. */
      if (expected_length > 2) {
        expected = false;
      }
      uint32_t cp = 0;
      bool valid = false;
      guni_utf8_decode(reinterpret_cast<const char *>(bytes), 2, &cp, &valid);
      ASSERT_EQ(valid, expected) << std::hex << b0 << " " << b1;
    }
  }
}

TEST(Utf8, EveryCodepointRoundTrips) {
  size_t count = 0;
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    if (guni_is_surrogate(cp)) {
      EXPECT_EQ(guni_utf8_length(cp), static_cast<size_t>(0));
      char buffer[GUNI_UTF8_MAX_LENGTH];
      EXPECT_EQ(guni_utf8_encode(cp, buffer), static_cast<size_t>(0));
      continue;
    }
    char buffer[GUNI_UTF8_MAX_LENGTH];
    size_t written = guni_utf8_encode(cp, buffer);
    ASSERT_GE(written, static_cast<size_t>(1));
    ASSERT_LE(written, static_cast<size_t>(GUNI_UTF8_MAX_LENGTH));
    uint32_t decoded = 0;
    bool valid = false;
    ASSERT_EQ(guni_utf8_decode(buffer, written, &decoded, &valid), written);
    ASSERT_TRUE(valid);
    ASSERT_EQ(decoded, cp);
    /* And stepping backwards from the end lands on the start. */
    EXPECT_EQ(guni_utf8_prev(buffer, written), static_cast<size_t>(0));
    ++count;
  }
  EXPECT_EQ(count, GUNI_CODEPOINT_COUNT - 2048);
}

TEST(Utf8, SurrogatesAndOverlongFormsAreNotDecoded) {
  /* The three vulnerabilities Table 3-7's second-byte ranges exist to close,
   * each spelled as the bytes a naive decoder would accept. */
  const char * const cases[] = {
    "\xC0\xAF",         /* overlong "/" */
    "\xC1\xBF",         /* overlong U+7F */
    "\xE0\x80\xAF",     /* overlong "/" in three bytes */
    "\xF0\x80\x80\xAF", /* overlong "/" in four bytes */
    "\xED\xA0\x80",     /* U+D800, a surrogate */
    "\xED\xBF\xBF",     /* U+DFFF */
    "\xF4\x90\x80\x80", /* U+110000, past the last codepoint */
    "\xF5\x80\x80\x80", /* leader for plane 17 */
    "\xFE",
    "\xFF",
  };
  for (const char * bytes : cases) {
    size_t offset = 0;
    EXPECT_EQ(guni_utf8_validate(bytes, std::strlen(bytes), &offset),
        GUNI_ERR_INVALID)
        << "accepted " << bytes;
    EXPECT_EQ(offset, static_cast<size_t>(0));
  }
}

TEST(Utf8, ReplacementIsOnePerMaximalSubpart) {
  /* The Standard's recommended practice, section 3.9. Each expectation is
   * derived from Table 3-7 rather than remembered: the subpart is the longest
   * prefix that could still become well formed, so a truncated sequence is
   * one error and a sequence whose second byte is out of its row's range is
   * an error of one byte followed by errors for the strays. */
  struct Case {
    const char * bytes;
    size_t length;
    std::vector<uint32_t> expected;
  };
  const uint32_t rc = GUNI_REPLACEMENT_CHARACTER;
  const std::vector<Case> cases = {
    /* Truncated three-byte sequence, then an ASCII letter: the two bytes are
     * one subpart, so one U+FFFD and not two. */
    {"\xE1\x80" "A", 3, {rc, 'A'}},
    /* Truncated four-byte sequence: three bytes, one subpart. */
    {"\xF0\x9F\x98" "A", 4, {rc, 'A'}},
    /* C0 is no row's lead byte, so each byte stands alone. */
    {"\xC0\xAF", 2, {rc, rc}},
    /* ED's row allows 80..9F; A0 is outside it, so "ED" is the subpart and
     * the two trailing bytes are two more errors. */
    {"\xED\xA0\x80", 3, {rc, rc, rc}},
    /* F0's row allows 90..BF. */
    {"\xF0\x80\x80\x80", 4, {rc, rc, rc, rc}},
    /* F4's row allows 80..8F. */
    {"\xF4\x90\x80\x80", 4, {rc, rc, rc, rc}},
    /* A valid sequence either side of an invalid one. */
    {"A\xC2\xA9\xFF" "B", 5, {'A', 0xA9, rc, 'B'}},
  };
  for (const Case & test : cases) {
    uint32_t out[8];
    size_t written = 0;
    ASSERT_EQ(guni_utf8_to_codepoints(test.bytes, test.length,
                  GUNI_INVALID_REPLACE, out, 8, &written),
        GUNI_OK);
    std::vector<uint32_t> got(out, out + written);
    EXPECT_EQ(got, test.expected) << "for " << test.bytes;
    /* The same input under REFUSE stops, and under SKIP drops. */
    size_t refused = 0;
    EXPECT_EQ(guni_utf8_to_codepoints(test.bytes, test.length,
                  GUNI_INVALID_REFUSE, out, 8, &refused),
        GUNI_ERR_INVALID);
    size_t skipped = 0;
    ASSERT_EQ(guni_utf8_to_codepoints(test.bytes, test.length,
                  GUNI_INVALID_SKIP, out, 8, &skipped),
        GUNI_OK);
    size_t errors = 0;
    for (uint32_t cp : test.expected) {
      if (cp == rc) {
        ++errors;
      }
    }
    EXPECT_EQ(skipped, test.expected.size() - errors);
  }
}

TEST(Utf8, ZeroRefuses) {
  /* GUNI_INVALID_REFUSE is 0, so a caller who zero-initialises a policy gets
   * the strict behaviour rather than silent replacement (design.md 4.3). */
  GUNI_Invalid policy = static_cast<GUNI_Invalid>(0);
  EXPECT_EQ(policy, GUNI_INVALID_REFUSE);
  size_t count = 0;
  EXPECT_EQ(guni_utf8_count("\xFF", 1, policy, &count), GUNI_ERR_INVALID);
}

TEST(Utf8, CountsAgreeWithTheDecoder) {
  const std::string text = "AΩ日\xF0\x9F\x98\x80" "z";
  size_t count = 0;
  ASSERT_EQ(guni_utf8_count(text.data(), text.size(), GUNI_INVALID_REFUSE,
                &count),
      GUNI_OK);
  EXPECT_EQ(count, static_cast<size_t>(5));
  uint32_t buffer[5];
  size_t written = 0;
  ASSERT_EQ(guni_utf8_to_codepoints(text.data(), text.size(),
                GUNI_INVALID_REFUSE, buffer, 5, &written),
      GUNI_OK);
  EXPECT_EQ(written, count);
  EXPECT_EQ(buffer[0], static_cast<uint32_t>('A'));
  EXPECT_EQ(buffer[1], UINT32_C(0x03A9));
  EXPECT_EQ(buffer[2], UINT32_C(0x65E5));
  EXPECT_EQ(buffer[3], UINT32_C(0x1F600));
}

TEST(Utf8, ReportsTheLengthItNeedsWhenTheBufferIsTooSmall) {
  /* The output contract of design.md 4.5: too small is GUNI_ERR_LIMIT with
   * out_len set to the requirement, so a caller preflights with cap 0. */
  const std::string text = "Ωα";
  size_t needed = 0;
  ASSERT_EQ(guni_utf8_to_codepoints(text.data(), text.size(),
                GUNI_INVALID_REFUSE, nullptr, 0, &needed),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, static_cast<size_t>(2));
  uint32_t one[1];
  size_t again = 0;
  EXPECT_EQ(guni_utf8_to_codepoints(text.data(), text.size(),
                GUNI_INVALID_REFUSE, one, 1, &again),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(again, static_cast<size_t>(2));

  const uint32_t codepoints[] = {0x1F600, 'x'};
  size_t bytes = 0;
  ASSERT_EQ(guni_utf8_from_codepoints(codepoints, 2, GUNI_INVALID_REFUSE,
                nullptr, 0, &bytes),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(bytes, static_cast<size_t>(5));
  char small[4];
  size_t wrote = 0;
  EXPECT_EQ(guni_utf8_from_codepoints(codepoints, 2, GUNI_INVALID_REFUSE,
                small, sizeof(small), &wrote),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(wrote, static_cast<size_t>(5));
}

TEST(Utf8, EncodingRefusesWhatIsNotACodepoint) {
  const uint32_t bad[] = {0xD800, 0x110000, 0xFFFFFFFF};
  for (uint32_t cp : bad) {
    char buffer[8];
    size_t written = 0;
    EXPECT_EQ(guni_utf8_from_codepoints(&cp, 1, GUNI_INVALID_REFUSE, buffer,
                  sizeof(buffer), &written),
        GUNI_ERR_INVALID);
    ASSERT_EQ(guni_utf8_from_codepoints(&cp, 1, GUNI_INVALID_REPLACE, buffer,
                  sizeof(buffer), &written),
        GUNI_OK);
    EXPECT_EQ(written, static_cast<size_t>(3));
    EXPECT_EQ(std::string(buffer, written), std::string("\xEF\xBF\xBD"));
    ASSERT_EQ(guni_utf8_from_codepoints(&cp, 1, GUNI_INVALID_SKIP, buffer,
                  sizeof(buffer), &written),
        GUNI_OK);
    EXPECT_EQ(written, static_cast<size_t>(0));
  }
}

TEST(Utf8, IteratorWalksAndReportsOffsets) {
  const std::string text = "aΩ\xF0\x9F\x98\x80";
  GUNI_Utf8Iter iter;
  guni_utf8_iter_init(&iter, text.data(), text.size(), GUNI_INVALID_REFUSE);
  uint32_t cp = 0;
  size_t offset = 0;
  GUNI_Result result = GUNI_OK;
  ASSERT_TRUE(guni_utf8_iter_next(&iter, &cp, &offset, &result));
  EXPECT_EQ(cp, static_cast<uint32_t>('a'));
  EXPECT_EQ(offset, static_cast<size_t>(0));
  ASSERT_TRUE(guni_utf8_iter_next(&iter, &cp, &offset, &result));
  EXPECT_EQ(cp, UINT32_C(0x03A9));
  EXPECT_EQ(offset, static_cast<size_t>(1));
  ASSERT_TRUE(guni_utf8_iter_next(&iter, &cp, &offset, &result));
  EXPECT_EQ(cp, UINT32_C(0x1F600));
  EXPECT_EQ(offset, static_cast<size_t>(3));
  EXPECT_FALSE(guni_utf8_iter_next(&iter, &cp, &offset, &result));
  EXPECT_EQ(result, GUNI_OK);

  /* Under REFUSE the iterator stops at the offset of the bad byte, which is
   * the diagnostic a caller needs. */
  const std::string broken = "ab\xE1zz";
  guni_utf8_iter_init(&iter, broken.data(), broken.size(), GUNI_INVALID_REFUSE);
  ASSERT_TRUE(guni_utf8_iter_next(&iter, &cp, &offset, &result));
  ASSERT_TRUE(guni_utf8_iter_next(&iter, &cp, &offset, &result));
  EXPECT_FALSE(guni_utf8_iter_next(&iter, &cp, &offset, &result));
  EXPECT_EQ(result, GUNI_ERR_INVALID);
  EXPECT_EQ(offset, static_cast<size_t>(2));
}

TEST(Utf8, SteppingBackwardsTerminatesOnAnyBytes) {
  /* A run of continuation bytes must not walk past the start of the buffer,
   * and an ill-formed byte must still make progress: a caller's loop over
   * guni_utf8_prev has to terminate whatever the bytes are. */
  const std::string junk = "\x80\x80\x80\x80\x80\x80";
  size_t position = junk.size();
  size_t steps = 0;
  while (position > 0) {
    size_t next = guni_utf8_prev(junk.data(), position);
    ASSERT_LT(next, position);
    position = next;
    ASSERT_LT(++steps, junk.size() + 1);
  }
  EXPECT_EQ(steps, junk.size());

  /* And on well-formed text it lands on codepoint starts. */
  const std::string text = "aΩ\xF0\x9F\x98\x80z";
  std::vector<size_t> starts;
  position = text.size();
  while (position > 0) {
    position = guni_utf8_prev(text.data(), position);
    starts.push_back(position);
  }
  EXPECT_EQ(starts, std::vector<size_t>({7, 3, 1, 0}));
}

TEST(Utf8, NoncharactersAreValidAndSurrogatesAreNot) {
  /* Noncharacters are codepoints: a library that refused them would refuse
   * text that is legal and in use internally. */
  EXPECT_TRUE(guni_is_noncharacter(0xFFFE));
  EXPECT_TRUE(guni_is_noncharacter(0xFFFF));
  EXPECT_TRUE(guni_is_noncharacter(0xFDD0));
  EXPECT_TRUE(guni_is_noncharacter(0xFDEF));
  EXPECT_TRUE(guni_is_noncharacter(0x10FFFE));
  EXPECT_FALSE(guni_is_noncharacter(0xFDF0));
  EXPECT_FALSE(guni_is_noncharacter(0x110000));
  size_t count = 0;
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    if (guni_is_noncharacter(cp)) {
      ++count;
      EXPECT_TRUE(guni_is_codepoint(cp));
      char buffer[GUNI_UTF8_MAX_LENGTH];
      EXPECT_GT(guni_utf8_encode(cp, buffer), static_cast<size_t>(0));
    }
  }
  EXPECT_EQ(count, static_cast<size_t>(66));
}

TEST(Utf8, EmptyAndNullInputsAreDefined) {
  uint32_t cp = 1;
  bool valid = true;
  EXPECT_EQ(guni_utf8_decode(nullptr, 0, &cp, &valid), static_cast<size_t>(0));
  EXPECT_FALSE(valid);
  EXPECT_EQ(guni_utf8_decode("x", 0, &cp, &valid), static_cast<size_t>(0));
  EXPECT_EQ(guni_utf8_validate(nullptr, 0, nullptr), GUNI_OK);
  size_t count = 1;
  EXPECT_EQ(guni_utf8_count(nullptr, 0, GUNI_INVALID_REFUSE, &count), GUNI_OK);
  EXPECT_EQ(count, static_cast<size_t>(0));
  EXPECT_EQ(guni_utf8_count("x", 1, GUNI_INVALID_REFUSE, nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_utf8_prev(nullptr, 5), static_cast<size_t>(0));
  EXPECT_EQ(guni_utf8_prev("abc", 0), static_cast<size_t>(0));
  EXPECT_EQ(guni_utf8_encode('a', nullptr), static_cast<size_t>(0));
  guni_utf8_iter_init(nullptr, "x", 1, GUNI_INVALID_REFUSE);
  GUNI_Utf8Iter iter;
  guni_utf8_iter_init(&iter, nullptr, 7, GUNI_INVALID_REFUSE);
  EXPECT_EQ(iter.length, static_cast<size_t>(0));
  GUNI_Result result = GUNI_OK;
  EXPECT_FALSE(guni_utf8_iter_next(&iter, nullptr, nullptr, &result));
  EXPECT_EQ(result, GUNI_ERR_INVALID);
}


TEST(Utf8, ValidateWalksTheWholeBuffer) {
  /* Every earlier test hands validate() either empty input or bytes whose
   * first byte is already wrong, so the loop never advanced and the advance
   * was untested. Coverage named the line. */
  const std::string text = "aΩ日\xF0\x9F\x98\x80z";
  size_t offset = 999;
  EXPECT_EQ(guni_utf8_validate(text.data(), text.size(), &offset), GUNI_OK);
  EXPECT_EQ(offset, static_cast<size_t>(999)) << "untouched on success";
  /* A bad byte after several good ones is reported at its own offset. */
  const std::string late = "abcΩ\xFF";
  ASSERT_EQ(guni_utf8_validate(late.data(), late.size(), &offset),
      GUNI_ERR_INVALID);
  EXPECT_EQ(offset, static_cast<size_t>(5));
  EXPECT_EQ(guni_utf8_validate(late.data(), late.size(), nullptr),
      GUNI_ERR_INVALID);
}

TEST(Utf8, CountUnderEveryPolicy) {
  const std::string broken = "a\xC0\xAF" "b";
  size_t count = 0;
  EXPECT_EQ(guni_utf8_count(broken.data(), broken.size(), GUNI_INVALID_REFUSE,
                &count),
      GUNI_ERR_INVALID);
  ASSERT_EQ(guni_utf8_count(broken.data(), broken.size(), GUNI_INVALID_REPLACE,
                &count),
      GUNI_OK);
  EXPECT_EQ(count, static_cast<size_t>(4)) << "one U+FFFD per maximal subpart";
  ASSERT_EQ(guni_utf8_count(broken.data(), broken.size(), GUNI_INVALID_SKIP,
                &count),
      GUNI_OK);
  EXPECT_EQ(count, static_cast<size_t>(2)) << "the two good bytes";
}

TEST(Utf8, IteratorSkipsWhenAskedTo) {
  const std::string broken = "a\xFF" "b";
  GUNI_Utf8Iter iter;
  guni_utf8_iter_init(&iter, broken.data(), broken.size(), GUNI_INVALID_SKIP);
  uint32_t cp = 0;
  size_t offset = 0;
  std::vector<uint32_t> seen;
  std::vector<size_t> offsets;
  while (guni_utf8_iter_next(&iter, &cp, &offset, nullptr)) {
    seen.push_back(cp);
    offsets.push_back(offset);
  }
  EXPECT_EQ(seen, std::vector<uint32_t>({'a', 'b'}));
  /* The offsets are still into the input, which is why SKIP is documented as
   * being for diagnostics: the text it describes is not the text it read. */
  EXPECT_EQ(offsets, std::vector<size_t>({0, 2}));

  guni_utf8_iter_init(&iter, broken.data(), broken.size(), GUNI_INVALID_REPLACE);
  seen.clear();
  while (guni_utf8_iter_next(&iter, &cp, nullptr, nullptr)) {
    seen.push_back(cp);
  }
  EXPECT_EQ(seen,
      std::vector<uint32_t>({'a', GUNI_REPLACEMENT_CHARACTER, 'b'}));
}

TEST(Utf8, ConversionRefusesAnUnusableBuffer) {
  uint32_t out[4];
  size_t written = 0;
  /* A null buffer with a non-zero capacity is a caller bug, not an empty
   * buffer: the two are told apart rather than one being read as the other. */
  EXPECT_EQ(guni_utf8_to_codepoints("a", 1, GUNI_INVALID_REFUSE, nullptr, 4,
                &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_utf8_to_codepoints("a", 1, GUNI_INVALID_REFUSE, out, 4,
                nullptr),
      GUNI_ERR_INVALID);
  const uint32_t codepoints[] = {'a'};
  char bytes[4];
  size_t length = 0;
  EXPECT_EQ(guni_utf8_from_codepoints(codepoints, 1, GUNI_INVALID_REFUSE,
                nullptr, 4, &length),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_utf8_from_codepoints(codepoints, 1, GUNI_INVALID_REFUSE, bytes,
                4, nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_utf8_from_codepoints(nullptr, 3, GUNI_INVALID_REFUSE, bytes, 4,
                &length),
      GUNI_ERR_INVALID);
  /* But a null input with a zero length is empty, and succeeds. */
  EXPECT_EQ(guni_utf8_from_codepoints(nullptr, 0, GUNI_INVALID_REFUSE, bytes, 4,
                &length),
      GUNI_OK);
  EXPECT_EQ(length, static_cast<size_t>(0));
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
