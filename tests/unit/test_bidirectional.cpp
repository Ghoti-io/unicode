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
 * The bidi API's own contract, and the things the conformance files do not
 * reach: the buffer protocol, the allocator variant, the UTF-8 variant, the
 * refusal to honour a raised max_depth, and mirroring.
 *
 * What the algorithm produces is tests/conformance/test_bidi.cpp's job, over
 * 770,241 class sequences and 91,707 real strings.
 */

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

#include "failing_allocator.h"
#include "test_helpers.h"

namespace {

std::vector<uint8_t> levels_of(const std::vector<uint32_t> & text,
    GUNI_BidiDirection direction, uint8_t * paragraph_level = nullptr) {
  std::vector<uint8_t> levels(text.size());
  uint8_t level = 0;
  EXPECT_EQ(guni_bidi_levels(text.data(), text.size(), direction, nullptr,
                levels.data(), levels.size(), &level),
      GUNI_OK);
  if (paragraph_level != nullptr) {
    *paragraph_level = level;
  }
  return levels;
}

std::vector<size_t> reorder(const std::vector<uint8_t> & levels) {
  std::vector<size_t> out(levels.size());
  EXPECT_EQ(guni_bidi_reorder(levels.data(), levels.size(), out.data(),
                out.size()),
      GUNI_OK);
  return out;
}

/* A Hebrew word, a space and a Latin word: the smallest string whose display
 * order is not its logical order. */
const uint32_t kAlef = 0x05D0;
const uint32_t kBet = 0x05D1;

TEST(Bidirectional, ParagraphDirectionComesFromTheFirstStrongCharacter) {
  /* Rules P2 and P3. AUTO is what a plain-text renderer should use, and it is
   * not the zero value: a caller who zeroes a struct gets left-to-right,
   * which is the safer thing not to have thought about. */
  uint8_t level = 9;
  levels_of({kAlef, ' ', 'a'}, GUNI_BIDI_AUTO, &level);
  EXPECT_EQ(level, 1) << "first strong character is right-to-left";
  levels_of({'a', ' ', kAlef}, GUNI_BIDI_AUTO, &level);
  EXPECT_EQ(level, 0);
  levels_of({'1', '2', '3'}, GUNI_BIDI_AUTO, &level);
  EXPECT_EQ(level, 0) << "P3: no strong character means left-to-right";
  levels_of({kAlef}, GUNI_BIDI_LTR, &level);
  EXPECT_EQ(level, 0) << "an explicit direction overrides P2";
  levels_of({'a'}, GUNI_BIDI_RTL, &level);
  EXPECT_EQ(level, 1);
  EXPECT_EQ(static_cast<GUNI_BidiDirection>(0), GUNI_BIDI_LTR);
}

TEST(Bidirectional, IsolatesAreSkippedWhenFindingTheParagraphDirection) {
  /* P2 skips from an isolate initiator to its matching PDI, so a Hebrew word
   * inside an isolate does not make the paragraph right-to-left. */
  uint8_t level = 9;
  levels_of({0x2068, kAlef, 0x2069, 'a'}, GUNI_BIDI_AUTO, &level); /* FSI */
  EXPECT_EQ(level, 0);
  levels_of({0x2068, kAlef, 0x2069, kBet}, GUNI_BIDI_AUTO, &level);
  EXPECT_EQ(level, 1) << "the strong character after the isolate decides";
  /* An unmatched initiator swallows the rest of the paragraph. */
  levels_of({0x2066, kAlef}, GUNI_BIDI_AUTO, &level); /* LRI, no PDI */
  EXPECT_EQ(level, 0);
}

TEST(Bidirectional, MixedDirectionTextReordersForDisplay) {
  /* "a<alef><bet>b" in a left-to-right paragraph: the Hebrew runs
   * right-to-left inside it, which is one level-2 run reversed by L2. */
  const std::vector<uint32_t> text = {'a', kAlef, kBet, 'b'};
  std::vector<uint8_t> levels = levels_of(text, GUNI_BIDI_LTR);
  EXPECT_EQ(levels, std::vector<uint8_t>({0, 1, 1, 0}));
  EXPECT_EQ(reorder(levels), std::vector<size_t>({0, 2, 1, 3}));

  /* The same text in a right-to-left paragraph. The two Latin letters are
   * two separate level-2 runs of one character each, so L2's reversal of the
   * whole level-1 run puts them at the two ends: "b bet alef a". A single
   * level-2 run would have stayed in its own order, which is the next case. */
  levels = levels_of(text, GUNI_BIDI_RTL);
  EXPECT_EQ(levels, std::vector<uint8_t>({2, 1, 1, 2}));
  EXPECT_EQ(reorder(levels), std::vector<size_t>({3, 2, 1, 0}));

  /* A number inside right-to-left text goes to level 2, which is rule I1's
   * "+2 for a number at an even level" seen from the other side: at level 1 a
   * number is +1, and it reads left to right inside the right-to-left run. */
  const std::vector<uint32_t> numbered = {kAlef, '1', '2', kBet};
  levels = levels_of(numbered, GUNI_BIDI_RTL);
  EXPECT_EQ(levels, std::vector<uint8_t>({1, 2, 2, 1}));
  EXPECT_EQ(reorder(levels), std::vector<size_t>({3, 1, 2, 0}));
}

TEST(Bidirectional, TrailingWhitespaceGoesBackToTheParagraphLevel) {
  /* Rule L1, and the reason it is not left to the caller: a right-to-left
   * line ending in a space puts the space on the left without it, and every
   * renderer would have to reimplement the rule. */
  const std::vector<uint32_t> text = {kAlef, ' '};
  std::vector<uint8_t> levels = levels_of(text, GUNI_BIDI_LTR);
  EXPECT_EQ(levels, std::vector<uint8_t>({1, 0}));
  /* A space in the middle keeps the level it resolved to. */
  const std::vector<uint32_t> middle = {kAlef, ' ', kBet};
  levels = levels_of(middle, GUNI_BIDI_LTR);
  EXPECT_EQ(levels, std::vector<uint8_t>({1, 1, 1}));
}

TEST(Bidirectional, BracketsTakeTheDirectionOfWhatIsInsideThem) {
  /* Rule N0, which is why BidiCharacterTest.txt exists beside BidiTest.txt: a
   * bracket is a character, not a class, so a class-based file cannot reach
   * this rule at all. */
  bool opening = false;
  EXPECT_EQ(guni_bidi_paired_bracket('(', &opening), static_cast<uint32_t>(')'));
  EXPECT_TRUE(opening);
  EXPECT_EQ(guni_bidi_paired_bracket(')', &opening), static_cast<uint32_t>('('));
  EXPECT_FALSE(opening);
  EXPECT_EQ(guni_bidi_paired_bracket('a', nullptr), 0u);
  EXPECT_EQ(guni_bidi_paired_bracket(0x110000, nullptr), 0u);
  /* The canonical-equivalence clause: U+2329 is treated as U+3008. */
  EXPECT_EQ(guni_bidi_paired_bracket(0x2329, nullptr), UINT32_C(0x232A));

  /* N0 has two arms and the difference between them is the context *outside*
   * the brackets, which is the part that surprises people.
   *
   * Hebrew inside brackets, Latin around them: the text before the opening
   * bracket is left-to-right, which is the embedding direction, so N0 c(2)
   * gives the brackets the embedding direction and only their contents go
   * right-to-left. */
  const std::vector<uint32_t> latin_context =
      {'a', ' ', '(', kAlef, kBet, ')', ' ', 'b'};
  std::vector<uint8_t> levels = levels_of(latin_context, GUNI_BIDI_LTR);
  EXPECT_EQ(levels, std::vector<uint8_t>({0, 0, 0, 1, 1, 0, 0, 0}));

  /* Change only the context, to Hebrew, and N0 c(1) applies instead: now the
   * brackets themselves go right-to-left with their contents. One character
   * different, a different arm of the rule - which is why a class-based test
   * file cannot reach this and BidiCharacterTest.txt exists. */
  const std::vector<uint32_t> hebrew_context =
      {kAlef, ' ', '(', kAlef, kBet, ')', ' ', kBet};
  levels = levels_of(hebrew_context, GUNI_BIDI_LTR);
  EXPECT_EQ(levels, std::vector<uint8_t>({1, 1, 1, 1, 1, 1, 1, 1}));

  /* N0 b is the arm where a strong type *matching the embedding direction* is
   * inside the brackets. In a left-to-right paragraph with Hebrew either side
   * and Latin inside, the brackets take the embedding direction and the
   * Hebrew around them does not pull them along. */
  const std::vector<uint32_t> latin_inside = {kAlef, '(', 'x', ')', kBet};
  levels = levels_of(latin_inside, GUNI_BIDI_LTR);
  EXPECT_EQ(levels, std::vector<uint8_t>({1, 0, 0, 0, 1}));

  /* The same shape in a right-to-left paragraph goes the other way, and shows
   * that "matching the embedding direction" is what the rule turns on rather
   * than "left-to-right": here the embedding is right-to-left, the Latin
   * inside is the opposite, the context outside is right-to-left, and so
   * N0 c(2) gives the brackets the embedding direction. */
  const std::vector<uint32_t> rtl_context =
      {kAlef, ' ', '(', 'x', ')', ' ', kBet};
  levels = levels_of(rtl_context, GUNI_BIDI_RTL);
  EXPECT_EQ(levels[2], 1) << "the brackets stay with the paragraph";
  EXPECT_EQ(levels[3], 2) << "and the Latin inside them does not";
  EXPECT_EQ(levels[4], 1);
}

TEST(Bidirectional, MirroringIsACharacterMapping) {
  /* Rule L4. The paired glyph for a character at an odd level. */
  EXPECT_EQ(guni_bidi_mirror('('), static_cast<uint32_t>(')'));
  EXPECT_EQ(guni_bidi_mirror(')'), static_cast<uint32_t>('('));
  EXPECT_EQ(guni_bidi_mirror('<'), static_cast<uint32_t>('>'));
  /* Something with no mirror is itself, not zero: a caller applies this
   * unconditionally to every character on an odd level. */
  EXPECT_EQ(guni_bidi_mirror('a'), static_cast<uint32_t>('a'));
  EXPECT_EQ(guni_bidi_mirror(kAlef), kAlef);
  EXPECT_EQ(guni_bidi_mirror(0x110000), UINT32_C(0x110000));
  /* Every mirrored character's mirror has the mirrored property too, and
   * mirroring twice is the identity for all but the handful the UCD maps
   * asymmetrically. */
  size_t mirrored = 0;
  size_t involutions = 0;
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    if (!guni_has_property(cp, GUNI_PROP_BIDI_MIRRORED)) {
      continue;
    }
    ++mirrored;
    if (guni_bidi_mirror(guni_bidi_mirror(cp)) == cp) {
      ++involutions;
    }
  }
  EXPECT_GT(mirrored, static_cast<size_t>(500));
  EXPECT_GT(involutions * 100, mirrored * 90)
      << "mirroring should be an involution for nearly every character";
}

