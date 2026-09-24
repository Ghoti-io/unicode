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
#include <iostream>
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

  /* **`normal` keeps it too**, which is the correction CSS Text forced: breaks
   * before class CJ are "forbidden for normal and strict line breaking and
   * allowed in loose". This test asserted the opposite for as long as the
   * header claimed `normal` resolved CJ to ID. */
  EXPECT_FALSE(guni_break_at_codepoints(&strict, kana.data(), kana.size(), 1))
      << "strict keeps the prolonged sound mark with its character";
  EXPECT_FALSE(guni_break_at_codepoints(&normal, kana.data(), kana.size(), 1))
      << "and so does normal: CSS forbids this break for both";
  EXPECT_TRUE(guni_break_at_codepoints(&loose, kana.data(), kana.size(), 1))
      << "loose is the only value that allows it";

  /* And the resolution function says the same thing on its own, which is what
   * regex will call when it applies LB1 itself. */
  EXPECT_EQ(guni_line_break_resolve(GUNI_LB_CJ, GUNI_GC_LM,
                GUNI_LINE_BREAK_STRICT),
      GUNI_LB_NS);
  EXPECT_EQ(guni_line_break_resolve(GUNI_LB_CJ, GUNI_GC_LM,
                GUNI_LINE_BREAK_NORMAL),
      GUNI_LB_NS);
  EXPECT_EQ(guni_line_break_resolve(GUNI_LB_CJ, GUNI_GC_LM,
                GUNI_LINE_BREAK_LOOSE),
      GUNI_LB_ID);
  /* Zero is strict, so a caller who does not choose gets regex's behaviour
   * and the Standard's worked example. */
  EXPECT_EQ(static_cast<GUNI_LineBreakTailoring>(0), GUNI_LINE_BREAK_STRICT);
  /* And zero is a neutral writing system, under which normal and strict are
   * the same rule set - so the axis that separates them has to be asked for. */
  EXPECT_EQ(static_cast<GUNI_WritingSystem>(0), GUNI_WRITING_SYSTEM_NEUTRAL);

  /* What *does* separate normal from strict: the CJK hyphens, in Japanese or
   * Chinese text only. U+301C is Nonstarter, so LB21 is what normal lifts. */
  const std::vector<uint32_t> wave = {0x4E00, 0x301C, 0x4E00};
  GUNI_BreakOptions normal_ja = normal;
  normal_ja.writing_system = GUNI_WRITING_SYSTEM_JAPANESE;
  GUNI_BreakOptions strict_ja = strict;
  strict_ja.writing_system = GUNI_WRITING_SYSTEM_JAPANESE;
  EXPECT_TRUE(guni_break_at_codepoints(&normal_ja, wave.data(), wave.size(), 1))
      << "normal allows a break before U+301C in Japanese text";
  EXPECT_FALSE(guni_break_at_codepoints(&strict_ja, wave.data(), wave.size(), 1))
      << "strict never does";
  EXPECT_FALSE(guni_break_at_codepoints(&normal, wave.data(), wave.size(), 1))
      << "and neither does normal when no writing system is claimed";
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
/**
 * One codepoint per Line_Break class, plus the codepoints CSS Text names.
 *
 * The first codepoint of each class is the sample, which covers every pair of
 * *classes* - and **that is structurally unable to see a rule written about
 * particular characters.** CSS Text's tailorings are largely of that kind: six
 * iteration marks and ten centred punctuation marks inside class NS, two hyphens
 * inside HH, and class PO or PR qualified by East_Asian_Width. A sweep over
 * class representatives reported the Japanese columns as identical to the
 * neutral ones, which read as "the writing system changes nothing" when what it
 * meant was "this sample cannot reach the rules that would show it".
 *
 * So the named characters are added as extra entries, keyed by a label rather
 * than by a class so that two rows can share a class. `guni_value_name` cannot
 * name them, which is why entries carry their own label.
 */
