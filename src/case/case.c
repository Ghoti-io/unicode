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
 * Case mapping, and the five conditions that make it more than a table.
 *
 * `SpecialCasing.txt` has sixteen conditional lines and five distinct
 * conditions between them, and each one is a rule about the text around the
 * character rather than about the character. They are implemented here one
 * function each, with the Standard's own wording in the comment, because the
 * wording is subtle in a way that paraphrase loses: "ignoring characters with
 * the Case_Ignorable property" is doing a lot of work in `Final_Sigma`, and
 * "no intervening character of combining class 0 or 230" is doing all of it in
 * the other four.
 */

#include <ghoti.io/unicode/break.h>
#include <ghoti.io/unicode/case.h>
#include <string.h>
#include "../char/tables/tables.h"

/** The row for a codepoint, or GUNI_CASE_COUNT. */
static size_t case_row(uint32_t cp) {
  /* Gated on the record's own bits: a codepoint with no case data at all -
   * which is nearly all of them - costs one trie lookup and no search. */
  const GuniPropRecord * record = guni_record(cp);
  bool mapped = (record->flags[GUNI_PROP_CHANGES_WHEN_CASEMAPPED >> 5]
                    >> (GUNI_PROP_CHANGES_WHEN_CASEMAPPED & 31u)) & 1u;
  bool folded = (record->flags[GUNI_PROP_CHANGES_WHEN_CASEFOLDED >> 5]
                    >> (GUNI_PROP_CHANGES_WHEN_CASEFOLDED & 31u)) & 1u;
  if (!mapped && !folded) {
    return GUNI_CASE_COUNT;
  }
  size_t low = 0;
  size_t high = GUNI_CASE_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (cp < guni_case_codepoint[middle]) {
      high = middle;
    }
    else if (cp > guni_case_codepoint[middle]) {
      low = middle + 1;
    }
    else {
      return middle;
    }
  }
  /* Unreachable: the property bits and the case table come from one generator
   * run over one file, so a codepoint the bits claim has case data has a row.
   * Here because the alternative to a guard is using GUNI_CASE_COUNT as an
   * index. */
  return GUNI_CASE_COUNT;
}

/** A simple mapping: the table's value, or the codepoint itself. */
static uint32_t simple(uint32_t cp, int column) {
  size_t row = case_row(cp);
  if (row == GUNI_CASE_COUNT) {
    return cp;
  }
  uint32_t mapped = guni_case_simple[row][column];
  return (mapped != 0) ? mapped : cp;
}

uint32_t guni_to_upper_simple(uint32_t cp) {
  return simple(cp, GUNI_CASE_UPPER);
}

uint32_t guni_to_lower_simple(uint32_t cp) {
  return simple(cp, GUNI_CASE_LOWER);
}

uint32_t guni_to_title_simple(uint32_t cp) {
  /* UnicodeData.txt leaves the titlecase field empty when it is the same as
   * the uppercase, which is all but the thirty-one digraphs. */
  size_t row = case_row(cp);
  if (row == GUNI_CASE_COUNT) {
    return cp;
  }
  uint32_t title = guni_case_simple[row][GUNI_CASE_TITLE];
  if (title != 0) {
    return title;
  }
  uint32_t upper = guni_case_simple[row][GUNI_CASE_UPPER];
  return (upper != 0) ? upper : cp;
}

uint32_t guni_case_fold_simple(uint32_t cp) {
  return simple(cp, GUNI_CASE_FOLD);
}

uint32_t guni_case_fold_simple_turkic(uint32_t cp) {
  for (size_t index = 0; index < GUNI_TURKIC_FOLD_COUNT; ++index) {
    if (guni_turkic_fold_from[index] == cp) {
      return guni_turkic_fold_to[index];
    }
  }
  return guni_case_fold_simple(cp);
}

