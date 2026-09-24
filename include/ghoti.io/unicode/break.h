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
 * UAX #29's grapheme, word and sentence boundaries, and UAX #14's line-break
 * opportunities.
 *
 * This is the module `regex` already had and `ctang` pays ICU for, and the
 * one substantive change on the way here is **LB1** (design.md section 7.3,
 * M2). `regex`'s generator resolves UAX #14's five undetermined classes while
 * generating its tables, and picks `NS` for `CJ` - which is exactly CSS's
 * `line-break: strict`, and leaves `normal` and `loose` unreachable. A layout
 * engine needs all three, so the table here carries the raw classes and the
 * resolution is a function the caller parameterises.
 *
 * The other consequence of that decision is `SA`. UAX #14 gives the
 * South-East Asian scripts that write without spaces - Thai, Lao, Khmer,
 * Myanmar - the class `SA`, which LB1 resolves to `AL`, which means **a Thai
 * paragraph has no interior line-break opportunity at all**. That is what the
 * Standard says to do without a dictionary, and it is useless for those
 * scripts. So the rules take an optional GUNI_BreakProvider, which is handed
 * each maximal run of `SA` characters and answers for the positions inside
 * it. Without one the behaviour is the Standard's default, and this paragraph
 * is the promise that it is documented rather than discovered.
 */

#ifndef GHOTI_IO_GUNI_BREAK_H
#define GHOTI_IO_GUNI_BREAK_H

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
 * @brief Which of the four boundary kinds to ask about.
 *
 * Grapheme is zero: it is the one a caller who has not thought about it
 * means, and it is the one a cursor, a delete key and a cluster map all need.
 */
typedef enum {
  GUNI_BREAK_GRAPHEME = 0, ///< UAX #29: extended grapheme clusters.
  GUNI_BREAK_WORD,         ///< UAX #29: word boundaries.
  GUNI_BREAK_SENTENCE,     ///< UAX #29: sentence boundaries.
  GUNI_BREAK_LINE,         ///< UAX #14: line-break opportunities.
  GUNI_BREAK_KIND_COUNT
} GUNI_BreakKind;

/**
 * @brief Which writing system's conventions apply, for the tailorings that
 * only exist in Chinese and Japanese text.
 *
 * Four of CSS Text's `line-break` tailorings apply **only if the writing system
 * is Chinese or Japanese** (see GUNI_LineBreakTailoring), and no property of a
 * character can answer that: the same codepoints occur in both, and in Korean
 * and in mixed text. It is the document's claim about its own content.
 *
 * This is **not locale data and not a locale identifier** (design.md section 2,
 * M6 and M7): there is no table behind it, nothing is loaded, nothing is read
 * from the environment, and it selects between rules that are written out in
 * UAX #14 and CSS Text terms. A caller that has a BCP 47 tag maps it here
 * itself; CSS Text's Appendix F describes how a UA decides, and that decision
 * is the caller's, not this library's.
 *
 * NEUTRAL is zero, so a caller who does not choose gets the behaviour that
 * makes no claim about the content - and under it CSS `normal` and `strict`
 * become identical, which is what CSS Text says they are outside Chinese and
 * Japanese.
 */
typedef enum {
  GUNI_WRITING_SYSTEM_NEUTRAL = 0, ///< No claim. `normal` and `strict` agree.
  GUNI_WRITING_SYSTEM_CHINESE,     ///< Chinese conventions apply.
  GUNI_WRITING_SYSTEM_JAPANESE     ///< Japanese conventions apply.
} GUNI_WritingSystem;

