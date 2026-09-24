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
 * The segmentation API's contract, and the axis no conformance file covers.
 *
 * LineBreakTest.txt tests the **default** algorithm, which is one of the three
 * resolutions UAX #14's rule LB1 permits for `CJ`. The other two are why this
 * library exists rather than `regex`'s tables being installed (design.md
 * section 2, M2), and nothing in the Consortium's data can check them: the
 * choice is the implementation's. So they are checked here, two ways - by the
 * case that CSS's `line-break` property is about, and by a pairwise sweep over
 * every ordered pair of Line_Break classes that records where the three
 * tailorings differ.
 *
 * Set GUNI_BREAK_DUMP=pairs to regenerate that sweep's fixture.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

#include "test_helpers.h"

namespace {

GUNI_BreakOptions options_for(GUNI_BreakKind kind,
    GUNI_LineBreakTailoring tailoring = GUNI_LINE_BREAK_STRICT,
    const GUNI_BreakProvider * provider = nullptr) {
  GUNI_BreakOptions options;
  std::memset(&options, 0, sizeof(options));
  options.kind = kind;
  options.tailoring = tailoring;
  options.provider = provider;
  return options;
}

std::vector<size_t> boundaries(const GUNI_BreakOptions & options,
    const std::vector<uint32_t> & text) {
  size_t needed = 0;
  GUNI_Result first = guni_break_all_codepoints(&options, text.data(),
      text.size(), nullptr, 0, &needed);
  EXPECT_TRUE(first == GUNI_OK || first == GUNI_ERR_LIMIT);
  std::vector<size_t> out(needed);
  size_t written = 0;
  EXPECT_EQ(guni_break_all_codepoints(&options, text.data(), text.size(),
                out.data(), out.size(), &written),
      GUNI_OK);
  EXPECT_EQ(written, needed);
  return out;
}

TEST(Segment, TheThreeLineBreakTailoringsDiffer) {
  /* This is M2, as one case. U+30FC KATAKANA-HIRAGANA PROLONGED SOUND MARK has
   * Line_Break class CJ, and LB1 resolves CJ to NS or ID at the
   * implementation's choice: NS keeps it with the character before it, ID lets
   * a line break there. CSS calls those `strict` and `normal`. A library that
   * resolved CJ while generating its tables - which is what regex does - can
   * only offer one of them. */
  const std::vector<uint32_t> kana = {0x30A2, 0x30FC, 0x30A2}; /* A, prolong, A */
  EXPECT_EQ(guni_line_break(0x30FC), GUNI_LB_CJ) << "the table is unresolved";

  const GUNI_BreakOptions strict =
      options_for(GUNI_BREAK_LINE, GUNI_LINE_BREAK_STRICT);
  const GUNI_BreakOptions normal =
      options_for(GUNI_BREAK_LINE, GUNI_LINE_BREAK_NORMAL);
  const GUNI_BreakOptions loose =
      options_for(GUNI_BREAK_LINE, GUNI_LINE_BREAK_LOOSE);

  EXPECT_FALSE(guni_break_at_codepoints(&strict, kana.data(), kana.size(), 1))
      << "strict keeps the prolonged sound mark with its character";
  EXPECT_TRUE(guni_break_at_codepoints(&normal, kana.data(), kana.size(), 1))
      << "normal allows the break";
  EXPECT_TRUE(guni_break_at_codepoints(&loose, kana.data(), kana.size(), 1));

  /* And the resolution function says the same thing on its own, which is what
   * regex will call when it applies LB1 itself. */
  EXPECT_EQ(guni_line_break_resolve(GUNI_LB_CJ, GUNI_GC_LM,
                GUNI_LINE_BREAK_STRICT),
      GUNI_LB_NS);
  EXPECT_EQ(guni_line_break_resolve(GUNI_LB_CJ, GUNI_GC_LM,
                GUNI_LINE_BREAK_NORMAL),
      GUNI_LB_ID);
  EXPECT_EQ(guni_line_break_resolve(GUNI_LB_CJ, GUNI_GC_LM,
                GUNI_LINE_BREAK_LOOSE),
      GUNI_LB_ID);
  /* Zero is strict, so a caller who does not choose gets regex's behaviour
   * and the Standard's worked example. */
  EXPECT_EQ(static_cast<GUNI_LineBreakTailoring>(0), GUNI_LINE_BREAK_STRICT);
}

TEST(Segment, Lb1ResolvesTheOtherFourClassesTheSameWayForEveryone) {
  /* AI, SG and XX become AL whatever the tailoring; SA becomes CM for a
   * combining mark and AL otherwise. Only CJ has a choice. */
  for (int which = 0; which < 3; ++which) {
    GUNI_LineBreakTailoring tailoring =
        static_cast<GUNI_LineBreakTailoring>(which);
    EXPECT_EQ(guni_line_break_resolve(GUNI_LB_AI, GUNI_GC_NO, tailoring),
        GUNI_LB_AL);
    EXPECT_EQ(guni_line_break_resolve(GUNI_LB_SG, GUNI_GC_CS, tailoring),
        GUNI_LB_AL);
    EXPECT_EQ(guni_line_break_resolve(GUNI_LB_XX, GUNI_GC_CN, tailoring),
        GUNI_LB_AL);
    EXPECT_EQ(guni_line_break_resolve(GUNI_LB_SA, GUNI_GC_LO, tailoring),
        GUNI_LB_AL);
    EXPECT_EQ(guni_line_break_resolve(GUNI_LB_SA, GUNI_GC_MN, tailoring),
        GUNI_LB_CM);
    EXPECT_EQ(guni_line_break_resolve(GUNI_LB_SA, GUNI_GC_MC, tailoring),
        GUNI_LB_CM);
    /* A class LB1 does not touch comes back unchanged, so the function is
     * safe to apply to anything. */
    EXPECT_EQ(guni_line_break_resolve(GUNI_LB_OP, GUNI_GC_PS, tailoring),
        GUNI_LB_OP);
  }
}

/** A codepoint for each unresolved Line_Break class, found in the data. */
class LineBreakSamples {
public:
  LineBreakSamples() {
    for (uint32_t cp = 1; cp <= GUNI_MAX_CODEPOINT; ++cp) {
      GUNI_LineBreak class_ = guni_line_break(cp);
      if (by_class_.find(class_) == by_class_.end()) {
        by_class_[class_] = cp;
      }
    }
  }
  const std::map<GUNI_LineBreak, uint32_t> & all() const { return by_class_; }

private:
  std::map<GUNI_LineBreak, uint32_t> by_class_;
};

TEST(Segment, ThePairwiseLineBreakSweepIsUnchanged) {
  /* Every ordered pair of Line_Break classes, under all three tailorings: the
   * artifact design.md section 12.3 asks for, and the one that will be diffed
   * against regex's answers when regex migrates. It is a **regression record,
   * not an oracle** - the oracle is LineBreakTest.txt - so what it buys is
   * that a change to any rule shows up as a reviewable diff rather than as a
   * conformance file that still passes because it never covered that pair.
   *
   * Regenerate with GUNI_BREAK_DUMP=pairs. */
  static const LineBreakSamples samples;
  ASSERT_GT(samples.all().size(), static_cast<size_t>(40))
      << "only " << samples.all().size() << " Line_Break classes found";

  std::string produced;
  produced += "# Every ordered pair of Line_Break classes, as two codepoints,\n";
  produced += "# and whether a line break is allowed between them under each\n";
  produced += "# of the three LB1 tailorings. A regression record: the oracle\n";
  produced += "# is LineBreakTest.txt. Regenerate with GUNI_BREAK_DUMP=pairs.\n";
  produced += "# left right strict normal loose\n";
  for (const auto & left : samples.all()) {
    for (const auto & right : samples.all()) {
      const std::vector<uint32_t> pair = {left.second, right.second};
      char line[128];
      bool answers[3];
      for (int which = 0; which < 3; ++which) {
        const GUNI_BreakOptions options = options_for(GUNI_BREAK_LINE,
            static_cast<GUNI_LineBreakTailoring>(which));
        answers[which] =
            guni_break_at_codepoints(&options, pair.data(), pair.size(), 1);
      }
      std::snprintf(line, sizeof(line), "%s %s %d %d %d\n",
          guni_value_name(GUNI_PROPERTY_LINE_BREAK,
              static_cast<uint32_t>(left.first)),
          guni_value_name(GUNI_PROPERTY_LINE_BREAK,
              static_cast<uint32_t>(right.first)),
          answers[0] ? 1 : 0, answers[1] ? 1 : 0, answers[2] ? 1 : 0);
      produced += line;
    }
  }

  const char * dump = std::getenv("GUNI_BREAK_DUMP");
  const std::string path = gunitest::data("break/linebreak-pairs.txt");
  if (dump != nullptr && std::strcmp(dump, "pairs") == 0) {
    FILE * handle = std::fopen(path.c_str(), "wb");
    ASSERT_NE(handle, nullptr) << path;
    std::fwrite(produced.data(), 1, produced.size(), handle);
    std::fclose(handle);
    std::fprintf(stderr, "wrote %s\n", path.c_str());
    return;
  }

  FILE * handle = std::fopen(path.c_str(), "rb");
  ASSERT_NE(handle, nullptr) << path << " is committed and cannot be absent";
  std::string expected;
  char buffer[4096];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), handle)) != 0) {
    expected.append(buffer, read);
  }
  std::fclose(handle);

  if (produced != expected) {
    /* Name the first differing line rather than printing two 200 KB blobs. */
    size_t line_number = 1;
    size_t at = 0;
    while (at < produced.size() && at < expected.size()
        && produced[at] == expected[at]) {
      if (produced[at] == '\n') {
        ++line_number;
      }
      ++at;
    }
    size_t produced_end = produced.find('\n', at);
    size_t expected_end = expected.find('\n', at);
    size_t start = produced.rfind('\n', at);
    start = (start == std::string::npos) ? 0 : start + 1;
    ADD_FAILURE() << "the pairwise sweep changed at line " << line_number
                  << "\n  now      " << produced.substr(start,
                         produced_end == std::string::npos
                             ? std::string::npos
                             : produced_end - start)
                  << "\n  recorded " << expected.substr(start,
                         expected_end == std::string::npos
                             ? std::string::npos
                             : expected_end - start)
                  << "\nRegenerate with GUNI_BREAK_DUMP=pairs and read the diff.";
  }
  /* And the axis is actually open: the three tailorings do not all agree. */
  size_t differing = 0;
  for (const auto & left : samples.all()) {
    for (const auto & right : samples.all()) {
      const std::vector<uint32_t> pair = {left.second, right.second};
      const GUNI_BreakOptions strict =
          options_for(GUNI_BREAK_LINE, GUNI_LINE_BREAK_STRICT);
      const GUNI_BreakOptions normal =
          options_for(GUNI_BREAK_LINE, GUNI_LINE_BREAK_NORMAL);
      if (guni_break_at_codepoints(&strict, pair.data(), 2, 1)
          != guni_break_at_codepoints(&normal, pair.data(), 2, 1)) {
        ++differing;
      }
    }
  }
  EXPECT_GT(differing, static_cast<size_t>(0))
      << "no pair of classes distinguishes strict from normal, which would "
         "mean the tailoring argument does nothing";
}