GUNI_Result guni_case_orbit(uint32_t cp, uint32_t * out, size_t cap,
    size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)) {
    return GUNI_ERR_INVALID;
  }
  uint32_t value = guni_case_fold_simple(cp);
  size_t low = 0;
  size_t high = GUNI_ORBIT_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (value < guni_orbit_value[middle]) {
      high = middle;
    }
    else if (value > guni_orbit_value[middle]) {
      low = middle + 1;
    }
    else {
      size_t count = guni_orbit_length[middle];
      *out_len = count;
      if (count > cap) {
        return GUNI_ERR_LIMIT;
      }
      memcpy(out, &guni_orbit_pool[guni_orbit_offset[middle]],
          count * sizeof(uint32_t));
      return GUNI_OK;
    }
  }
  /* A codepoint that shares its fold with nothing is its own orbit, which is
   * one member and not zero: a caller building a character class adds the
   * orbit unconditionally. */
  *out_len = 1;
  if (cap < 1) {
    return GUNI_ERR_LIMIT;
  }
  out[0] = cp;
  return GUNI_OK;
}

// --------------------------------------------------------------------------
// The conditions
//
// Each takes the text and the position of the character being mapped. The
// Standard's wording is in each comment; "C" is that character.
// --------------------------------------------------------------------------

static bool is_case_ignorable(uint32_t cp) {
  return guni_has_property(cp, GUNI_PROP_CASE_IGNORABLE);
}

static bool is_cased(uint32_t cp) {
  return guni_has_property(cp, GUNI_PROP_CASED);
}

/**
 * Final_Sigma: "C is preceded by a sequence consisting of a cased letter and
 * then zero or more case-ignorable characters, and C is not followed by a
 * sequence consisting of zero or more case-ignorable characters and then a
 * cased letter."
 *
 * This is the one condition that looks both ways, and the reason the full
 * mappings take the whole text.
 */
static bool final_sigma(const uint32_t * text, size_t len, size_t position) {
  bool before = false;
  for (size_t index = position; index > 0; --index) {
    uint32_t cp = text[index - 1];
    if (is_case_ignorable(cp)) {
      continue;
    }
    before = is_cased(cp);
    break;
  }
  if (!before) {
    return false;
  }
  for (size_t index = position + 1; index < len; ++index) {
    uint32_t cp = text[index];
    if (is_case_ignorable(cp)) {
      continue;
    }
    return !is_cased(cp);
  }
  return true;
}

/*
 * The "no intervening character of combining class 0 or 230" walk that
 * After_Soft_Dotted, After_I, More_Above and Not_Before_Dot all share, in the
 * two directions the conditions look.
 *
 * Both return 0 when the walk runs out, and 0 is not a codepoint any of these
 * conditions matches, so no caller has to tell "nothing there" from "something
 * that does not match". They are two functions rather than one with a
 * direction flag because only the forward callers want the combining class,
 * and the one-function version had a line no caller could reach.
 */

static uint32_t previous_relevant(const uint32_t * text, size_t position) {
  for (size_t index = position; index > 0; --index) {
    uint8_t combining = guni_combining_class(text[index - 1]);
    if (combining == 0 || combining == 230) {
      return text[index - 1];
    }
  }
  return 0;
}

static uint32_t next_relevant(const uint32_t * text, size_t len,
    size_t position, uint8_t * combining_out) {
  for (size_t index = position + 1; index < len; ++index) {
    uint8_t combining = guni_combining_class(text[index]);
    if (combining == 0 || combining == 230) {
      *combining_out = combining;
      return text[index];
    }
  }
  *combining_out = 0;
  return 0;
}

/**
 * After_Soft_Dotted: "There is a Soft_Dotted character before C, with no
 * intervening character of combining class 0 or 230 (Above)."
 */
static bool after_soft_dotted(const uint32_t * text, size_t len,
    size_t position) {
  (void)len;
  uint32_t found = previous_relevant(text, position);
  return found != 0 && guni_has_property(found, GUNI_PROP_SOFT_DOTTED);
}

/**
 * More_Above: "C is followed by a character of combining class 230 (Above)
 * with no intervening character of combining class 0 or 230."
 */
static bool more_above(const uint32_t * text, size_t len, size_t position) {
  uint8_t combining = 0;
  uint32_t found = next_relevant(text, len, position, &combining);
  return found != 0 && combining == 230;
}

/**
 * After_I: "There is an uppercase I before C, and there is no intervening
 * combining character class 230 (Above) or 0."
 */
static bool after_i(const uint32_t * text, size_t len, size_t position) {
  (void)len;
  return previous_relevant(text, position) == UINT32_C(0x0049);
}

