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
 * UTF-8 decoding and encoding.
 *
 * The decoder follows the Unicode Standard's Table 3-7 exactly rather than
 * the "shift and mask" shape, because the shortcuts are where the bugs are:
 * Table 3-7 makes the second byte's legal range depend on the first byte, and
 * a decoder that accepts 80..BF for every leader accepts overlong forms,
 * surrogates encoded in three bytes, and codepoints above U+10FFFF. Those
 * three are the classic UTF-8 security bugs and they are all the same bug.
 *
 * On ill-formed input the decoder consumes a **maximal subpart** (Standard
 * section 3.9): the longest prefix of the bytes that could still become a
 * well-formed sequence. That is what makes one U+FFFD per error rather than
 * one per byte, and it is a decision two libraries in the field make
 * differently.
 */

#include <ghoti.io/unicode/utf.h>
#include <string.h>

bool guni_is_surrogate(uint32_t cp) {
  return cp >= UINT32_C(0xD800) && cp <= UINT32_C(0xDFFF);
}

bool guni_is_codepoint(uint32_t cp) {
  return cp <= GUNI_MAX_CODEPOINT && !guni_is_surrogate(cp);
}

bool guni_is_noncharacter(uint32_t cp) {
  if (cp >= UINT32_C(0xFDD0) && cp <= UINT32_C(0xFDEF)) {
    return true;
  }
  return cp <= GUNI_MAX_CODEPOINT && (cp & UINT32_C(0xFFFE)) == UINT32_C(0xFFFE);
}

size_t guni_utf8_length(uint32_t cp) {
  if (!guni_is_codepoint(cp)) {
    return 0;
  }
  if (cp < UINT32_C(0x80)) {
    return 1;
  }
  if (cp < UINT32_C(0x800)) {
    return 2;
  }
  if (cp < UINT32_C(0x10000)) {
    return 3;
  }
  return 4;
}

size_t guni_utf8_encode(uint32_t cp, char * out) {
  size_t length = guni_utf8_length(cp);
  if (length == 0 || out == NULL) {
    return 0;
  }
  switch (length) {
    case 1:
      out[0] = (char)cp;
      break;
    case 2:
      out[0] = (char)(0xC0u | (cp >> 6));
      out[1] = (char)(0x80u | (cp & 0x3Fu));
      break;
    case 3:
      out[0] = (char)(0xE0u | (cp >> 12));
      out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
      out[2] = (char)(0x80u | (cp & 0x3Fu));
      break;
    default:
      out[0] = (char)(0xF0u | (cp >> 18));
      out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
      out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
      out[3] = (char)(0x80u | (cp & 0x3Fu));
      break;
  }
  return length;
}

/**
 * The legal range of the second byte, given the first. The third and fourth
 * bytes are always 80..BF. Table 3-7 of the Standard, as a table rather than
 * as a chain of conditions, because the chain is what gets one arm wrong.
 *
 * A leader of 0xC0, 0xC1 or 0xF5..0xFF has no legal second byte: C0 and C1
 * can only begin an overlong two-byte form, and F5 and above can only begin a
 * codepoint past U+10FFFF.
 */
static void second_byte_range(unsigned char lead, unsigned char * low,
    unsigned char * high) {
  switch (lead) {
    case 0xE0: *low = 0xA0; *high = 0xBF; break;
    case 0xED: *low = 0x80; *high = 0x9F; break;
    case 0xF0: *low = 0x90; *high = 0xBF; break;
    case 0xF4: *low = 0x80; *high = 0x8F; break;
    default:   *low = 0x80; *high = 0xBF; break;
  }
}

/** How many bytes a leader announces, or 0 if it is not a leader. */
static size_t sequence_length(unsigned char lead) {
  if (lead < 0x80) {
    return 1;
  }
  if (lead < 0xC2) {
    return 0; /* a continuation byte, or an overlong two-byte leader */
  }
  if (lead < 0xE0) {
    return 2;
  }
  if (lead < 0xF0) {
    return 3;
  }
  if (lead < 0xF5) {
    return 4;
  }
  return 0;
}

