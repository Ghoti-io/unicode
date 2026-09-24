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
 * Every property the Unicode Character Database gives a codepoint, as a
 * function of that codepoint.
 *
 * This is the point-oriented half of design.md section 4.2: a shaper asking
 * "what is the script of U+03B1" a million times a second wants a
 * constant-time answer, and every function here is three dependent loads
 * into one shared record. The set-oriented half - "which codepoints have
 * Script=Greek" - is set.h, and both read the same table, so they cannot
 * disagree.
 *
 * Every function here is defined for every `uint32_t` (section 4.4). Above
 * U+10FFFF the answer is the one an unassigned codepoint would give, except
 * that a value above U+10FFFF is not a codepoint and so has no binary
 * property at all, not even Any. Nothing here indexes a table with an
 * unchecked value, nothing here can fail, and nothing here allocates.
 */

#ifndef GHOTI_IO_GUNI_CHAR_H
#define GHOTI_IO_GUNI_CHAR_H

#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/macros.h>
#include <ghoti.io/unicode/enums.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The Unicode version every table in this library was generated from.
 *
 * "Which Unicode version classified this string" is an audit question, which
 * is why it is queryable at run time and not only a compile-time macro
 * (design.md section 5.4).
 *
 * @return A static string like "17.0.0", never NULL.
 */
GUNI_API const char * guni_ucd_version(void);

/// @brief The same, packed as GUNI_MAKE_VERSION() packs a version.
GUNI_API unsigned guni_ucd_version_number(void);

/**
 * @brief General_Category.
 *
 * GUNI_GC_UNASSIGNED (Cn) for an unassigned codepoint, a surrogate's own
 * category for a surrogate, and GUNI_GC_UNASSIGNED above U+10FFFF.
 */
GUNI_API GUNI_GeneralCategory guni_general_category(uint32_t cp);

/**
 * @brief General_Category as a one-bit mask, for testing against a group.
 *
 * `guni_general_category_mask(cp) & GUNI_GC_MASK_L` is `\p{L}`, and is one
 * test rather than five comparisons.
 */
GUNI_API uint32_t guni_general_category_mask(uint32_t cp);

/// @brief Script, from Scripts.txt. GUNI_SCRIPT_UNKNOWN (Zzzz) by default.
GUNI_API GUNI_Script guni_script(uint32_t cp);

/**
 * @brief Script_Extensions, the set.
 *
 * Never empty: UAX #24 makes the default the codepoint's own Script value, so
 * a caller never has to special-case the absence of a set.
 *
 * @param cp The codepoint.
 * @param out Receives the scripts, sorted. May be NULL when @p cap is 0.
 * @param cap How many @p out holds.
 * @param out_len Receives how many the set has, whatever @p cap is. Required.
 * @return GUNI_OK, or GUNI_ERR_LIMIT when @p cap was too small, with
 *         @p out_len set to the requirement.
 */
GUNI_API GUNI_Result guni_script_extensions(uint32_t cp, GUNI_Script * out,
    size_t cap, size_t * out_len);

/**
 * @brief Is @p script in @p cp's Script_Extensions?
 *
 * This is what `\p{scx=Greek}` asks, and what a shaper's itemiser asks when
 * deciding whether a shared codepoint can stay in the current run.
 */
GUNI_API bool guni_script_extensions_contains(uint32_t cp, GUNI_Script script);

/// @brief Canonical_Combining_Class, 0 to 254. 0 (Not_Reordered) by default.
GUNI_API uint8_t guni_combining_class(uint32_t cp);

/**
 * @brief Bidi_Class.
 *
 * The default is not one value: DerivedBidiClass.txt gives twenty-nine ranges
 * whose *unassigned* codepoints are right-to-left or Arabic-letter rather
 * than left-to-right, so that a new character in the Hebrew block behaves
 * before it is assigned. Those defaults are in the table.
 */
GUNI_API GUNI_BidiClass guni_bidi_class(uint32_t cp);

/// @brief East_Asian_Width, from EastAsianWidth.txt.
GUNI_API GUNI_EastAsianWidth guni_east_asian_width(uint32_t cp);

