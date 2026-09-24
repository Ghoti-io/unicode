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
 * Case mapping on text nobody chose, against the properties UAX #21
 * guarantees.
 *
 * The options byte selects the tailoring and the Turkic fold. What is
 * asserted:
 *
 *   * **folding is idempotent**, which is what makes it usable as a
 *     normalisation for comparison;
 *   * **guni_case_folded_equal() agrees with folding both sides and
 *     comparing**, which is the whole point of the streaming comparison and
 *     the one thing its cursor could get wrong;
 *   * **the expansion bound holds**: no mapping produces more than
 *     GUNI_CASE_MAX_EXPANSION per character, which is what every caller sizes
 *     a buffer from;
 *   * **the UTF-8 and codepoint entry points agree**, which is where the
 *     UTF-8 path's chunking at word boundaries could go wrong;
 *   * **a fold orbit is an equivalence class**: every member reports the same
 *     orbit.
 *
 * What is deliberately *not* asserted: that folding an upper-cased string
 * equals folding the original. It reads like a law and it is not one - UAX #21
 * gives no such guarantee for the full mappings, and a fuzzer asserting it
 * would be reporting the Standard as a defect.
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <ghoti.io/unicode/unicode.h>

namespace {

std::vector<uint32_t> fold(const std::vector<uint32_t> & text, bool turkic) {
  std::vector<uint32_t> out(text.size() * GUNI_CASE_MAX_EXPANSION + 1);
  size_t written = 0;
  if (guni_case_fold(text.data(), text.size(), turkic, nullptr, out.data(),
          out.size(), &written)
      != GUNI_OK) {
    std::abort(); /* a buffer sized from the constant is always enough */
  }
  if (written > text.size() * GUNI_CASE_MAX_EXPANSION) {
    std::abort();
  }
  out.resize(written);
  return out;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }
  const GUNI_CaseTailoring tailoring =
      static_cast<GUNI_CaseTailoring>(data[0] % 3);
  const bool turkic = (data[0] & 0x80u) != 0;
  const char * text = reinterpret_cast<const char *>(data + 1);
  const size_t length = size - 1;

  std::vector<uint32_t> input;
  size_t count = 0;
  if (guni_utf8_count(text, length, GUNI_INVALID_REPLACE, &count) != GUNI_OK) {
    std::abort();
  }
  input.resize(count);
  size_t decoded = 0;
  if (count != 0
      && guni_utf8_to_codepoints(text, length, GUNI_INVALID_REPLACE,
              input.data(), input.size(), &decoded)
          != GUNI_OK) {
    std::abort();
  }
  input.resize(decoded);

  /* Folding is idempotent, and the comparison agrees with it. */
  const std::vector<uint32_t> once = fold(input, turkic);
  if (fold(once, turkic) != once) {
    std::abort();
  }
  if (!guni_case_folded_equal(input.data(), input.size(), once.data(),
          once.size(), turkic)) {
    std::abort();
  }
  if (!guni_case_folded_equal(input.data(), input.size(), input.data(),
          input.size(), turkic)) {
    std::abort();
  }
  /* And it agrees with folding both sides and comparing, which is the
   * streaming cursor's only job. */
  if (!input.empty()) {
    std::vector<uint32_t> tail(input.begin() + 1, input.end());
    const std::vector<uint32_t> folded_tail = fold(tail, turkic);
    const bool streamed = guni_case_folded_equal(input.data(), input.size(),
        tail.data(), tail.size(), turkic);
    const bool materialised = (once == folded_tail);
    if (streamed != materialised) {
      std::abort();
    }
  }