/**
 * Not_Before_Dot: "C is not followed by U+0307 COMBINING DOT ABOVE. Any
 * sequence of characters with a combining class that is neither 0 nor 230 may
 * intervene between the current character and the combining dot above."
 */
static bool not_before_dot(const uint32_t * text, size_t len, size_t position) {
  uint8_t combining = 0;
  return next_relevant(text, len, position, &combining) != UINT32_C(0x0307);
}

static bool condition_holds(uint8_t condition, const uint32_t * text,
    size_t len, size_t position) {
  switch (condition) {
    case GUNI_CASE_COND_NONE:
      return true;
    case GUNI_CASE_COND_FINAL_SIGMA:
      return final_sigma(text, len, position);
    case GUNI_CASE_COND_AFTER_SOFT_DOTTED:
      return after_soft_dotted(text, len, position);
    case GUNI_CASE_COND_MORE_ABOVE:
      return more_above(text, len, position);
    case GUNI_CASE_COND_AFTER_I:
      return after_i(text, len, position);
    case GUNI_CASE_COND_NOT_BEFORE_DOT:
      return not_before_dot(text, len, position);
    default:
      /* Unreachable: the generator refuses a condition it does not know (see
       * CASE_CONDITIONS there), so this arm exists only because C needs one. */
      return false;
  }
}

/**
 * The full mapping of one character, into a buffer of GUNI_CASE_MAX_EXPANSION.
 *
 * The order is the Standard's: a conditional mapping whose language and
 * condition both match wins over the unconditional full mapping, which wins
 * over the simple one. The conditional table is sixteen rows, so it is scanned
 * rather than indexed - and the scan only happens for a codepoint that has a
 * case row at all.
 */
static size_t full_mapping(const uint32_t * text, size_t len, size_t position,
    int column, GUNI_CaseTailoring tailoring, uint32_t * buffer) {
  uint32_t cp = text[position];
  for (size_t index = 0; index < GUNI_CASE_CONDITIONAL_COUNT; ++index) {
    const GuniCaseConditional * entry = &guni_case_conditional[index];
    if (entry->codepoint != cp) {
      continue;
    }
    if (entry->language != GUNI_LANG_NONE
        && entry->language != (uint8_t)tailoring) {
      continue;
    }
    if (!condition_holds(entry->condition, text, len, position)) {
      continue;
    }
    size_t length = entry->length[column];
    /* A conditional line can map to nothing at all - Lithuanian drops a
     * combining dot above when it upper-cases - and zero is the answer, not
     * "no mapping". */
    for (size_t part = 0; part < length; ++part) {
      buffer[part] = guni_case_pool[entry->offset[column] + part];
    }
    return length;
  }

  size_t row = case_row(cp);
  if (row == GUNI_CASE_COUNT) {
    buffer[0] = cp;
    return 1;
  }
  size_t length = guni_case_full_length[row][column];
  if (length != 0) {
    for (size_t part = 0; part < length; ++part) {
      buffer[part] = guni_case_pool[guni_case_full_offset[row][column] + part];
    }
    return length;
  }
  uint32_t mapped = guni_case_simple[row][column];
  if (column == GUNI_CASE_TITLE && mapped == 0) {
    mapped = guni_case_simple[row][GUNI_CASE_UPPER];
  }
  buffer[0] = (mapped != 0) ? mapped : cp;
  return 1;
}

static GUNI_Result mapping_at(const uint32_t * text, size_t len,
    size_t position, int column, GUNI_CaseTailoring tailoring, uint32_t * out,
    size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0) || text == NULL
      || position >= len) {
    return GUNI_ERR_INVALID;
  }
  uint32_t buffer[GUNI_CASE_MAX_EXPANSION];
  size_t length = full_mapping(text, len, position, column, tailoring, buffer);
  *out_len = length;
  if (length > cap) {
    return GUNI_ERR_LIMIT;
  }
  memcpy(out, buffer, length * sizeof(uint32_t));
  return GUNI_OK;
}

GUNI_Result guni_to_upper_at(const uint32_t * text, size_t len, size_t position,
    GUNI_CaseTailoring tailoring, uint32_t * out, size_t cap, size_t * out_len) {
  return mapping_at(text, len, position, GUNI_CASE_UPPER, tailoring, out, cap,
      out_len);
}