class LineBreakSamples {
public:
  LineBreakSamples() {
    std::map<GUNI_LineBreak, uint32_t> first_of_class;
    for (uint32_t cp = 1; cp <= GUNI_MAX_CODEPOINT; ++cp) {
      GUNI_LineBreak class_ = guni_line_break(cp);
      if (first_of_class.find(class_) == first_of_class.end()) {
        first_of_class[class_] = cp;
      }
    }
    classes_ = first_of_class.size();
    for (const auto & entry : first_of_class) {
      entries_.push_back({guni_value_name(GUNI_PROPERTY_LINE_BREAK,
                              static_cast<uint32_t>(entry.first)),
          entry.second});
    }
    /* Every character CSS Text section 5.2 names, one row each, so that the
     * fixture covers the rule and not merely the class it lives in. */
    static const struct {
      const char * label;
      uint32_t codepoint;
    } named[] = {
        {"cjk-hyphen-301C", 0x301C}, {"cjk-hyphen-30A0", 0x30A0},
        {"hyphen-2010", 0x2010}, {"hyphen-2013", 0x2013},
        {"iteration-3005", 0x3005}, {"iteration-303B", 0x303B},
        {"iteration-309D", 0x309D}, {"iteration-309E", 0x309E},
        {"iteration-30FD", 0x30FD}, {"iteration-30FE", 0x30FE},
        {"inseparable-2025", 0x2025}, {"inseparable-2026", 0x2026},
        {"centred-30FB", 0x30FB}, {"centred-FF1A", 0xFF1A},
        {"centred-FF1B", 0xFF1B}, {"centred-FF65", 0xFF65},
        {"centred-203C", 0x203C}, {"centred-2047", 0x2047},
        {"centred-2048", 0x2048}, {"centred-2049", 0x2049},
        {"centred-FF01", 0xFF01}, {"centred-FF1F", 0xFF1F},
        {"suffix-wide-FF05", 0xFF05}, {"suffix-narrow-0025", 0x0025},
        {"prefix-wide-FFE5", 0xFFE5}, {"prefix-narrow-0024", 0x0024},
        {"small-kana-3041", 0x3041}, {"prolonged-30FC", 0x30FC},
        {"ideograph-4E00", 0x4E00},
    };
    for (const auto & entry : named) {
      entries_.push_back({entry.label, entry.codepoint});
    }
  }

  struct Entry {
    std::string label;
    uint32_t codepoint;
  };

  const std::vector<Entry> & all() const { return entries_; }
  /** How many of the entries are class representatives rather than named. */
  size_t classes() const { return classes_; }

private:
  std::vector<Entry> entries_;
  size_t classes_ = 0;
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
  ASSERT_GT(samples.classes(), static_cast<size_t>(40))
      << "only " << samples.classes() << " Line_Break classes found";
  ASSERT_GT(samples.all().size(), samples.classes())
      << "the CSS-named characters are not in the sample, so the rules written "
         "about particular characters are not covered by this sweep";

