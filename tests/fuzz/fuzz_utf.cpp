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
 * The UTF-8 decoder under every policy, on bytes nobody chose.
 *
 * The input is the bytes; the first byte selects the policy, per
 * CONVENTIONS.md section 7, so one corpus exercises all three arms. What is
 * asserted is not "it did not crash" - ASan says that - but the invariants a
 * caller relies on and that a fixture would have to be written to catch:
 *
 *   * the decoder always makes progress, so no caller's loop can hang;
 *   * it never reads past the length it was given, which ASan checks because
 *     the buffer is the fuzzer's and is exactly as long as it says;
 *   * decode and encode are inverse on everything that decodes;
 *   * under REPLACE the codepoint count is the byte count at most, and
 *     re-encoding then re-decoding is stable - one pass of replacement, not
 *     a decoder that keeps finding new errors in its own output;
 *   * the iterator and the bulk conversion agree exactly, because they are
 *     two entry points over one decoder and a caller may mix them.
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <ghoti.io/unicode/unicode.h>

namespace {

void check_progress_and_bounds(const char * text, size_t length,
    GUNI_Invalid policy) {
  size_t offset = 0;
  size_t steps = 0;
  while (offset < length) {
    uint32_t cp = 0;
    bool valid = false;
    size_t used = guni_utf8_decode(text + offset, length - offset, &cp, &valid);
    if (used == 0) {
      std::abort(); /* would be an infinite loop in every caller */
    }
    if (offset + used > length) {
      std::abort(); /* consumed bytes it was not given */
    }
    if (valid) {
      if (!guni_is_codepoint(cp)) {
        std::abort(); /* a "valid" surrogate or out-of-range codepoint */
      }
      if (guni_utf8_length(cp) != used) {
        std::abort(); /* the shortest form is the only valid form */
      }
      char again[GUNI_UTF8_MAX_LENGTH];
      size_t written = guni_utf8_encode(cp, again);
      if (written != used || std::memcmp(again, text + offset, used) != 0) {
        std::abort(); /* decode and encode are not inverse */
      }
    }
    else if (cp != GUNI_REPLACEMENT_CHARACTER) {
      std::abort();
    }
    offset += used;
    if (++steps > length) {
      std::abort();
    }
  }
  (void)policy;
}

void check_iterator_matches_bulk(const char * text, size_t length,
    GUNI_Invalid policy) {
  std::vector<uint32_t> bulk;
  size_t needed = 0;
  GUNI_Result result =
      guni_utf8_to_codepoints(text, length, policy, nullptr, 0, &needed);
  if (result == GUNI_ERR_INVALID) {
    /* REFUSE, and the bytes are ill-formed: the iterator must refuse too. */
    GUNI_Utf8Iter iter;
    guni_utf8_iter_init(&iter, text, length, policy);
    uint32_t cp = 0;
    GUNI_Result iterated = GUNI_OK;
    while (guni_utf8_iter_next(&iter, &cp, nullptr, &iterated)) {
      /* keep going until it stops */
    }
    if (iterated != GUNI_ERR_INVALID) {
      std::abort();
    }
    return;
  }
  bulk.resize(needed);
  size_t written = 0;
  if (guni_utf8_to_codepoints(text, length, policy, bulk.data(), needed,
          &written)
      != GUNI_OK) {
    std::abort();
  }
  if (written != needed) {
    std::abort();
  }

  GUNI_Utf8Iter iter;
  guni_utf8_iter_init(&iter, text, length, policy);
  uint32_t cp = 0;
  size_t index = 0;
  GUNI_Result iterated = GUNI_OK;
  while (guni_utf8_iter_next(&iter, &cp, nullptr, &iterated)) {
    if (index >= bulk.size() || bulk[index] != cp) {
      std::abort();
    }
    ++index;
  }
  if (index != bulk.size() || iterated != GUNI_OK) {
    std::abort();
  }

  /* Re-encoding what came out and decoding it again is the identity, and it
   * is where a replacement policy that replaced its own output would show. */
  size_t bytes = 0;
  if (guni_utf8_from_codepoints(bulk.data(), bulk.size(), GUNI_INVALID_REFUSE,
          nullptr, 0, &bytes)
      == GUNI_ERR_INVALID) {
    std::abort(); /* the decoder produced something it cannot encode */
  }
  std::vector<char> encoded(bytes ? bytes : 1);
  if (guni_utf8_from_codepoints(bulk.data(), bulk.size(), GUNI_INVALID_REFUSE,
          encoded.data(), encoded.size(), &bytes)
      != GUNI_OK) {
    std::abort();
  }
  std::vector<uint32_t> round(bulk.size());
  size_t round_len = 0;
  if (guni_utf8_to_codepoints(encoded.data(), bytes, GUNI_INVALID_REFUSE,
          round.data(), round.size(), &round_len)
      != GUNI_OK) {
    std::abort();
  }
  if (round_len != bulk.size()
      || (round_len != 0
          && std::memcmp(round.data(), bulk.data(), round_len * sizeof(uint32_t))
              != 0)) {
    std::abort();
  }
}

void check_backwards(const char * text, size_t length) {
  size_t position = length;
  size_t steps = 0;
  while (position > 0) {
    size_t next = guni_utf8_prev(text, position);
    if (next >= position) {
      std::abort(); /* a loop over this would not terminate */
    }
    position = next;
    if (++steps > length) {
      std::abort();
    }
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size == 0) {
    return 0;
  }
  /* The options byte, per CONVENTIONS.md section 7: one corpus, every arm. */
  const GUNI_Invalid policies[] = {GUNI_INVALID_REFUSE, GUNI_INVALID_REPLACE,
      GUNI_INVALID_SKIP};
  GUNI_Invalid policy = policies[data[0] % 3];
  const char * text = reinterpret_cast<const char *>(data + 1);
  size_t length = size - 1;

  check_progress_and_bounds(text, length, policy);
  check_iterator_matches_bulk(text, length, policy);
  check_backwards(text, length);

  size_t count = 0;
  GUNI_Result counted = guni_utf8_count(text, length, policy, &count);
  if (counted == GUNI_OK && count > length) {
    std::abort(); /* more codepoints than bytes is impossible */
  }
  size_t offset = length + 1;
  if (guni_utf8_validate(text, length, &offset) == GUNI_ERR_INVALID
      && offset >= length) {
    std::abort(); /* an error offset outside the buffer names nothing */
  }
  return 0;
}