TEST(Bidirectional, ReorderIsAPermutation) {
  /* L2 on levels a caller made up, which is what a layout engine does with a
   * slice of a paragraph's levels after it has chosen its line breaks. */
  const std::vector<uint8_t> levels = {0, 1, 2, 2, 1, 0};
  std::vector<size_t> order = reorder(levels);
  std::vector<bool> seen(levels.size(), false);
  for (size_t index : order) {
    ASSERT_LT(index, levels.size());
    ASSERT_FALSE(seen[index]) << "index " << index << " appears twice";
    seen[index] = true;
  }
  EXPECT_EQ(order, std::vector<size_t>({0, 4, 2, 3, 1, 5}));

  /* An all-even paragraph is the identity, and an empty one is not an error. */
  const std::vector<uint8_t> flat = {0, 0, 0};
  EXPECT_EQ(reorder(flat), std::vector<size_t>({0, 1, 2}));
  size_t nothing = 0;
  EXPECT_EQ(guni_bidi_reorder(nullptr, 0, &nothing, 0), GUNI_OK);
}

TEST(Bidirectional, TheAllocatorVariantHandlesAnyLength) {
  /* The stack variant refuses a paragraph longer than it can hold rather than
   * truncating it, and says so; the allocating one is the way through
   * (design.md section 13.1). */
  std::vector<uint32_t> text(GUNI_BIDI_MAX_STACK_LENGTH + 1, 'a');
  text[0] = kAlef;
  std::vector<uint8_t> levels(text.size());
  uint8_t level = 0;
  EXPECT_EQ(guni_bidi_levels(text.data(), text.size(), GUNI_BIDI_AUTO, nullptr,
                levels.data(), levels.size(), &level),
      GUNI_ERR_LIMIT);
  ASSERT_EQ(guni_bidi_levels_with_allocator(text.data(), text.size(),
                GUNI_BIDI_AUTO, nullptr, levels.data(), levels.size(), &level,
                nullptr),
      GUNI_OK);
  EXPECT_EQ(level, 1);
  EXPECT_EQ(levels[0], 1);
  EXPECT_EQ(levels[1], 2) << "Latin inside a right-to-left paragraph";

  /* And the two agree on a paragraph they can both do. */
  const std::vector<uint32_t> short_text = {'a', kAlef, '1'};
  std::vector<uint8_t> stack_levels(3);
  std::vector<uint8_t> heap_levels(3);
  ASSERT_EQ(guni_bidi_levels(short_text.data(), 3, GUNI_BIDI_AUTO, nullptr,
                stack_levels.data(), 3, nullptr),
      GUNI_OK);
  ASSERT_EQ(guni_bidi_levels_with_allocator(short_text.data(), 3,
                GUNI_BIDI_AUTO, nullptr, heap_levels.data(), 3, nullptr,
                nullptr),
      GUNI_OK);
  EXPECT_EQ(stack_levels, heap_levels);
}

