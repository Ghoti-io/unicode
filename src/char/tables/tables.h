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

/*
 * GENERATED FILE - DO NOT EDIT.
 *
 * Written by tools/ucd/gen_tables.py from the Unicode Character
 * Database version 17.0.0. Regenerate with "make gen-ucd-tables";
 * "make check-ucd-tables" fails if this file and the generator
 * disagree. Its content is derived from the whole UCD.
 */

/**
 * @file
 *
 * The generated property tables, and the one lookup every accessor in
 * char.c goes through.
 *
 * Internal: not installed, not part of the API. The shape is
 * documentation/design.md section 4.2 - a two-stage trie to a shared record
 * for the point query, and a run table for set.h's bulk enumeration, both
 * from the same map so that they cannot disagree.
 */

#ifndef GHOTI_IO_GUNI_CHAR_TABLES_H
#define GHOTI_IO_GUNI_CHAR_TABLES_H

#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/enums.h>
#include <ghoti.io/unicode/macros.h>
#include <stddef.h>
#include <stdint.h>

/// Words of binary-property bits in a record.
#define GUNI_PROP_FLAG_WORDS 3
/// Codepoints per stage-2 block; 64 measured smallest.
#define GUNI_PROP_STAGE1_SHIFT 6
#define GUNI_PROP_STAGE2_MASK 63
#define GUNI_PROP_RECORD_COUNT 2374
#define GUNI_PROP_STAGE1_COUNT 17408
#define GUNI_PROP_STAGE2_COUNT 38208
#define GUNI_PROP_RUN_COUNT 8035
#define GUNI_SCX_POOL_COUNT 986
#define GUNI_BLOCK_RANGE_COUNT 346
#define GUNI_NUMERIC_RANGE_COUNT 1980
#define GUNI_DECOMP_COUNT 5914
#define GUNI_DECOMP_POOL_COUNT 6763
#define GUNI_COMPOSE_COUNT 961
/* UTS #39 script runs. GUNI_SCRIPT_RUN_WORDS is in enums.h:
 * GUNI_ScriptRun holds the bitset, so its width is public. */
#define GUNI_SCRIPT_RUN_SET_COUNT 284
#define GUNI_DIGIT_ZERO_COUNT 77
#define GUNI_CASE_COUNT 3037
#define GUNI_CASE_POOL_COUNT 494
#define GUNI_CASE_CONDITIONAL_COUNT 16
#define GUNI_TURKIC_FOLD_COUNT 2
#define GUNI_ORBIT_COUNT 1482
#define GUNI_ORBIT_POOL_COUNT 2994
/* The four columns of every case table, in this order. */
#define GUNI_CASE_UPPER 0
#define GUNI_CASE_LOWER 1
#define GUNI_CASE_TITLE 2
#define GUNI_CASE_FOLD 3
#define GUNI_MIRROR_COUNT 428
#define GUNI_BRACKET_COUNT 128
/* A composition key: the two codepoints, 21 bits each. */
#define GUNI_COMPOSE_KEY(first, second) (((uint64_t)(first) << 21) | (uint64_t)(second))
#define GUNI_PROPERTY_ALIAS_COUNT 184
#define GUNI_VALUE_ALIAS_COUNT 2527

/**
 * @brief Every property of a codepoint, in one record.
 *
 * 2,369 of these cover all 1,114,112 codepoints, because properties
 * correlate: a codepoint's script very nearly determines its bidi class, its
 * line-break class and its Indic categories. Fields are widest-first so the
 * struct has no padding holes.
 */
typedef struct {
  uint32_t flags[GUNI_PROP_FLAG_WORDS];
  uint16_t script;  ///< Script
  uint16_t joining_group;  ///< Joining_Group
  uint16_t scx;
  uint8_t gc;  ///< General_Category
  uint8_t bidi_class;  ///< Bidi_Class
  uint8_t eaw;  ///< East_Asian_Width
  uint8_t line_break;  ///< Line_Break (unresolved: LB1 is the caller's)
  uint8_t gcb;  ///< Grapheme_Cluster_Break
  uint8_t wb;  ///< Word_Break
  uint8_t sb;  ///< Sentence_Break
  uint8_t joining_type;  ///< Joining_Type
  uint8_t insc;  ///< Indic_Syllabic_Category
  uint8_t inpc;  ///< Indic_Positional_Category
  uint8_t vo;  ///< Vertical_Orientation
  uint8_t hst;  ///< Hangul_Syllable_Type
  uint8_t dt;  ///< Decomposition_Type
  uint8_t nt;  ///< Numeric_Type
  uint8_t incb;  ///< Indic_Conjunct_Break
  uint8_t ccc;
  uint8_t qc;
} GuniPropRecord;