/**
 * @brief CSS Text's `line-break` values, which are UAX #14 plus a named set of
 * typographic tailorings.
 *
 * **These are the CSS values, not merely LB1's two resolutions.** An earlier
 * version of this header named them after CSS and implemented only LB1, which
 * the ICU differential caught (`make check-oracle-icu`); CSS Text section 5.2
 * requires more, and all of it is implemented here.
 *
 * What LB1 contributes is the `CJ` resolution, and **CSS gives `normal` the
 * same answer as `strict`** - which is the opposite of what this header used to
 * say. Quoting the specification: breaks before class `CJ` are *"forbidden for
 * normal and strict line breaking and allowed in loose"*. So the LB1 axis is
 * binary, `loose` is the only value that takes `ID`, and `normal` differs from
 * `strict` by one tailoring that applies only to Chinese and Japanese.
 *
 * The full set, in the specification's order. "zh/ja" marks the ones that need
 * GUNI_WritingSystem to be Chinese or Japanese; the rest apply in any text.
 *
 * | tailoring | strict | normal | loose |
 * | --- | --- | --- | --- |
 * | break before `U+301C`, `U+30A0` (zh/ja) | no | **yes** | **yes** |
 * | break before `U+2010`, `U+2013` after an `ID` character | no | no | **yes** |
 * | break before class `CJ` (this is LB1) | no | no | **yes** |
 * | break before `U+3005 U+303B U+309D U+309E U+30FD U+30FE` | no | no | **yes** |
 * | break between two class `IN` characters | no | no | **yes** |
 * | break before centred punctuation (zh/ja) | no | no | **yes** |
 * | break before class `PO` with East_Asian_Width A/F/W (zh/ja) | no | no | **yes** |
 * | break after class `PR` with East_Asian_Width A/F/W (zh/ja) | no | no | **yes** |
 *
 * The centred punctuation is `U+30FB U+FF1A U+FF1B U+FF65 U+203C U+2047
 * U+2048 U+2049 U+FF01 U+FF1F`.
 *
 * None of these override a *mandatory* rule or a structural prohibition: a
 * break is still forbidden before a space, inside a CRLF, before a combining
 * mark, around a word joiner and after a zero-width joiner. They are applied
 * after LB12a and before LB13, which is where the prohibitions they lift begin.
 *
 * STRICT is zero, and not because it is stricter: it is the Standard's own
 * worked example and `regex`'s existing behaviour, so it is the answer that
 * changes nothing for a caller who does not choose (design.md section 15.7).
 *
 * CSS's fifth value, `auto`, is deliberately absent: the specification defines
 * it as whatever the UA decides, varying with line length, so it is a policy
 * and not a rule set. A caller that wants it chooses among these per line.
 */
typedef enum {
  GUNI_LINE_BREAK_STRICT = 0, ///< CSS `strict`. `CJ` resolves to `NS`.
  GUNI_LINE_BREAK_NORMAL,     ///< CSS `normal`. `CJ` resolves to `NS`.
  GUNI_LINE_BREAK_LOOSE,      ///< CSS `loose`. `CJ` resolves to `ID`.
  /**
   * @brief CSS `anywhere`: a break opportunity around every typographic
   * character unit, disregarding every prohibition.
   *
   * Implemented as the grapheme cluster boundaries, which is what CSS's
   * "typographic character unit" is, and which is exactly why the clause
   * *"even those introduced by characters with the GL, WJ, or ZWJ line breaking
   * classes"* comes out right: a word joiner and a no-break space each form
   * their own cluster, so a boundary falls on both sides of them, and a
   * zero-width joiner ends a cluster unless it is joining two pictographs.
   * Mandatory breaks are unaffected - they are not prohibitions - and a CRLF
   * stays one unit because GB3 keeps it together.
   *
   * As with every other line-break answer, the start of text is not reported
   * (UAX #14 LB2), which is where this differs from asking for
   * GUNI_BREAK_GRAPHEME directly.
   */
  GUNI_LINE_BREAK_ANYWHERE
} GUNI_LineBreakTailoring;

/**
 * @brief The text the boundary rules are reading, as a provider sees it.
 *
 * Opaque, because it is either UTF-8 or codepoints and a provider should not
 * have to care which: guni_break_text_at() decodes either one. Offsets are in
 * whatever unit the caller's buffer is - bytes or codepoints - and are the
 * same offsets the rest of this header speaks in.
 */
