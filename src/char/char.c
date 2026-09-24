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
 * The property accessors. Each is a field of the record the trie in
 * src/char/tables/tables.h found, so each costs the same three loads.
 *
 * There is deliberately no validation in this file. guni_record() answers for
 * every uint32_t, so there is no input that reaches a table index it should
 * not, and a range check per accessor would be a branch on the hot path
 * buying nothing (design.md section 4.4).
 */

#include <ghoti.io/unicode/char.h>
#include "tables/tables.h"

const char * guni_ucd_version(void) {
  return GUNI_UCD_VERSION_STRING;
}

unsigned guni_ucd_version_number(void) {
  return GUNI_UCD_VERSION_NUMBER;
}

GUNI_GeneralCategory guni_general_category(uint32_t cp) {
  return (GUNI_GeneralCategory)guni_record(cp)->gc;
}

uint32_t guni_general_category_mask(uint32_t cp) {
  return GUNI_GC_MASK(guni_record(cp)->gc);
}

GUNI_Script guni_script(uint32_t cp) {
  return (GUNI_Script)guni_record(cp)->script;
}

GUNI_Result guni_script_extensions(uint32_t cp, GUNI_Script * out, size_t cap,
    size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)) {
    return GUNI_ERR_INVALID;
  }
  const uint16_t * set = &guni_scx_pool[guni_record(cp)->scx];
  size_t count = set[0];
  *out_len = count;
  if (count > cap) {
    return GUNI_ERR_LIMIT;
  }
  for (size_t index = 0; index < count; ++index) {
    out[index] = (GUNI_Script)set[index + 1];
  }
  return GUNI_OK;
}

bool guni_script_extensions_contains(uint32_t cp, GUNI_Script script) {
  const uint16_t * set = &guni_scx_pool[guni_record(cp)->scx];
  size_t count = set[0];
  for (size_t index = 0; index < count; ++index) {
    if (set[index + 1] == (uint16_t)script) {
      return true;
    }
  }
  return false;
}

uint8_t guni_combining_class(uint32_t cp) {
  return guni_record(cp)->ccc;
}

GUNI_BidiClass guni_bidi_class(uint32_t cp) {
  return (GUNI_BidiClass)guni_record(cp)->bidi_class;
}

GUNI_EastAsianWidth guni_east_asian_width(uint32_t cp) {
  return (GUNI_EastAsianWidth)guni_record(cp)->eaw;
}

GUNI_LineBreak guni_line_break(uint32_t cp) {
  return (GUNI_LineBreak)guni_record(cp)->line_break;
}

GUNI_GraphemeClusterBreak guni_grapheme_cluster_break(uint32_t cp) {
  return (GUNI_GraphemeClusterBreak)guni_record(cp)->gcb;
}

GUNI_WordBreak guni_word_break(uint32_t cp) {
  return (GUNI_WordBreak)guni_record(cp)->wb;
}

GUNI_SentenceBreak guni_sentence_break(uint32_t cp) {
  return (GUNI_SentenceBreak)guni_record(cp)->sb;
}

GUNI_JoiningType guni_joining_type(uint32_t cp) {
  return (GUNI_JoiningType)guni_record(cp)->joining_type;
}

GUNI_JoiningGroup guni_joining_group(uint32_t cp) {
  return (GUNI_JoiningGroup)guni_record(cp)->joining_group;
}

GUNI_IndicSyllabicCategory guni_indic_syllabic_category(uint32_t cp) {
  return (GUNI_IndicSyllabicCategory)guni_record(cp)->insc;
}

GUNI_IndicPositionalCategory guni_indic_positional_category(uint32_t cp) {
  return (GUNI_IndicPositionalCategory)guni_record(cp)->inpc;
}

GUNI_IndicConjunctBreak guni_indic_conjunct_break(uint32_t cp) {
  return (GUNI_IndicConjunctBreak)guni_record(cp)->incb;
}

GUNI_VerticalOrientation guni_vertical_orientation(uint32_t cp) {
  return (GUNI_VerticalOrientation)guni_record(cp)->vo;
}

GUNI_HangulSyllableType guni_hangul_syllable_type(uint32_t cp) {
  return (GUNI_HangulSyllableType)guni_record(cp)->hst;
}

GUNI_DecompositionType guni_decomposition_type(uint32_t cp) {
  return (GUNI_DecompositionType)guni_record(cp)->dt;
}

GUNI_NumericType guni_numeric_type(uint32_t cp) {
  return (GUNI_NumericType)guni_record(cp)->nt;
}

bool guni_has_property(uint32_t cp, GUNI_BinaryProperty property) {
  if ((unsigned)property >= GUNI_PROP_COUNT) {
    return false;
  }
  const GuniPropRecord * record = guni_record(cp);
  return (record->flags[(unsigned)property >> 5]
      >> ((unsigned)property & 31u)) & 1u;
}

/**
 * Binary search a sorted, non-overlapping range table. Blocks and numeric
 * values are ranges rather than record fields, so they are the two properties
 * that cost a search: 9 iterations for Blocks, 12 for Numeric_Value.
 */
static bool find_range(const uint32_t * first, const uint32_t * last,
    size_t count, uint32_t cp, size_t * index_out) {
  size_t low = 0;
  size_t high = count;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (cp < first[middle]) {
      high = middle;
    }
    else if (cp > last[middle]) {
      low = middle + 1;
    }
    else {
      *index_out = middle;
      return true;
    }
  }
  return false;
}

GUNI_Block guni_block(uint32_t cp) {
  size_t index = 0;
  if (cp > GUNI_MAX_CODEPOINT
      || !find_range(guni_block_first, guni_block_last,
          GUNI_BLOCK_RANGE_COUNT, cp, &index)) {
    return GUNI_BLOCK_NO_BLOCK;
  }
  return (GUNI_Block)guni_block_id[index];
}

bool guni_numeric_value(uint32_t cp, int64_t * numerator,
    uint32_t * denominator) {
  if (numerator == NULL || denominator == NULL) {
    return false;
  }
  size_t index = 0;
  if (cp > GUNI_MAX_CODEPOINT
      || !find_range(guni_numeric_first, guni_numeric_last,
          GUNI_NUMERIC_RANGE_COUNT, cp, &index)) {
    return false;
  }
  *numerator = guni_numeric_numerator[index];
  *denominator = guni_numeric_denominator[index];
  return true;
}

uint32_t guni_property_value(uint32_t cp, GUNI_Property property) {
  if ((unsigned)property >= GUNI_PROPERTY_COUNT) {
    return 0;
  }
  switch (guni_property_info[property].kind) {
    case GUNI_PROP_KIND_BLOCK:
      return (uint32_t)guni_block(cp);
    case GUNI_PROP_KIND_SCX:
      /* Set-valued: see the header. */
      return 0;
    default:
      return guni_record_property_value(guni_record(cp), property);
  }
}
