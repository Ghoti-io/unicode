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
 * UAX #29 and UAX #14 against the Consortium's four files.
 *
 * Each line of each file is a string of codepoints with a break marker
 * between every pair - U+00F7 for a break, U+00D7 for none - so a line is not
 * one assertion but one per position, and the whole file is every rule
 * interaction the Consortium could think of.
 *
 * Every line is checked **four ways**: the point query and the iterator, over
 * UTF-8 and over codepoints. Those are two entry points and two encodings of
 * one rule engine, and the ways they can disagree - an offset computed in
 * bytes where the rules wanted characters, an iterator that skips a boundary
 * it has already passed - are not things a rule citation can catch.
 *
 * LineBreakTest.txt is the **default** algorithm, which resolves `CJ` as `NS`,
 * so it is checked with GUNI_LINE_BREAK_STRICT. That the other two tailorings
 * differ from it, and where, is tests/unit/test_segment.cpp's business: no
 * conformance file covers them, because the choice is the implementation's.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

#include "test_helpers.h"

namespace {

/** One line: the codepoints, and whether a break falls before each of them. */
struct Case {
  int line = 0;
  std::vector<uint32_t> codepoints;
  /** breaks[i] is the expectation at the position before codepoints[i]; the
   * last entry is the expectation at the end of the text. */
  std::vector<bool> breaks;
};

/**
 * Parse one line of a break test file.
 *
 * The markers are U+00F7 and U+00D7, which are two bytes each in the file's
 * UTF-8; matching on the bytes rather than decoding keeps the parser to a
 * handful of lines and cannot mistake a hex digit for a marker.
 */
bool parse_case(const std::string & raw, int number, Case * out) {
  std::string line = raw;
  size_t hash = line.find('#');
  if (hash != std::string::npos) {
    line.erase(hash);
  }
  out->line = number;
  out->codepoints.clear();
  out->breaks.clear();
  size_t index = 0;
  bool have_marker = false;
  while (index < line.size()) {
    unsigned char byte = (unsigned char)line[index];
    if (byte == 0xC3 && index + 1 < line.size()) {
      unsigned char second = (unsigned char)line[index + 1];
      if (second == 0xB7) { /* U+00F7 DIVISION SIGN: a break */
        out->breaks.push_back(true);
        have_marker = true;
        index += 2;
        continue;
      }
      if (second == 0x97) { /* U+00D7 MULTIPLICATION SIGN: no break */
        out->breaks.push_back(false);
        have_marker = true;
        index += 2;
        continue;
      }
    }
    if (std::isxdigit(byte)) {
      size_t start = index;
      while (index < line.size() && std::isxdigit((unsigned char)line[index])) {
        ++index;
      }
      out->codepoints.push_back(static_cast<uint32_t>(
          std::strtoul(line.substr(start, index - start).c_str(), nullptr, 16)));
      continue;
    }
    ++index;
  }
  /* A well-formed line has one marker more than it has codepoints. */
  return have_marker && out->breaks.size() == out->codepoints.size() + 1;
}

std::vector<Case> load(const std::string & name) {
  std::vector<Case> out;
  const std::string path =
      gunitest::data(std::string("ucd/") + guni_ucd_version() + "/" + name);
  FILE * handle = std::fopen(path.c_str(), "rb");
  if (handle == nullptr) {
    return out;
  }
  char buffer[65536];
  int number = 0;
  Case parsed;
  while (std::fgets(buffer, sizeof(buffer), handle) != nullptr) {
    ++number;
    if (parse_case(buffer, number, &parsed)) {
      out.push_back(parsed);
    }
  }
  std::fclose(handle);
  return out;
}

std::string to_utf8(const std::vector<uint32_t> & codepoints,
    std::vector<size_t> * offsets) {
  std::string out;
  offsets->clear();
  char bytes[GUNI_UTF8_MAX_LENGTH];
  for (uint32_t cp : codepoints) {
    offsets->push_back(out.size());
    size_t used = guni_utf8_encode(cp, bytes);
    out.append(bytes, used);
  }
  offsets->push_back(out.size());
  return out;
}

std::string describe(const Case & test) {
  std::string out;
  char buffer[16];
  for (uint32_t cp : test.codepoints) {
    std::snprintf(buffer, sizeof(buffer), "%04X ", cp);
    out += buffer;
  }
  return out;
}

/**
 * Check every position of every case, four ways.
 *
 * @param failures Incremented per failing position, and reported as a total:
 *        a run that printed one line per failure would print thousands.
 */
void check(const std::vector<Case> & cases, GUNI_BreakKind kind,
    const char * what, size_t expected_at_least) {
  ASSERT_FALSE(cases.empty()) << what << " parsed no cases; the file is "
                                 "committed and cannot be absent";
  EXPECT_GE(cases.size(), expected_at_least)
      << what << ": only " << cases.size() << " cases parsed";

  GUNI_BreakOptions options;
  std::memset(&options, 0, sizeof(options));
  options.kind = kind;

  size_t positions = 0;
  size_t failures = 0;
  size_t reported = 0;
  for (const Case & test : cases) {
    std::vector<size_t> offsets;
    const std::string utf8 = to_utf8(test.codepoints, &offsets);

    /* The point query, both encodings. */
    for (size_t index = 0; index < test.breaks.size(); ++index) {
      const bool want = test.breaks[index];
      /* The file marks a break at offset 0 and at the end; UAX #14 does not
       * break at the start, and the file's leading marker for a line-break
       * case is a break only because the file writes one there. Both
       * standards agree about the end. */
      const bool at_start = (index == 0);
      if (at_start && kind == GUNI_BREAK_LINE) {
        continue;
      }
      const bool bytes = guni_break_at(&options, utf8.data(), utf8.size(),
          offsets[index]);
      const bool points = guni_break_at_codepoints(&options,
          test.codepoints.data(), test.codepoints.size(), index);
      if (bytes != want || points != want) {
        ++failures;
        if (reported < 10) {
          ++reported;
          ADD_FAILURE() << what << " line " << test.line << " position "
                        << index << " of " << describe(test)
                        << "\n  expected " << (want ? "break" : "no break")
                        << ", got utf8=" << (bytes ? "break" : "no break")
                        << " codepoints=" << (points ? "break" : "no break");
        }
      }
      ++positions;
    }

    /* The iterator, both encodings, against the same expectations. */
    for (int encoding = 0; encoding < 2; ++encoding) {
      GUNI_BreakIter iter;
      if (encoding == 0) {
        guni_break_iter_init(&iter, &options, utf8.data(), utf8.size());
      }
      else {
        guni_break_iter_init_codepoints(&iter, &options,
            test.codepoints.data(), test.codepoints.size());
      }
      std::vector<bool> seen(test.breaks.size(), false);
      size_t position = 0;
      size_t previous = 0;
      bool first = true;
      while (guni_break_iter_next(&iter, &position)) {
        if (!first) {
          ASSERT_GT(position, previous)
              << what << " line " << test.line
              << ": the iterator went backwards";
        }
        first = false;
        previous = position;
        size_t index = 0;
        if (encoding == 0) {
          while (index < offsets.size() && offsets[index] != position) {
            ++index;
          }
          ASSERT_LT(index, offsets.size())
              << what << " line " << test.line << ": the iterator stopped at "
              << position << ", which is not a character boundary";
        }
        else {
          index = position;
        }
        seen[index] = true;
      }
      for (size_t index = 0; index < test.breaks.size(); ++index) {
        if (index == 0 && kind == GUNI_BREAK_LINE) {
          continue;
        }
        if (seen[index] != test.breaks[index]) {
          ++failures;
          if (reported < 10) {
            ++reported;
            ADD_FAILURE() << what << " line " << test.line
                          << ": the iterator " << (seen[index] ? "" : "did not ")
                          << "report position " << index << " of "
                          << describe(test) << " and the file says "
                          << (test.breaks[index] ? "break" : "no break");
          }
        }
      }
    }
  }
  EXPECT_EQ(failures, static_cast<size_t>(0))
      << failures << " failures over " << positions << " positions in "
      << cases.size() << " cases";
  /* The denominator, reported: a parser that read the file as comments would
   * otherwise pass by checking nothing. */
  EXPECT_GT(positions, expected_at_least)
      << what << ": only " << positions << " positions checked";
}

TEST(Break, GraphemeClusters) {
  check(load("GraphemeBreakTest.txt"), GUNI_BREAK_GRAPHEME,
      "GraphemeBreakTest.txt", 600);
}

TEST(Break, Words) {
  check(load("WordBreakTest.txt"), GUNI_BREAK_WORD, "WordBreakTest.txt", 1800);
}

TEST(Break, Sentences) {
  check(load("SentenceBreakTest.txt"), GUNI_BREAK_SENTENCE,
      "SentenceBreakTest.txt", 400);
}

TEST(Break, Lines) {
  check(load("LineBreakTest.txt"), GUNI_BREAK_LINE, "LineBreakTest.txt", 8000);
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
