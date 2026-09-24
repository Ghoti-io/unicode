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
 * UTS #39 script runs and the shaper's itemisation.
 *
 * The run check is a port of `regex/src/unicode/script_run.c`, whose comments
 * record what each line is for; the arithmetic is unchanged and the table it
 * reads is this library's. The itemiser is new, and is what `font` will call.
 */

#include <ghoti.io/unicode/script.h>
#include <string.h>
#include "../char/tables/tables.h"

/** No digit block seen yet. */
#define NO_DIGIT_BLOCK UINT32_C(0xFFFFFFFF)

/** The augmented script set of a codepoint, and what kind of set it is. */
typedef struct {
  const uint64_t * mask;
  size_t count;      ///< How many scripts the Script_Extensions set names.
  uint32_t single;   ///< The one script, when count is 1.
} ScriptSet;

static ScriptSet script_set_of(uint32_t cp) {
  const GuniPropRecord * record = guni_record(cp);
  uint16_t offset = record->scx;
  const uint16_t * set = &guni_scx_pool[offset];
  ScriptSet out;
  out.mask = guni_script_run_sets[guni_script_run_by_offset[offset]];
  out.count = set[0];
  out.single = (set[0] == 1) ? set[1] : 0;
  return out;
}

/**
 * The block of ten this decimal digit belongs to, or NO_DIGIT_BLOCK.
 *
 * The digit rule is independent of the script rule and applies even to a
 * Common character, which every ASCII digit is: `0` and the Arabic-Indic `٠`
 * both pass the script half and are two different sets of ten.
 */
static uint32_t digit_block_of(uint32_t cp) {
  size_t low = 0;
  size_t high = GUNI_DIGIT_ZERO_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    uint32_t zero = guni_digit_zeros[middle];
    if (cp < zero) {
      high = middle;
    }
    else if (cp > zero + 9) {
      low = middle + 1;
    }
    else {
      return zero;
    }
  }
  return NO_DIGIT_BLOCK;
}

void guni_script_run_begin(GUNI_ScriptRun * state) {
  if (state == NULL) {
    return;
  }
  for (size_t word = 0; word < GUNI_SCRIPT_RUN_WORDS; ++word) {
    state->intersection[word] = ~(uint64_t)0;
  }
  state->digits = NO_DIGIT_BLOCK;
  state->unknown = false;
  state->count = 0;
}

bool guni_script_run_add(GUNI_ScriptRun * state, uint32_t cp) {
  if (state == NULL) {
    return false;
  }

  uint32_t block = digit_block_of(cp);
  if (block != NO_DIGIT_BLOCK) {
    if (state->digits == NO_DIGIT_BLOCK) {
      state->digits = block;
    }
    else if (state->digits != block) {
      return false;
    }
  }

  ScriptSet set = script_set_of(cp);
  ++state->count;

  /* "A string that is less than two characters long is a script run. This is
   * the only case in which an Unknown character can be part of one." That is a
   * rule and not a consequence: a single unassigned codepoint has the Unknown
   * script, which intersects nothing.
   *
   * Remembered rather than answered on the spot, because the character that
   * makes the string long enough to matter may arrive later - and it may be
   * Common, which takes the early return below without looking at what came
   * before it. */
  if (set.count == 1 && set.single == (uint32_t)GUNI_SCRIPT_UNKNOWN) {
    state->unknown = true;
    return state->count < 2;
  }
  if (state->unknown) {
    return false;
  }

  /* Exactly Inherited is always accepted; exactly Common is accepted subject
   * to the digit rule, which was applied above. Neither narrows the
   * intersection, and this is checked *before* the length rule rather than
   * after it: a run beginning with a full stop constrains no script, and
   * letting the first character write its set into the intersection anyway
   * made ".a" fail where every other implementation accepts it. */
  if (set.count == 1
      && (set.single == (uint32_t)GUNI_SCRIPT_COMMON
          || set.single == (uint32_t)GUNI_SCRIPT_INHERITED)) {
    return true;
  }

  uint64_t remaining = 0;
  for (size_t word = 0; word < GUNI_SCRIPT_RUN_WORDS; ++word) {
    state->intersection[word] &= set.mask[word];
    remaining |= state->intersection[word];
  }
  /* Under two characters the answer is yes whatever the intersection says, and
   * the intersection is still written: it is what the *next* character will be
   * checked against. */
  return state->count < 2 || remaining != 0;
}

bool guni_script_run(const uint32_t * text, size_t len) {
  if (len < 2) {
    return true;
  }
  if (text == NULL) {
    return false;
  }
  GUNI_ScriptRun state;
  guni_script_run_begin(&state);
  for (size_t index = 0; index < len; ++index) {
    if (!guni_script_run_add(&state, text[index])) {
      return false;
    }
  }
  return true;
}

