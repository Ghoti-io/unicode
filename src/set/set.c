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
 * Properties as sets, and the name lookup that gets to them.
 *
 * The range walk is the interesting part. Every enumerated and binary
 * property is a field of one record, and the runs table gives the maximal
 * ranges over which that record is constant, so "every codepoint whose script
 * is Greek" is a filter over 8,035 runs. Adjacent accepted runs are merged,
 * which is what makes the output depend on the data and not on where the
 * table's block boundaries happen to fall - a caller comparing two versions
 * of this library sees a range change only when a codepoint changed.
 */

#include <ghoti.io/unicode/set.h>
#include <string.h>
#include "../char/tables/tables.h"

/** The longest loose-matched name in the tables, plus room to notice. */
#define GUNI_NAME_MAX 96

/**
 * UAX #44-LM3 loose matching, applied to the query. The tables store names
 * already in this form, so the comparison is a plain strcmp and the rule
 * lives in exactly one place.
 *
 * Returns false when the name cannot match anything: too long, or empty.
 */
static bool normalise(const char * name, size_t len, char * out) {
  if (name == NULL) {
    return false;
  }
  size_t written = 0;
  for (size_t index = 0; index < len; ++index) {
    unsigned char byte = (unsigned char)name[index];
    if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r'
        || byte == '\f' || byte == '\v' || byte == '_' || byte == '-') {
      continue;
    }
    if (byte >= 'A' && byte <= 'Z') {
      byte = (unsigned char)(byte - 'A' + 'a');
    }
    if (written + 1 >= GUNI_NAME_MAX) {
      return false;
    }
    out[written++] = (char)byte;
  }
  out[written] = '\0';
  return written != 0;
}

GUNI_Result guni_property_by_name(const char * name, size_t len,
    GUNI_Property * out) {
  if (out == NULL) {
    return GUNI_ERR_INVALID;
  }
  char key[GUNI_NAME_MAX];
  if (!normalise(name, len, key)) {
    return GUNI_ERR_INVALID;
  }
  size_t low = 0;
  size_t high = GUNI_PROPERTY_ALIAS_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    int order = strcmp(key, guni_property_aliases[middle].name);
    if (order < 0) {
      high = middle;
    }
    else if (order > 0) {
      low = middle + 1;
    }
    else {
      *out = (GUNI_Property)guni_property_aliases[middle].property;
      return GUNI_OK;
    }
  }
  return GUNI_ERR_INVALID;
}

GUNI_Result guni_value_by_name(GUNI_Property property, const char * name,
    size_t len, uint32_t * out) {
  if (out == NULL || (unsigned)property >= GUNI_PROPERTY_COUNT) {
    return GUNI_ERR_INVALID;
  }
  char key[GUNI_NAME_MAX];
  if (!normalise(name, len, key)) {
    return GUNI_ERR_INVALID;
  }
  /* The table is sorted by name and then by property, so a name shared by
   * several properties - "AL" is Above_Left for ccc and Alphabetic for lb -
   * gives a run of entries to scan. Scanning from the first match rather
   * than from the hit keeps the answer independent of where the search
   * landed in that run. */
  size_t low = 0;
  size_t high = GUNI_VALUE_ALIAS_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (strcmp(guni_value_aliases[middle].name, key) < 0) {
      low = middle + 1;
    }
    else {
      high = middle;
    }
  }
  for (size_t index = low;
      index < GUNI_VALUE_ALIAS_COUNT
          && strcmp(guni_value_aliases[index].name, key) == 0;
      ++index) {
    if (guni_value_aliases[index].property == (uint16_t)property) {
      *out = guni_value_aliases[index].value;
      return GUNI_OK;
    }
  }
  return GUNI_ERR_INVALID;
}

const char * guni_property_name(GUNI_Property property) {
  if ((unsigned)property >= GUNI_PROPERTY_COUNT) {
    return NULL;
  }
  return guni_property_info[property].name;
}

const char * guni_value_name(GUNI_Property property, uint32_t value) {
  if ((unsigned)property >= GUNI_PROPERTY_COUNT) {
    return NULL;
  }
  const GuniPropertyInfo * info = &guni_property_info[property];
  if (value >= info->value_count) {
    return NULL;
  }
  return info->value_names[value];
}

GUNI_PropertyKind guni_property_kind(GUNI_Property property) {
  if ((unsigned)property >= GUNI_PROPERTY_COUNT) {
    return GUNI_PROP_KIND_ENUM;
  }
  return (GUNI_PropertyKind)guni_property_info[property].kind;
}

uint32_t guni_property_value_count(GUNI_Property property) {
  if ((unsigned)property >= GUNI_PROPERTY_COUNT) {
    return 0;
  }
  return guni_property_info[property].value_count;
}

bool guni_set_contains(GUNI_Property property, uint32_t value, uint32_t cp) {
  if ((unsigned)property >= GUNI_PROPERTY_COUNT) {
    return false;
  }
  switch (guni_property_info[property].kind) {
    case GUNI_PROP_KIND_SCX:
      return guni_script_extensions_contains(cp, (GUNI_Script)value);
    case GUNI_PROP_KIND_BLOCK:
      return (uint32_t)guni_block(cp) == value;
    default:
      return guni_property_value(cp, property) == value;
  }
}