GUNI_Result guni_to_lower_at(const uint32_t * text, size_t len, size_t position,
    GUNI_CaseTailoring tailoring, uint32_t * out, size_t cap, size_t * out_len) {
  return mapping_at(text, len, position, GUNI_CASE_LOWER, tailoring, out, cap,
      out_len);
}

GUNI_Result guni_to_title_at(const uint32_t * text, size_t len, size_t position,
    GUNI_CaseTailoring tailoring, uint32_t * out, size_t cap, size_t * out_len) {
  return mapping_at(text, len, position, GUNI_CASE_TITLE, tailoring, out, cap,
      out_len);
}

// --------------------------------------------------------------------------
// Whole strings
// --------------------------------------------------------------------------

/** The full fold of one character, which has no conditions. */
static size_t fold_mapping(uint32_t cp, bool turkic, uint32_t * buffer) {
  if (turkic) {
    for (size_t index = 0; index < GUNI_TURKIC_FOLD_COUNT; ++index) {
      if (guni_turkic_fold_from[index] == cp) {
        buffer[0] = guni_turkic_fold_to[index];
        return 1;
      }
    }
  }
  size_t row = case_row(cp);
  if (row == GUNI_CASE_COUNT) {
    buffer[0] = cp;
    return 1;
  }
  size_t length = guni_case_full_length[row][GUNI_CASE_FOLD];
  if (length != 0) {
    for (size_t part = 0; part < length; ++part) {
      buffer[part] =
          guni_case_pool[guni_case_full_offset[row][GUNI_CASE_FOLD] + part];
    }
    return length;
  }
  uint32_t mapped = guni_case_simple[row][GUNI_CASE_FOLD];
  buffer[0] = (mapped != 0) ? mapped : cp;
  return 1;
}

static const GUNI_Limits * effective_limits(const GUNI_Limits * limits,
    GUNI_Limits * storage) {
  if (limits != NULL) {
    return limits;
  }
  guni_limits_default(storage);
  return storage;
}

/**
 * Map a whole string, one character at a time.
 *
 * @param column The case column, or -1 for folding.
 * @param title_starts For title-casing: which positions take the titlecase
 *        mapping rather than the lowercase one. NULL for the other mappings.
 */
static GUNI_Result map_string(const uint32_t * text, size_t len, int column,
    bool turkic, GUNI_CaseTailoring tailoring, const GUNI_Limits * limits,
    const unsigned char * title_starts, uint32_t * out, size_t cap,
    size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0) || (text == NULL && len != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  GUNI_Limits storage;
  const GUNI_Limits * active = effective_limits(limits, &storage);
  if (len > active->max_text_bytes / 4) {
    return GUNI_ERR_LIMIT;
  }
  size_t written = 0;
  bool overflow = false;
  for (size_t index = 0; index < len; ++index) {
    uint32_t buffer[GUNI_CASE_MAX_EXPANSION];
    size_t length;
    if (column < 0) {
      length = fold_mapping(text[index], turkic, buffer);
    }
    else if (title_starts != NULL) {
      length = full_mapping(text, len, index,
          title_starts[index] ? GUNI_CASE_TITLE : GUNI_CASE_LOWER, tailoring,
          buffer);
    }
    else {
      length = full_mapping(text, len, index, column, tailoring, buffer);
    }
    for (size_t part = 0; part < length; ++part) {
      if (written < cap) {
        out[written] = buffer[part];
      }
      else {
        overflow = true;
      }
      ++written;
    }
  }
  *out_len = written;
  return overflow ? GUNI_ERR_LIMIT : GUNI_OK;
}

GUNI_Result guni_to_upper(const uint32_t * text, size_t len,
    GUNI_CaseTailoring tailoring, const GUNI_Limits * limits, uint32_t * out,
    size_t cap, size_t * out_len) {
  return map_string(text, len, GUNI_CASE_UPPER, false, tailoring, limits, NULL,
      out, cap, out_len);
}