TEST(Bidirectional, TheAllocatorVariantReportsAFailedAllocation) {
  /* One allocation, so one failure to report, and nothing leaked behind it. */
  gunitest::FailingAllocator failing(0);
  std::vector<uint32_t> text(GUNI_BIDI_MAX_STACK_LENGTH + 1, kAlef);
  std::vector<uint8_t> levels(text.size());
  EXPECT_EQ(guni_bidi_levels_with_allocator(text.data(), text.size(),
                GUNI_BIDI_AUTO, nullptr, levels.data(), levels.size(), nullptr,
                const_cast<GUNI_Allocator *>(failing.get())),
      GUNI_ERR_OOM);
  EXPECT_TRUE(failing.failed());
  EXPECT_EQ(failing.live(), 0u) << "nothing kept after the refusal";
  EXPECT_EQ(failing.requests(), 1u) << "one allocation, so one refusal";

  /* And with the refusal lifted it succeeds through the same allocator, so the
   * failure was the allocator's and not the code's. */
  failing.stop_failing();
  EXPECT_EQ(guni_bidi_levels_with_allocator(text.data(), text.size(),
                GUNI_BIDI_AUTO, nullptr, levels.data(), levels.size(), nullptr,
                const_cast<GUNI_Allocator *>(failing.get())),
      GUNI_OK);
  EXPECT_EQ(failing.live(), 0u) << "the working block is released";
}