/**
 * Emit one range, merging it with the previous one when they touch.
 *
 * Counting and writing share this function rather than being two loops,
 * because two loops with the same condition drift: the count and the write
 * would agree until someone changed one of them.
 */
typedef struct {
  GUNI_Range * out;
  size_t cap;
  size_t count;   /* ranges the set has, whether or not they fitted */
  bool overflow;
  bool open;      /* the last emitted range is still extendable */
  uint32_t first;
  uint32_t last;
} RangeSink;

static void sink_flush(RangeSink * sink) {
  if (!sink->open) {
    return;
  }
  if (sink->count < sink->cap) {
    sink->out[sink->count].first = sink->first;
    sink->out[sink->count].last = sink->last;
  }
  else {
    sink->overflow = true;
  }
  ++sink->count;
  sink->open = false;
}

static void sink_add(RangeSink * sink, uint32_t first, uint32_t last) {
  if (sink->open && first == sink->last + 1) {
    sink->last = last;
    return;
  }
  sink_flush(sink);
  sink->open = true;
  sink->first = first;
  sink->last = last;
}

/** The last codepoint of run @p index. */
static uint32_t run_last(size_t index) {
  return (index + 1 < GUNI_PROP_RUN_COUNT)
      ? guni_prop_run_first[index + 1] - 1
      : GUNI_MAX_CODEPOINT;
}

static GUNI_Result finish(RangeSink * sink, size_t * out_len) {
  sink_flush(sink);
  *out_len = sink->count;
  return sink->overflow ? GUNI_ERR_LIMIT : GUNI_OK;
}

GUNI_Result guni_set_ranges(GUNI_Property property, uint32_t value,
    GUNI_Range * out, size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)
      || (unsigned)property >= GUNI_PROPERTY_COUNT) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  RangeSink sink = {out, cap, 0, false, false, 0, 0};
  const GuniPropertyInfo * info = &guni_property_info[property];

  if (info->kind == GUNI_PROP_KIND_BLOCK) {
    /* Blocks are their own range table, and the gaps between them are the
     * one value that is not in it: No_Block. Walking the gaps as well is
     * what makes \p{blk=No_Block} answerable. */
    uint32_t next = 0;
    for (size_t index = 0; index < GUNI_BLOCK_RANGE_COUNT; ++index) {
      if (value == GUNI_BLOCK_NO_BLOCK && guni_block_first[index] > next) {
        sink_add(&sink, next, guni_block_first[index] - 1);
      }
      if (guni_block_id[index] == value) {
        sink_add(&sink, guni_block_first[index], guni_block_last[index]);
      }
      next = guni_block_last[index] + 1;
    }
    /* The tail gap, if the last block does not reach the end of the space.
     * At UCD 17.0.0 it does - Supplementary Private Use Area-B ends at
     * U+10FFFF - so this line is unreachable with today's data and is not
     * dead code: it is the arm a future Blocks.txt needs, and leaving it out
     * would drop codepoints from \p{blk=No_Block} the first time Unicode
     * stops at the boundary. Coverage names it; this comment is the reason. */
    if (value == GUNI_BLOCK_NO_BLOCK && next <= GUNI_MAX_CODEPOINT) {
      sink_add(&sink, next, GUNI_MAX_CODEPOINT);
    }
    return finish(&sink, out_len);
  }

  if (info->kind == GUNI_PROP_KIND_SCX) {
    /* Set membership, so the test is per record rather than an equality, but
     * the walk is the same one. */
    for (size_t index = 0; index < GUNI_PROP_RUN_COUNT; ++index) {
      const uint16_t * set =
          &guni_scx_pool[guni_prop_records[guni_prop_run_record[index]].scx];
      for (size_t member = 0; member < set[0]; ++member) {
        if (set[member + 1] == (uint16_t)value) {
          sink_add(&sink, guni_prop_run_first[index], run_last(index));
          break;
        }
      }
    }
    return finish(&sink, out_len);
  }

  for (size_t index = 0; index < GUNI_PROP_RUN_COUNT; ++index) {
    if (guni_record_property_value(
            &guni_prop_records[guni_prop_run_record[index]], property)
        == value) {
      sink_add(&sink, guni_prop_run_first[index], run_last(index));
    }
  }
  return finish(&sink, out_len);
}

GUNI_Result guni_gc_mask_ranges(uint32_t mask, GUNI_Range * out, size_t cap,
    size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  RangeSink sink = {out, cap, 0, false, false, 0, 0};
  for (size_t index = 0; index < GUNI_PROP_RUN_COUNT; ++index) {
    uint8_t gc = guni_prop_records[guni_prop_run_record[index]].gc;
    if (GUNI_GC_MASK(gc) & mask) {
      sink_add(&sink, guni_prop_run_first[index], run_last(index));
    }
  }
  return finish(&sink, out_len);
}

bool guni_gc_mask_contains(uint32_t mask, uint32_t cp) {
  return (guni_general_category_mask(cp) & mask) != 0;
}