GUNI_Result guni_to_lower(const uint32_t * text, size_t len,
    GUNI_CaseTailoring tailoring, const GUNI_Limits * limits, uint32_t * out,
    size_t cap, size_t * out_len) {
  return map_string(text, len, GUNI_CASE_LOWER, false, tailoring, limits, NULL,
      out, cap, out_len);
}

GUNI_Result guni_case_fold(const uint32_t * text, size_t len, bool turkic,
    const GUNI_Limits * limits, uint32_t * out, size_t cap, size_t * out_len) {
  return map_string(text, len, -1, turkic, GUNI_LANG_NONE, limits, NULL, out,
      cap, out_len);
}

/**
 * Which positions begin a word, for title-casing.
 *
 * UAX #21's toTitlecase: the first cased character of each word takes the
 * titlecase mapping and the rest take the lowercase. "Word" is UAX #29's, so
 * this is the one case function that reads break.h - and it is why
 * title-casing cannot be done a character at a time by a caller who only has
 * this module.
 *
 * The buffer is the caller's, sized by the text, so this allocates nothing;
 * the string entry point below keeps it on the stack for short text and
 * refuses longer text rather than allocating behind the caller's back.
 */
static void mark_title_starts(const uint32_t * text, size_t len,
    unsigned char * starts) {
  memset(starts, 0, len);
  GUNI_BreakOptions options;
  memset(&options, 0, sizeof(options));
  options.kind = GUNI_BREAK_WORD;
  GUNI_BreakIter iter;
  guni_break_iter_init_codepoints(&iter, &options, text, len);
  size_t boundary = 0;
  size_t previous = 0;
  bool have_previous = false;
  while (guni_break_iter_next(&iter, &boundary)) {
    if (have_previous) {
      /* The first cased character of the word that runs from `previous` to
       * `boundary`. A word of punctuation has none, and nothing in it is
       * title-cased. */
      for (size_t index = previous; index < boundary && index < len; ++index) {
        if (is_cased(text[index])) {
          starts[index] = 1;
          break;
        }
      }
    }
    previous = boundary;
    have_previous = true;
  }
}

/** How much text guni_to_title() handles without an allocator. */
#define GUNI_TITLE_MAX_STACK 1024

GUNI_Result guni_to_title(const uint32_t * text, size_t len,
    GUNI_CaseTailoring tailoring, const GUNI_Limits * limits, uint32_t * out,
    size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0) || (text == NULL && len != 0)) {
    return GUNI_ERR_INVALID;
  }
  if (len > GUNI_TITLE_MAX_STACK) {
    /* One byte of working state per character, to remember which positions
     * begin a word. Refused rather than allocated: this is the only case
     * function that needs any, and a caller with more text than this is
     * title-casing a document, which it can do a paragraph at a time. */
    *out_len = 0;
    return GUNI_ERR_LIMIT;
  }
  unsigned char starts[GUNI_TITLE_MAX_STACK];
  mark_title_starts(text, len, starts);
  return map_string(text, len, GUNI_CASE_TITLE, false, tailoring, limits,
      starts, out, cap, out_len);
}

// --------------------------------------------------------------------------
// UTF-8
// --------------------------------------------------------------------------

/** How much UTF-8 the string entry points decode at once. */
#define GUNI_CASE_CHUNK 512

/**
 * The UTF-8 entry points decode into a fixed buffer, map, and encode.
 *
 * Chunked at **word boundaries** rather than anywhere, because the conditions
 * look at the characters around the one being mapped and title-casing looks at
 * whole words: a chunk that split a word would lower-case a letter that should
 * have been title-cased, and one that split at a case-ignorable character
 * could get Final_Sigma wrong. A run longer than the buffer with no word
 * boundary in it is refused.
 */