extern const GuniPropRecord guni_prop_records[GUNI_PROP_RECORD_COUNT];
extern const GuniPropRecord guni_prop_record_default;
extern const uint16_t guni_prop_stage1[GUNI_PROP_STAGE1_COUNT];
extern const uint16_t guni_prop_stage2[GUNI_PROP_STAGE2_COUNT];
extern const uint32_t guni_prop_run_first[GUNI_PROP_RUN_COUNT];
extern const uint16_t guni_prop_run_record[GUNI_PROP_RUN_COUNT];
extern const uint16_t guni_scx_pool[GUNI_SCX_POOL_COUNT];

extern const uint32_t guni_block_first[GUNI_BLOCK_RANGE_COUNT];
extern const uint32_t guni_block_last[GUNI_BLOCK_RANGE_COUNT];
extern const uint16_t guni_block_id[GUNI_BLOCK_RANGE_COUNT];

/* Decompositions. The pool holds every canonical and compatibility
 * decomposition, shared where they are equal; the row arrays are sorted by
 * codepoint. Hangul is absent on purpose: Standard section 3.12 gives it as
 * arithmetic (norm.c), and a table would be 11,172 entries of what a dozen
 * lines compute. */
extern const uint32_t guni_decomp_pool[GUNI_DECOMP_POOL_COUNT];
extern const uint32_t guni_decomp_codepoint[GUNI_DECOMP_COUNT];
extern const uint16_t guni_decomp_nfd_offset[GUNI_DECOMP_COUNT];
extern const uint8_t guni_decomp_nfd_length[GUNI_DECOMP_COUNT];
extern const uint16_t guni_decomp_nfkd_offset[GUNI_DECOMP_COUNT];
extern const uint8_t guni_decomp_nfkd_length[GUNI_DECOMP_COUNT];

/* Canonical composition: the pairs that compose, sorted by a packed key so
 * that the lookup is one binary search. A canonical decomposition of exactly
 * two codepoints whose codepoint is not Full_Composition_Exclusion. */
extern const uint64_t guni_compose_key[GUNI_COMPOSE_COUNT];
extern const uint32_t guni_compose_value[GUNI_COMPOSE_COUNT];

/* UTS #39 script runs. One augmented bitset per distinct Script_Extensions
 * set, indexed by that set's offset in guni_scx_pool - which is a field of the
 * property record, so a check costs no second search. */
extern const uint64_t
    guni_script_run_sets[GUNI_SCRIPT_RUN_SET_COUNT][GUNI_SCRIPT_RUN_WORDS];
extern const uint16_t guni_script_run_by_offset[GUNI_SCX_POOL_COUNT];

/* The first codepoint of each block of ten decimal digits. */
extern const uint32_t guni_digit_zeros[GUNI_DIGIT_ZERO_COUNT];

/* Case mappings. One sorted table over every codepoint with any, gated by
 * Changes_When_Casemapped and Changes_When_Casefolded in the property record,
 * so a codepoint with none costs no search. A simple mapping of 0 means the
 * identity: 0 is not a mapping target, so it is unambiguous. */
extern const uint32_t guni_case_codepoint[GUNI_CASE_COUNT];
extern const uint32_t guni_case_simple[GUNI_CASE_COUNT][4];
extern const uint16_t guni_case_full_offset[GUNI_CASE_COUNT][4];
extern const uint8_t guni_case_full_length[GUNI_CASE_COUNT][4];
extern const uint32_t guni_case_pool[GUNI_CASE_POOL_COUNT];

/** SpecialCasing.txt's conditions, which case.c implements one by one. */
typedef enum {
  GUNI_CASE_COND_NONE = 0,
  GUNI_CASE_COND_FINAL_SIGMA,
  GUNI_CASE_COND_AFTER_SOFT_DOTTED,
  GUNI_CASE_COND_MORE_ABOVE,
  GUNI_CASE_COND_AFTER_I,
  GUNI_CASE_COND_NOT_BEFORE_DOT
} GuniCaseCondition;

/** One conditional mapping: sixteen lines of SpecialCasing.txt. */
typedef struct {
  uint32_t codepoint;
  uint8_t language;  ///< A GUNI_CaseTailoring.
  uint8_t condition; ///< A GuniCaseCondition.
  uint16_t offset[4];
  uint8_t length[4];
} GuniCaseConditional;