typedef struct GUNI_BreakText GUNI_BreakText;

/// @brief The length of the text, in the caller's own units.
GUNI_API size_t guni_break_text_length(const GUNI_BreakText * text);

/**
 * @brief The codepoint at @p offset, and where the next one starts.
 *
 * @param text The text.
 * @param offset Where to read. Must be a character boundary.
 * @param cp_out Receives the codepoint. Required.
 * @param next_out Receives the offset just past it. May be NULL.
 * @return false at or past the end.
 */
GUNI_API bool guni_break_text_at(const GUNI_BreakText * text, size_t offset,
    uint32_t * cp_out, size_t * next_out);

/**
 * @brief A source of break opportunities for the scripts UAX #14 cannot
 * segment.
 *
 * One function, asked about one position inside one maximal run of `SA`
 * characters. A provider that wants to segment the whole run once - which a
 * dictionary breaker does - caches that on its own `ctx`, keyed by the run's
 * bounds; the run's bounds are passed for exactly that reason.
 *
 * ICU's `brkitr` dictionaries, `libthai`, or an application's own word list
 * all fit behind this. The library ships nothing behind it and never will:
 * the dictionary is megabytes of somebody's licensed data and the decision to
 * carry it is the application's (design.md section 9).
 */
typedef struct GUNI_BreakProvider {
  void * ctx; ///< Passed to the callback untouched.
  /**
   * @brief Is a line break allowed at @p position?
   *
   * @param ctx The `ctx` field.
   * @param text The whole text, read through guni_break_text_at().
   * @param start The first offset of the run of `SA` characters.
   * @param end One past its last offset.
   * @param position Where the break is being considered, strictly between
   *        @p start and @p end.
   * @return true to allow a break there. Returning false everywhere is the
   *         same as having no provider at all.
   */
  bool (*sa_break_at)(void * ctx, const GUNI_BreakText * text, size_t start,
      size_t end, size_t position);
} GUNI_BreakProvider;

/**
 * @brief What to ask, and how.
 *
 * A zeroed struct asks for grapheme boundaries with the strict line-break
 * tailoring, a neutral writing system and no provider, which is the answer to
 * "I did not think about it" that changes nothing.
 */
typedef struct {
  GUNI_BreakKind kind;                  ///< Which boundary.
  GUNI_LineBreakTailoring tailoring;    ///< For GUNI_BREAK_LINE only.
  const GUNI_BreakProvider * provider;  ///< For `SA` runs. May be NULL.
  /**
   * @brief For GUNI_BREAK_LINE only: whose typographic conventions apply.
   *
   * Zero is GUNI_WRITING_SYSTEM_NEUTRAL, under which the four Chinese and
   * Japanese tailorings do not apply and CSS `normal` and `strict` agree.
   */
  GUNI_WritingSystem writing_system;
} GUNI_BreakOptions;

/**
 * @brief Rule LB1: resolve a Line_Break class that the character alone does
 * not determine.
 *
 * `AI`, `SG` and `XX` become `AL`; `SA` becomes `CM` for a combining mark and
 * `AL` otherwise; `CJ` becomes `NS` or `ID` by @p tailoring. Every other class
 * is returned unchanged, so this is safe to apply to anything.
 *
 * Public because a caller with its own rule engine - `regex` applies LB1 and
 * then does its own matching - needs the same resolution this module uses,
 * and two copies of it would be two answers.
 *
 * @param class_ The unresolved class, from guni_line_break().
 * @param category The codepoint's General_Category, which `SA` needs.
 * @param tailoring Which way `CJ` goes.
 */
GUNI_API GUNI_LineBreak guni_line_break_resolve(GUNI_LineBreak class_,
    GUNI_GeneralCategory category, GUNI_LineBreakTailoring tailoring);