size_t guni_utf8_decode(const char * text, size_t len, uint32_t * cp_out,
    bool * valid_out) {
  if (valid_out != NULL) {
    *valid_out = false;
  }
  if (cp_out != NULL) {
    *cp_out = GUNI_REPLACEMENT_CHARACTER;
  }
  if (text == NULL || len == 0) {
    return 0;
  }

  const unsigned char * bytes = (const unsigned char *)text;
  size_t expected = sequence_length(bytes[0]);
  if (expected == 0) {
    return 1; /* Not a leader at all: the maximal subpart is one byte. */
  }
  if (expected == 1) {
    if (cp_out != NULL) {
      *cp_out = bytes[0];
    }
    if (valid_out != NULL) {
      *valid_out = true;
    }
    return 1;
  }

  /* Walk the continuation bytes, stopping at the first that is not legal.
   * Whatever was consumed up to that point is the maximal subpart: it is a
   * prefix of a well-formed sequence, and no longer prefix is. */
  unsigned char low;
  unsigned char high;
  second_byte_range(bytes[0], &low, &high);
  size_t have = 1;
  while (have < expected && have < len) {
    unsigned char byte = bytes[have];
    unsigned char lo = (have == 1) ? low : 0x80;
    unsigned char hi = (have == 1) ? high : 0xBF;
    if (byte < lo || byte > hi) {
      break;
    }
    ++have;
  }
  if (have < expected) {
    /* Truncated at the end of the buffer, or stopped at an illegal byte.
     * Either way the subpart is what was consumed, and it is at least 1. */
    return have;
  }

  uint32_t cp;
  switch (expected) {
    case 2:
      cp = ((uint32_t)(bytes[0] & 0x1Fu) << 6) | (uint32_t)(bytes[1] & 0x3Fu);
      break;
    case 3:
      cp = ((uint32_t)(bytes[0] & 0x0Fu) << 12)
          | ((uint32_t)(bytes[1] & 0x3Fu) << 6)
          | (uint32_t)(bytes[2] & 0x3Fu);
      break;
    default:
      cp = ((uint32_t)(bytes[0] & 0x07u) << 18)
          | ((uint32_t)(bytes[1] & 0x3Fu) << 12)
          | ((uint32_t)(bytes[2] & 0x3Fu) << 6)
          | (uint32_t)(bytes[3] & 0x3Fu);
      break;
  }
  if (cp_out != NULL) {
    *cp_out = cp;
  }
  if (valid_out != NULL) {
    *valid_out = true;
  }
  return expected;
}

size_t guni_utf8_prev(const char * text, size_t pos) {
  if (text == NULL || pos == 0) {
    return 0;
  }
  const unsigned char * bytes = (const unsigned char *)text;
  size_t start = pos - 1;
  /* At most three continuation bytes precede a leader, so the scan is
   * bounded whatever the bytes are: a run of 0x80 does not walk to 0. */
  size_t limit = (pos > GUNI_UTF8_MAX_LENGTH) ? pos - GUNI_UTF8_MAX_LENGTH : 0;
  while (start > limit && (bytes[start] & 0xC0u) == 0x80u) {
    --start;
  }
  /* Only step back to a leader if what is there really decodes to reach
   * pos; otherwise the bytes are ill-formed and one byte is the answer,
   * which is what keeps a caller's loop making progress. */
  uint32_t cp;
  bool valid = false;
  size_t used = guni_utf8_decode(text + start, pos - start, &cp, &valid);
  if (valid && start + used == pos) {
    return start;
  }
  return pos - 1;
}

GUNI_Result guni_utf8_validate(const char * text, size_t len,
    size_t * error_offset) {
  size_t offset = 0;
  while (offset < len) {
    bool valid = false;
    size_t used = guni_utf8_decode(text + offset, len - offset, NULL, &valid);
    if (!valid) {
      if (error_offset != NULL) {
        *error_offset = offset;
      }
      return GUNI_ERR_INVALID;
    }
    offset += used;
  }
  return GUNI_OK;
}

