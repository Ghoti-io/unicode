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
 * @file namespace.h
 *
 * Maps every public name of this library into its version namespace.
 *
 * Kept in one file rather than beside each declaration: a type rename has to
 * be in effect before any struct tag that uses the name, and an internal
 * header may define such a tag without including the public header that
 * declares the typedef.
 *
 * `make check-symbols` fails if an exported symbol is missing from this list.
 *
 * See CONVENTIONS.md section 4.
 */

#ifndef GHOTI_IO_GUNI_NAMESPACE_H
#define GHOTI_IO_GUNI_NAMESPACE_H

#include <ghoti.io/unicode/libver.h>

/// @cond HIDDEN_SYMBOLS

// Public types. Renamed as well as the functions, so that two versions whose
// structs differ in layout cannot be confused for one another. GCU_* names
// are deliberately absent: they are cutil's, and cutil has already renamed
// them.
#define GUNI_Allocator GHOTIIO_UNICODE(GUNI_Allocator)
#define GUNI_Limits GHOTIIO_UNICODE(GUNI_Limits)
#define GUNI_Result GHOTIIO_UNICODE(GUNI_Result)

// Generated enumerations (enums.h). Renamed for the same reason as the
// structs: a consumer that stores a GUNI_Script must not be able to read one
// back through a differently-versioned header without the linker noticing.
#define GUNI_BidiClass GHOTIIO_UNICODE(GUNI_BidiClass)
#define GUNI_BinaryProperty GHOTIIO_UNICODE(GUNI_BinaryProperty)
#define GUNI_Block GHOTIIO_UNICODE(GUNI_Block)
#define GUNI_DecompositionType GHOTIIO_UNICODE(GUNI_DecompositionType)
#define GUNI_EastAsianWidth GHOTIIO_UNICODE(GUNI_EastAsianWidth)
#define GUNI_GeneralCategory GHOTIIO_UNICODE(GUNI_GeneralCategory)
#define GUNI_GraphemeClusterBreak GHOTIIO_UNICODE(GUNI_GraphemeClusterBreak)
#define GUNI_HangulSyllableType GHOTIIO_UNICODE(GUNI_HangulSyllableType)
#define GUNI_IndicConjunctBreak GHOTIIO_UNICODE(GUNI_IndicConjunctBreak)
#define GUNI_IndicPositionalCategory GHOTIIO_UNICODE(GUNI_IndicPositionalCategory)
#define GUNI_IndicSyllabicCategory GHOTIIO_UNICODE(GUNI_IndicSyllabicCategory)
#define GUNI_JoiningGroup GHOTIIO_UNICODE(GUNI_JoiningGroup)
#define GUNI_JoiningType GHOTIIO_UNICODE(GUNI_JoiningType)
#define GUNI_LineBreak GHOTIIO_UNICODE(GUNI_LineBreak)
#define GUNI_NumericType GHOTIIO_UNICODE(GUNI_NumericType)
#define GUNI_Property GHOTIIO_UNICODE(GUNI_Property)
#define GUNI_PropertyKind GHOTIIO_UNICODE(GUNI_PropertyKind)
#define GUNI_QuickCheck GHOTIIO_UNICODE(GUNI_QuickCheck)
#define GUNI_Script GHOTIIO_UNICODE(GUNI_Script)
#define GUNI_SentenceBreak GHOTIIO_UNICODE(GUNI_SentenceBreak)
#define GUNI_VerticalOrientation GHOTIIO_UNICODE(GUNI_VerticalOrientation)
#define GUNI_WordBreak GHOTIIO_UNICODE(GUNI_WordBreak)