/** A provider that breaks every three characters, to prove the seam works. */
bool every_third(void * ctx, const GUNI_BreakText * text, size_t start,
    size_t end, size_t position) {
  (void)text;
  (void)end;
  size_t * calls = static_cast<size_t *>(ctx);
  ++*calls;
  return ((position - start) % 3) == 0;
}

TEST(Segment, ThaiHasNoInteriorBreakWithoutAProvider) {
  /* UAX #14 gives Thai the class SA, LB1 resolves SA to AL, and AL x AL
   * prohibits a break: a Thai paragraph does not wrap. That is what the
   * Standard says to do without a dictionary, and it is why
   * GUNI_BreakProvider exists. */
  const std::vector<uint32_t> thai =
      {0x0E01, 0x0E38, 0x0E49, 0x0E07, 0x0E40, 0x0E17, 0x0E1E}; /* Bangkok */
  for (uint32_t cp : thai) {
    ASSERT_EQ(guni_line_break(cp), GUNI_LB_SA) << std::hex << cp;
  }
  const GUNI_BreakOptions plain = options_for(GUNI_BREAK_LINE);
  std::vector<size_t> found = boundaries(plain, thai);
  EXPECT_EQ(found, std::vector<size_t>({thai.size()}))
      << "only the end of the text is a break opportunity";

  /* With a provider, the interior opens up - and the provider is asked only
   * about positions strictly inside the run. */
  size_t calls = 0;
  GUNI_BreakProvider provider;
  provider.ctx = &calls;
  provider.sa_break_at = &every_third;
  const GUNI_BreakOptions with = options_for(GUNI_BREAK_LINE,
      GUNI_LINE_BREAK_STRICT, &provider);
  found = boundaries(with, thai);
  EXPECT_EQ(found, std::vector<size_t>({3, 6, thai.size()}));
  EXPECT_GT(calls, static_cast<size_t>(0));

  /* A provider that never breaks leaves the Standard's behaviour exactly. */
  size_t unused = 0;
  GUNI_BreakProvider silent;
  silent.ctx = &unused;
  silent.sa_break_at = [](void *, const GUNI_BreakText *, size_t, size_t,
                           size_t) { return false; };
  const GUNI_BreakOptions quiet = options_for(GUNI_BREAK_LINE,
      GUNI_LINE_BREAK_STRICT, &silent);
  EXPECT_EQ(boundaries(quiet, thai), std::vector<size_t>({thai.size()}));

  /* And it is not consulted for text with no SA in it at all. */
  calls = 0;
  const std::vector<uint32_t> latin = {'a', ' ', 'b'};
  boundaries(with, latin);
  EXPECT_EQ(calls, static_cast<size_t>(0));
}