/**
 * @brief Is there a boundary at @p position in this UTF-8 text?
 *
 * @param options What to ask. NULL means a zeroed struct: grapheme, strict.
 * @param text The text.
 * @param len Its length in bytes.
 * @param position A byte offset. Must be a character boundary; a position
 *        inside a sequence is not a boundary and is answered false.
 * @return Whether a boundary of the requested kind falls there.
 *
 * The ends: UAX #29 puts a boundary at both ends of the text (GB1, GB2, and
 * the same for words and sentences); UAX #14 puts one at the end and not at
 * the start (LB2, LB3). An **empty** text has no boundary of any kind, which
 * neither standard says in those words - both would break at offset 0 - and
 * which is what `regex` answers and what Perl answers: there are no
 * characters, so there is nothing for a boundary to fall between.
 */
GUNI_API bool guni_break_at(const GUNI_BreakOptions * options,
    const char * text, size_t len, size_t position);

/// @brief guni_break_at() over codepoints, with @p position an index.
GUNI_API bool guni_break_at_codepoints(const GUNI_BreakOptions * options,
    const uint32_t * text, size_t len, size_t position);

/**
 * @brief A cursor over the boundaries of a buffer.
 *
 * Caller-owned, no allocation, no hidden state: two iterators over one buffer
 * do not interact (design.md section 13.3). It is the shape a layout engine
 * wants - segment a paragraph once - where the point query is the shape a
 * regex engine wants, and both are the same rules: the iterator asks the point
 * query at each position, so there is one implementation and they cannot
 * disagree.
 */
typedef struct {
  const char * utf8;        ///< The text, if it is UTF-8. Not owned.
  const uint32_t * codepoints; ///< The text, if it is codepoints. Not owned.
  size_t length;            ///< Its length, in the caller's units.
  size_t position;          ///< The next offset to consider.
  GUNI_BreakOptions options; ///< A copy, so the caller's may go out of scope.
} GUNI_BreakIter;

/**
 * @brief Point an iterator at UTF-8 text.
 *
 * @param iter The iterator. Required.
 * @param options Copied. NULL means a zeroed struct.
 * @param text Borrowed, and must outlive @p iter.
 * @param len Its length in bytes.
 */
GUNI_API void guni_break_iter_init(GUNI_BreakIter * iter,
    const GUNI_BreakOptions * options, const char * text, size_t len);

/// @brief guni_break_iter_init() over codepoints.
GUNI_API void guni_break_iter_init_codepoints(GUNI_BreakIter * iter,
    const GUNI_BreakOptions * options, const uint32_t * text, size_t len);

/**
 * @brief The next boundary.
 *
 * Boundaries come out strictly increasing, which is what makes a cluster map
 * monotone. The first call reports the boundary at offset 0 when the kind has
 * one there - every kind but line - and the last reports the end of the text.
 *
 * @param iter The iterator.
 * @param position_out Receives the boundary's offset. Required.
 * @return false when there are no more.
 */
GUNI_API bool guni_break_iter_next(GUNI_BreakIter * iter,
    size_t * position_out);

/**
 * @brief Every boundary, at once.
 *
 * The bulk form, following the output contract of design.md section 4.5: ask
 * with a cap of 0 to learn the count. For a caller that wants a table rather
 * than a walk - a line breaker that will try several widths against one
 * paragraph, which is the "shape once, break many" case font is designed
 * around.
 *
 * @param out_len Receives the number of boundaries. Required.
 * @return GUNI_OK, GUNI_ERR_LIMIT, GUNI_ERR_INVALID.
 */
GUNI_API GUNI_Result guni_break_all(const GUNI_BreakOptions * options,
    const char * text, size_t len, size_t * out, size_t cap, size_t * out_len);

/// @brief guni_break_all() over codepoints.
GUNI_API GUNI_Result guni_break_all_codepoints(const GUNI_BreakOptions * options,
    const uint32_t * text, size_t len, size_t * out, size_t cap,
    size_t * out_len);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GUNI_BREAK_H
