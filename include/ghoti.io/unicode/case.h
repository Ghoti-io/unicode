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
 * Case mapping: the simple mappings, the full ones, and the conditions.
 *
 * The conditions are the reason this module is not four lookup tables
 * (design.md section 2, M13). `toupper('ß')` returns `ß` in every C library
 * and the answer is `SS`; a final sigma lower-cases differently from a medial
 * one and needs to see the rest of the word; and Turkish `i` is the case every
 * library gets wrong by making it a property of the process locale. So:
 *
 *   * the full mappings take **the whole text and a position**, because
 *     `Final_Sigma` cannot be answered from one codepoint;
 *   * the three language-sensitive rules are an **argument**, not a locale.
 *     They are UCD data - `SpecialCasing.txt` lists them - and what CLDR adds
 *     is only the decision of when to apply them, which is the caller's
 *     (section 8).
 *
 * The simple mappings are here too, and are what a caller wants for a
 * case-insensitive comparison of identifiers, a hash key, or anything where
 * the length must not change. They are a codepoint in and a codepoint out and
 * cannot fail.
 */

#ifndef GHOTI_IO_GUNI_CASE_H
#define GHOTI_IO_GUNI_CASE_H

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

/*
 * GUNI_CaseTailoring is in enums.h, because SpecialCasing.txt names the
 * languages and so the generator emits them: a new one appends a member rather
 * than needing a hand-edit here. Zero is GUNI_LANG_NONE, so a caller who does
 * not know gets the language-neutral answer rather than whatever the
 * environment was set to.
 */

/**
 * @brief Simple uppercase: one codepoint in, one out.
 *
 * The mapping `UnicodeData.txt` gives, which is the identity where there is
 * none - including where the *full* mapping is not the identity. `ß` is the
 * example: its simple uppercase is `ß` and its full uppercase is `SS`, and a
 * caller who must not change the length has to accept the first.
 */
GUNI_API uint32_t guni_to_upper_simple(uint32_t cp);

/// @brief Simple lowercase.
GUNI_API uint32_t guni_to_lower_simple(uint32_t cp);

/// @brief Simple titlecase, which differs from uppercase for the digraphs.
GUNI_API uint32_t guni_to_title_simple(uint32_t cp);

/**
 * @brief Simple case folding: `CaseFolding.txt`'s C and S statuses.
 *
 * What a case-insensitive comparison of equal-length strings uses. Folding is
 * not lower-casing: `ẛ` folds to `ṡ`, and the Greek final and medial sigmas
 * fold together.
 */
GUNI_API uint32_t guni_case_fold_simple(uint32_t cp);

/**
 * @brief Simple case folding with the Turkic tailoring.
 *
 * `CaseFolding.txt`'s T status, which is two codepoints: with it, dotless and
 * dotted `i` fold apart, which is what a Turkish caller wants and what every
 * other caller must not have.
 */
GUNI_API uint32_t guni_case_fold_simple_turkic(uint32_t cp);

/**
 * @brief Every codepoint that case-folds to the same value as @p cp.
 *
 * The **fold orbit**, which is what a case-insensitive character class needs:
 * `[k]` matches `K` and `K` (U+212A KELVIN SIGN) because all three fold to
 * `k`. `regex` builds classes from this.
 *
 * @param cp The codepoint. Always a member of its own orbit.
 * @param out Receives the orbit, sorted. May be NULL when @p cap is 0.
 * @param cap How many @p out holds.
 * @param out_len Receives the orbit's size, which is 1 for a codepoint that
 *        shares its fold with nothing. Required.
 * @return GUNI_OK, or GUNI_ERR_LIMIT with @p out_len set to the requirement.
 */
GUNI_API GUNI_Result guni_case_orbit(uint32_t cp, uint32_t * out, size_t cap,
    size_t * out_len);

/**
 * @brief The full uppercase of the codepoint at @p position.
 *
 * Takes the whole text because the conditions need it: `Final_Sigma` looks
 * both ways, `After_I` and `After_Soft_Dotted` look back, `More_Above` and
 * `Not_Before_Dot` look forward. A function that took one codepoint could not
 * implement them, which is why every library that has one gets them wrong.
 *
 * @param text The text, as codepoints.
 * @param len How many.
 * @param position Which codepoint to map. Must be less than @p len.
 * @param tailoring Which language's rules, if any.
 * @param out Receives up to GUNI_CASE_MAX_EXPANSION codepoints. May be NULL
 *        when @p cap is 0.
 * @param cap How many @p out holds.
 * @param out_len Receives the mapping's length, which is 1 where the mapping
 *        is the identity. Required.
 * @return GUNI_OK, GUNI_ERR_LIMIT, GUNI_ERR_INVALID.
 */