TEST(Bidirectional, TheUtf8VariantReportsOneLevelPerCharacter) {
  /* Levels are per character and not per byte: a level per continuation byte
   * would be something every caller has to unpick. */
  const std::string text = "a\xD7\x90" "b"; /* a, U+05D0, b */
  uint8_t levels[8];
  size_t count = 0;
  uint8_t paragraph = 9;
  ASSERT_EQ(guni_bidi_levels_utf8(text.data(), text.size(), GUNI_INVALID_REFUSE,
                GUNI_BIDI_AUTO, nullptr, levels, 8, &count, &paragraph),
      GUNI_OK);
  EXPECT_EQ(count, static_cast<size_t>(3));
  EXPECT_EQ(paragraph, 0);
  EXPECT_EQ(levels[0], 0);
  EXPECT_EQ(levels[1], 1);
  EXPECT_EQ(levels[2], 0);

  /* Ill-formed bytes follow utf.h's policy. */
  const std::string broken = "a\xFF" "b";
  EXPECT_EQ(guni_bidi_levels_utf8(broken.data(), broken.size(),
                GUNI_INVALID_REFUSE, GUNI_BIDI_AUTO, nullptr, levels, 8, &count,
                nullptr),
      GUNI_ERR_INVALID);
  ASSERT_EQ(guni_bidi_levels_utf8(broken.data(), broken.size(),
                GUNI_INVALID_REPLACE, GUNI_BIDI_AUTO, nullptr, levels, 8,
                &count, nullptr),
      GUNI_OK);
  EXPECT_EQ(count, static_cast<size_t>(3)) << "U+FFFD is one character";
  ASSERT_EQ(guni_bidi_levels_utf8(broken.data(), broken.size(),
                GUNI_INVALID_SKIP, GUNI_BIDI_AUTO, nullptr, levels, 8, &count,
                nullptr),
      GUNI_OK);
  EXPECT_EQ(count, static_cast<size_t>(2));

  /* Too small a buffer reports the count it needed. */
  size_t needed = 0;
  EXPECT_EQ(guni_bidi_levels_utf8(text.data(), text.size(), GUNI_INVALID_REFUSE,
                GUNI_BIDI_AUTO, nullptr, levels, 1, &needed, nullptr),
      GUNI_ERR_LIMIT);
  EXPECT_EQ(needed, static_cast<size_t>(3));
}

