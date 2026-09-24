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
 * The four segmentations on text nobody chose, against their own invariants.
 *
 * The options byte selects the kind and the line-break tailoring, so one
 * corpus exercises all twelve combinations. What is asserted is what a
 * conformance file cannot say:
 *
 *   * **boundaries are strictly increasing**, which is what makes a cluster
 *     map monotone and a layout engine's line loop terminate;
 *   * **no boundary falls inside a character**, which is the defect that a
 *     caller mapping clusters to glyphs would believe rather than notice;
 *   * **the iterator and the point query agree** at every position - two
 *     entry points, one rule engine;
 *   * **UTF-8 and codepoints agree**, once the offsets are mapped, which is
 *     the other pair of entry points;
 *   * the ends follow the standards: UAX #29 breaks at both, UAX #14 at the
 *     end only, and an empty text has no boundary at all;
 *   * a provider is asked only about positions strictly inside a run of `SA`
 *     characters, and cannot make the walk stop advancing.
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <ghoti.io/unicode/unicode.h>

namespace {

/** A provider that answers from the input, to exercise the seam. */
struct ProviderState {
  const uint8_t * data;
  size_t size;
  size_t calls;
};

bool provider_answer(void * ctx, const GUNI_BreakText * text, size_t start,
    size_t end, size_t position) {
  ProviderState * state = static_cast<ProviderState *>(ctx);
  ++state->calls;
  if (position <= start || position >= end) {
    std::abort(); /* asked outside the run it was given */
  }
  if (guni_break_text_length(text) < end) {
    std::abort();
  }
  /* The run has to be readable through the accessors, start to end. */
  size_t offset = start;
  uint32_t codepoint = 0;
  size_t next = 0;
  size_t steps = 0;
  while (offset < end) {
    if (!guni_break_text_at(text, offset, &codepoint, &next) || next <= offset) {
      std::abort();
    }
    offset = next;
    if (++steps > end - start) {
      std::abort();
    }
  }
  return state->size != 0
      && (state->data[position % state->size] & 1u) != 0;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }
  GUNI_BreakOptions options;
  std::memset(&options, 0, sizeof(options));
  options.kind = static_cast<GUNI_BreakKind>(data[0] % GUNI_BREAK_KIND_COUNT);
  options.tailoring = static_cast<GUNI_LineBreakTailoring>((data[0] >> 2) % 3);

  ProviderState state = {data, size, 0};
  GUNI_BreakProvider provider = {&state, &provider_answer};
  if ((data[0] & 0x80u) != 0) {
    options.provider = &provider;
  }

  const char * text = reinterpret_cast<const char *>(data + 1);
  const size_t length = size - 1;

  /* Every boundary over the bytes, through the bulk entry point. */
  size_t needed = 0;
  if (guni_break_all(&options, text, length, nullptr, 0, &needed) == GUNI_ERR_INVALID) {
    std::abort();
  }
  std::vector<size_t> found(needed ? needed : 1);
  size_t written = 0;
  if (guni_break_all(&options, text, length, found.data(), found.size(),
          &written)
      != GUNI_OK
      || written != needed) {
    std::abort();
  }
  found.resize(written);

  size_t previous = 0;
  bool first = true;
  for (size_t position : found) {
    if (position > length) {
      std::abort();
    }
    if (!first && position <= previous) {
      std::abort(); /* not strictly increasing */
    }
    if (position > 0 && position < length
        && ((unsigned char)text[position] & 0xC0u) == 0x80u) {
      std::abort(); /* a boundary inside a character */
    }
    /* The point query has to agree with the walk. */
    if (!guni_break_at(&options, text, length, position)) {
      std::abort();
    }
    previous = position;
    first = false;
  }

  /* And every position the walk did *not* report must be a non-boundary, for
   * the positions that are character boundaries at all. */
  size_t offset = 0;
  while (offset <= length) {
    bool reported = false;
    for (size_t position : found) {
      if (position == offset) {
        reported = true;
        break;
      }
    }
    if (guni_break_at(&options, text, length, offset) != reported) {
      std::abort();
    }
    if (offset == length) {
      break;
    }
    uint32_t codepoint = 0;
    bool valid = false;
    size_t used = guni_utf8_decode(text + offset, length - offset, &codepoint,
        &valid);
    if (used == 0) {
      std::abort();
    }
    offset += used;
  }

  if (length != 0) {
    /* The ends. UAX #29 breaks at the start, UAX #14 does not; both break at
     * the end. */
    const bool at_start = guni_break_at(&options, text, length, 0);
    if (at_start != (options.kind != GUNI_BREAK_LINE)) {
      std::abort();
    }
    if (!guni_break_at(&options, text, length, length)) {
      std::abort();
    }
  }
  else if (guni_break_at(&options, text, 0, 0)) {
    std::abort(); /* an empty text has no boundary */
  }

  /* The codepoint entry point, against the byte one. The offsets differ, so
   * the comparison is over the characters they name. */
  std::vector<uint32_t> codepoints;
  size_t count = 0;
  if (guni_utf8_count(text, length, GUNI_INVALID_REPLACE, &count) != GUNI_OK) {
    std::abort();
  }
  codepoints.resize(count ? count : 1);
  size_t decoded = 0;
  if (count != 0
      && guni_utf8_to_codepoints(text, length, GUNI_INVALID_REPLACE,
              codepoints.data(), codepoints.size(), &decoded)
          != GUNI_OK) {
    std::abort();
  }
  codepoints.resize(decoded);
  /* Only comparable when the text is well formed: under replacement the two
   * are different texts, since an ill-formed run becomes one U+FFFD. */
  if (guni_utf8_validate(text, length, nullptr) == GUNI_OK
      && options.provider == nullptr) {
    size_t point_needed = 0;
    if (guni_break_all_codepoints(&options, codepoints.data(),
            codepoints.size(), nullptr, 0, &point_needed)
        == GUNI_ERR_INVALID) {
      std::abort();
    }
    std::vector<size_t> point_found(point_needed ? point_needed : 1);
    size_t point_written = 0;
    if (guni_break_all_codepoints(&options, codepoints.data(),
            codepoints.size(), point_found.data(), point_found.size(),
            &point_written)
        != GUNI_OK) {
      std::abort();
    }
    point_found.resize(point_written);
    if (point_found.size() != found.size()) {
      std::abort();
    }
    /* Map each codepoint index to its byte offset and compare. */
    std::vector<size_t> offsets;
    size_t walk = 0;
    while (walk < length) {
      offsets.push_back(walk);
      uint32_t codepoint = 0;
      bool valid = false;
      walk += guni_utf8_decode(text + walk, length - walk, &codepoint, &valid);
    }
    offsets.push_back(length);
    for (size_t index = 0; index < point_found.size(); ++index) {
      if (point_found[index] >= offsets.size()
          || offsets[point_found[index]] != found[index]) {
        std::abort();
      }
    }
  }
  return 0;
}
