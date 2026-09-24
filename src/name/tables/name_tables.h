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
 * disagree. Its content is derived from UnicodeData.txt, NameAliases.txt, NamedSequences.txt and Jamo.txt.
 */

/**
 * @file
 *
 * The generated name tables. **Tier 1**: internal, and not included by
 * anything in tier 0.
 *
 * A token is a word number in the low 15 bits and, in bit 15, the separator
 * that precedes it - set for "-" and clear for a space. 18,457 words and
 * 162,649 tokens encode 40,951 names that are 1,044,804 bytes as text.
 */

#ifndef GHOTI_IO_GUNI_NAME_TABLES_H
#define GHOTI_IO_GUNI_NAME_TABLES_H

#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/macros.h>
#include <stddef.h>
#include <stdint.h>

#define GUNI_NAME_WORD_COUNT 18457
#define GUNI_NAME_TOKEN_COUNT 162649
#define GUNI_NAME_COUNT 40951
#define GUNI_NAME_PRIMARY_COUNT 40470
#define GUNI_NAME_ALIAS_COUNT 481
#define GUNI_NAME_RANGE_COUNT 14
#define GUNI_SEQUENCE_COUNT 461
#define GUNI_SEQUENCE_TOKEN_COUNT 1925
#define GUNI_SEQUENCE_POOL_COUNT 972
/* The separator bit of a token, and the word-number mask. */
#define GUNI_NAME_HYPHEN 0x8000u
#define GUNI_NAME_WORD_MASK 0x7FFFu

extern const char * const guni_name_words[GUNI_NAME_WORD_COUNT];
extern const uint16_t guni_name_tokens[GUNI_NAME_TOKEN_COUNT];
extern const uint32_t guni_name_offsets[GUNI_NAME_COUNT + 1];
extern const uint32_t guni_name_codepoints[GUNI_NAME_COUNT];
/** The name entries in loose-name order, for the reverse lookup. */
extern const uint32_t guni_name_loose_order[GUNI_NAME_COUNT];

/** The codepoints with a name of their own, and which entry it is. */
extern const uint32_t guni_name_primary_cp[GUNI_NAME_PRIMARY_COUNT];
extern const uint32_t guni_name_primary_index[GUNI_NAME_PRIMARY_COUNT];

/** The aliases, sorted by codepoint. */
extern const uint32_t guni_name_alias_cp[GUNI_NAME_ALIAS_COUNT];
extern const uint8_t guni_name_alias_kind[GUNI_NAME_ALIAS_COUNT];
extern const uint32_t guni_name_alias_index[GUNI_NAME_ALIAS_COUNT];

/** The families whose names are computed: CJK, Tangut, Hangul syllables. */
extern const uint32_t guni_name_range_first[GUNI_NAME_RANGE_COUNT];
extern const uint32_t guni_name_range_last[GUNI_NAME_RANGE_COUNT];
extern const char * const guni_name_range_prefix[GUNI_NAME_RANGE_COUNT];
/** 1 for the Hangul rule, which spells jamo rather than hex. */
extern const uint8_t guni_name_range_hangul[GUNI_NAME_RANGE_COUNT];

/** The jamo short names, by index within their part of a syllable. */
extern const char * const guni_jamo_lead[19];
extern const char * const guni_jamo_vowel[21];
extern const char * const guni_jamo_trail[28];

/** Named sequences, in loose-name order. */
extern const uint16_t guni_sequence_tokens[GUNI_SEQUENCE_TOKEN_COUNT];
extern const uint32_t guni_sequence_offsets[GUNI_SEQUENCE_COUNT + 1];
extern const uint32_t guni_sequence_pool[GUNI_SEQUENCE_POOL_COUNT];
extern const uint32_t guni_sequence_pool_offsets[GUNI_SEQUENCE_COUNT + 1];

#endif // GHOTI_IO_GUNI_NAME_TABLES_H