TEST(Bidirectional, DepthOverflowFollowsTheStandardRatherThanTheCaller) {
  /* max_depth is 125 and is not raisable: the Standard's own conformance data
   * depends on the overflow behaviour at exactly that value, so a library that
   * honoured a larger one would pass its own tests and disagree with every
   * other implementation. */
  GUNI_Limits limits;
  guni_limits_default(&limits);
  EXPECT_EQ(limits.max_bidi_depth, static_cast<size_t>(GUNI_BIDI_MAX_DEPTH));
  const std::vector<uint32_t> text = {kAlef};
  std::vector<uint8_t> levels(1);
  limits.max_bidi_depth = 200;
  EXPECT_EQ(guni_bidi_levels(text.data(), 1, GUNI_BIDI_AUTO, &limits,
                levels.data(), 1, nullptr),
      GUNI_ERR_LIMIT);
  limits.max_bidi_depth = 10;
  EXPECT_EQ(guni_bidi_levels(text.data(), 1, GUNI_BIDI_AUTO, &limits,
                levels.data(), 1, nullptr),
      GUNI_ERR_LIMIT);

  /* Beyond 125 nestings the overflow counters take over and the levels stop
   * rising, which is what the Standard says to do rather than failing. */
  std::vector<uint32_t> deep;
  for (int index = 0; index < 200; ++index) {
    deep.push_back(0x202B); /* RLE */
  }
  deep.push_back('a');
  std::vector<uint8_t> deep_levels(deep.size());
  ASSERT_EQ(guni_bidi_levels(deep.data(), deep.size(), GUNI_BIDI_LTR, nullptr,
                deep_levels.data(), deep_levels.size(), nullptr),
      GUNI_OK);
  EXPECT_LE(deep_levels.back(), GUNI_BIDI_MAX_DEPTH + 1);
  EXPECT_GE(deep_levels.back(), GUNI_BIDI_MAX_DEPTH - 1);
}

