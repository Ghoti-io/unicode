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
 * UTF-8, which is this library's primary encoding: every algorithm takes a
 * `const char *` and reports byte offsets into it.
 *
 * There is no UTF-16 entry point anywhere in this library
 * (documentation/design.md section 4.1, M3). A caller holding UTF-16
 * converts once at the edge with cutil's `utf.h`; carrying a second copy of
 * every function is what ICU does and what makes every UTF-8 program pay for
 * two conversions per call.
 *
 * What invalid input does is the caller's choice on every entry point, and
 * the choice that is zero is to refuse (section 4.3).
 */

#ifndef GHOTI_IO_GUNI_UTF_H
#define GHOTI_IO_GUNI_UTF_H

#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/macros.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The most bytes one codepoint takes in UTF-8.
#define GUNI_UTF8_MAX_LENGTH 4

/// U+FFFD REPLACEMENT CHARACTER, what GUNI_INVALID_REPLACE substitutes.
#define GUNI_REPLACEMENT_CHARACTER UINT32_C(0xFFFD)

/**
 * @brief What an entry point does with a byte sequence that is not UTF-8.
 *
 * Zero refuses, following chron section 3.7: leniency is a word the caller
 * has to write. A decoder that replaces silently is the one every consumer
 * eventually finds out about (design.md section 2, M10).
 */
typedef enum {
  /**
   * Stop and return GUNI_ERR_INVALID, reporting the byte offset of the
   * first ill-formed byte.
   */
  GUNI_INVALID_REFUSE = 0,
  /**
   * Substitute one U+FFFD per **maximal subpart**, per the Unicode Standard
   * section 3.9's recommended practice: `\xE1\x80` followed by `A` yields one
   * U+FFFD and then `A`, not two U+FFFDs. Implementations in the field
   * disagree about this; this one follows the Standard, which is also what
   * WHATWG and Python do.
   */
  GUNI_INVALID_REPLACE,
  /**
   * Drop the ill-formed bytes. For diagnostics: dropping changes the length
   * of the text, so byte offsets from a skipping pass do not index the
   * input.
   */
  GUNI_INVALID_SKIP
} GUNI_Invalid;

/**
 * @brief Is this a codepoint at all?
 *
 * False above U+10FFFF and for the surrogate range, which UTF-8 cannot
 * encode. True for noncharacters and unassigned codepoints, which are
 * codepoints.
 */
GUNI_API bool guni_is_codepoint(uint32_t cp);

/**
 * @brief Is this one of the 66 noncharacters?
 *
 * U+FDD0..U+FDEF and the last two of every plane. They are valid in UTF-8 and
 * are for internal use; this library never rejects one.
 */
GUNI_API bool guni_is_noncharacter(uint32_t cp);

/// @brief Is this a UTF-16 surrogate codepoint (U+D800..U+DFFF)?
GUNI_API bool guni_is_surrogate(uint32_t cp);

/**
 * @brief Bytes @p cp takes in UTF-8, or 0 if it is not a codepoint.
 */
GUNI_API size_t guni_utf8_length(uint32_t cp);

/**
 * @brief Encode one codepoint.
 *
 * @param cp The codepoint. Surrogates and values above U+10FFFF are refused
 *        rather than encoded: CESU-8 and WTF-8 are conversions a caller does
 *        at the edge, not something this library emits.
 * @param out Receives between 1 and GUNI_UTF8_MAX_LENGTH bytes.
 * @return The number of bytes written, or 0 if @p cp is not a codepoint.
 */
GUNI_API size_t guni_utf8_encode(uint32_t cp, char * out);

/**
 * @brief Decode one codepoint.
 *
 * Defined for every input, including a truncated sequence at the end of the
 * buffer and a zero length.
 *
 * @param text The bytes. May be NULL only when @p len is 0.
 * @param len How many bytes are readable. Never reads past it.
 * @param cp_out Receives the codepoint, or GUNI_REPLACEMENT_CHARACTER when
 *        the bytes are ill-formed. Required.
 * @param valid_out Receives false when the bytes were ill-formed. May be NULL.
 * @return The bytes consumed: the length of the sequence when it is
 *         well-formed, and the length of the **maximal subpart** when it is
 *         not, which is at least 1 whenever @p len is not 0. 0 only when
 *         @p len is 0.
 */
GUNI_API size_t guni_utf8_decode(const char * text, size_t len,
    uint32_t * cp_out, bool * valid_out);

/**
 * @brief Step backwards to the start of the codepoint before @p pos.
 *
 * @param text The bytes.
 * @param pos The offset to step back from.
 * @return The offset of the previous codepoint's first byte, or @p pos when
 *         @p pos is 0. Ill-formed bytes step back one byte at a time, so a
 *         loop over this terminates on any input.
 */