bool guni_script_run_utf8(const char * text, size_t len) {
  if (text == NULL) {
    return len == 0;
  }
  GUNI_ScriptRun state;
  guni_script_run_begin(&state);
  size_t offset = 0;
  size_t count = 0;
  while (offset < len) {
    uint32_t cp = 0;
    bool valid = false;
    size_t used = guni_utf8_decode(text + offset, len - offset, &cp, &valid);
    offset += used;
    if (!valid) {
      /* Bytes that are not text are not a script run: the question is about a
       * string, and this is not one. */
      return false;
    }
    ++count;
    if (!guni_script_run_add(&state, cp)) {
      return false;
    }
  }
  return true;
}

// --------------------------------------------------------------------------
// Itemisation
// --------------------------------------------------------------------------

/** Does this codepoint's Script_Extensions set include @p script? */
static bool extensions_include(uint32_t cp, uint32_t script) {
  const uint16_t * set = &guni_scx_pool[guni_record(cp)->scx];
  for (size_t index = 0; index < set[0]; ++index) {
    if (set[index + 1] == (uint16_t)script) {
      return true;
    }
  }
  return false;
}

static bool is_neutral(uint32_t script) {
  return script == (uint32_t)GUNI_SCRIPT_COMMON
      || script == (uint32_t)GUNI_SCRIPT_INHERITED;
}

/**
 * The itemiser, over a cursor that both entry points drive.
 *
 * One pass. The only state is the current run's script and where it started,
 * plus the position of the first run so that a leading neutral run can be
 * given the script of what follows it.
 */
typedef struct {
  GUNI_ScriptItem * out;
  size_t cap;
  size_t count;
  bool overflow;
  bool open;
  GUNI_ScriptItem current;
  bool current_is_neutral; ///< The run so far is all Common or Inherited.
} ItemSink;

static void sink_flush(ItemSink * sink) {
  if (!sink->open) {
    return;
  }
  if (sink->count < sink->cap) {
    sink->out[sink->count] = sink->current;
  }
  else {
    sink->overflow = true;
  }
  ++sink->count;
  sink->open = false;
}

static void sink_add(ItemSink * sink, size_t start, size_t length,
    uint32_t script, uint32_t cp) {
  if (sink->open) {
    if (is_neutral(script)) {
      /* A neutral character joins the run before it. */
      sink->current.length += length;
      return;
    }
    if (sink->current_is_neutral) {
      /* A leading run of neutrals takes the script of what follows it, which
       * is why the decision is deferred rather than made when it started. */
      sink->current.script = (GUNI_Script)script;
      sink->current.length += length;
      sink->current_is_neutral = false;
      return;
    }
    if ((uint32_t)sink->current.script == script
        || extensions_include(cp, (uint32_t)sink->current.script)) {
      /* The same script, or a character this run's script is one of the
       * possibilities for - a Devanagari danda in Devanagari text. */
      sink->current.length += length;
      return;
    }
    sink_flush(sink);
  }
  sink->open = true;
  sink->current.start = start;
  sink->current.length = length;
  sink->current.script = (GUNI_Script)script;
  sink->current_is_neutral = is_neutral(script);
}

GUNI_Result guni_script_items(const uint32_t * text, size_t len,
    GUNI_ScriptItem * out, size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0) || (text == NULL && len != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  ItemSink sink;
  memset(&sink, 0, sizeof(sink));
  sink.out = out;
  sink.cap = cap;
  for (size_t index = 0; index < len; ++index) {
    sink_add(&sink, index, 1, (uint32_t)guni_script(text[index]), text[index]);
  }
  sink_flush(&sink);
  *out_len = sink.count;
  return sink.overflow ? GUNI_ERR_LIMIT : GUNI_OK;
}

GUNI_Result guni_script_items_utf8(const char * text, size_t len,
    GUNI_Invalid policy, GUNI_ScriptItem * out, size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0) || (text == NULL && len != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  ItemSink sink;
  memset(&sink, 0, sizeof(sink));
  sink.out = out;
  sink.cap = cap;
  size_t offset = 0;
  while (offset < len) {
    uint32_t cp = 0;
    bool valid = false;
    size_t used = guni_utf8_decode(text + offset, len - offset, &cp, &valid);
    if (!valid) {
      if (policy == GUNI_INVALID_REFUSE) {
        return GUNI_ERR_INVALID;
      }
      if (policy == GUNI_INVALID_SKIP) {
        offset += used;
        continue;
      }
      cp = GUNI_REPLACEMENT_CHARACTER;
    }
    sink_add(&sink, offset, used, (uint32_t)guni_script(cp), cp);
    offset += used;
  }
  sink_flush(&sink);
  *out_len = sink.count;
  return sink.overflow ? GUNI_ERR_LIMIT : GUNI_OK;
}