static GUNI_Result map_utf8(const char * text, size_t len, GUNI_Invalid policy,
    int column, bool turkic, GUNI_CaseTailoring tailoring,
    const GUNI_Limits * limits, char * out, size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0) || (text == NULL && len != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  GUNI_Limits storage;
  const GUNI_Limits * active = effective_limits(limits, &storage);
  if (len > active->max_text_bytes) {
    return GUNI_ERR_LIMIT;
  }

  GUNI_BreakOptions words;
  memset(&words, 0, sizeof(words));
  words.kind = GUNI_BREAK_WORD;

  uint32_t chunk[GUNI_CASE_CHUNK];
  uint32_t mapped[GUNI_CASE_CHUNK * GUNI_CASE_MAX_EXPANSION];
  unsigned char starts[GUNI_CASE_CHUNK];
  size_t offset = 0;
  size_t written = 0;
  bool overflow = false;

  while (offset < len) {
    /* Fill a chunk, remembering the last word boundary it contained. */
    size_t count = 0;
    size_t scan = offset;
    size_t boundary_count = 0;
    size_t boundary_offset = offset;
    while (scan < len && count < GUNI_CASE_CHUNK) {
      uint32_t cp = 0;
      bool valid = false;
      size_t used = guni_utf8_decode(text + scan, len - scan, &cp, &valid);
      if (!valid) {
        if (policy == GUNI_INVALID_REFUSE) {
          return GUNI_ERR_INVALID;
        }
        if (policy == GUNI_INVALID_SKIP) {
          scan += used;
          continue;
        }
        cp = GUNI_REPLACEMENT_CHARACTER;
      }
      if (count != 0 && guni_break_at(&words, text, len, scan)) {
        boundary_count = count;
        boundary_offset = scan;
      }
      chunk[count++] = cp;
      scan += used;
    }
    size_t take = count;
    size_t next = scan;
    if (scan < len) {
      if (boundary_count == 0) {
        /* A single word longer than the buffer. Refused rather than split:
         * splitting it would change the answer, which is worse. */
        return GUNI_ERR_LIMIT;
      }
      take = boundary_count;
      next = boundary_offset;
    }
    if (take == 0) {
      break; /* every remaining byte was skipped */
    }

    size_t produced = 0;
    GUNI_Result result;
    if (column == GUNI_CASE_TITLE) {
      mark_title_starts(chunk, take, starts);
      result = map_string(chunk, take, column, false, tailoring, limits, starts,
          mapped, sizeof(mapped) / sizeof(*mapped), &produced);
    }
    else {
      result = map_string(chunk, take, column, turkic, tailoring, limits, NULL,
          mapped, sizeof(mapped) / sizeof(*mapped), &produced);
    }
    if (result != GUNI_OK) {
      /* Unreachable: the chunk is at most GUNI_CASE_CHUNK characters and the
       * buffer holds that times the expansion bound, so map_string() cannot
       * overflow it. Here because the two constants are independent. */
      return result;
    }
    for (size_t index = 0; index < produced; ++index) {
      char bytes[GUNI_UTF8_MAX_LENGTH];
      size_t used = guni_utf8_encode(mapped[index], bytes);
      if (used == 0) {
        /* Unreachable: every codepoint a case mapping produces is encodable.
         * Here so that a table that ever said otherwise could not write a
         * zero-length character into the output. */
        used = guni_utf8_encode(GUNI_REPLACEMENT_CHARACTER, bytes);
      }
      if (written + used <= cap) {
        memcpy(out + written, bytes, used);
      }
      else {
        overflow = true;
      }
      written += used;
    }
    if (next <= offset) {
      return GUNI_ERR_INTERNAL; /* no progress would loop forever */
    }
    offset = next;
  }
  *out_len = written;
  return overflow ? GUNI_ERR_LIMIT : GUNI_OK;
}

GUNI_Result guni_to_upper_utf8(const char * text, size_t len,
    GUNI_Invalid policy, GUNI_CaseTailoring tailoring,
    const GUNI_Limits * limits, char * out, size_t cap, size_t * out_len) {
  return map_utf8(text, len, policy, GUNI_CASE_UPPER, false, tailoring, limits,
      out, cap, out_len);
}

GUNI_Result guni_to_lower_utf8(const char * text, size_t len,
    GUNI_Invalid policy, GUNI_CaseTailoring tailoring,
    const GUNI_Limits * limits, char * out, size_t cap, size_t * out_len) {
  return map_utf8(text, len, policy, GUNI_CASE_LOWER, false, tailoring, limits,
      out, cap, out_len);
}

GUNI_Result guni_to_title_utf8(const char * text, size_t len,
    GUNI_Invalid policy, GUNI_CaseTailoring tailoring,
    const GUNI_Limits * limits, char * out, size_t cap, size_t * out_len) {
  return map_utf8(text, len, policy, GUNI_CASE_TITLE, false, tailoring, limits,
      out, cap, out_len);
}