GUNI_API size_t guni_utf8_prev(const char * text, size_t pos);

/**
 * @brief Is every byte of @p text well-formed UTF-8?
 *
 * @param text The bytes. May be NULL only when @p len is 0.
 * @param len How many bytes to check.
 * @param error_offset Receives the offset of the first ill-formed byte when
 *        the answer is GUNI_ERR_INVALID. May be NULL.
 * @return GUNI_OK, or GUNI_ERR_INVALID.
 */
GUNI_API GUNI_Result guni_utf8_validate(const char * text, size_t len,
    size_t * error_offset);

/**
 * @brief Count the codepoints in @p text.
 *
 * @param text The bytes.
 * @param len How many bytes.
 * @param policy What to do with ill-formed bytes. Under REFUSE this returns
 *        GUNI_ERR_INVALID; under REPLACE each maximal subpart counts as one;
 *        under SKIP they count as none.
 * @param count_out Receives the count. Required.
 * @return GUNI_OK, GUNI_ERR_INVALID.
 */
GUNI_API GUNI_Result guni_utf8_count(const char * text, size_t len,
    GUNI_Invalid policy, size_t * count_out);

/**
 * @brief Decode @p text into codepoints.
 *
 * The output contract of design.md section 4.5: writes what fits, reports
 * what is needed, and returns GUNI_ERR_LIMIT with @p out_len set to the
 * requirement when @p cap is too small. The buffer's contents are
 * unspecified on any failure.
 *
 * @param text The bytes.
 * @param len How many bytes.
 * @param policy What to do with ill-formed bytes.
 * @param out Receives the codepoints. May be NULL when @p cap is 0, which is
 *        how a caller asks for the length alone.
 * @param cap How many codepoints @p out holds.
 * @param out_len Receives the number of codepoints the text has. Required.
 * @return GUNI_OK, GUNI_ERR_LIMIT, GUNI_ERR_INVALID.
 */
GUNI_API GUNI_Result guni_utf8_to_codepoints(const char * text, size_t len,
    GUNI_Invalid policy, uint32_t * out, size_t cap, size_t * out_len);

/**
 * @brief Encode codepoints into UTF-8.
 *
 * @param text The codepoints.
 * @param len How many.
 * @param policy What to do with a value that is not a codepoint. REFUSE
 *        returns GUNI_ERR_INVALID; REPLACE writes U+FFFD; SKIP writes
 *        nothing for it.
 * @param out Receives the bytes. May be NULL when @p cap is 0.
 * @param cap How many bytes @p out holds.
 * @param out_len Receives the number of bytes the encoding needs. Required.
 * @return GUNI_OK, GUNI_ERR_LIMIT, GUNI_ERR_INVALID.
 */
GUNI_API GUNI_Result guni_utf8_from_codepoints(const uint32_t * text,
    size_t len, GUNI_Invalid policy, char * out, size_t cap,
    size_t * out_len);

/**
 * @brief A cursor over the codepoints of a UTF-8 buffer.
 *
 * Caller-owned, no allocation, no hidden state: two iterators over one
 * buffer do not interact (design.md section 13.3).
 */
typedef struct {
  const char * text;    ///< The buffer. Not owned.
  size_t length;        ///< Its length in bytes.
  size_t position;      ///< The next byte to read.
  GUNI_Invalid policy;  ///< What ill-formed bytes do.
} GUNI_Utf8Iter;

/**
 * @brief Point an iterator at a buffer.
 *
 * @param iter The iterator. Required.
 * @param text The bytes. Borrowed, and must outlive @p iter.
 * @param len How many bytes.
 * @param policy What ill-formed bytes do.
 */
GUNI_API void guni_utf8_iter_init(GUNI_Utf8Iter * iter, const char * text,
    size_t len, GUNI_Invalid policy);

/**
 * @brief Take the next codepoint.
 *
 * @param iter The iterator.
 * @param cp_out Receives the codepoint. Required.
 * @param offset_out Receives the byte offset the codepoint started at. May be
 *        NULL.
 * @param result_out Receives GUNI_ERR_INVALID when the policy is REFUSE and
 *        the bytes are ill-formed; GUNI_OK otherwise. May be NULL.
 * @return true when a codepoint was produced, false at the end of the buffer
 *         or on a refusal.
 */
GUNI_API bool guni_utf8_iter_next(GUNI_Utf8Iter * iter, uint32_t * cp_out,
    size_t * offset_out, GUNI_Result * result_out);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GUNI_UTF_H