TEST(Segment, ProvidersReadTheTextThroughTheAccessors) {
  /* The provider is handed the text rather than a copy of the run, so that a
   * dictionary breaker sees the real offsets. This checks that the accessors
   * it is given actually work over both encodings. */
  struct Recorder {
    std::vector<uint32_t> seen;
    size_t length = 0;
  };
  Recorder recorder;
  GUNI_BreakProvider provider;
  provider.ctx = &recorder;
  provider.sa_break_at = [](void * ctx, const GUNI_BreakText * text,
                            size_t start, size_t end, size_t position) {
    Recorder * self = static_cast<Recorder *>(ctx);
    self->length = guni_break_text_length(text);
    self->seen.clear();
    size_t offset = start;
    uint32_t codepoint = 0;
    size_t next = 0;
    while (offset < end && guni_break_text_at(text, offset, &codepoint, &next)) {
      self->seen.push_back(codepoint);
      offset = next;
    }
    /* "Between the first and second characters of the run", which over UTF-8
     * is not start + 1: the accessor is how a provider steps. Getting this
     * wrong in the test is exactly the mistake the accessors exist to stop a
     * provider making. */
    size_t second = start;
    if (guni_break_text_at(text, start, &codepoint, &next)) {
      second = next;
    }
    return position == second;
  };
  const GUNI_BreakOptions options = options_for(GUNI_BREAK_LINE,
      GUNI_LINE_BREAK_STRICT, &provider);

  const std::vector<uint32_t> thai = {0x0E01, 0x0E02, 0x0E03};
  boundaries(options, thai);
  EXPECT_EQ(recorder.seen, thai);
  EXPECT_EQ(recorder.length, thai.size());

  /* The same over UTF-8: the offsets are bytes, and the accessor decodes. */
  std::string utf8;
  char bytes[GUNI_UTF8_MAX_LENGTH];
  for (uint32_t cp : thai) {
    utf8.append(bytes, guni_utf8_encode(cp, bytes));
  }
  recorder.seen.clear();
  size_t count = 0;
  ASSERT_EQ(guni_break_all(&options, utf8.data(), utf8.size(), nullptr, 0,
                &count),
      GUNI_ERR_LIMIT);
  std::vector<size_t> found(count);
  size_t written = 0;
  ASSERT_EQ(guni_break_all(&options, utf8.data(), utf8.size(), found.data(),
                found.size(), &written),
      GUNI_OK);
  EXPECT_EQ(recorder.seen, thai);
  EXPECT_EQ(recorder.length, utf8.size()) << "lengths are in the caller's units";
  EXPECT_EQ(found, std::vector<size_t>({3, utf8.size()}))
      << "a Thai character is three bytes, so the break is at byte 3";
}

