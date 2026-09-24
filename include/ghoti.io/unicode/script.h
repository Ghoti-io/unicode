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
 * Script runs, and script itemisation: two questions about the same data.
 *
 * **A script run** is UTS #39 section 5.1's notion, and it exists to answer a
 * security question rather than a typographical one: "paypal.com" written with
 * a Cyrillic `а` looks identical and is not the same string. A script run is a
 * sequence that *could* have been written in one script.
 *
 * "One script" is not "one Script property value". Punctuation and the ASCII
 * digits are Common and go with anything; a diacritic is Inherited and takes
 * the script of what it modifies; and Han is written alongside Hiragana and
 * Katakana in Japanese, alongside Hangul in Korean, and alongside Bopomofo in
 * Taiwanese Mandarin - but a string mixing Hangul, Bopomofo and Han is none of
 * those three. The generated tables carry **augmented** sets so that the
 * three-way case falls out of an ordinary intersection (see the comment on
 * SCRIPT_AUGMENTATIONS in the generator).
 *
 * **Itemisation** is the shaper's question, and the reason `font` wants this
 * module: a paragraph has to be cut into runs of one script before it can be
 * shaped, because the shaper is chosen per script. The rules are different -
 * an itemiser has to *decide* a script for Common and Inherited characters
 * rather than merely accept them - and the data is the same, which is why both
 * are here.
 */

#ifndef GHOTI_IO_GUNI_SCRIPT_H
#define GHOTI_IO_GUNI_SCRIPT_H

#include <ghoti.io/unicode/char.h>
#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/enums.h>
#include <ghoti.io/unicode/macros.h>
#include <ghoti.io/unicode/utf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The state of a script-run check in progress.
 *
 * Caller-owned, no allocation. Offered incrementally as well as over a whole
 * string because **the rule is not decomposable**: {A,B}, {B,C} and {C,A}
 * intersect pairwise and not at all together, so a caller with a long span
 * cannot check it in windows and has to keep the state.
 */
typedef struct {
  uint64_t intersection[GUNI_SCRIPT_RUN_WORDS]; ///< Scripts still possible.
  uint32_t digits;  ///< The digit block seen, or 0xFFFFFFFF for none.
  bool unknown;     ///< Whether an Unknown-script character has been seen.
  size_t count;     ///< How many characters have been added.
} GUNI_ScriptRun;

/// @brief Begin a script-run check. Required before the first add.
GUNI_API void guni_script_run_begin(GUNI_ScriptRun * state);

/**
 * @brief Add one codepoint to a script-run check.
 *
 * @return Whether what has been added so far is still a script run. Once this
 *         returns false it returns false for every later call, so a caller can
 *         stop at the first refusal.
 */
GUNI_API bool guni_script_run_add(GUNI_ScriptRun * state, uint32_t cp);

/**
 * @brief Is this whole sequence a script run?
 *
 * A sequence of fewer than two characters always is - which is the only case
 * in which an Unknown-script character can be part of one, and is a rule
 * rather than a consequence.
 */
GUNI_API bool guni_script_run(const uint32_t * text, size_t len);

/// @brief guni_script_run() over UTF-8. Ill-formed bytes are never a run.
GUNI_API bool guni_script_run_utf8(const char * text, size_t len);

/**
 * @brief One run of one script, as an itemiser produces them.
 *
 * Offsets are in the caller's units - codepoints or bytes, whichever entry
 * point was used - and the runs tile the text with no gaps and no overlaps,
 * which is what a shaper needs in order to account for every character.
 */
typedef struct {
  size_t start;        ///< Where the run begins.
  size_t length;       ///< How long it is.
  GUNI_Script script;  ///< The script it is shaped as.
} GUNI_ScriptItem;

/**
 * @brief Cut text into runs of one script, for a shaper.
 *
 * The itemiser's rules, which are not the script-run checker's:
 *
 *   * a **Common or Inherited** character joins the run before it, because a
 *     full stop after Greek text is shaped as Greek and a combining mark takes
 *     the script of what it modifies. At the start of the text, where there is
 *     no run before it, it joins the run after it instead;
 *   * a character whose `Script_Extensions` includes the current run's script
 *     joins that run rather than starting a new one. This is what keeps a
 *     Devanagari danda with Devanagari text rather than making it a run of its
 *     own;
 *   * everything else starts a new run.
 *
 * The result is the itemisation a shaper wants, and it is deliberately not the
 * only defensible one: a caller with a higher-level protocol - a language tag
 * on the paragraph, say - can do better, which is why this is a function and
 * not the only way to ask.
 *
 * @param text The codepoints.
 * @param len How many.
 * @param out Receives the runs. May be NULL when @p cap is 0, to learn the
 *        count first.
 * @param cap How many runs @p out holds.
 * @param out_len Receives the number of runs. Required.
 * @return GUNI_OK, GUNI_ERR_LIMIT, GUNI_ERR_INVALID. Empty text is zero runs
 *         and GUNI_OK.
 */
GUNI_API GUNI_Result guni_script_items(const uint32_t * text, size_t len,
    GUNI_ScriptItem * out, size_t cap, size_t * out_len);

/**
 * @brief guni_script_items() over UTF-8, with byte offsets.
 *
 * @param policy What ill-formed bytes do. Under REPLACE each maximal subpart
 *        is one character of Unknown script; under REFUSE the call returns
 *        GUNI_ERR_INVALID.
 */
GUNI_API GUNI_Result guni_script_items_utf8(const char * text, size_t len,
    GUNI_Invalid policy, GUNI_ScriptItem * out, size_t cap, size_t * out_len);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GUNI_SCRIPT_H
