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
 * UAX #9, the Unicode Bidirectional Algorithm: the levels, the isolates, the
 * bracket pairs, the reordering and the mirroring.
 *
 * This is new work rather than code moved from another library in this suite,
 * and it is what a layout engine needs that nothing here had. The shape of the
 * API follows from one fact about layout: **an engine reorders glyph runs, not
 * characters.** So this module's product is one embedding level per character,
 * and reordering is a separate function that takes levels and returns a
 * permutation. A resolver that handed back reordered text would be useless to
 * the consumer it exists for, because by the time text is shaped the
 * characters have become glyphs and the mapping is not one to one.
 *
 * The algorithm is stateful and the state is the caller's: nothing here is
 * global and two resolutions can run at once (design.md section 13.3).
 */

#ifndef GHOTI_IO_GUNI_BIDI_H
#define GHOTI_IO_GUNI_BIDI_H

#include <ghoti.io/unicode/allocator.h>
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
 * @brief UAX #9's `max_depth`: the deepest embedding the Standard allows.
 *
 * 125, and not raisable: it is in the Standard because an implementation has
 * to have a bound, and the Standard's own conformance tests depend on the
 * overflow behaviour at exactly this value. A library that raised it would
 * pass its own tests and disagree with every other implementation on the
 * inputs that matter.
 */
#define GUNI_BIDI_MAX_DEPTH 125

/**
 * @brief How long a paragraph guni_bidi_levels() handles without an allocator.
 *
 * The resolver needs about eleven bytes of working state per character - the
 * mutable class each rule rewrites, and where each isolate initiator's
 * matching PDI is - and that state is a fixed buffer here. A longer paragraph
 * is guni_bidi_levels_with_allocator()'s job, which is the one place in this
 * library an allocator appears (design.md section 13.1).
 */
#define GUNI_BIDI_MAX_STACK_LENGTH 1024

/**
 * @brief The paragraph direction a caller asks for.
 *
 * AUTO is rules P2 and P3: the direction of the first strong character,
 * skipping isolated runs, and left-to-right if there is none. It is what a
 * plain-text renderer with no higher-level protocol should use, and it is
 * deliberately not zero: "the direction I did not think about" should be
 * left-to-right, which is what a zeroed struct gets.
 */
typedef enum {
  GUNI_BIDI_LTR = 0, ///< Paragraph level 0.
  GUNI_BIDI_RTL,     ///< Paragraph level 1.
  GUNI_BIDI_AUTO     ///< Rules P2 and P3 decide.
} GUNI_BidiDirection;

/**
 * @brief Resolve the embedding level of every character in one paragraph.
 *
 * The whole of UAX #9 up to and including rule L1: the paragraph level (P2,
 * P3), the explicit embeddings, overrides and isolates (X1-X8) with the
 * overflow counters the Standard specifies, the isolating run sequences (X10,
 * BD13), the weak types (W1-W7), the paired brackets (N0, BD16), the neutrals
 * (N1, N2), the implicit levels (I1, I2), and L1's reset of separators and
 * trailing whitespace to the paragraph level.
 *
 * L1 is included because it is not optional: a line that ends in a space
 * shows the reset, and a resolver that left it to the caller would be handing
 * out levels that are wrong for the only use they have.
 *
 * @param text The paragraph, as codepoints. One paragraph: rule P1's split is
 *        the caller's, because a caller with a document knows where its
 *        paragraphs are and this function would have to guess.
 * @param len How many codepoints.
 * @param direction What the paragraph level is, or AUTO to derive it.
 * @param limits May be NULL for the defaults. `max_bidi_depth` is fixed at
 *        GUNI_BIDI_MAX_DEPTH and a larger value is refused rather than
 *        honoured.
 * @param levels Receives one level per character, 0 to GUNI_BIDI_MAX_DEPTH+1.
 *        Must hold @p len.
 * @param paragraph_level_out Receives the paragraph's own level, 0 or 1. May
 *        be NULL.
 * @return GUNI_OK; GUNI_ERR_LIMIT when @p len exceeds
 *         GUNI_BIDI_MAX_STACK_LENGTH or `max_text_bytes`, or when
 *         `max_bidi_depth` was raised above the Standard's;
 *         GUNI_ERR_INVALID for a null or short buffer.
 */
GUNI_API GUNI_Result guni_bidi_levels(const uint32_t * text, size_t len,
    GUNI_BidiDirection direction, const GUNI_Limits * limits, uint8_t * levels,
    size_t cap, uint8_t * paragraph_level_out);

/**
 * @brief guni_bidi_levels() for a paragraph of any length.
 *
 * Identical in every respect except that the working state comes from
 * @p allocator instead of the stack, and is released before returning. This
 * is the only allocating function in the library.
 *
 * @param allocator The allocator, or NULL for the default.
 * @return As guni_bidi_levels(), plus GUNI_ERR_OOM.
 */
GUNI_API GUNI_Result guni_bidi_levels_with_allocator(const uint32_t * text,
    size_t len, GUNI_BidiDirection direction, const GUNI_Limits * limits,
    uint8_t * levels, size_t cap, uint8_t * paragraph_level_out,
    GUNI_Allocator * allocator);

/**
 * @brief guni_bidi_levels() over UTF-8, reporting one level per codepoint.
 *
 * Levels are per character and not per byte, because a level per byte would
 * be a level per continuation byte as well and a caller would have to unpick
 * it. The mapping back to bytes is guni_utf8_iter_next()'s offsets.
 *
 * @param policy What ill-formed bytes do. Under REPLACE each maximal subpart
 *        is one character with the class of U+FFFD, which is ON.
 * @param count_out Receives how many levels were written. Required.
 */
GUNI_API GUNI_Result guni_bidi_levels_utf8(const char * text, size_t len,
    GUNI_Invalid policy, GUNI_BidiDirection direction,
    const GUNI_Limits * limits, uint8_t * levels, size_t cap,
    size_t * count_out, uint8_t * paragraph_level_out);

/**
 * @brief Rule L2: the visual order of one line.
 *
 * Takes the levels of a line - a slice of what guni_bidi_levels() produced,
 * after the caller has decided where the line breaks - and produces the
 * permutation that puts it in display order: `out[visual] = logical`.
 *
 * A permutation rather than reordered text, for the reason in this file's
 * header: what a layout engine reorders is glyph runs.
 *
 * @param levels The levels of the line's characters.
 * @param len How many.
 * @param out Receives the permutation. Must hold @p len.
 * @param cap How many @p out holds.
 * @return GUNI_OK, or GUNI_ERR_INVALID for a null or short buffer.
 */
GUNI_API GUNI_Result guni_bidi_reorder(const uint8_t * levels, size_t len,
    size_t * out, size_t cap);

/**
 * @brief Rule L4: the mirrored form of a codepoint, or the codepoint itself.
 *
 * Applied to a character whose resolved level is odd and whose
 * Bidi_Mirrored property is true: a left parenthesis is drawn as a right
 * parenthesis in a right-to-left line. This is a character mapping, not a
 * glyph one - a font's `rtlm` feature is the other half, and font's business.
 */
GUNI_API uint32_t guni_bidi_mirror(uint32_t cp);

/**
 * @brief BD14 and BD15: the paired bracket of a codepoint.
 *
 * @param cp The codepoint.
 * @param opening_out Receives true when @p cp opens the pair and false when it
 *        closes it. May be NULL. Untouched when there is no pair.
 * @return The paired bracket, or 0 when @p cp is not a paired bracket.
 */
GUNI_API uint32_t guni_bidi_paired_bracket(uint32_t cp, bool * opening_out);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GUNI_BIDI_H