TEST(Segment, GraphemeClustersHoldEmojiAndFlagsTogether) {
  /* What a cursor, a delete key and a cluster map all need. These are the
   * sequences that a naive implementation splits. */
  struct Case {
    const char * what;
    std::vector<uint32_t> text;
    std::vector<size_t> expected;
  };
  const std::vector<Case> cases = {
    {"a family, joined by ZWJ",
        {0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467}, {0, 5}},
    {"two flags", {0x1F1FA, 0x1F1F8, 0x1F1EC, 0x1F1E7}, {0, 2, 4}},
    {"three regional indicators: the third starts a new cluster",
        {0x1F1FA, 0x1F1F8, 0x1F1EC}, {0, 2, 3}},
    {"a base and two marks", {'a', 0x0301, 0x0316, 'b'}, {0, 3, 4}},
    {"CR LF is one cluster", {0x000D, 0x000A}, {0, 2}},
    {"LF CR is two", {0x000A, 0x000D}, {0, 1, 2}},
    {"a Hangul syllable from jamo", {0x1100, 0x1161, 0x11A8}, {0, 3}},
    {"a Devanagari conjunct", {0x0915, 0x094D, 0x0937}, {0, 3}},
    {"an emoji with a skin-tone modifier", {0x1F44D, 0x1F3FB}, {0, 2}},
  };
  const GUNI_BreakOptions options = options_for(GUNI_BREAK_GRAPHEME);
  for (const Case & test : cases) {
    EXPECT_EQ(boundaries(options, test.text), test.expected) << test.what;
  }
}