  /* The three mappings, and their bounds. */
  for (int which = 0; which < 3; ++which) {
    std::vector<uint32_t> out(input.size() * GUNI_CASE_MAX_EXPANSION + 1);
    size_t written = 0;
    GUNI_Result result;
    if (which == 0) {
      result = guni_to_upper(input.data(), input.size(), tailoring, nullptr,
          out.data(), out.size(), &written);
    }
    else if (which == 1) {
      result = guni_to_lower(input.data(), input.size(), tailoring, nullptr,
          out.data(), out.size(), &written);
    }
    else {
      result = guni_to_title(input.data(), input.size(), tailoring, nullptr,
          out.data(), out.size(), &written);
      if (result == GUNI_ERR_LIMIT) {
        continue; /* title-casing refuses text longer than its working state */
      }
    }
    if (result != GUNI_OK) {
      std::abort();
    }
    if (written > input.size() * GUNI_CASE_MAX_EXPANSION) {
      std::abort();
    }
    out.resize(written);

    /* The preflight reports the exact length for case mapping, unlike
     * normalisation's composing forms. */
    size_t needed = 0;
    GUNI_Result preflight;
    if (which == 0) {
      preflight = guni_to_upper(input.data(), input.size(), tailoring, nullptr,
          nullptr, 0, &needed);
    }
    else if (which == 1) {
      preflight = guni_to_lower(input.data(), input.size(), tailoring, nullptr,
          nullptr, 0, &needed);
    }
    else {
      preflight = guni_to_title(input.data(), input.size(), tailoring, nullptr,
          nullptr, 0, &needed);
    }
    if (input.empty()) {
      if (preflight != GUNI_OK || needed != 0) {
        std::abort();
      }
    }
    else if (preflight == GUNI_OK) {
      if (needed != written) {
        std::abort();
      }
    }
    else if (preflight == GUNI_ERR_LIMIT) {
      if (needed != written) {
        std::abort();
      }
    }
    else {
      std::abort();
    }
  }

  /* The UTF-8 entry points, against the codepoint ones. Only comparable when
   * the input is well formed: under replacement the two are different texts. */
  if (guni_utf8_validate(text, length, nullptr) == GUNI_OK) {
    std::vector<char> bytes(length * GUNI_CASE_MAX_EXPANSION
        * GUNI_UTF8_MAX_LENGTH + 4);
    size_t written = 0;
    if (guni_to_upper_utf8(text, length, GUNI_INVALID_REFUSE, tailoring, nullptr,
            bytes.data(), bytes.size(), &written)
        == GUNI_OK) {
      std::vector<uint32_t> upper(input.size() * GUNI_CASE_MAX_EXPANSION + 1);
      size_t count_out = 0;
      if (guni_to_upper(input.data(), input.size(), tailoring, nullptr,
              upper.data(), upper.size(), &count_out)
          != GUNI_OK) {
        std::abort();
      }
      upper.resize(count_out);
      std::vector<char> expected(upper.size() * GUNI_UTF8_MAX_LENGTH + 1);
      size_t expected_length = 0;
      if (guni_utf8_from_codepoints(upper.data(), upper.size(),
              GUNI_INVALID_REPLACE, expected.data(), expected.size(),
              &expected_length)
          != GUNI_OK) {
        std::abort();
      }
      if (written != expected_length
          || std::memcmp(bytes.data(), expected.data(), written) != 0) {
        std::abort();
      }
    }
  }

  /* A fold orbit is an equivalence class: every member reports the same one. */
  for (uint32_t cp : input) {
    uint32_t orbit[8];
    size_t members = 0;
    if (guni_case_orbit(cp, orbit, 8, &members) != GUNI_OK || members == 0) {
      std::abort();
    }
    bool contains_self = false;
    for (size_t index = 0; index < members; ++index) {
      if (orbit[index] == cp) {
        contains_self = true;
      }
      uint32_t again[8];
      size_t again_count = 0;
      if (guni_case_orbit(orbit[index], again, 8, &again_count) != GUNI_OK
          || again_count != members
          || std::memcmp(again, orbit, members * sizeof(uint32_t)) != 0) {
        std::abort();
      }
    }
    if (!contains_self) {
      std::abort();
    }
  }
  return 0;
}