TEST(Bidirectional, ErrorPathsAreRefusedRatherThanGuessed) {
  const std::vector<uint32_t> text = {'a'};
  std::vector<uint8_t> levels(1);
  EXPECT_EQ(guni_bidi_levels(nullptr, 1, GUNI_BIDI_LTR, nullptr, levels.data(),
                1, nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_bidi_levels(text.data(), 1, GUNI_BIDI_LTR, nullptr, nullptr, 1,
                nullptr),
      GUNI_ERR_INVALID);
  /* A buffer shorter than the text is refused rather than partly filled: the
   * output contract has no partial answer for this, because a level array with
   * a hole in it is worse than no answer. */
  EXPECT_EQ(guni_bidi_levels(text.data(), 1, GUNI_BIDI_LTR, nullptr,
                levels.data(), 0, nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_bidi_reorder(levels.data(), 1, nullptr, 1), GUNI_ERR_INVALID);
  std::vector<size_t> order(1);
  EXPECT_EQ(guni_bidi_reorder(nullptr, 1, order.data(), 1), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_bidi_reorder(levels.data(), 1, order.data(), 0),
      GUNI_ERR_INVALID);
  size_t count = 0;
  EXPECT_EQ(guni_bidi_levels_utf8("a", 1, GUNI_INVALID_REFUSE, GUNI_BIDI_LTR,
                nullptr, levels.data(), 1, nullptr, nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_bidi_levels_utf8(nullptr, 1, GUNI_INVALID_REFUSE,
                GUNI_BIDI_LTR, nullptr, levels.data(), 1, &count, nullptr),
      GUNI_ERR_INVALID);

  /* An empty paragraph has a level and no characters. */
  uint8_t paragraph = 9;
  EXPECT_EQ(guni_bidi_levels(nullptr, 0, GUNI_BIDI_RTL, nullptr, nullptr, 0,
                &paragraph),
      GUNI_OK);
  EXPECT_EQ(paragraph, 1);
  EXPECT_EQ(guni_bidi_levels_with_allocator(nullptr, 0, GUNI_BIDI_AUTO, nullptr,
                nullptr, 0, &paragraph, nullptr),
      GUNI_OK);
  EXPECT_EQ(paragraph, 0);

  /* max_text_bytes applies here too, counted as four bytes per codepoint. */
  GUNI_Limits limits;
  guni_limits_default(&limits);
  limits.max_text_bytes = 3;
  EXPECT_EQ(guni_bidi_levels(text.data(), 1, GUNI_BIDI_LTR, &limits,
                levels.data(), 1, nullptr),
      GUNI_ERR_LIMIT);
}


TEST(Bidirectional, TheUtf8VariantRefusesMoreCharactersThanItsBuffer) {
  /* The UTF-8 entry point decodes into a fixed buffer, so it has the stack
   * variant's length bound and says so rather than truncating. A caller with
   * more text than that uses the codepoint entry point and the allocating
   * resolver, which is what the conformance runner does. */
  std::string text;
  for (size_t index = 0; index <= GUNI_BIDI_MAX_STACK_LENGTH; ++index) {
    text += 'a';
  }
  std::vector<uint8_t> levels(text.size() + 1);
  size_t count = 0;
  EXPECT_EQ(guni_bidi_levels_utf8(text.data(), text.size(),
                GUNI_INVALID_REFUSE, GUNI_BIDI_LTR, nullptr, levels.data(),
                levels.size(), &count, nullptr),
      GUNI_ERR_LIMIT);
}

TEST(Bidirectional, TheAllocatingVariantChecksItsArgumentsToo) {
  /* Both entry points share one argument check, and this is the test that the
   * allocating one actually calls it: a version that only checked in the
   * stack variant would be a way round every bound in the library. */
  const std::vector<uint32_t> text = {'a'};
  std::vector<uint8_t> levels(1);
  EXPECT_EQ(guni_bidi_levels_with_allocator(nullptr, 1, GUNI_BIDI_LTR, nullptr,
                levels.data(), 1, nullptr, nullptr),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_bidi_levels_with_allocator(text.data(), 1, GUNI_BIDI_LTR,
                nullptr, levels.data(), 0, nullptr, nullptr),
      GUNI_ERR_INVALID);
  GUNI_Limits limits;
  guni_limits_default(&limits);
  limits.max_bidi_depth = 300;
  EXPECT_EQ(guni_bidi_levels_with_allocator(text.data(), 1, GUNI_BIDI_LTR,
                &limits, levels.data(), 1, nullptr, nullptr),
      GUNI_ERR_LIMIT);
}

TEST(Bidirectional, APdiAtALevelRunBoundaryIsNotASequenceStart) {
  /* A PDI that continues an isolating run sequence must not start one of its
   * own, and it reaches that decision only when it happens to begin a level
   * run - which takes an embedding inside the isolate. The levels either side
   * of the inner run have to come out the same, because the PDI belongs to the
   * outer sequence. */
  const std::vector<uint32_t> text =
      {0x2066, 0x202B, kAlef, 0x202C, 0x2069, 'x'}; /* LRI RLE alef PDF PDI x */
  std::vector<uint8_t> levels(text.size());
  ASSERT_EQ(guni_bidi_levels(text.data(), text.size(), GUNI_BIDI_LTR, nullptr,
                levels.data(), levels.size(), nullptr),
      GUNI_OK);
  EXPECT_EQ(levels[0], 0) << "the isolate initiator is at the paragraph level";
  EXPECT_EQ(levels[4], 0) << "and so is its matching PDI";
  EXPECT_EQ(levels[5], 0);
  EXPECT_EQ(levels[2], 3) << "the Hebrew inside the embedding";
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