GUNI_API GUNI_Result guni_to_upper_at(const uint32_t * text, size_t len,
    size_t position, GUNI_CaseTailoring tailoring, uint32_t * out, size_t cap,
    size_t * out_len);

/// @brief The full lowercase of the codepoint at @p position. This is the one
/// `Final_Sigma` applies to.
GUNI_API GUNI_Result guni_to_lower_at(const uint32_t * text, size_t len,
    size_t position, GUNI_CaseTailoring tailoring, uint32_t * out, size_t cap,
    size_t * out_len);

/// @brief The full titlecase of the codepoint at @p position.
GUNI_API GUNI_Result guni_to_title_at(const uint32_t * text, size_t len,
    size_t position, GUNI_CaseTailoring tailoring, uint32_t * out, size_t cap,
    size_t * out_len);

/**
 * @brief Upper-case a whole string.
 *
 * The output contract of design.md section 4.5: ask with a cap of 0 to learn
 * the length, or size the buffer as `len * GUNI_CASE_MAX_EXPANSION` and never
 * ask. Unlike normalisation's composing forms, the requirement reported here is
 * exact: case mapping is per character and the length is known before anything
 * is written.
 */
GUNI_API GUNI_Result guni_to_upper(const uint32_t * text, size_t len,
    GUNI_CaseTailoring tailoring, const GUNI_Limits * limits, uint32_t * out,
    size_t cap, size_t * out_len);

/// @brief Lower-case a whole string.
GUNI_API GUNI_Result guni_to_lower(const uint32_t * text, size_t len,
    GUNI_CaseTailoring tailoring, const GUNI_Limits * limits, uint32_t * out,
    size_t cap, size_t * out_len);

/**
 * @brief Title-case a whole string: UAX #21's `toTitlecase`.
 *
 * The first cased character of each word takes its titlecase mapping and the
 * rest take their lowercase. "Each word" is UAX #29's word segmentation, which
 * is break.h's - so this is the one case function that depends on the
 * segmentation module, and it is why "title-case" cannot be done character by
 * character by a caller.
 */
GUNI_API GUNI_Result guni_to_title(const uint32_t * text, size_t len,
    GUNI_CaseTailoring tailoring, const GUNI_Limits * limits, uint32_t * out,
    size_t cap, size_t * out_len);

/**
 * @brief Case-fold a whole string, for a case-insensitive comparison.
 *
 * The full folding: `ß` folds to `ss`, so the length changes and two strings
 * that compare equal after folding may be of different lengths before it.
 *
 * @param turkic Whether to apply `CaseFolding.txt`'s T status, which folds
 *        dotted and dotless `i` apart.
 */
GUNI_API GUNI_Result guni_case_fold(const uint32_t * text, size_t len,
    bool turkic, const GUNI_Limits * limits, uint32_t * out, size_t cap,
    size_t * out_len);

/// @brief guni_to_upper() over UTF-8, reporting byte lengths.
GUNI_API GUNI_Result guni_to_upper_utf8(const char * text, size_t len,
    GUNI_Invalid policy, GUNI_CaseTailoring tailoring,
    const GUNI_Limits * limits, char * out, size_t cap, size_t * out_len);

/// @brief guni_to_lower() over UTF-8.
GUNI_API GUNI_Result guni_to_lower_utf8(const char * text, size_t len,
    GUNI_Invalid policy, GUNI_CaseTailoring tailoring,
    const GUNI_Limits * limits, char * out, size_t cap, size_t * out_len);

/// @brief guni_to_title() over UTF-8.
GUNI_API GUNI_Result guni_to_title_utf8(const char * text, size_t len,
    GUNI_Invalid policy, GUNI_CaseTailoring tailoring,
    const GUNI_Limits * limits, char * out, size_t cap, size_t * out_len);

/// @brief guni_case_fold() over UTF-8.
GUNI_API GUNI_Result guni_case_fold_utf8(const char * text, size_t len,
    GUNI_Invalid policy, bool turkic, const GUNI_Limits * limits, char * out,
    size_t cap, size_t * out_len);

/**
 * @brief Do two strings match under full case folding?
 *
 * The comparison a caller actually wants - "is this the same identifier" -
 * without either side having to allocate: the folding is done a character at a
 * time and compared as it goes, so a difference in the first character costs
 * nothing.
 *
 * Canonical equivalence is **not** applied: `guni_normalize()` first if that
 * matters, which is UAX #21's "canonical caseless match" and is a decision a
 * caller has to make rather than one this function can make for it.
 */
GUNI_API bool guni_case_folded_equal(const uint32_t * left, size_t left_len,
    const uint32_t * right, size_t right_len, bool turkic);

/// @brief guni_case_folded_equal() over UTF-8. Ill-formed bytes never match.
GUNI_API bool guni_case_folded_equal_utf8(const char * left, size_t left_len,
    const char * right, size_t right_len, bool turkic);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GUNI_CASE_H
