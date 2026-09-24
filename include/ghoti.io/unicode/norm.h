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
 * UAX #15: the four normalisation forms, the quick checks, canonical
 * ordering, and the Stream-Safe Text Format.
 *
 * NFD is what a shaper needs and is the reason this module is in phase B
 * rather than later: a font that has a base and a mark but not the
 * precomposed character can only draw the text if the text is decomposed.
 * NFC is what `text` has today and what every protocol that says "normalise"
 * means. NFKC and NFKD are what a security check or a search index wants,
 * and are the two with an 18-codepoint expansion.
 *
 * Nothing here allocates. Everything writes into a caller's buffer and
 * reports the length it needs (design.md section 4.5), and the expansion
 * constants in enums.h let a caller size that buffer without a preflight
 * call at all.
 */

#ifndef GHOTI_IO_GUNI_NORM_H
#define GHOTI_IO_GUNI_NORM_H

#include <ghoti.io/unicode/char.h>
#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/enums.h>
#include <ghoti.io/unicode/macros.h>
#include <ghoti.io/unicode/utf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One of UAX #15's four normalisation forms.
 *
 * NFC is zero because it is what a caller who does not know which one they
 * want means, and because it is what the web and every IETF protocol
 * specify.
 */
typedef enum {
  GUNI_NFC = 0,  ///< Canonical decomposition, then canonical composition.
  GUNI_NFD,      ///< Canonical decomposition.
  GUNI_NFKC,     ///< Compatibility decomposition, then canonical composition.
  GUNI_NFKD,     ///< Compatibility decomposition.
  GUNI_NORM_FORM_COUNT
} GUNI_NormForm;

/**
 * @brief The full decomposition of one codepoint.
 *
 * Fully expanded: the result is never itself decomposable, because the
 * generator took the fixed point (design.md section 5.3) and this library
 * therefore never recurses. Hangul syllables are computed rather than looked
 * up, per the Standard's section 3.12.
 *
 * @param cp The codepoint. A value that is not a codepoint decomposes to
 *        itself, like every other function here (section 4.4).
 * @param compatibility Whether to apply compatibility mappings as well:
 *        false is what NFD and NFC use, true is what NFKD and NFKC use.
 * @param out Receives the codepoints. May be NULL when @p cap is 0.
 * @param cap How many @p out holds; GUNI_NORM_MAX_EXPANSION_NFKD is always
 *        enough.
 * @param out_len Receives how many the decomposition has, which is 1 for a
 *        codepoint that does not decompose. Required.
 * @return GUNI_OK, or GUNI_ERR_LIMIT with @p out_len set to the requirement.
 */
GUNI_API GUNI_Result guni_decompose(uint32_t cp, bool compatibility,
    uint32_t * out, size_t cap, size_t * out_len);

/**
 * @brief The primary composite of two codepoints, or 0.
 *
 * The inverse of a canonical decomposition of exactly two codepoints, minus
 * the Full_Composition_Exclusion set - so U+0041 U+0301 composes to U+00C1
 * and U+212B ANGSTROM SIGN is not produced by composing anything, because it
 * is excluded.
 *
 * Hangul is computed: L + V and LV + T compose by arithmetic.
 *
 * @return The composite, or 0 when the pair does not compose. 0 is not a
 *         codepoint that anything composes to, so it is unambiguous.
 */
GUNI_API uint32_t guni_compose(uint32_t first, uint32_t second);

/**
 * @brief The quick-check property of one codepoint, from the UCD.
 *
 * GUNI_QC_MAYBE means the codepoint can compose with what precedes it, so the
 * answer for the text depends on the text. It is never a guess.
 */
GUNI_API GUNI_QuickCheck guni_quick_check(GUNI_NormForm form, uint32_t cp);

/**
 * @brief UAX #15 section 9's quick check over a string.
 *
 * Costs one property lookup per codepoint and no buffer, and answers YES for
 * nearly all real text - which is the point: a caller normalises only what
 * this cannot dismiss.
 *
 * @return GUNI_QC_YES when the text is certainly in @p form, GUNI_QC_NO when
 *         it is certainly not, and GUNI_QC_MAYBE when only normalising can
 *         say. guni_is_normalized() does that.
 */
GUNI_API GUNI_QuickCheck guni_quick_check_text(GUNI_NormForm form,
    const uint32_t * text, size_t len);

/// @brief guni_quick_check_text() over UTF-8. Ill-formed bytes are not
/// normalised, so they answer GUNI_QC_NO rather than being replaced.
GUNI_API GUNI_QuickCheck guni_quick_check_utf8(GUNI_NormForm form,
    const char * text, size_t len);

/**
 * @brief Is @p text already in @p form?
 *
 * The definitive answer, which for text the quick check calls MAYBE means
 * normalising a chunk at a time and comparing. No allocation: the working
 * buffer is a fixed one on the stack and the text is processed between
 * normalisation boundaries.
 *
 * @param form Which form.
 * @param text The codepoints.
 * @param len How many.
 * @param limits May be NULL for the defaults.
 * @param out Receives the answer. Required.
 * @return GUNI_OK, GUNI_ERR_INVALID, or GUNI_ERR_LIMIT when a single
 *         normalisation chunk exceeded the working buffer - see
 *         guni_normalize().
 */