/**
 * @brief Line_Break, **unresolved**.
 *
 * The classes UAX #14's rule LB1 resolves - AI, SG, XX, SA and CJ - are
 * reported as themselves, not as what LB1 would make of them. Resolving CJ
 * is the difference between CSS's `line-break: strict` and `normal`, and a
 * shared table cannot make that choice for a layout engine (design.md
 * section 7.3, M2). guni_line_break_resolve() in break.h applies LB1 with a
 * tailoring the caller names.
 */
GUNI_API GUNI_LineBreak guni_line_break(uint32_t cp);

/// @brief Grapheme_Cluster_Break, for UAX #29.
GUNI_API GUNI_GraphemeClusterBreak guni_grapheme_cluster_break(uint32_t cp);

/// @brief Word_Break, for UAX #29.
GUNI_API GUNI_WordBreak guni_word_break(uint32_t cp);

/// @brief Sentence_Break, for UAX #29.
GUNI_API GUNI_SentenceBreak guni_sentence_break(uint32_t cp);

/// @brief Joining_Type, from ArabicShaping.txt, for a cursive shaper.
GUNI_API GUNI_JoiningType guni_joining_type(uint32_t cp);

/// @brief Joining_Group, from ArabicShaping.txt.
GUNI_API GUNI_JoiningGroup guni_joining_group(uint32_t cp);

/// @brief Indic_Syllabic_Category, an input to the Universal Shaping Engine.
GUNI_API GUNI_IndicSyllabicCategory guni_indic_syllabic_category(uint32_t cp);

/// @brief Indic_Positional_Category, the other USE input.
GUNI_API GUNI_IndicPositionalCategory guni_indic_positional_category(uint32_t cp);

/// @brief Indic_Conjunct_Break, which UAX #29's rule GB9c reads.
GUNI_API GUNI_IndicConjunctBreak guni_indic_conjunct_break(uint32_t cp);

/// @brief Vertical_Orientation, from UAX #50, for vertical layout.
GUNI_API GUNI_VerticalOrientation guni_vertical_orientation(uint32_t cp);

/// @brief Hangul_Syllable_Type, for the algorithmic Hangul composition.
GUNI_API GUNI_HangulSyllableType guni_hangul_syllable_type(uint32_t cp);

/// @brief Decomposition_Type: Canonical, one of the sixteen compatibility
/// tags, or None.
GUNI_API GUNI_DecompositionType guni_decomposition_type(uint32_t cp);

/// @brief Numeric_Type: Decimal, Digit, Numeric or None.
GUNI_API GUNI_NumericType guni_numeric_type(uint32_t cp);

/**
 * @brief Numeric_Value, as an exact rational.
 *
 * @param cp The codepoint.
 * @param numerator Receives the numerator, which may be negative: U+0F33
 *        TIBETAN DIGIT HALF ZERO is -1/2. Required.
 * @param denominator Receives the denominator, never 0. Required.
 * @return true when @p cp has a numeric value. The numerator is 64-bit
 *         because the UCD contains 10^12, which does not survive a double.
 */
GUNI_API bool guni_numeric_value(uint32_t cp, int64_t * numerator,
    uint32_t * denominator);

/// @brief Block, from Blocks.txt. GUNI_BLOCK_NO_BLOCK where there is none.
GUNI_API GUNI_Block guni_block(uint32_t cp);

/**
 * @brief Does @p cp have binary property @p property?
 *
 * False for every property when @p cp is above U+10FFFF, GUNI_PROP_ANY
 * included: a value that is not a codepoint is not in the codepoint space.
 */
GUNI_API bool guni_has_property(uint32_t cp, GUNI_BinaryProperty property);

/**
 * @brief Any property's value, as an unsigned integer.
 *
 * The generic form, for a caller that has a GUNI_Property rather than a call
 * site per property - a regex compiler, or the exhaustive sweep. The value is
 * the property's enum value, 0 or 1 for a binary property, the class number
 * for Canonical_Combining_Class, a GUNI_QuickCheck for the four quick checks,
 * and a GUNI_Block for Block.
 *
 * Script_Extensions is set-valued and has no single value: it answers 0 here,
 * and guni_script_extensions_contains() is the question to ask instead.
 * guni_property_kind() distinguishes the two cases without a table of special
 * cases in the caller.
 */
GUNI_API uint32_t guni_property_value(uint32_t cp, GUNI_Property property);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GUNI_CHAR_H