GUNI_Result guni_case_fold_utf8(const char * text, size_t len,
    GUNI_Invalid policy, bool turkic, const GUNI_Limits * limits, char * out,
    size_t cap, size_t * out_len) {
  return map_utf8(text, len, policy, -1, turkic, GUNI_LANG_NONE, limits, out,
      cap, out_len);
}

// --------------------------------------------------------------------------
// Comparison
// --------------------------------------------------------------------------

/**
 * A cursor that yields the folded characters of a string one at a time.
 *
 * What makes guni_case_folded_equal() allocate nothing and stop at the first
 * difference: folding a character can produce three, so the comparison needs
 * somewhere to hold them, and that somewhere is three words on the stack.
 */
typedef struct {
  const uint32_t * text;
  size_t length;
  size_t position;
  uint32_t buffer[GUNI_CASE_MAX_EXPANSION];
  size_t held;
  size_t taken;
  bool turkic;
} FoldCursor;

static bool fold_next(FoldCursor * cursor, uint32_t * out) {
  while (cursor->taken >= cursor->held) {
    if (cursor->position >= cursor->length) {
      return false;
    }
    cursor->held = fold_mapping(cursor->text[cursor->position], cursor->turkic,
        cursor->buffer);
    cursor->taken = 0;
    ++cursor->position;
  }
  *out = cursor->buffer[cursor->taken++];
  return true;
}

bool guni_case_folded_equal(const uint32_t * left, size_t left_len,
    const uint32_t * right, size_t right_len, bool turkic) {
  if ((left == NULL && left_len != 0) || (right == NULL && right_len != 0)) {
    return false;
  }
  FoldCursor a = {left, left_len, 0, {0, 0, 0}, 0, 0, turkic};
  FoldCursor b = {right, right_len, 0, {0, 0, 0}, 0, 0, turkic};
  for (;;) {
    uint32_t one = 0;
    uint32_t two = 0;
    bool have_one = fold_next(&a, &one);
    bool have_two = fold_next(&b, &two);
    if (!have_one || !have_two) {
      return have_one == have_two;
    }
    if (one != two) {
      return false;
    }
  }
}

bool guni_case_folded_equal_utf8(const char * left, size_t left_len,
    const char * right, size_t right_len, bool turkic) {
  if ((left == NULL && left_len != 0) || (right == NULL && right_len != 0)) {
    return false;
  }
  /* The same comparison over UTF-8, decoding one character at a time so that
   * neither side is copied and a difference in the first character costs
   * nothing. Ill-formed bytes never match anything, including each other:
   * two strings that are not text are not the same identifier. */
  size_t left_at = 0;
  size_t right_at = 0;
  uint32_t left_buffer[GUNI_CASE_MAX_EXPANSION];
  uint32_t right_buffer[GUNI_CASE_MAX_EXPANSION];
  size_t left_held = 0;
  size_t left_taken = 0;
  size_t right_held = 0;
  size_t right_taken = 0;
  for (;;) {
    while (left_taken >= left_held) {
      if (left_at >= left_len) {
        break;
      }
      uint32_t cp = 0;
      bool valid = false;
      size_t used = guni_utf8_decode(left + left_at, left_len - left_at, &cp,
          &valid);
      if (!valid) {
        return false;
      }
      left_at += used;
      left_held = fold_mapping(cp, turkic, left_buffer);
      left_taken = 0;
    }
    while (right_taken >= right_held) {
      if (right_at >= right_len) {
        break;
      }
      uint32_t cp = 0;
      bool valid = false;
      size_t used = guni_utf8_decode(right + right_at, right_len - right_at,
          &cp, &valid);
      if (!valid) {
        return false;
      }
      right_at += used;
      right_held = fold_mapping(cp, turkic, right_buffer);
      right_taken = 0;
    }
    bool have_left = left_taken < left_held;
    bool have_right = right_taken < right_held;
    if (!have_left || !have_right) {
      return have_left == have_right;
    }
    if (left_buffer[left_taken++] != right_buffer[right_taken++]) {
      return false;
    }
  }
}