extern const GuniCaseConditional
    guni_case_conditional[GUNI_CASE_CONDITIONAL_COUNT];

/* CaseFolding.txt's T status: the two codepoints a Turkic fold differs on. */
extern const uint32_t guni_turkic_fold_from[GUNI_TURKIC_FOLD_COUNT];
extern const uint32_t guni_turkic_fold_to[GUNI_TURKIC_FOLD_COUNT];

/* The fold orbits: every codepoint that folds to the same value, keyed by
 * that value. What a case-insensitive character class needs. */
extern const uint32_t guni_orbit_value[GUNI_ORBIT_COUNT];
extern const uint16_t guni_orbit_offset[GUNI_ORBIT_COUNT];
extern const uint8_t guni_orbit_length[GUNI_ORBIT_COUNT];
extern const uint32_t guni_orbit_pool[GUNI_ORBIT_POOL_COUNT];

/* UAX #9's rule L4: the mirrored glyph, and BD14/BD15's bracket pairs for
 * rule N0. Sorted by codepoint; both are small enough that a binary search is
 * the whole implementation. */
extern const uint32_t guni_mirror_from[GUNI_MIRROR_COUNT];
extern const uint32_t guni_mirror_to[GUNI_MIRROR_COUNT];
extern const uint32_t guni_bracket_from[GUNI_BRACKET_COUNT];
extern const uint32_t guni_bracket_pair[GUNI_BRACKET_COUNT];
/** 1 for an opening bracket, 2 for a closing one. */
extern const uint8_t guni_bracket_kind[GUNI_BRACKET_COUNT];

extern const uint32_t guni_numeric_first[GUNI_NUMERIC_RANGE_COUNT];
extern const uint32_t guni_numeric_last[GUNI_NUMERIC_RANGE_COUNT];
extern const int64_t guni_numeric_numerator[GUNI_NUMERIC_RANGE_COUNT];
extern const uint32_t guni_numeric_denominator[GUNI_NUMERIC_RANGE_COUNT];

/** @brief One row of the table a property name is looked up in. */
typedef struct {
  const char * name;                ///< The long alias.
  const char * const * value_names; ///< Value to long alias; "" where none.
  uint16_t value_count;             ///< One past the largest value.
  uint8_t kind;                     ///< A GUNI_PropertyKind.
  uint8_t bit;                      ///< The GUNI_BinaryProperty, for BINARY.
  uint8_t qc_index;                 ///< 0..3 selecting the form, for QC.
} GuniPropertyInfo;

extern const GuniPropertyInfo guni_property_info[GUNI_PROPERTY_COUNT];

/** @brief A loose-matched spelling of a property name. */
typedef struct {
  const char * name;
  uint16_t property;
} GuniPropertyAlias;

/** @brief A loose-matched spelling of one property's value. */
typedef struct {
  const char * name;
  uint16_t property;
  uint16_t value;
} GuniValueAlias;

extern const GuniPropertyAlias guni_property_aliases[GUNI_PROPERTY_ALIAS_COUNT];
extern const GuniValueAlias guni_value_aliases[GUNI_VALUE_ALIAS_COUNT];

/**
 * @brief The record for a codepoint. Three dependent loads, no branch on the
 * fast path.
 *
 * Defined for every uint32_t: above the last codepoint the Standard defines,
 * the answer is the all-defaults record, so that no input indexes a table
 * (design.md section 4.4). That record is a synthesised one rather than some
 * unassigned codepoint's, because an unassigned codepoint is still a
 * codepoint and answers yes to Any.
 */
static inline const GuniPropRecord * guni_record(uint32_t cp) {
  if (cp > GUNI_MAX_CODEPOINT) {
    return &guni_prop_record_default;
  }
  return &guni_prop_records[guni_prop_stage2[
      ((size_t)guni_prop_stage1[cp >> GUNI_PROP_STAGE1_SHIFT]
       << GUNI_PROP_STAGE1_SHIFT)
      + (cp & GUNI_PROP_STAGE2_MASK)]];
}

/**
 * @brief The value of any property of a record, as an unsigned integer.
 *
 * Generated, so that the switch over properties is never hand-maintained.
 * Returns 0 for a property whose value is not in the record - Block, whose
 * value is a range lookup - and set.c handles those by kind.
 */
uint32_t guni_record_property_value(const GuniPropRecord * record,
    GUNI_Property property);

#endif // GHOTI_IO_GUNI_CHAR_TABLES_H