TEST(Segment, WordAndSentenceBoundaries) {
  const GUNI_BreakOptions words = options_for(GUNI_BREAK_WORD);
  const std::vector<uint32_t> text = {'o', 'n', 'e', ' ', 't', 'w', 'o'};
  EXPECT_EQ(boundaries(words, text), std::vector<size_t>({0, 3, 4, 7}));

  const GUNI_BreakOptions sentences = options_for(GUNI_BREAK_SENTENCE);
  const std::string prose = "One. Two.";
  std::vector<uint32_t> codepoints(prose.begin(), prose.end());
  EXPECT_EQ(boundaries(sentences, codepoints),
      std::vector<size_t>({0, 5, prose.size()}));

  /* "Mr. Smith" breaks after "Mr." per UAX #29, which is the case CLDR's
   * suppression lists exist for and which this library does not tailor
   * (design.md section 9.2). */
  const std::string abbreviated = "Mr. Smith left.";
  std::vector<uint32_t> abbreviated_points(abbreviated.begin(),
      abbreviated.end());
  std::vector<size_t> found = boundaries(sentences, abbreviated_points);
  EXPECT_EQ(found.size(), static_cast<size_t>(3))
      << "the Standard breaks after \"Mr.\"; a suppression provider would not";
}

TEST(Segment, TheIteratorAndThePointQueryAgreeAtTheEdges) {
  const GUNI_BreakOptions grapheme = options_for(GUNI_BREAK_GRAPHEME);
  const GUNI_BreakOptions line = options_for(GUNI_BREAK_LINE);

  /* Empty text has no boundary of any kind, which is neither standard's
   * literal answer and is what regex and Perl both give: there are no
   * characters, so there is nothing for a boundary to fall between. */
  for (const GUNI_BreakOptions * options : {&grapheme, &line}) {
    EXPECT_FALSE(guni_break_at(options, "", 0, 0));
    EXPECT_FALSE(guni_break_at_codepoints(options, nullptr, 0, 0));
    size_t count = 99;
    EXPECT_EQ(guni_break_all(options, "", 0, nullptr, 0, &count), GUNI_OK);
    EXPECT_EQ(count, static_cast<size_t>(0));
  }

  /* A one-character text: a grapheme boundary at each end, a line break only
   * at the end. */
  const std::vector<uint32_t> one = {'a'};
  EXPECT_EQ(boundaries(grapheme, one), std::vector<size_t>({0, 1}));
  EXPECT_EQ(boundaries(line, one), std::vector<size_t>({1}));

  /* A position past the end, and one inside a character, are both "no". */
  const std::string utf8 = "\xC3\xA9x"; /* U+00E9 then x */
  EXPECT_TRUE(guni_break_at(&grapheme, utf8.data(), utf8.size(), 2));
  EXPECT_FALSE(guni_break_at(&grapheme, utf8.data(), utf8.size(), 1))
      << "the second byte of a two-byte character is not a boundary";
  EXPECT_FALSE(guni_break_at(&grapheme, utf8.data(), utf8.size(), 99));
}