GUNI_Result guni_utf8_count(const char * text, size_t len,
    GUNI_Invalid policy, size_t * count_out) {
  if (count_out == NULL) {
    return GUNI_ERR_INVALID;
  }
  *count_out = 0;
  size_t offset = 0;
  size_t count = 0;
  while (offset < len) {
    bool valid = false;
    size_t used = guni_utf8_decode(text + offset, len - offset, NULL, &valid);
    if (!valid) {
      if (policy == GUNI_INVALID_REFUSE) {
        return GUNI_ERR_INVALID;
      }
      if (policy == GUNI_INVALID_REPLACE) {
        ++count;
      }
    }
    else {
      ++count;
    }
    offset += used;
  }
  *count_out = count;
  return GUNI_OK;
}

GUNI_Result guni_utf8_to_codepoints(const char * text, size_t len,
    GUNI_Invalid policy, uint32_t * out, size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  size_t offset = 0;
  size_t written = 0;
  bool overflow = false;
  while (offset < len) {
    uint32_t cp = 0;
    bool valid = false;
    size_t used = guni_utf8_decode(text + offset, len - offset, &cp, &valid);
    offset += used;
    if (!valid) {
      if (policy == GUNI_INVALID_REFUSE) {
        return GUNI_ERR_INVALID;
      }
      if (policy == GUNI_INVALID_SKIP) {
        continue;
      }
    }
    if (written < cap) {
      out[written] = cp;
    }
    else {
      overflow = true;
    }
    ++written;
  }
  *out_len = written;
  return overflow ? GUNI_ERR_LIMIT : GUNI_OK;
}

GUNI_Result guni_utf8_from_codepoints(const uint32_t * text, size_t len,
    GUNI_Invalid policy, char * out, size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0) || (text == NULL && len != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  size_t written = 0;
  bool overflow = false;
  for (size_t index = 0; index < len; ++index) {
    uint32_t cp = text[index];
    if (!guni_is_codepoint(cp)) {
      if (policy == GUNI_INVALID_REFUSE) {
        return GUNI_ERR_INVALID;
      }
      if (policy == GUNI_INVALID_SKIP) {
        continue;
      }
      cp = GUNI_REPLACEMENT_CHARACTER;
    }
    char buffer[GUNI_UTF8_MAX_LENGTH];
    size_t used = guni_utf8_encode(cp, buffer);
    if (written + used <= cap) {
      memcpy(out + written, buffer, used);
    }
    else {
      overflow = true;
    }
    written += used;
  }
  *out_len = written;
  return overflow ? GUNI_ERR_LIMIT : GUNI_OK;
}

void guni_utf8_iter_init(GUNI_Utf8Iter * iter, const char * text, size_t len,
    GUNI_Invalid policy) {
  if (iter == NULL) {
    return;
  }
  iter->text = text;
  iter->length = (text == NULL) ? 0 : len;
  iter->position = 0;
  iter->policy = policy;
}

bool guni_utf8_iter_next(GUNI_Utf8Iter * iter, uint32_t * cp_out,
    size_t * offset_out, GUNI_Result * result_out) {
  if (result_out != NULL) {
    *result_out = GUNI_OK;
  }
  if (iter == NULL || cp_out == NULL) {
    if (result_out != NULL) {
      *result_out = GUNI_ERR_INVALID;
    }
    return false;
  }
  while (iter->position < iter->length) {
    uint32_t cp = 0;
    bool valid = false;
    size_t offset = iter->position;
    size_t used = guni_utf8_decode(iter->text + offset, iter->length - offset,
        &cp, &valid);
    iter->position += used;
    if (!valid) {
      if (iter->policy == GUNI_INVALID_REFUSE) {
        if (result_out != NULL) {
          *result_out = GUNI_ERR_INVALID;
        }
        if (offset_out != NULL) {
          *offset_out = offset;
        }
        return false;
      }
      if (iter->policy == GUNI_INVALID_SKIP) {
        continue;
      }
    }
    *cp_out = cp;
    if (offset_out != NULL) {
      *offset_out = offset;
    }
    return true;
  }
  return false;
}