GUNI_API GUNI_Result guni_is_normalized(GUNI_NormForm form,
    const uint32_t * text, size_t len, const GUNI_Limits * limits, bool * out);

/// @brief guni_is_normalized() over UTF-8. Ill-formed bytes answer false.
GUNI_API GUNI_Result guni_is_normalized_utf8(GUNI_NormForm form,
    const char * text, size_t len, const GUNI_Limits * limits, bool * out);

/**
 * @brief Normalise codepoints.
 *
 * @param form Which form.
 * @param text The codepoints. Values that are not codepoints pass through
 *        unchanged: normalisation is defined over codepoints and this
 *        function does not also validate.
 * @param len How many.
 * @param limits May be NULL for the defaults. `max_text_bytes` caps the
 *        input, counted as four bytes per codepoint.
 * @param out Receives the result. May be NULL when @p cap is 0.
 * @param cap How many codepoints @p out holds.
 * @param out_len On success, the exact length of the result. On
 *        GUNI_ERR_LIMIT, a length that is certainly enough - see below.
 * @return GUNI_OK, GUNI_ERR_LIMIT, GUNI_ERR_INVALID.
 *
 * **The composing forms need room for the decomposition.** NFC and NFKC are
 * computed by decomposing, ordering, and then composing in place, because
 * composing before the ordering is finished gets the wrong answer: a mark
 * that sorts before an already-composed one would have to be un-composed.
 * The buffer therefore has to hold the intermediate, which can be longer
 * than the result - and so on GUNI_ERR_LIMIT @p out_len is the intermediate's
 * length, a sufficient size rather than the exact one. A caller that
 * preflights with @p cap 0, allocates what it is told, and asks again gets
 * GUNI_OK and the exact length. A caller that sizes from
 * GUNI_NORM_MAX_EXPANSION_NFKC never preflights at all.
 */
GUNI_API GUNI_Result guni_normalize(GUNI_NormForm form, const uint32_t * text,
    size_t len, const GUNI_Limits * limits, uint32_t * out, size_t cap,
    size_t * out_len);

/**
 * @brief Normalise UTF-8, reporting byte lengths.
 *
 * Processed between **normalisation boundaries** - a starter whose
 * quick-check property is YES, which is a position where the text before and
 * the text after normalise independently - so that the working buffer is a
 * fixed one on the stack rather than proportional to the input. Real text has
 * such a boundary at nearly every character.
 *
 * @param policy What ill-formed bytes do, per utf.h.
 * @return GUNI_OK; GUNI_ERR_LIMIT when @p cap is too small, with @p out_len
 *         set to a sufficient length; GUNI_ERR_INVALID for ill-formed bytes
 *         under GUNI_INVALID_REFUSE. GUNI_ERR_LIMIT also when a single run
 *         between boundaries does not fit the working buffer, which takes
 *         more than a thousand combining marks on one base: no natural text
 *         does that, and guni_stream_safe() is the fix for text that is not
 *         text.
 */
GUNI_API GUNI_Result guni_normalize_utf8(GUNI_NormForm form,
    const char * text, size_t len, GUNI_Invalid policy,
    const GUNI_Limits * limits, char * out, size_t cap, size_t * out_len);

/**
 * @brief Put a sequence into canonical order, in place.
 *
 * UAX #15's Canonical Ordering Algorithm: a stable sort of each maximal run
 * of non-starters by Canonical_Combining_Class. Stable matters - it is what
 * makes the order of two marks with the same class the text's own - and an
 * insertion sort is what the Standard describes, because the runs are short.
 *
 * @param text Modified in place.
 * @param len How many codepoints.
 */
GUNI_API void guni_canonical_order(uint32_t * text, size_t len);

/**
 * @brief Is @p text in the Stream-Safe Text Format?
 *
 * UAX #15 section 13: at most `limits->max_nonstarters` non-starters in a
 * row, 30 by default. Text that is stream-safe can be normalised
 * incrementally in bounded memory, which is why the Standard defines it.
 */
GUNI_API bool guni_is_stream_safe(const uint32_t * text, size_t len,
    const GUNI_Limits * limits);

/**
 * @brief Insert U+034F COMBINING GRAPHEME JOINER to make @p text stream-safe.
 *
 * The transform of UAX #15 section 13. The result is canonically equivalent
 * to the input for every purpose the Standard cares about, and is not equal
 * to it: this changes the text, which is why it is a separate function and
 * not something the normaliser does on its own.
 *
 * @param out May be NULL when @p cap is 0, to learn the length.
 * @param out_len Receives the length the result needs. Required.
 * @return GUNI_OK, GUNI_ERR_LIMIT, GUNI_ERR_INVALID.
 */
GUNI_API GUNI_Result guni_stream_safe(const uint32_t * text, size_t len,
    const GUNI_Limits * limits, uint32_t * out, size_t cap, size_t * out_len);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GUNI_NORM_H