TEST(Segment, ErrorPathsAndDefaults) {
  const std::vector<uint32_t> text = {'a', ' ', 'b'};
  /* No options at all means grapheme with the strict tailoring, which is the
   * answer to "I did not think about it". */
  EXPECT_TRUE(guni_break_at_codepoints(nullptr, text.data(), text.size(), 1));
  size_t count = 0;
  ASSERT_EQ(guni_break_all_codepoints(nullptr, text.data(), text.size(),
                nullptr, 0, &count),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(count, static_cast<size_t>(4));

  /* A kind out of range answers false rather than reading a function pointer
   * off the end of a table. */
  GUNI_BreakOptions bad = options_for(GUNI_BREAK_GRAPHEME);
  bad.kind = static_cast<GUNI_BreakKind>(GUNI_BREAK_KIND_COUNT);
  EXPECT_FALSE(guni_break_at_codepoints(&bad, text.data(), text.size(), 1));
  bad.kind = static_cast<GUNI_BreakKind>(99);
  EXPECT_FALSE(guni_break_at_codepoints(&bad, text.data(), text.size(), 1));

  const GUNI_BreakOptions options = options_for(GUNI_BREAK_GRAPHEME);
  size_t out[4];
  EXPECT_EQ(guni_break_all(&options, nullptr, 3, out, 4, &count),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_break_all(&options, "abc", 3, out, 4, nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_break_all(&options, "abc", 3, nullptr, 4, &count),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_break_all_codepoints(&options, nullptr, 3, out, 4, &count),
      GUNI_ERR_INVALID);

  /* A null iterator, and a null out-parameter, are refused rather than
   * dereferenced. */
  guni_break_iter_init(nullptr, &options, "abc", 3);
  guni_break_iter_init_codepoints(nullptr, &options, text.data(), 3);
  GUNI_BreakIter iter;
  guni_break_iter_init(&iter, &options, nullptr, 7);
  EXPECT_EQ(iter.length, static_cast<size_t>(0));
  size_t position = 0;
  EXPECT_FALSE(guni_break_iter_next(&iter, &position));
  EXPECT_FALSE(guni_break_iter_next(nullptr, &position));
  guni_break_iter_init(&iter, &options, "abc", 3);
  EXPECT_FALSE(guni_break_iter_next(&iter, nullptr));

  /* The accessors a provider is given refuse the same way. */
  EXPECT_EQ(guni_break_text_length(nullptr), static_cast<size_t>(0));
  uint32_t codepoint = 0;
  EXPECT_FALSE(guni_break_text_at(nullptr, 0, &codepoint, nullptr));
}

TEST(Segment, IllFormedUtf8DoesNotStopTheRules) {
  /* A boundary query must not have to validate the whole buffer first, so an
   * ill-formed byte is stepped over as one character with the replacement
   * character's properties. What matters is that the walk terminates and the
   * offsets stay inside the buffer. */
  const std::string broken = "a\xFF\xC3z";
  const GUNI_BreakOptions options = options_for(GUNI_BREAK_GRAPHEME);
  size_t count = 0;
  ASSERT_EQ(guni_break_all(&options, broken.data(), broken.size(), nullptr, 0,
                &count),
      GUNI_ERR_LIMIT);
  std::vector<size_t> found(count);
  size_t written = 0;
  ASSERT_EQ(guni_break_all(&options, broken.data(), broken.size(), found.data(),
                found.size(), &written),
      GUNI_OK);
  ASSERT_FALSE(found.empty());
  for (size_t position : found) {
    EXPECT_LE(position, broken.size());
  }
  EXPECT_EQ(found.back(), broken.size());
}


TEST(Segment, RulesTheConformanceFilesDoNotReach) {
  /* Three positions the four files leave uncovered, found by measuring rather
   * than by reading: a conformance file is a sample of rule interactions, not
   * an enumeration of them, and these are the ones its sample misses. */
  const GUNI_BreakOptions line = options_for(GUNI_BREAK_LINE);
  const GUNI_BreakOptions sentence = options_for(GUNI_BREAK_SENTENCE);

  /* LB25's `(PO | PR) x OP IS? NU` with the optional IS present: "$(.5" must
   * not break after the currency sign. The file has the form without the IS. */
  const std::vector<uint32_t> money = {'$', '(', '.', '5'};
  EXPECT_FALSE(guni_break_at_codepoints(&line, money.data(), money.size(), 1));

  /* SB11 after a paragraph separator, which needs the separator to be the
   * character the lookback lands on. */
  const std::vector<uint32_t> paragraphs = {'A', '.', 0x2029, 'B'};
  std::vector<size_t> found = boundaries(sentence, paragraphs);
  EXPECT_EQ(found, std::vector<size_t>({0, 3, 4}))
      << "the sentence ends after the separator, not before it";
}

/** A provider that reads past the end of its run, to check the accessor. */
bool reads_past_end(void * ctx, const GUNI_BreakText * text, size_t start,
    size_t end, size_t position) {
  (void)start;
  (void)position;
  bool * saw_end = static_cast<bool *>(ctx);
  uint32_t codepoint = 0;
  /* At the end of the *text* the accessor says no, which is how a provider
   * knows to stop without being told the length separately. */
  if (!guni_break_text_at(text, guni_break_text_length(text), &codepoint,
          nullptr)) {
    *saw_end = true;
  }
  (void)end;
  return false;
}

TEST(Segment, ProvidersCanReadToTheEndOfTheText) {
  bool saw_end = false;
  GUNI_BreakProvider provider;
  provider.ctx = &saw_end;
  provider.sa_break_at = &reads_past_end;
  const GUNI_BreakOptions options = options_for(GUNI_BREAK_LINE,
      GUNI_LINE_BREAK_STRICT, &provider);
  const std::vector<uint32_t> thai = {0x0E01, 0x0E02, 0x0E03};
  boundaries(options, thai);
  EXPECT_TRUE(saw_end);
}

TEST(Segment, AnSaRunBeginningAfterOtherTextIsStillARun) {
  /* The provider is consulted only strictly inside a run of SA characters, so
   * the position where the run *starts* - with a non-SA character before it -
   * has to be recognised as not being inside one. */
  /* The positions asked about, as a set: boundaries() walks the text twice -
   * once to count and once to fill - so counting calls would be counting the
   * walks. */
  std::set<size_t> asked;
  GUNI_BreakProvider provider;
  provider.ctx = &asked;
  provider.sa_break_at = [](void * ctx, const GUNI_BreakText *, size_t,
                            size_t, size_t position) {
    static_cast<std::set<size_t> *>(ctx)->insert(position);
    return true;
  };
  const GUNI_BreakOptions options = options_for(GUNI_BREAK_LINE,
      GUNI_LINE_BREAK_STRICT, &provider);
  const std::vector<uint32_t> mixed = {'a', 0x0E01, 0x0E02, 0x0E03};
  std::vector<size_t> found = boundaries(options, mixed);
  /* Position 1 is not inside the run - the character before it is Latin - so
   * the provider is not asked there; positions 2 and 3 are. */
  EXPECT_EQ(asked, std::set<size_t>({2, 3}));
  EXPECT_EQ(found, std::vector<size_t>({2, 3, 4}));
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