// Public types.
#define GUNI_Invalid GHOTIIO_UNICODE(GUNI_Invalid)
#define GUNI_BidiDirection GHOTIIO_UNICODE(GUNI_BidiDirection)
#define GUNI_BreakIter GHOTIIO_UNICODE(GUNI_BreakIter)
#define GUNI_BreakKind GHOTIIO_UNICODE(GUNI_BreakKind)
#define GUNI_BreakOptions GHOTIIO_UNICODE(GUNI_BreakOptions)
#define GUNI_BreakProvider GHOTIIO_UNICODE(GUNI_BreakProvider)
#define GUNI_BreakText GHOTIIO_UNICODE(GUNI_BreakText)
#define GUNI_LineBreakTailoring GHOTIIO_UNICODE(GUNI_LineBreakTailoring)
#define GUNI_NormForm GHOTIIO_UNICODE(GUNI_NormForm)
#define GUNI_Range GHOTIIO_UNICODE(GUNI_Range)
#define GUNI_Utf8Iter GHOTIIO_UNICODE(GUNI_Utf8Iter)

// Public functions.
#define guni_allocator_default GHOTIIO_UNICODE(guni_allocator_default)
#define guni_bidi_class GHOTIIO_UNICODE(guni_bidi_class)
#define guni_bidi_levels GHOTIIO_UNICODE(guni_bidi_levels)
#define guni_bidi_levels_utf8 GHOTIIO_UNICODE(guni_bidi_levels_utf8)
#define guni_bidi_levels_with_allocator GHOTIIO_UNICODE(guni_bidi_levels_with_allocator)
#define guni_bidi_mirror GHOTIIO_UNICODE(guni_bidi_mirror)
#define guni_bidi_paired_bracket GHOTIIO_UNICODE(guni_bidi_paired_bracket)
#define guni_bidi_reorder GHOTIIO_UNICODE(guni_bidi_reorder)
#define guni_break_all GHOTIIO_UNICODE(guni_break_all)
#define guni_break_all_codepoints GHOTIIO_UNICODE(guni_break_all_codepoints)
#define guni_break_at GHOTIIO_UNICODE(guni_break_at)
#define guni_break_at_codepoints GHOTIIO_UNICODE(guni_break_at_codepoints)
#define guni_break_iter_init GHOTIIO_UNICODE(guni_break_iter_init)
#define guni_break_iter_init_codepoints GHOTIIO_UNICODE(guni_break_iter_init_codepoints)
#define guni_break_iter_next GHOTIIO_UNICODE(guni_break_iter_next)
#define guni_break_text_at GHOTIIO_UNICODE(guni_break_text_at)
#define guni_break_text_length GHOTIIO_UNICODE(guni_break_text_length)
#define guni_canonical_order GHOTIIO_UNICODE(guni_canonical_order)
#define guni_block GHOTIIO_UNICODE(guni_block)
#define guni_combining_class GHOTIIO_UNICODE(guni_combining_class)
#define guni_compose GHOTIIO_UNICODE(guni_compose)
#define guni_decompose GHOTIIO_UNICODE(guni_decompose)
#define guni_decomposition_type GHOTIIO_UNICODE(guni_decomposition_type)
#define guni_east_asian_width GHOTIIO_UNICODE(guni_east_asian_width)
#define guni_gc_mask_contains GHOTIIO_UNICODE(guni_gc_mask_contains)
#define guni_gc_mask_ranges GHOTIIO_UNICODE(guni_gc_mask_ranges)
#define guni_general_category GHOTIIO_UNICODE(guni_general_category)
#define guni_general_category_mask GHOTIIO_UNICODE(guni_general_category_mask)
#define guni_grapheme_cluster_break GHOTIIO_UNICODE(guni_grapheme_cluster_break)
#define guni_hangul_syllable_type GHOTIIO_UNICODE(guni_hangul_syllable_type)
#define guni_has_property GHOTIIO_UNICODE(guni_has_property)
#define guni_indic_conjunct_break GHOTIIO_UNICODE(guni_indic_conjunct_break)
#define guni_indic_positional_category GHOTIIO_UNICODE(guni_indic_positional_category)
#define guni_indic_syllabic_category GHOTIIO_UNICODE(guni_indic_syllabic_category)
#define guni_is_codepoint GHOTIIO_UNICODE(guni_is_codepoint)
#define guni_is_noncharacter GHOTIIO_UNICODE(guni_is_noncharacter)
#define guni_is_normalized GHOTIIO_UNICODE(guni_is_normalized)
#define guni_is_normalized_utf8 GHOTIIO_UNICODE(guni_is_normalized_utf8)
#define guni_is_stream_safe GHOTIIO_UNICODE(guni_is_stream_safe)
#define guni_is_surrogate GHOTIIO_UNICODE(guni_is_surrogate)
#define guni_joining_group GHOTIIO_UNICODE(guni_joining_group)
#define guni_joining_type GHOTIIO_UNICODE(guni_joining_type)
#define guni_line_break GHOTIIO_UNICODE(guni_line_break)
#define guni_line_break_resolve GHOTIIO_UNICODE(guni_line_break_resolve)
#define guni_normalize GHOTIIO_UNICODE(guni_normalize)
#define guni_normalize_utf8 GHOTIIO_UNICODE(guni_normalize_utf8)
#define guni_numeric_type GHOTIIO_UNICODE(guni_numeric_type)
#define guni_numeric_value GHOTIIO_UNICODE(guni_numeric_value)
#define guni_property_by_name GHOTIIO_UNICODE(guni_property_by_name)
#define guni_property_kind GHOTIIO_UNICODE(guni_property_kind)
#define guni_property_name GHOTIIO_UNICODE(guni_property_name)
#define guni_property_value GHOTIIO_UNICODE(guni_property_value)
#define guni_property_value_count GHOTIIO_UNICODE(guni_property_value_count)
#define guni_quick_check GHOTIIO_UNICODE(guni_quick_check)
#define guni_quick_check_text GHOTIIO_UNICODE(guni_quick_check_text)
#define guni_quick_check_utf8 GHOTIIO_UNICODE(guni_quick_check_utf8)
#define guni_script GHOTIIO_UNICODE(guni_script)
#define guni_script_extensions GHOTIIO_UNICODE(guni_script_extensions)
#define guni_script_extensions_contains GHOTIIO_UNICODE(guni_script_extensions_contains)
#define guni_sentence_break GHOTIIO_UNICODE(guni_sentence_break)
#define guni_set_contains GHOTIIO_UNICODE(guni_set_contains)
#define guni_set_ranges GHOTIIO_UNICODE(guni_set_ranges)
#define guni_stream_safe GHOTIIO_UNICODE(guni_stream_safe)
#define guni_ucd_version GHOTIIO_UNICODE(guni_ucd_version)
#define guni_ucd_version_number GHOTIIO_UNICODE(guni_ucd_version_number)
#define guni_utf8_count GHOTIIO_UNICODE(guni_utf8_count)
#define guni_utf8_decode GHOTIIO_UNICODE(guni_utf8_decode)
#define guni_utf8_encode GHOTIIO_UNICODE(guni_utf8_encode)
#define guni_utf8_from_codepoints GHOTIIO_UNICODE(guni_utf8_from_codepoints)
#define guni_utf8_iter_init GHOTIIO_UNICODE(guni_utf8_iter_init)
#define guni_utf8_iter_next GHOTIIO_UNICODE(guni_utf8_iter_next)
#define guni_utf8_length GHOTIIO_UNICODE(guni_utf8_length)
#define guni_utf8_prev GHOTIIO_UNICODE(guni_utf8_prev)
#define guni_utf8_to_codepoints GHOTIIO_UNICODE(guni_utf8_to_codepoints)
#define guni_utf8_validate GHOTIIO_UNICODE(guni_utf8_validate)
#define guni_value_by_name GHOTIIO_UNICODE(guni_value_by_name)
#define guni_value_name GHOTIIO_UNICODE(guni_value_name)
#define guni_vertical_orientation GHOTIIO_UNICODE(guni_vertical_orientation)
#define guni_word_break GHOTIIO_UNICODE(guni_word_break)
#define guni_limits_default GHOTIIO_UNICODE(guni_limits_default)
#define guni_result_string GHOTIIO_UNICODE(guni_result_string)
#define guni_version_number GHOTIIO_UNICODE(guni_version_number)
#define guni_version_string GHOTIIO_UNICODE(guni_version_string)
/// @endcond

#endif // GHOTI_IO_GUNI_NAMESPACE_H
