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
 * UAX #9 on paragraphs nobody chose, against its own invariants.
 *
 * The conformance files cover what the levels should be; this covers what is
 * true of them whatever they are, which is what catches the input the
 * Consortium did not think of:
 *
 *   * every level is between the paragraph level and max_depth + 1. A level
 *     below the paragraph level is impossible - the rules only raise - and one
 *     above the bound means the overflow counters leaked;
 *   * the reordering is a permutation: every index once. A bidi implementation
 *     that loses a character loses it here;
 *   * **the stack and the allocator variants agree.** Two code paths through
 *     one algorithm, differing only in where the working arrays come from, so
 *     a difference is a bug in one of them and the differential is free;
 *   * the UTF-8 entry point agrees with the codepoint one;
 *   * a paragraph of any length terminates, which for an algorithm with a
 *     stack, overflow counters and a rule that jumps to a matching PDI is not
 *     obvious.
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <ghoti.io/unicode/unicode.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }
  /* The options byte: the paragraph direction, per CONVENTIONS.md section 7. */
  const GUNI_BidiDirection direction =
      static_cast<GUNI_BidiDirection>(data[0] % 3);
  const char * text = reinterpret_cast<const char *>(data + 1);
  const size_t length = size - 1;

  std::vector<uint32_t> codepoints;
  size_t count = 0;
  if (guni_utf8_count(text, length, GUNI_INVALID_REPLACE, &count) != GUNI_OK) {
    std::abort();
  }
  codepoints.resize(count);
  size_t written = 0;
  if (count != 0
      && (guni_utf8_to_codepoints(text, length, GUNI_INVALID_REPLACE,
              codepoints.data(), codepoints.size(), &written)
              != GUNI_OK
          || written != count)) {
    std::abort();
  }

  std::vector<uint8_t> heap_levels(count);
  uint8_t heap_paragraph = 0;
  if (guni_bidi_levels_with_allocator(codepoints.data(), count, direction,
          nullptr, heap_levels.data(), heap_levels.size(), &heap_paragraph,
          nullptr)
      != GUNI_OK) {
    std::abort(); /* the allocating variant has no length limit to hit */
  }

  for (uint8_t level : heap_levels) {
    if (level < heap_paragraph || level > GUNI_BIDI_MAX_DEPTH + 1) {
      std::abort();
    }
  }

  /* The reordering is a permutation. */
  std::vector<size_t> order(count);
  if (guni_bidi_reorder(heap_levels.data(), heap_levels.size(), order.data(),
          order.size())
      != GUNI_OK) {
    std::abort();
  }
  std::vector<bool> seen(count, false);
  for (size_t index : order) {
    if (index >= count || seen[index]) {
      std::abort();
    }
    seen[index] = true;
  }

  /* An all-left-to-right paragraph reorders to the identity, which is the one
   * answer that can be stated without reimplementing L2. */
  bool all_even = true;
  for (uint8_t level : heap_levels) {
    if (level & 1) {
      all_even = false;
      break;
    }
  }
  if (all_even) {
    for (size_t index = 0; index < count; ++index) {
      if (order[index] != index) {
        std::abort();
      }
    }
  }

  /* The two variants, against each other. */
  if (count <= GUNI_BIDI_MAX_STACK_LENGTH) {
    std::vector<uint8_t> stack_levels(count);
    uint8_t stack_paragraph = 0;
    if (guni_bidi_levels(codepoints.data(), count, direction, nullptr,
            stack_levels.data(), stack_levels.size(), &stack_paragraph)
        != GUNI_OK) {
      std::abort();
    }
    if (stack_paragraph != heap_paragraph || stack_levels != heap_levels) {
      std::abort();
    }
  }

  /* And the UTF-8 entry point against the codepoint one, on the text it can
   * take: it decodes with the same policy, so the characters are the same. */
  if (count <= GUNI_BIDI_MAX_STACK_LENGTH) {
    std::vector<uint8_t> utf8_levels(count ? count : 1);
    size_t utf8_count = 0;
    uint8_t utf8_paragraph = 0;
    GUNI_Result result = guni_bidi_levels_utf8(text, length,
        GUNI_INVALID_REPLACE, direction, nullptr, utf8_levels.data(),
        utf8_levels.size(), &utf8_count, &utf8_paragraph);
    if (result == GUNI_OK) {
      if (utf8_count != count || utf8_paragraph != heap_paragraph) {
        std::abort();
      }
      if (count != 0
          && std::memcmp(utf8_levels.data(), heap_levels.data(), count) != 0) {
        std::abort();
      }
    }
    else if (result != GUNI_ERR_LIMIT) {
      std::abort();
    }
  }
  return 0;
}
