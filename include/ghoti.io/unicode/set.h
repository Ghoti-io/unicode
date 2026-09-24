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
 * The same properties as **sets**: "which codepoints have Script=Greek", and
 * the name lookup a regex compiler needs to get from `\p{Script=Greek}` to
 * that question.
 *
 * This is the set-oriented half of design.md section 4.2. It is not a second
 * copy of the data: the ranges are materialised on demand from the same table
 * char.h reads - the maximal runs over which every property is constant - so
 * the two call shapes cannot drift, because they are not two sources. That is
 * an amendment to the design, which specified a committed range array per
 * property value; the runs cost about 48 KB in total where the arrays cost
 * about 320 KB, and "cannot disagree" became a property of the construction
 * rather than of a test.
 *
 * Materialising follows the output contract of section 4.5: ask with a cap of
 * 0 to learn the count, then ask again. A regex compiler does this once per
 * property per pattern and the walk is over 8,035 runs, not 1,114,112
 * codepoints.
 */

#ifndef GHOTI_IO_GUNI_SET_H
#define GHOTI_IO_GUNI_SET_H

#include <ghoti.io/unicode/char.h>
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
 * @brief An inclusive range of codepoints.
 *
 * The same shape as regex's GRX_CharRange, which is what it will replace.
 */
typedef struct {
  uint32_t first; ///< The first codepoint, inclusive.
  uint32_t last;  ///< The last codepoint, inclusive.
} GUNI_Range;

/**
 * @brief Look a property up by any of its names.
 *
 * Loose matching per UAX #44-LM3: case, whitespace, `_` and `-` are ignored,
 * so "Line_Break", "linebreak" and "lb" are one key. That is not a
 * convenience - a regex dialect that did not accept every spelling would
 * reject patterns every other engine accepts (design.md section 2, M15).
 *
 * @param name The name. Need not be NUL-terminated.
 * @param len Its length in bytes.
 * @param out Receives the property. Required.
 * @return GUNI_OK, or GUNI_ERR_INVALID when no property has that name.
 */
GUNI_API GUNI_Result guni_property_by_name(const char * name, size_t len,
    GUNI_Property * out);

/**
 * @brief Look one property's value up by any of its names.
 *
 * Loose-matched as above. Numbers work for Canonical_Combining_Class ("230"),
 * and Y/N/T/F/Yes/No/True/False all work for a binary property.
 *
 * @param property The property.
 * @param name The value's name. Need not be NUL-terminated.
 * @param len Its length in bytes.
 * @param out Receives the value. Required.
 * @return GUNI_OK, or GUNI_ERR_INVALID when that property has no such value.
 */
GUNI_API GUNI_Result guni_value_by_name(GUNI_Property property,
    const char * name, size_t len, uint32_t * out);

/**
 * @brief A property's long name, as the UCD spells it.
 *
 * @return A static string, or NULL when @p property is out of range.
 */
GUNI_API const char * guni_property_name(GUNI_Property property);

/**
 * @brief One of a property's values, by its long name.
 *
 * @return A static string, NULL when @p property is out of range, and "" for a
 *         value the UCD names nothing - which happens for
 *         Canonical_Combining_Class, where the class is a number 0..254 and
 *         only some of them have names.
 */
GUNI_API const char * guni_value_name(GUNI_Property property, uint32_t value);

/**
 * @brief How this property's value is reached, which says which question to
 * ask of it.
 *
 * GUNI_PROP_KIND_SCX is the one a caller must notice: Script_Extensions is
 * set-valued, so guni_property_value() has no answer for it and
 * guni_set_contains() does.
 */
GUNI_API GUNI_PropertyKind guni_property_kind(GUNI_Property property);

/// @brief One past the largest value @p property has. 0 if out of range.
GUNI_API uint32_t guni_property_value_count(GUNI_Property property);

/**
 * @brief Is @p cp in the set of codepoints whose @p property is @p value?
 *
 * For Script_Extensions this is set membership rather than equality, which is
 * what `\p{scx=Greek}` means.
 */
GUNI_API bool guni_set_contains(GUNI_Property property, uint32_t value,
    uint32_t cp);

/**
 * @brief The set of codepoints whose @p property is @p value, as ranges.
 *
 * Ranges come out sorted, non-empty, non-overlapping and non-adjacent: two
 * runs that touch are one range, so the output is the same whatever the
 * table's internal boundaries are. That matters because it makes the output a
 * function of the data rather than of the compression.
 *
 * @param property The property.
 * @param value Its value.
 * @param out Receives the ranges. May be NULL when @p cap is 0, which is how
 *        a caller asks for the count first.
 * @param cap How many ranges @p out holds.
 * @param out_len Receives the number of ranges the set has, whatever @p cap
 *        is. Required.
 * @return GUNI_OK; GUNI_ERR_LIMIT when @p cap was too small, with @p out_len
 *         set to the requirement; GUNI_ERR_INVALID for a property out of
 *         range. An empty set is GUNI_OK with @p out_len 0.
 */
GUNI_API GUNI_Result guni_set_ranges(GUNI_Property property, uint32_t value,
    GUNI_Range * out, size_t cap, size_t * out_len);

/**
 * @brief The set of codepoints whose General_Category is in @p mask.
 *
 * `\p{L}` is a group of five categories, and GUNI_GC_MASK_L is the mask.
 * Spelled separately because a group is not a value and giving it one would
 * have put a fake member in GUNI_GeneralCategory.
 *
 * @param mask A bitwise OR of GUNI_GC_MASK() values.
 * @param out Receives the ranges. May be NULL when @p cap is 0.
 * @param cap How many ranges @p out holds.
 * @param out_len Receives the number of ranges. Required.
 * @return GUNI_OK, or GUNI_ERR_LIMIT with @p out_len set to the requirement.
 */
GUNI_API GUNI_Result guni_gc_mask_ranges(uint32_t mask, GUNI_Range * out,
    size_t cap, size_t * out_len);

/// @brief Is @p cp's General_Category in @p mask?
GUNI_API bool guni_gc_mask_contains(uint32_t mask, uint32_t cp);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GUNI_SET_H
