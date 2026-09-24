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
 * UAX #44 character names: **tier 1**.
 *
 * This is the only header in the library that is not tier 0, and the split is
 * about size rather than dependencies (design.md section 3). The name tables
 * are 37,610 lines of generated C - more than half the generated bulk of
 * everything here - and one consumer has ever wanted them: `regex`, for
 * `\N{...}`. Nothing in tier 0 includes this header, and `make check-layering`
 * fails naming the file if that changes, so a consumer of a grapheme iterator
 * does not link the names.
 *
 * What is here: the names, including the **algorithmic** ones a table would be
 * absurd for (`HANGUL SYLLABLE GAG` alone would cost 11,172 rows); the five
 * kinds of name alias, which is where a control character's name comes from
 * because it has none of its own; the named sequences; and the reverse lookup
 * with UAX #44-LM2's loose matching.
 */

#ifndef GHOTI_IO_GUNI_NAME_H
#define GHOTI_IO_GUNI_NAME_H

#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/enums.h>
#include <ghoti.io/unicode/macros.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The five kinds of name alias `NameAliases.txt` defines.
 *
 * A control character has no name of its own - `UnicodeData.txt` gives U+0000
 * as `<control>` - so `NULL` is a `control` alias and `NUL` is an
 * `abbreviation`. A caller that wants something to show a user wants the
 * control alias; a caller parsing `\N{NUL}` wants to accept all five.
 */
typedef enum {
  GUNI_NAME_ALIAS_CORRECTION = 0, ///< A name the Standard got wrong and fixed.
  GUNI_NAME_ALIAS_CONTROL,        ///< A name for a control character.
  GUNI_NAME_ALIAS_ALTERNATE,      ///< A widely used alternative.
  GUNI_NAME_ALIAS_FIGMENT,        ///< A name for a character never encoded so.
  GUNI_NAME_ALIAS_ABBREVIATION    ///< A short form, like NUL or ZWJ.
} GUNI_NameAliasKind;

/**
 * @brief The name of a codepoint.
 *
 * Includes the algorithmic families: a CJK ideograph is its prefix and its
 * codepoint in hex, and a Hangul syllable is spelled from its jamo, both
 * computed rather than looked up.
 *
 * @param cp The codepoint.
 * @param out Receives the name and a NUL. May be NULL when @p cap is 0.
 * @param cap How many bytes @p out holds; GUNI_NAME_MAX_LENGTH + 1 is always
 *        enough.
 * @param out_len Receives the name's length without the NUL. Required.
 * @return GUNI_OK; GUNI_ERR_LIMIT when @p cap is too small, with @p out_len set
 *         to the requirement; GUNI_ERR_INVALID when @p cp has no name at all,
 *         which is every unassigned codepoint, every surrogate, every private-use
 *         codepoint and every control - a control's name is an alias, and
 *         guni_name_alias() is where it is.
 */
GUNI_API GUNI_Result guni_name(uint32_t cp, char * out, size_t cap,
    size_t * out_len);

/// @brief Does @p cp have a name of its own? False for the controls, whose
/// names are aliases.
GUNI_API bool guni_has_name(uint32_t cp);

/// @brief How many name aliases @p cp has.
GUNI_API size_t guni_name_alias_count(uint32_t cp);

/**
 * @brief One of @p cp's name aliases.
 *
 * Aliases come out in the order `NameAliases.txt` lists them, which groups
 * them by kind.
 *
 * @param cp The codepoint.
 * @param index Which alias, from 0.
 * @param kind_out Receives the alias's kind. May be NULL.
 * @param out Receives the name and a NUL. May be NULL when @p cap is 0.
 * @param cap How many bytes @p out holds.
 * @param out_len Receives the length without the NUL. Required.
 * @return GUNI_OK, GUNI_ERR_LIMIT, or GUNI_ERR_INVALID when @p index is past
 *         the end.
 */
GUNI_API GUNI_Result guni_name_alias(uint32_t cp, size_t index,
    GUNI_NameAliasKind * kind_out, char * out, size_t cap, size_t * out_len);

/**
 * @brief The codepoint with this name, matched loosely.
 *
 * UAX #44-LM2: case, whitespace, underscores and **medial** hyphens are
 * ignored. Two subtleties, both of which cost a codepoint if missed:
 *
 *   * the hyphen in U+1180 HANGUL JUNGSEONG O-E is kept, which the Standard
 *     states as its one exception, because U+116C is HANGUL JUNGSEONG OE;
 *   * "medial" means a hyphen with a real character either side. Nineteen
 *     names have a hyphen after a space - U+11C88 is MARCHEN LETTER -A - and
 *     ignoring that one makes it MARCHEN LETTER A, which is U+11C8F.
 *
 * Every spelling resolves: the name, all five kinds of alias, and the
 * algorithmic families. A named sequence does not - it is not one codepoint -
 * and guni_named_sequence() is where those are.
 *
 * @param name The name. Need not be NUL-terminated.
 * @param len Its length in bytes.
 * @param out Receives the codepoint. Required.
 * @return GUNI_OK, or GUNI_ERR_INVALID when nothing has that name.
 */
GUNI_API GUNI_Result guni_codepoint_by_name(const char * name, size_t len,
    uint32_t * out);

/**
 * @brief The codepoints of a named sequence.
 *
 * `NamedSequences.txt`: 461 names for sequences of two to four codepoints,
 * matched loosely as above.
 *
 * @param name The name.
 * @param len Its length in bytes.
 * @param out Receives the codepoints. May be NULL when @p cap is 0.
 * @param cap How many @p out holds; GUNI_SEQUENCE_MAX_LENGTH is always enough.
 * @param out_len Receives the sequence's length. Required.
 * @return GUNI_OK, GUNI_ERR_LIMIT, or GUNI_ERR_INVALID when nothing has that
 *         name.
 */
GUNI_API GUNI_Result guni_named_sequence(const char * name, size_t len,
    uint32_t * out, size_t cap, size_t * out_len);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GUNI_NAME_H
