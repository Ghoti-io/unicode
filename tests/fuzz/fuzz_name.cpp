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
 * The name lookup on bytes nobody chose.
 *
 * A name lookup takes a caller's string, which means it takes anything, and
 * the loose matching writes into a fixed buffer - so this is the harness that
 * covers the one place in the library where a caller's length drives a copy.
 * What is asserted:
 *
 *   * **the lookup either resolves or refuses**, and never reports success
 *     with a codepoint it did not set;
 *   * **a resolved name resolves to something that has that name**, which
 *     closes the loop between the two directions;
 *   * **the buffer contract holds**: the length reported when a buffer is too
 *     small is the length that works;
 *   * **aliases are bounded**: the count and the enumeration agree, so a
 *     caller's loop terminates.
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <ghoti.io/unicode/name.h>
#include <ghoti.io/unicode/unicode.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }
  const char * text = reinterpret_cast<const char *>(data + 1);
  const size_t length = size - 1;

  /* The reverse lookup, on whatever the bytes are. */
  uint32_t cp = 0xFFFFFFFFu;
  GUNI_Result result = guni_codepoint_by_name(text, length, &cp);
  if (result == GUNI_OK) {
    if (cp == 0xFFFFFFFFu) {
      std::abort(); /* success without setting the answer */
    }
    /* Whatever it resolved to must have that name, as its own name or as one
     * of its aliases. Names are the one place the two directions can disagree
     * without either looking wrong on its own. */
    char buffer[GUNI_NAME_MAX_LENGTH + 1];
    size_t named = 0;
    bool matched = false;
    if (guni_name(cp, buffer, sizeof(buffer), &named) == GUNI_OK) {
      uint32_t back = 0;
      if (guni_codepoint_by_name(buffer, named, &back) != GUNI_OK || back != cp) {
        std::abort();
      }
      matched = true;
    }
    for (size_t index = 0; index < guni_name_alias_count(cp); ++index) {
      size_t alias_length = 0;
      if (guni_name_alias(cp, index, nullptr, buffer, sizeof(buffer),
              &alias_length)
          != GUNI_OK) {
        std::abort();
      }
      uint32_t back = 0;
      if (guni_codepoint_by_name(buffer, alias_length, &back) != GUNI_OK
          || back != cp) {
        std::abort();
      }
      matched = true;
    }
    if (!matched) {
      std::abort(); /* resolved to a codepoint with no name and no alias */
    }
  }
  else if (result != GUNI_ERR_INVALID) {
    std::abort();
  }

  /* The named sequences, over the same bytes. */
  uint32_t sequence[GUNI_SEQUENCE_MAX_LENGTH];
  size_t sequence_length = 0;
  result = guni_named_sequence(text, length, sequence, GUNI_SEQUENCE_MAX_LENGTH,
      &sequence_length);
  if (result == GUNI_OK) {
    if (sequence_length < 2 || sequence_length > GUNI_SEQUENCE_MAX_LENGTH) {
      std::abort(); /* a named sequence is two to four codepoints */
    }
    for (size_t index = 0; index < sequence_length; ++index) {
      if (!guni_is_codepoint(sequence[index])) {
        std::abort();
      }
    }
  }
  else if (result != GUNI_ERR_INVALID && result != GUNI_ERR_LIMIT) {
    std::abort();
  }

  /* The forward direction, on a codepoint the input chooses, and the buffer
   * contract: the length reported for a buffer that is too small is the length
   * that works. */
  uint32_t chosen = 0;
  for (size_t index = 0; index < length && index < 4; ++index) {
    chosen = (chosen << 8) | (uint8_t)text[index];
  }
  chosen %= 0x110000u;
  size_t needed = 0;
  if (guni_name(chosen, nullptr, 0, &needed) == GUNI_ERR_LIMIT) {
    if (needed == 0 || needed > GUNI_NAME_MAX_LENGTH) {
      std::abort();
    }
    std::vector<char> exact(needed + 1);
    size_t again = 0;
    if (guni_name(chosen, exact.data(), exact.size(), &again) != GUNI_OK
        || again != needed || std::strlen(exact.data()) != needed) {
      std::abort();
    }
    /* One byte short must fail rather than truncate. */
    std::vector<char> tight(needed);
    size_t short_length = 0;
    if (guni_name(chosen, tight.data(), tight.size(), &short_length)
        != GUNI_ERR_LIMIT) {
      std::abort();
    }
    uint32_t back = 0;
    if (guni_codepoint_by_name(exact.data(), needed, &back) != GUNI_OK
        || back != chosen) {
      std::abort();
    }
  }
  return 0;
}