  std::string produced;
  produced += "# Every ordered pair of Line_Break classes, as two codepoints,\n";
  produced += "# and whether a line break is allowed between them under each of\n";
  produced += "# CSS Text's line-break values, in a neutral writing system and\n";
  produced += "# then in Japanese. Six columns rather than three because four of\n";
  produced += "# the tailorings apply only to Chinese and Japanese text, and\n";
  produced += "# under a neutral system normal and strict are the same rules.\n";
  produced += "# `anywhere` is not here: it does not read Line_Break at all.\n";
  produced += "# A regression record: the oracle is LineBreakTest.txt and the\n";
  produced += "# ICU differential. Regenerate with GUNI_BREAK_DUMP=pairs.\n";
  produced += "# left right strict normal loose strict_ja normal_ja loose_ja\n";
  static const GUNI_WritingSystem systems[2] = {
      GUNI_WRITING_SYSTEM_NEUTRAL, GUNI_WRITING_SYSTEM_JAPANESE};
  for (const auto & left : samples.all()) {
    for (const auto & right : samples.all()) {
      const std::vector<uint32_t> pair = {left.codepoint, right.codepoint};
      char line[160];
      bool answers[6];
      for (int system = 0; system < 2; ++system) {
        for (int which = 0; which < 3; ++which) {
          GUNI_BreakOptions options = options_for(GUNI_BREAK_LINE,
              static_cast<GUNI_LineBreakTailoring>(which));
          options.writing_system = systems[system];
          answers[system * 3 + which] =
              guni_break_at_codepoints(&options, pair.data(), pair.size(), 1);
        }
      }
      std::snprintf(line, sizeof(line), "%s %s %d %d %d %d %d %d\n",
          left.label.c_str(), right.label.c_str(),
          answers[0] ? 1 : 0, answers[1] ? 1 : 0, answers[2] ? 1 : 0,
          answers[3] ? 1 : 0, answers[4] ? 1 : 0, answers[5] ? 1 : 0);
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
  /* And every axis is actually open, which is what stops the arguments from
   * being decorative. Three claims, because the axes are not equivalent:
   *
   *   strict vs loose      differs with no writing system claimed
   *   strict vs normal     differs ONLY in Chinese or Japanese - so asking it
   *                        without one is the check that used to pass here and
   *                        now cannot, because CSS gives them the same rules
   *   neutral vs Japanese  differs, at some pair, for at least one value
   *
   * The second is the interesting one: this test previously asserted that some
   * pair separates strict from normal in a neutral writing system, and that
   * assertion was satisfied only by the defect - `normal` resolving CJ to ID. */
  size_t strict_vs_loose = 0;
  size_t strict_vs_normal_neutral = 0;
  size_t strict_vs_normal_ja = 0;
  size_t neutral_vs_ja = 0;
  for (const auto & left : samples.all()) {
    for (const auto & right : samples.all()) {
      const std::vector<uint32_t> pair = {left.codepoint, right.codepoint};
      GUNI_BreakOptions strict =
          options_for(GUNI_BREAK_LINE, GUNI_LINE_BREAK_STRICT);
      GUNI_BreakOptions normal =
          options_for(GUNI_BREAK_LINE, GUNI_LINE_BREAK_NORMAL);
      GUNI_BreakOptions loose =
          options_for(GUNI_BREAK_LINE, GUNI_LINE_BREAK_LOOSE);
      GUNI_BreakOptions normal_ja = normal;
      normal_ja.writing_system = GUNI_WRITING_SYSTEM_JAPANESE;
      GUNI_BreakOptions strict_ja = strict;
      strict_ja.writing_system = GUNI_WRITING_SYSTEM_JAPANESE;
      GUNI_BreakOptions loose_ja = loose;
      loose_ja.writing_system = GUNI_WRITING_SYSTEM_JAPANESE;

      const bool s = guni_break_at_codepoints(&strict, pair.data(), 2, 1);
      const bool n = guni_break_at_codepoints(&normal, pair.data(), 2, 1);
      const bool l = guni_break_at_codepoints(&loose, pair.data(), 2, 1);
      const bool s_ja = guni_break_at_codepoints(&strict_ja, pair.data(), 2, 1);
      const bool n_ja = guni_break_at_codepoints(&normal_ja, pair.data(), 2, 1);
      const bool l_ja = guni_break_at_codepoints(&loose_ja, pair.data(), 2, 1);

      strict_vs_loose += (s != l) ? 1 : 0;
      strict_vs_normal_neutral += (s != n) ? 1 : 0;
      strict_vs_normal_ja += (s_ja != n_ja) ? 1 : 0;
      neutral_vs_ja += (s != s_ja || n != n_ja || l != l_ja) ? 1 : 0;
    }
  }
  EXPECT_GT(strict_vs_loose, static_cast<size_t>(0))
      << "no pair separates strict from loose, so the tailoring does nothing";
  EXPECT_EQ(strict_vs_normal_neutral, static_cast<size_t>(0))
      << "strict and normal must agree everywhere with no writing system "
         "claimed: CSS Text gives them the same rules outside Chinese and "
         "Japanese, and a difference here is the defect this test used to "
         "assert as a feature";
  EXPECT_GT(strict_vs_normal_ja, static_cast<size_t>(0))
      << "no pair separates strict from normal in Japanese either, so the "
         "CJK-hyphen tailoring is not reached";
  EXPECT_GT(neutral_vs_ja, static_cast<size_t>(0))
      << "the writing system changes nothing, so its field does nothing";
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

TEST(Segment, LooseIsTheWholeOfCssLoose) {
  /* CSS Text section 5.2, rule by rule. This test replaced one that asserted
   * the *opposite* - that this library implemented LB1 and none of the rest -
   * which was true, recorded honestly, and a conformance gap rather than a
   * decision. The ICU differential found two of the eight; reading the
   * specification found the other six, including one ICU does not implement
   * either (the hyphens) and one that corrects `normal`.
   *
   * Every case is a minimal pair so that a failure names one rule. The left
   * character is Han (class ID) except where a rule needs otherwise. */
  const uint32_t han = 0x4E00;

  GUNI_BreakOptions strict = options_for(GUNI_BREAK_LINE,
      GUNI_LINE_BREAK_STRICT);
  GUNI_BreakOptions normal = options_for(GUNI_BREAK_LINE,
      GUNI_LINE_BREAK_NORMAL);
  GUNI_BreakOptions loose = options_for(GUNI_BREAK_LINE, GUNI_LINE_BREAK_LOOSE);
  GUNI_BreakOptions strict_ja = strict;
  GUNI_BreakOptions normal_ja = normal;
  GUNI_BreakOptions loose_ja = loose;
  strict_ja.writing_system = GUNI_WRITING_SYSTEM_JAPANESE;
  normal_ja.writing_system = GUNI_WRITING_SYSTEM_JAPANESE;
  loose_ja.writing_system = GUNI_WRITING_SYSTEM_JAPANESE;

  auto breaks = [](const GUNI_BreakOptions & options, uint32_t left,
                    uint32_t right) {
    const std::vector<uint32_t> pair = {left, right};
    return guni_break_at_codepoints(&options, pair.data(), pair.size(), 1);
  };

  /* 1. CJK hyphen-like characters: normal and loose, Chinese or Japanese only. */
  for (const uint32_t cp : {0x301CU, 0x30A0U}) {
    EXPECT_TRUE(breaks(normal_ja, han, cp)) << std::hex << cp;
    EXPECT_TRUE(breaks(loose_ja, han, cp)) << std::hex << cp;
    EXPECT_FALSE(breaks(strict_ja, han, cp)) << std::hex << cp;
    EXPECT_FALSE(breaks(normal, han, cp))
        << "no writing system claimed, so the rule does not apply";
    EXPECT_FALSE(breaks(loose, han, cp));
  }

  /* 2. Hyphens, loose only, and only after an ID character - a condition on the
   *    neighbour rather than on the writing system, so it holds in neutral. */
  for (const uint32_t cp : {0x2010U, 0x2013U}) {
    EXPECT_TRUE(breaks(loose, han, cp)) << std::hex << cp;
    EXPECT_FALSE(breaks(normal, han, cp)) << std::hex << cp;
    EXPECT_FALSE(breaks(loose, 'A', cp))
        << "the preceding character must be ID, and Latin A is AL";
  }

  /* 3. Class CJ, loose only. This is LB1 and is asserted in full elsewhere. */
  EXPECT_TRUE(breaks(loose, han, 0x3041));
  EXPECT_FALSE(breaks(normal, han, 0x3041));

  /* 4. Iteration marks, loose only, in any writing system. */
  for (const uint32_t cp : {0x3005U, 0x303BU, 0x309DU, 0x309EU, 0x30FDU, 0x30FEU}) {
    EXPECT_TRUE(breaks(loose, han, cp)) << std::hex << cp;
    EXPECT_FALSE(breaks(normal, han, cp)) << std::hex << cp;
    EXPECT_FALSE(breaks(strict, han, cp)) << std::hex << cp;
  }

  /* 5. Between two Inseparables, loose only. *Between*, so the pair matters:
   *    ID x IN keeps LB22 and only IN x IN lifts it. */
  ASSERT_EQ(guni_line_break(0x2025), GUNI_LB_IN);
  ASSERT_EQ(guni_line_break(0x2026), GUNI_LB_IN);
  EXPECT_TRUE(breaks(loose, 0x2025, 0x2026));
  EXPECT_FALSE(breaks(normal, 0x2025, 0x2026));
  EXPECT_FALSE(breaks(loose, han, 0x2026))
      << "ID x IN is not a pair of Inseparables";

  /* 6. Centred punctuation, loose and Chinese or Japanese only. U+FF01 and
   *    U+FF1F are Exclamation rather than Nonstarter, so they prove the rule
   *    runs before LB13 and not only before LB21. */
  for (const uint32_t cp : {0x30FBU, 0xFF1AU, 0xFF1BU, 0xFF65U, 0x203CU,
           0x2047U, 0x2048U, 0x2049U, 0xFF01U, 0xFF1FU}) {
    EXPECT_TRUE(breaks(loose_ja, han, cp)) << std::hex << cp;
    EXPECT_FALSE(breaks(loose, han, cp)) << std::hex << cp;
    EXPECT_FALSE(breaks(normal_ja, han, cp)) << std::hex << cp;
  }
  EXPECT_EQ(guni_line_break(0xFF01), GUNI_LB_EX)
      << "if this stops being EX the LB13 half of case 6 stops being tested";

  /* 7 and 8. Suffixes and prefixes, by class and width rather than by
   *    codepoint, so the narrow members of the same classes must NOT break. */
  EXPECT_EQ(guni_line_break(0xFF05), GUNI_LB_PO);
  EXPECT_EQ(guni_east_asian_width(0xFF05), GUNI_EAW_FULLWIDTH);
  EXPECT_TRUE(breaks(loose_ja, han, 0xFF05)) << "PO and fullwidth";
  EXPECT_FALSE(breaks(loose, han, 0xFF05));
  EXPECT_EQ(guni_line_break(0x0025), GUNI_LB_PO);
  EXPECT_EQ(guni_east_asian_width(0x0025), GUNI_EAW_NARROW);
  EXPECT_FALSE(breaks(loose_ja, han, 0x0025))
      << "PO but narrow, so the suffix rule must not reach it";

  EXPECT_EQ(guni_line_break(0xFFE5), GUNI_LB_PR);
  EXPECT_TRUE(breaks(loose_ja, 0xFFE5, han)) << "break AFTER a wide prefix";
  EXPECT_FALSE(breaks(loose, 0xFFE5, han));
  EXPECT_EQ(guni_line_break(0x0024), GUNI_LB_PR);
  EXPECT_EQ(guni_east_asian_width(0x0024), GUNI_EAW_NARROW);
  EXPECT_FALSE(breaks(loose_ja, 0x0024, han))
      << "PR but narrow, so the prefix rule must not reach it";

  /* And none of the eight may lift a prohibition CSS does not name. A break
   * before a space is LB7 and stays forbidden under every value. */
  for (const GUNI_BreakOptions & options : {strict, normal, loose, strict_ja,
           normal_ja, loose_ja}) {
    EXPECT_FALSE(breaks(options, han, 0x0020))
        << "LB7: never break before a space";
    EXPECT_FALSE(breaks(options, han, 0x0301))
        << "LB9: never break before a combining mark";
    EXPECT_FALSE(breaks(options, han, 0x2060))
        << "LB11: never break before a word joiner";
  }
}

TEST(Segment, AnywhereBreaksAroundEveryTypographicCharacterUnit) {
  /* CSS `anywhere`, which is grapheme cluster boundaries with LB2 still
   * excluding the start of text. */
  GUNI_BreakOptions anywhere = options_for(GUNI_BREAK_LINE,
      GUNI_LINE_BREAK_ANYWHERE);
  GUNI_BreakOptions grapheme = options_for(GUNI_BREAK_GRAPHEME);
  GUNI_BreakOptions strict = options_for(GUNI_BREAK_LINE,
      GUNI_LINE_BREAK_STRICT);

  /* The prohibitions CSS names explicitly: GL, WJ and ZWJ. Each is forbidden
   * under strict and allowed under anywhere. */
  const std::vector<uint32_t> nbsp = {'a', 0x00A0, 'b'};   /* GL */
  const std::vector<uint32_t> joiner = {'a', 0x2060, 'b'}; /* WJ */
  const std::vector<uint32_t> zwj = {'a', 0x200D, 'b'};    /* ZWJ */
  for (const std::vector<uint32_t> * text : {&nbsp, &joiner, &zwj}) {
    EXPECT_FALSE(guni_break_at_codepoints(&strict, text->data(), text->size(), 1))
        << "the pair rules forbid this";
    EXPECT_TRUE(guni_break_at_codepoints(&anywhere, text->data(), text->size(), 2))
        << "anywhere disregards it";
  }

  /* What it does *not* disregard is the cluster itself: a combining mark stays
   * with its base, because CSS says "around every typographic character unit"
   * and not inside one. */
  const std::vector<uint32_t> combining = {'a', 0x0301, 'b'};
  EXPECT_FALSE(guni_break_at_codepoints(&anywhere, combining.data(),
      combining.size(), 1))
      << "a combining mark must not be left on a line of its own";
  EXPECT_TRUE(guni_break_at_codepoints(&anywhere, combining.data(),
      combining.size(), 2));

  /* And CRLF stays one unit, because GB3 keeps it together. */
  const std::vector<uint32_t> crlf = {'a', 0x000D, 0x000A, 'b'};
  EXPECT_FALSE(guni_break_at_codepoints(&anywhere, crlf.data(), crlf.size(), 2))
      << "GB3: never between CR and LF";

  /* Over a whole string, anywhere agrees with the grapheme boundaries except
   * at the start of text, which LB2 excludes and GB1 reports. */
  const std::vector<uint32_t> mixed = {'a', 0x0301, 0x00A0, 0x4E00, 0x3041,
      0x1F1EF, 0x1F1F5, 'z'};
  std::vector<size_t> by_line = boundaries(anywhere, mixed);
  std::vector<size_t> by_cluster = boundaries(grapheme, mixed);
  ASSERT_FALSE(by_cluster.empty());
  EXPECT_EQ(by_cluster.front(), static_cast<size_t>(0))
      << "GB1 reports the start";
  by_cluster.erase(by_cluster.begin());
  EXPECT_EQ(by_line, by_cluster)
      << "anywhere is the cluster boundaries, less the start of text";
  /* The regional indicator pair is one cluster, so anywhere does not split it. */
  EXPECT_FALSE(guni_break_at_codepoints(&anywhere, mixed.data(), mixed.size(), 6))
      << "GB12/GB13 keep a flag together";
}

/**
 * The library's side of the ICU differential's protocol, reached with
 * GUNI_BREAK_DUMP=stdin.
 *
 * The same reasoning as testSweep's GUNI_SWEEP_DUMP: a differential wants this
 * library's answers as text, and a test binary already links the library, so a
 * driver of its own would be a second thing to keep correct. One request per
 * line, one answer per line, one process for the batch.
 *
 *   request   <kind>\t<hex of the UTF-8 bytes>[\t<tailoring>]
 *   answer    <kind>\t<hex>\t<space-separated byte offsets>
 *
 * **The answer echoes the request**, which is the framing. One answer per
 * request would let a stray line on stdout shift every answer after it by one
 * and be absorbed silently - and there is a live way to emit one, since the
 * container engine on this machine writes a banner. An echo the parent checks
 * cannot absorb it.
 *
 * Offsets are byte offsets into the UTF-8, position 0 excluded and the length
 * included, which is what guni_break_all() reports. ICU's iterator reports 0
 * as a boundary and its offsets are UTF-16 code units, so its driver converts;
 * the two sides agree on this format and not on the format either library
 * finds natural.
 */
int dump_from_stdin() {
  static const struct {
    const char * name;
    GUNI_BreakKind kind;
  } kinds[] = {
      {"grapheme", GUNI_BREAK_GRAPHEME},
      {"word", GUNI_BREAK_WORD},
      {"sentence", GUNI_BREAK_SENTENCE},
      {"line", GUNI_BREAK_LINE},
  };
  static const struct {
    const char * name;
    GUNI_LineBreakTailoring tailoring;
  } tailorings[] = {
      {"strict", GUNI_LINE_BREAK_STRICT},
      {"normal", GUNI_LINE_BREAK_NORMAL},
      {"loose", GUNI_LINE_BREAK_LOOSE},
      /* What a caller who does not choose gets, which for this library is the
       * zero value and therefore strict. The ICU driver answers the same
       * request from its *unqualified* root locale, so comparing the two
       * checks that the two libraries' defaults agree - which is the question
       * a consumer moving from one to the other actually has. ICU's root
       * measures as strict, and that was measured rather than assumed. */
      {"default", GUNI_LINE_BREAK_STRICT},
      {"anywhere", GUNI_LINE_BREAK_ANYWHERE},
  };
  static const struct {
    const char * name;
    GUNI_WritingSystem writing_system;
  } systems[] = {
      {"neutral", GUNI_WRITING_SYSTEM_NEUTRAL},
      {"chinese", GUNI_WRITING_SYSTEM_CHINESE},
      {"japanese", GUNI_WRITING_SYSTEM_JAPANESE},
  };

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      continue;
    }
    const size_t first = line.find('\t');
    if (first == std::string::npos) {
      std::fprintf(stderr, "guni break dump: no tab in %s\n", line.c_str());
      return 2;
    }
    const size_t second = line.find('\t', first + 1);
    const size_t third = (second == std::string::npos)
        ? std::string::npos
        : line.find('\t', second + 1);
    const std::string kind_name = line.substr(0, first);
    const std::string hex = (second == std::string::npos)
        ? line.substr(first + 1)
        : line.substr(first + 1, second - first - 1);
    const std::string tailoring_name = (second == std::string::npos)
        ? std::string("strict")
        : line.substr(second + 1,
              third == std::string::npos ? std::string::npos
                                         : third - second - 1);
    const std::string system_name = (third == std::string::npos)
        ? std::string("neutral")
        : line.substr(third + 1);

    GUNI_BreakKind kind = GUNI_BREAK_KIND_COUNT;
    for (const auto & entry : kinds) {
      if (kind_name == entry.name) {
        kind = entry.kind;
      }
    }
    GUNI_LineBreakTailoring tailoring = GUNI_LINE_BREAK_STRICT;
    bool known_tailoring = false;
    for (const auto & entry : tailorings) {
      if (tailoring_name == entry.name) {
        tailoring = entry.tailoring;
        known_tailoring = true;
      }
    }
    GUNI_WritingSystem writing_system = GUNI_WRITING_SYSTEM_NEUTRAL;
    bool known_system = false;
    for (const auto & entry : systems) {
      if (system_name == entry.name) {
        writing_system = entry.writing_system;
        known_system = true;
      }
    }
    if (kind == GUNI_BREAK_KIND_COUNT || !known_tailoring || !known_system) {
      std::fprintf(stderr, "guni break dump: %s/%s/%s is not a request\n",
          kind_name.c_str(), tailoring_name.c_str(), system_name.c_str());
      return 2;
    }
    if (hex.size() % 2 != 0) {
      std::fprintf(stderr, "guni break dump: odd hex length\n");
      return 2;
    }

    std::string text;
    for (size_t at = 0; at + 1 < hex.size(); at += 2) {
      text.push_back(static_cast<char>(
          std::stoul(hex.substr(at, 2), nullptr, 16)));
    }

    GUNI_BreakOptions options = options_for(kind, tailoring);
    options.writing_system = writing_system;
    size_t needed = 0;
    GUNI_Result result = guni_break_all(&options, text.data(), text.size(),
        nullptr, 0, &needed);
    if (result != GUNI_OK && result != GUNI_ERR_LIMIT) {
      /* Not silently an empty answer: a refusal is a thing the parent has to
       * be able to see, and an empty boundary list is a valid answer for the
       * empty string. */
      std::printf("%s\t%s\trefused %s\n", kind_name.c_str(), hex.c_str(),
          guni_result_string(result));
      continue;
    }
    std::vector<size_t> found(needed);
    size_t written = 0;
    result = guni_break_all(&options, text.data(), text.size(),
        found.data(), found.size(), &written);
    if (result != GUNI_OK) {
      std::printf("%s\t%s\trefused %s\n", kind_name.c_str(), hex.c_str(),
          guni_result_string(result));
      continue;
    }
    std::string answer;
    for (size_t at = 0; at < written; ++at) {
      char number[32];
      std::snprintf(number, sizeof(number), "%s%zu", at ? " " : "",
          found[at]);
      answer += number;
    }
    std::printf("%s\t%s\t%s\n", kind_name.c_str(), hex.c_str(),
        answer.c_str());
    (void)0;
  }
  std::fflush(stdout);
  return 0;
}

} // namespace

int main(int argc, char ** argv) {
  const char * dump = std::getenv("GUNI_BREAK_DUMP");
  if (dump != nullptr && std::strcmp(dump, "stdin") == 0) {
    return dump_from_stdin();
  }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
