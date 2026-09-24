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
 * UAX #29's three segmentations and UAX #14's line breaking, rule by rule,
 * with the rule numbers in the comments.
 *
 * **The rules are a port.** `regex/src/unicode/break.c` implements all four
 * and is gated against the Consortium's four conformance files; that code
 * moves here essentially unchanged, because the valuable part of it is the
 * 145 rule citations and the cases they were fixed for, and rewriting it
 * would be throwing that away to arrive at the same place. What changed on
 * the way, and why, is in three places:
 *
 *   * **LB1.** `regex`'s generator resolves `AI`, `SG`, `XX`, `SA` and `CJ`
 *     while building its table and picks `NS` for `CJ`. Here the table carries
 *     the raw classes and guni_line_break_resolve() applies LB1 with the
 *     caller's tailoring, so all three of CSS's `line-break` values are
 *     reachable (design.md section 7.3, M2).
 *   * **`[\p{Pi}&QU]` and `[\p{Pf}&QU]`.** `regex` resolves those into two
 *     extra classes for the same reason; here they are a General_Category
 *     lookup at the point of use, which is one field of the same record.
 *   * **The text is either UTF-8 or codepoints.** One rule engine, two entry
 *     points, because this library promises both (section 4.1).
 *
 * Everything else - the walking, the folding of `CM ZWJ` runs into their base,
 * the regional-indicator parity, GB9c's Indic conjuncts, the ordered list of
 * line-break rules - is the code that was already passing.
 */

#include <ghoti.io/unicode/break.h>
#include <string.h>

// --------------------------------------------------------------------------
// Property values
//
// The rules are written in the UCD's own spellings - GCB_Extend, LB_QU, WB_MidNumLet -
// and this library's generated enums carry those spellings as aliases, so the
// rule bodies read as the Standard writes them.
// --------------------------------------------------------------------------

/** Grapheme_Cluster_Break. */
static uint32_t gcb_of(uint32_t codepoint) {
  return (uint32_t)guni_grapheme_cluster_break(codepoint);
}

/** Word_Break. */
static uint32_t wb_of(uint32_t codepoint) {
  return (uint32_t)guni_word_break(codepoint);
}

/** Sentence_Break. */
static uint32_t sb_of(uint32_t codepoint) {
  return (uint32_t)guni_sentence_break(codepoint);
}

/** Indic_Conjunct_Break, for GB9c. */
static uint32_t incb_of(uint32_t codepoint) {
  return (uint32_t)guni_indic_conjunct_break(codepoint);
}

/** Extended_Pictographic, which GB11 and WB3c need. */
static int extended_pictographic(uint32_t codepoint) {
  return guni_has_property(codepoint, GUNI_PROP_EXTENDED_PICTOGRAPHIC) ? 1 : 0;
}

/** East_Asian_Width in {F, W, H}, which UAX #14 spells `$EastAsian`. */
static int east_asian(uint32_t codepoint) {
  GUNI_EastAsianWidth width = guni_east_asian_width(codepoint);
  return (width == GUNI_EAW_F || width == GUNI_EAW_W || width == GUNI_EAW_H)
      ? 1
      : 0;
}

/** Extended_Pictographic and unassigned: LB30b's second line. */
static int pictographic_unassigned(uint32_t codepoint) {
  return (guni_general_category(codepoint) == GUNI_GC_CN
             && extended_pictographic(codepoint))
      ? 1
      : 0;
}

GUNI_LineBreak guni_line_break_resolve(GUNI_LineBreak class_,
    GUNI_GeneralCategory category, GUNI_LineBreakTailoring tailoring) {
  switch (class_) {
    case GUNI_LB_AI:
    case GUNI_LB_SG:
    case GUNI_LB_XX:
      return GUNI_LB_AL;
    case GUNI_LB_SA:
      /* A combining mark keeps a mark's behaviour; everything else becomes
       * ordinary alphabetic, which is what leaves a Thai paragraph with no
       * interior break at all. GUNI_BreakProvider is the answer to that, and
       * it is consulted before these rules rather than through them. */
      return (category == GUNI_GC_MN || category == GUNI_GC_MC)
          ? GUNI_LB_CM
          : GUNI_LB_AL;
    case GUNI_LB_CJ:
      /* The one place LB1 has a choice, and the reason this function is
       * public and parameterised. */
      return (tailoring == GUNI_LINE_BREAK_STRICT) ? GUNI_LB_NS : GUNI_LB_ID;
    default:
      return class_;
  }
}

/* Defined with the entry points, because it reads the tailoring the caller
 * chose and that lives in the Text. */
static uint32_t lb_of(const struct GUNI_BreakText * text, uint32_t codepoint);

// --------------------------------------------------------------------------
// Walking the subject
// --------------------------------------------------------------------------

/**
 * The text, as the boundary rules read it.
 *
 * Either UTF-8 or codepoints, because this library has both entry points and
 * one rule engine (design.md section 4.1). Offsets are bytes in the first case
 * and indices in the second, and every rule below is written in terms of
 * at_next() and at_prev(), so neither the rules nor a provider has to know
 * which it is looking at.
 *
 * `tailoring` and `provider` are here rather than passed through thirty
 * functions: they are properties of the question, not of the position.
 */
struct GUNI_BreakText {
  const char * utf8;
  const uint32_t * codepoints;
  size_t length;
  GUNI_LineBreakTailoring tailoring;
  const GUNI_BreakProvider * provider;
};

typedef struct GUNI_BreakText Text;

/**
 * The codepoint beginning at `at`, and where the next one begins.
 *
 * A byte that does not begin a well-formed sequence is stepped over as one
 * codepoint with the replacement character's properties, rather than looping:
 * the rules must terminate on any input, and refusing here would mean every
 * boundary query validated the whole buffer first.
 */
static int at_next(const Text * text, size_t at, uint32_t * out,
    size_t * out_end) {
  if (at >= text->length) {
    return 0;
  }
  if (text->codepoints != NULL) {
    *out = text->codepoints[at];
    *out_end = at + 1;
    return 1;
  }
  bool valid = false;
  size_t width = guni_utf8_decode(text->utf8 + at, text->length - at, out,
      &valid);
  if (width == 0) {
    /* Unreachable: the length was checked above, and the decoder consumes at
     * least one byte of any non-empty input. Here because a zero would make
     * every loop over at_next() spin. */
    return 0;
  }
  *out_end = at + width;
  return 1;
}

/** The codepoint ending at `at`, and where it begins. */
static int at_prev(const Text * text, size_t at, uint32_t * out,
    size_t * out_start) {
  if (at == 0) {
    return 0;
  }
  if (text->codepoints != NULL) {
    *out = text->codepoints[at - 1];
    *out_start = at - 1;
    return 1;
  }
  size_t start = guni_utf8_prev(text->utf8, at);
  bool valid = false;
  guni_utf8_decode(text->utf8 + start, at - start, out, &valid);
  *out_start = start;
  return 1;
}

// --------------------------------------------------------------------------
// UAX #29: grapheme cluster boundaries
// --------------------------------------------------------------------------

/**
 * Whether an even number of Regional_Indicators precedes `at`.
 *
 * GB12 and GB13 break between the pairs of a flag sequence, so what decides
 * a boundary before an RI is the parity of the unbroken run behind it. The
 * scan is bounded by that run, not by the subject: the first character that
 * is not an RI ends it.
 */
static int even_regional_indicators(const Text * text, size_t at) {
  size_t count = 0;
  size_t scan = at;
  for (;;) {
    uint32_t codepoint = 0;
    size_t start = 0;
    if (!at_prev(text, scan, &codepoint, &start) || gcb_of(codepoint) != GUNI_GCB_RI) {
      break;
    }
    count++;
    scan = start;
  }

  return (count % 2) == 0;
}

/**
 * GB9c: whether an Indic conjunct sequence ends at `at`.
 *
 * `Consonant [Extend Linker]* Linker [Extend Linker]*` immediately before
 * the position, which is to say: walk back over InCB Extend and Linker, see
 * at least one Linker on the way, and land on a Consonant.
 */
static int indic_conjunct_before(const Text * text, size_t at) {
  int seen_linker = 0;
  size_t scan = at;
  for (;;) {
    uint32_t codepoint = 0;
    size_t start = 0;
    if (!at_prev(text, scan, &codepoint, &start)) {
      return 0;
    }
    uint32_t value = incb_of(codepoint);
    if (value == GUNI_INCB_LINKER) {
      seen_linker = 1;
      scan = start;
      continue;
    }
    if (value == GUNI_INCB_EXTEND) {
      scan = start;
      continue;
    }
    return seen_linker && value == GUNI_INCB_CONSONANT;
  }
}

/**
 * GB11: whether `ExtPict Extend* ZWJ` ends at `at`.
 *
 * The ZWJ immediately before the position is the caller's business; this
 * walks the Extend run behind it and asks what is on the far side.
 */
static int pictographic_zwj_before(const Text * text, size_t at) {
  size_t scan = at;
  for (;;) {
    uint32_t codepoint = 0;
    size_t start = 0;
    if (!at_prev(text, scan, &codepoint, &start)) {
      return 0;
    }
    if (gcb_of(codepoint) == GUNI_GCB_EXTEND) {
      scan = start;
      continue;
    }
    return extended_pictographic(codepoint);
  }
}

/** UAX #29 section 3.1.1, rules GB3 to GB999. `before` ends at `at`. */
static int grapheme_break(const Text * text, size_t at, uint32_t before,
    size_t before_start, uint32_t after) {
  uint32_t left = gcb_of(before);
  uint32_t right = gcb_of(after);

  if (left == GUNI_GCB_CR && right == GUNI_GCB_LF) {
    return 0; // GB3
  }
  if (left == GUNI_GCB_CONTROL || left == GUNI_GCB_CR || left == GUNI_GCB_LF) {
    return 1; // GB4
  }
  if (right == GUNI_GCB_CONTROL || right == GUNI_GCB_CR || right == GUNI_GCB_LF) {
    return 1; // GB5
  }
  if (left == GUNI_GCB_L
      && (right == GUNI_GCB_L || right == GUNI_GCB_V || right == GUNI_GCB_LV
          || right == GUNI_GCB_LVT)) {
    return 0; // GB6
  }
  if ((left == GUNI_GCB_LV || left == GUNI_GCB_V) && (right == GUNI_GCB_V || right == GUNI_GCB_T)) {
    return 0; // GB7
  }
  if ((left == GUNI_GCB_LVT || left == GUNI_GCB_T) && right == GUNI_GCB_T) {
    return 0; // GB8
  }
  if (right == GUNI_GCB_EXTEND || right == GUNI_GCB_ZWJ) {
    return 0; // GB9
  }
  if (right == GUNI_GCB_SPACINGMARK) {
    return 0; // GB9a
  }
  if (left == GUNI_GCB_PREPEND) {
    return 0; // GB9b
  }
  if (incb_of(after) == GUNI_INCB_CONSONANT && indic_conjunct_before(text, at)) {
    return 0; // GB9c
  }
  if (left == GUNI_GCB_ZWJ && extended_pictographic(after)
      && pictographic_zwj_before(text, before_start)) {
    return 0; // GB11
  }
  if (left == GUNI_GCB_RI && right == GUNI_GCB_RI
      && even_regional_indicators(text, before_start)) {
    // GB12, GB13: `sot (RI RI)* RI x RI`. An even run behind the left RI
    // makes that RI the *first* of a pair, so this position is inside a flag
    // sequence; an odd one means the pair is already complete.
    return 0;
  }

  return 1; // GB999
}

// --------------------------------------------------------------------------
// UAX #29: word boundaries
// --------------------------------------------------------------------------

/** WB4's ignorables: what a character absorbs without changing what it is. */
static int wb_ignorable(uint32_t value) {
  return value == GUNI_WB_EXTEND || value == GUNI_WB_FORMAT || value == GUNI_WB_ZWJ;
}

/** AHLetter: ALetter or Hebrew_Letter, which most of the rules pair up. */
static int wb_ah_letter(uint32_t value) {
  return value == GUNI_WB_ALETTER || value == GUNI_WB_HEBREW_LETTER;
}

/** MidNumLetQ: MidNumLet or Single_Quote. */
static int wb_mid_num_letq(uint32_t value) {
  return value == GUNI_WB_MIDNUMLET || value == GUNI_WB_SINGLE_QUOTE;
}

/**
 * The last character ending at or before `at` that WB4 does not absorb.
 *
 * Returns its Word_Break value and, through `out_start`, where it begins -
 * which is where a rule that needs to look one further back carries on from.
 *
 * UAX #29 section 6.2 states the limit of the ignore rules, and it is not a
 * detail: they do not apply "after sot, CR, LF, and Newline". Those four
 * have already forced a break by the time these rules are consulted, so an
 * Extend or a Format following one has nothing to attach to and stands for
 * itself. Absorbing it anyway would put a CR on the left of rules that are
 * asking about letters.
 */
static uint32_t wb_before(const Text * text, size_t at, size_t * out_start) {
  size_t scan = at;
  uint32_t ignored = GUNI_WB_OTHER;
  size_t ignored_start = at;
  int any_ignored = 0;
  for (;;) {
    uint32_t codepoint = 0;
    size_t start = 0;
    if (!at_prev(text, scan, &codepoint, &start)) {
      break; // The start of the subject: sot, where WB4 does not reach.
    }
    uint32_t value = wb_of(codepoint);
    if (wb_ignorable(value)) {
      ignored = value;
      ignored_start = start;
      any_ignored = 1;
      scan = start;
      continue;
    }
    if (any_ignored
        && (value == GUNI_WB_CR || value == GUNI_WB_LF || value == GUNI_WB_NEWLINE)) {
      break;
    }
    if (out_start) {
      *out_start = start;
    }
    return value;
  }

  if (out_start) {
    *out_start = any_ignored ? ignored_start : scan;
  }
  return any_ignored ? ignored : GUNI_WB_OTHER;
}

/** The first character at or after `at` that WB4 does not absorb. */
static uint32_t wb_after(const Text * text, size_t at) {
  size_t scan = at;
  for (;;) {
    uint32_t codepoint = 0;
    size_t end = 0;
    if (!at_next(text, scan, &codepoint, &end)) {
      return GUNI_WB_OTHER;
    }
    uint32_t value = wb_of(codepoint);
    if (!wb_ignorable(value)) {
      return value;
    }
    scan = end;
  }
}

/** WB15, WB16: as GB12 and GB13, over the characters WB4 leaves. */
static int wb_even_regional_indicators(const Text * text, size_t at) {
  size_t count = 0;
  size_t scan = at;
  for (;;) {
    uint32_t codepoint = 0;
    size_t start = 0;
    if (!at_prev(text, scan, &codepoint, &start)) {
      break;
    }
    uint32_t value = wb_of(codepoint);
    scan = start;
    if (wb_ignorable(value)) {
      continue;
    }
    if (value != GUNI_WB_RI) {
      break;
    }
    count++;
  }

  return (count % 2) == 0;
}

/** UAX #29 section 4.1, rules WB3 to WB999. `before` ends at `at`. */
static int word_break(const Text * text, size_t at, uint32_t before,
    uint32_t after, size_t after_end) {
  uint32_t raw_left = wb_of(before);
  uint32_t right = wb_of(after);

  if (raw_left == GUNI_WB_CR && right == GUNI_WB_LF) {
    return 0; // WB3
  }
  if (raw_left == GUNI_WB_NEWLINE || raw_left == GUNI_WB_CR || raw_left == GUNI_WB_LF) {
    return 1; // WB3a
  }
  if (right == GUNI_WB_NEWLINE || right == GUNI_WB_CR || right == GUNI_WB_LF) {
    return 1; // WB3b
  }
  if (raw_left == GUNI_WB_ZWJ && extended_pictographic(after)) {
    return 0; // WB3c
  }
  if (raw_left == GUNI_WB_WSEGSPACE && right == GUNI_WB_WSEGSPACE) {
    return 0; // WB3d
  }
  if (wb_ignorable(right)) {
    return 0; // WB4: the character on the right is absorbed by this one.
  }

  // Everything below reads the sequence WB4 leaves behind, so the character
  // on the left is the last one that was not absorbed.
  size_t left_start = 0;
  uint32_t left = wb_before(text, at, &left_start);

  if (wb_ah_letter(left) && wb_ah_letter(right)) {
    return 0; // WB5
  }
  if (wb_ah_letter(left) && (right == GUNI_WB_MIDLETTER || wb_mid_num_letq(right))
      && wb_ah_letter(wb_after(text, after_end))) {
    return 0; // WB6
  }
  if (wb_ah_letter(right) && (left == GUNI_WB_MIDLETTER || wb_mid_num_letq(left))
      && wb_ah_letter(wb_before(text, left_start, NULL))) {
    return 0; // WB7
  }
  if (left == GUNI_WB_HEBREW_LETTER && right == GUNI_WB_SINGLE_QUOTE) {
    return 0; // WB7a
  }
  if (left == GUNI_WB_HEBREW_LETTER && right == GUNI_WB_DOUBLE_QUOTE
      && wb_after(text, after_end) == GUNI_WB_HEBREW_LETTER) {
    return 0; // WB7b
  }
  if (left == GUNI_WB_DOUBLE_QUOTE && right == GUNI_WB_HEBREW_LETTER
      && wb_before(text, left_start, NULL) == GUNI_WB_HEBREW_LETTER) {
    return 0; // WB7c
  }
  if (left == GUNI_WB_NUMERIC && right == GUNI_WB_NUMERIC) {
    return 0; // WB8
  }
  if (wb_ah_letter(left) && right == GUNI_WB_NUMERIC) {
    return 0; // WB9
  }
  if (left == GUNI_WB_NUMERIC && wb_ah_letter(right)) {
    return 0; // WB10
  }
  if (right == GUNI_WB_NUMERIC && (left == GUNI_WB_MIDNUM || wb_mid_num_letq(left))
      && wb_before(text, left_start, NULL) == GUNI_WB_NUMERIC) {
    return 0; // WB11
  }
  if (left == GUNI_WB_NUMERIC && (right == GUNI_WB_MIDNUM || wb_mid_num_letq(right))
      && wb_after(text, after_end) == GUNI_WB_NUMERIC) {
    return 0; // WB12
  }
  if (left == GUNI_WB_KATAKANA && right == GUNI_WB_KATAKANA) {
    return 0; // WB13
  }
  if ((wb_ah_letter(left) || left == GUNI_WB_NUMERIC || left == GUNI_WB_KATAKANA
          || left == GUNI_WB_EXTENDNUMLET)
      && right == GUNI_WB_EXTENDNUMLET) {
    return 0; // WB13a
  }
  if (left == GUNI_WB_EXTENDNUMLET
      && (wb_ah_letter(right) || right == GUNI_WB_NUMERIC
          || right == GUNI_WB_KATAKANA)) {
    return 0; // WB13b
  }
  if (left == GUNI_WB_RI && right == GUNI_WB_RI
      && wb_even_regional_indicators(text, left_start)) {
    return 0; // WB15, WB16
  }

  return 1; // WB999
}

// --------------------------------------------------------------------------
// UAX #29: sentence boundaries
// --------------------------------------------------------------------------

/** SB5's ignorables. */
static int sb_ignorable(uint32_t value) {
  return value == GUNI_SB_EXTEND || value == GUNI_SB_FORMAT;
}

/** ParaSep: the three that end a paragraph. */
static int sb_para_sep(uint32_t value) {
  return value == GUNI_SB_SEP || value == GUNI_SB_CR || value == GUNI_SB_LF;
}

/**
 * The last character ending at or before `at` that SB5 does not absorb.
 *
 * The same shape as wb_before(), and with the same limit from UAX #29
 * section 6.2: the ignore rules do not apply after sot, Sep, CR or LF. An
 * Extend after a line feed is its own character, and treating it as part of
 * the line feed makes SB11 end a sentence that had already ended.
 */
static uint32_t sb_before(const Text * text, size_t at, size_t * out_start) {
  size_t scan = at;
  uint32_t ignored = GUNI_SB_OTHER;
  size_t ignored_start = at;
  int any_ignored = 0;
  for (;;) {
    uint32_t codepoint = 0;
    size_t start = 0;
    if (!at_prev(text, scan, &codepoint, &start)) {
      break;
    }
    uint32_t value = sb_of(codepoint);
    if (sb_ignorable(value)) {
      ignored = value;
      ignored_start = start;
      any_ignored = 1;
      scan = start;
      continue;
    }
    if (any_ignored && sb_para_sep(value)) {
      break;
    }
    if (out_start) {
      *out_start = start;
    }
    return value;
  }

  if (out_start) {
    *out_start = any_ignored ? ignored_start : scan;
  }
  return any_ignored ? ignored : GUNI_SB_OTHER;
}

/**
 * What kind of sentence terminator the left context is, if any.
 *
 * Several rules share the shape `(STerm | ATerm) Close* Sp*`, differing only
 * in how much of the tail they allow, so the walk is written once: skip the
 * `Sp*` when `skip_spaces` says to, then the `Close*` always, and report
 * what is on the far side.
 *
 * @return GUNI_SB_ATERM, GUNI_SB_STERM, or GUNI_SB_OTHER for neither.
 */
static uint32_t sentence_terminator(
    const Text * text, size_t at, int skip_spaces) {
  size_t scan = at;
  if (skip_spaces) {
    for (;;) {
      size_t start = 0;
      uint32_t value = sb_before(text, scan, &start);
      if (value != GUNI_SB_SP) {
        break;
      }
      scan = start;
    }
  }
  for (;;) {
    size_t start = 0;
    uint32_t value = sb_before(text, scan, &start);
    if (value != GUNI_SB_CLOSE) {
      return value == GUNI_SB_ATERM || value == GUNI_SB_STERM ? value : GUNI_SB_OTHER;
    }
    scan = start;
  }
}

/**
 * SB8: whether a lower-case letter follows, with nothing sentence-ending in
 * between.
 *
 * `( !(OLetter | Upper | Lower | Sep | CR | LF | STerm | ATerm) )* Lower`.
 * The scan is unbounded, which is the one place these rules look arbitrarily
 * far ahead - "Mr. Smith" is not two sentences, and finding that out means
 * reading to the `S`.
 */
static int sentence_lower_follows(const Text * text, size_t at) {
  size_t scan = at;
  for (;;) {
    uint32_t codepoint = 0;
    size_t end = 0;
    if (!at_next(text, scan, &codepoint, &end)) {
      return 0;
    }
    uint32_t value = sb_of(codepoint);
    scan = end;
    if (sb_ignorable(value)) {
      continue; // SB5 applies here too.
    }
    if (value == GUNI_SB_LOWER) {
      return 1;
    }
    if (value == GUNI_SB_OLETTER || value == GUNI_SB_UPPER || sb_para_sep(value)
        || value == GUNI_SB_STERM || value == GUNI_SB_ATERM) {
      return 0;
    }
  }
}

/**
 * UAX #29 section 5.1, rules SB3 to SB998.
 *
 * The default is the opposite of the other two: SB998 is "do not break", so
 * a sentence runs on unless a rule ends it.
 */
static int sentence_break(const Text * text, size_t at, uint32_t before,
    uint32_t after) {
  uint32_t raw_left = sb_of(before);
  uint32_t right = sb_of(after);

  if (raw_left == GUNI_SB_CR && right == GUNI_SB_LF) {
    return 0; // SB3
  }
  if (sb_para_sep(raw_left)) {
    return 1; // SB4
  }
  if (sb_ignorable(right)) {
    return 0; // SB5
  }

  size_t left_start = 0;
  uint32_t left = sb_before(text, at, &left_start);

  if (left == GUNI_SB_ATERM && right == GUNI_SB_NUMERIC) {
    return 0; // SB6
  }
  if (left == GUNI_SB_ATERM && right == GUNI_SB_UPPER) {
    uint32_t further = sb_before(text, left_start, NULL);
    if (further == GUNI_SB_UPPER || further == GUNI_SB_LOWER) {
      return 0; // SB7
    }
  }
  if (sentence_terminator(text, at, 1) == GUNI_SB_ATERM
      && sentence_lower_follows(text, at)) {
    return 0; // SB8
  }
  if (sentence_terminator(text, at, 1) != GUNI_SB_OTHER
      && (right == GUNI_SB_SCONTINUE || right == GUNI_SB_STERM || right == GUNI_SB_ATERM)) {
    return 0; // SB8a
  }
  if (sentence_terminator(text, at, 0) != GUNI_SB_OTHER
      && (right == GUNI_SB_CLOSE || right == GUNI_SB_SP || sb_para_sep(right))) {
    return 0; // SB9
  }
  if (sentence_terminator(text, at, 1) != GUNI_SB_OTHER
      && (right == GUNI_SB_SP || sb_para_sep(right))) {
    return 0; // SB10
  }

  // SB11: a terminator, its closes and spaces, and at most one paragraph
  // separator, all end the sentence here.
  size_t tail = at;
  size_t start = 0;
  if (sb_para_sep(sb_before(text, tail, &start))) {
    /* Unreachable, and kept: rule SB4 breaks after every ParaSep and is listed
     * before this one, so a position immediately after a paragraph separator
     * has already been answered. The clause is in SB11's own text, and a
     * version of this function that dropped it would no longer read as the
     * rule - which is how a later editor comes to delete the wrong line. */
    tail = start;
  }
  if (sentence_terminator(text, tail, 1) != GUNI_SB_OTHER) {
    return 1; // SB11
  }

  return 0; // SB998
}

// --------------------------------------------------------------------------
// UAX #14: line break opportunities
// --------------------------------------------------------------------------

/** U+25CC DOTTED CIRCLE, which LB28a names on its own. */
#define DOTTED_CIRCLE UINT32_C(0x25CC)

/** One character, as the line break rules read it. */
typedef struct {
  uint32_t codepoint; ///< The character, for the rules that need it directly.
  uint32_t value;     ///< Its resolved Line_Break class.
  size_t start;       ///< Where it begins.
  size_t end;         ///< One past where it ends.
  int present;        ///< Zero at sot or eot.
} LbChar;

/** The three quotation classes LB19 and LB19a treat alike. */
static int lb_quote(uint32_t value) {
  return value == GUNI_LB_QU;
}

/**
 * `[\p{Pi}&QU]` and `[\p{Pf}&QU]`, which LB15a, LB15b and LB19 are written
 * in terms of.
 *
 * `regex`'s generator resolves these into two extra classes while building its
 * table, so that a boundary test reads one table. Here the class stays `QU`
 * and the category is asked at the point of use, for the same reason LB1 is
 * the caller's: a table that has resolved something cannot be asked about the
 * unresolved form, and General_Category is one lookup in the same record.
 */
static int lb_initial_quote(LbChar character) {
  return character.present && character.value == GUNI_LB_QU
      && guni_general_category(character.codepoint) == GUNI_GC_PI;
}

static int lb_final_quote(LbChar character) {
  return character.present && character.value == GUNI_LB_QU
      && guni_general_category(character.codepoint) == GUNI_GC_PF;
}

/** LB9's ignorables. */
static int lb_combining(uint32_t value) {
  return value == GUNI_LB_CM || value == GUNI_LB_ZWJ;
}

/** The classes LB9 refuses to attach a combining mark to. */
static int lb_no_attach(uint32_t value) {
  return value == GUNI_LB_BK || value == GUNI_LB_CR || value == GUNI_LB_LF
      || value == GUNI_LB_NL || value == GUNI_LB_SP || value == GUNI_LB_ZW;
}

/**
 * The character ending at `at`, after LB9 and LB10.
 *
 * LB9 folds a `(CM | ZWJ)*` run into the character before it, so the class
 * on the left of a position is the base's; LB10 turns a run that had nothing
 * to attach to - at the start of the text, or after a space or a hard break -
 * into AL.
 */
static LbChar lb_prev(const Text * text, size_t at) {
  LbChar out = {0, GUNI_LB_AL, at, at, 0};
  size_t scan = at;
  LbChar first_combining = {0, GUNI_LB_AL, at, at, 0};

  for (;;) {
    uint32_t codepoint = 0;
    size_t start = 0;
    if (!at_prev(text, scan, &codepoint, &start)) {
      break; // sot: LB10 applies to whatever run we walked over.
    }
    uint32_t value = lb_of(text, codepoint);
    if (lb_combining(value)) {
      first_combining.codepoint = codepoint;
      first_combining.value = GUNI_LB_AL;
      first_combining.start = start;
      first_combining.end = scan;
      first_combining.present = 1;
      scan = start;
      continue;
    }
    if (first_combining.present && lb_no_attach(value)) {
      return first_combining; // LB10
    }
    out.codepoint = codepoint;
    out.value = value;
    out.start = start;
    out.end = scan;
    out.present = 1;
    return out;
  }

  return first_combining; // LB10 at the start of the text, or nothing at all.
}

/** The character beginning at `at`; no folding, which the caller does. */
static LbChar lb_next(const Text * text, size_t at) {
  LbChar out = {0, GUNI_LB_AL, at, at, 0};
  uint32_t codepoint = 0;
  size_t end = 0;
  if (!at_next(text, at, &codepoint, &end)) {
    return out;
  }
  out.codepoint = codepoint;
  out.value = lb_of(text, codepoint);
  out.start = at;
  out.end = end;
  out.present = 1;
  return out;
}

/**
 * The character after `at`, with any `(CM | ZWJ)*` run skipped.
 *
 * What a rule looking two characters ahead wants: LB9 has already made the
 * run part of the character before it, so the next *character* the rules see
 * is the next base.
 */
static LbChar lb_next_base(const Text * text, size_t at) {
  size_t scan = at;
  for (;;) {
    LbChar here = lb_next(text, scan);
    if (!here.present || !lb_combining(here.value)) {
      return here;
    }
    scan = here.end;
  }
}

/** Walk back over a run of one class, and report where it starts. */
static size_t lb_skip_back(const Text * text, size_t at, uint32_t value) {
  size_t scan = at;
  for (;;) {
    LbChar here = lb_prev(text, scan);
    if (!here.present || here.value != value) {
      return scan;
    }
    scan = here.start;
  }
}

/**
 * LB25's `NU ( SY | IS )*` read backwards from `at`.
 *
 * regex's version reports where the number started; both callers there pass
 * NULL for it, and both callers here do, so the parameter is gone.
 */
static int lb_number_before(const Text * text, size_t at) {
  size_t scan = at;
  for (;;) {
    LbChar here = lb_prev(text, scan);
    if (!here.present) {
      return 0;
    }
    if (here.value == GUNI_LB_SY || here.value == GUNI_LB_IS) {
      scan = here.start;
      continue;
    }
    if (here.value == GUNI_LB_NU) {
      return 1;
    }
    return 0;
  }
}

/** LB30a: an even number of regional indicators before `at`. */
static int lb_even_regional_indicators(const Text * text, size_t at) {
  size_t count = 0;
  size_t scan = at;
  for (;;) {
    LbChar here = lb_prev(text, scan);
    if (!here.present || here.value != GUNI_LB_RI) {
      break;
    }
    count++;
    scan = here.start;
  }

  return (count % 2) == 0;
}

/**
 * UAX #14 revision 55 (Unicode 17.0.0), rules LB4 to LB31.
 *
 * In the rules' own order, because they are ordered: the first that applies
 * decides, and LB31 is the "break everywhere else" that ends the list. LB1
 * is not here - the generator resolved it into the table - and LB2 and LB3
 * are the ends of the subject, which the caller answers.
 */
static int line_break(const Text * text, size_t at) {
  LbChar left = lb_prev(text, at);
  LbChar right = lb_next(text, at);
  uint32_t a = left.value;
  uint32_t raw_b = right.value;

  // The character immediately on the left, before LB9 folds a combining run
  // into its base. Only LB8a wants it: that rule is listed *before* LB9, so
  // it is about the zero-width joiner itself rather than about whatever the
  // joiner has been made part of.
  uint32_t raw_a = GUNI_LB_AL;
  {
    uint32_t codepoint = 0;
    size_t start = 0;
    if (at_prev(text, at, &codepoint, &start)) {
      raw_a = lb_of(text, codepoint);
    }
  }

  if (a == GUNI_LB_BK) {
    return 1; // LB4
  }
  if (a == GUNI_LB_CR && raw_b == GUNI_LB_LF) {
    return 0; // LB5
  }
  if (a == GUNI_LB_CR || a == GUNI_LB_LF || a == GUNI_LB_NL) {
    return 1; // LB5
  }
  if (raw_b == GUNI_LB_BK || raw_b == GUNI_LB_CR || raw_b == GUNI_LB_LF || raw_b == GUNI_LB_NL) {
    return 0; // LB6
  }
  if (raw_b == GUNI_LB_SP || raw_b == GUNI_LB_ZW) {
    return 0; // LB7
  }

  // LB8: `ZW SP* ÷`. The spaces are walked back over before asking, because
  // the rule reaches through them.
  {
    size_t before_spaces = lb_skip_back(text, at, GUNI_LB_SP);
    LbChar anchor = lb_prev(text, before_spaces);
    if (anchor.present && anchor.value == GUNI_LB_ZW) {
      return 1;
    }
  }

  if (raw_a == GUNI_LB_ZWJ) {
    return 0; // LB8a
  }
  if (lb_combining(raw_b) && left.present && !lb_no_attach(a)) {
    return 0; // LB9: the run on the right belongs to the base on the left.
  }

  // LB10: a combining mark with no base is an A.
  uint32_t b = lb_combining(raw_b) ? (uint32_t)GUNI_LB_AL : raw_b;
  if (b == GUNI_LB_WJ || a == GUNI_LB_WJ) {
    return 0; // LB11
  }
  if (a == GUNI_LB_GL) {
    return 0; // LB12
  }
  if (b == GUNI_LB_GL && a != GUNI_LB_SP && a != GUNI_LB_BA && a != GUNI_LB_HY && a != GUNI_LB_HH) {
    return 0; // LB12a
  }
  if (b == GUNI_LB_CL || b == GUNI_LB_CP || b == GUNI_LB_EX || b == GUNI_LB_SY) {
    return 0; // LB13
  }

  // LB14: `OP SP* ×`.
  {
    size_t before_spaces = lb_skip_back(text, at, GUNI_LB_SP);
    LbChar anchor = lb_prev(text, before_spaces);
    if (anchor.present && anchor.value == GUNI_LB_OP) {
      return 0;
    }
  }

  // LB15a: an initial quote at the start of a line, after a space, an
  // opener, another quote, or a hard break - reaching through spaces.
  {
    size_t before_spaces = lb_skip_back(text, at, GUNI_LB_SP);
    LbChar quote = lb_prev(text, before_spaces);
    if (lb_initial_quote(quote)) {
      LbChar anchor = lb_prev(text, quote.start);
      if (!anchor.present || anchor.value == GUNI_LB_BK || anchor.value == GUNI_LB_CR
          || anchor.value == GUNI_LB_LF || anchor.value == GUNI_LB_NL
          || anchor.value == GUNI_LB_OP || lb_quote(anchor.value)
          || anchor.value == GUNI_LB_GL || anchor.value == GUNI_LB_SP
          || anchor.value == GUNI_LB_ZW) {
        return 0;
      }
    }
  }

  // LB15b: a final quote before a space, a prohibited break, another quote,
  // or the end of the text.
  if (lb_final_quote(right)) {
    LbChar after = lb_next_base(text, right.end);
    uint32_t c = after.value;
    if (!after.present || c == GUNI_LB_SP || c == GUNI_LB_GL || c == GUNI_LB_WJ
        || c == GUNI_LB_CL || lb_quote(c) || c == GUNI_LB_CP || c == GUNI_LB_EX
        || c == GUNI_LB_IS || c == GUNI_LB_SY || c == GUNI_LB_BK || c == GUNI_LB_CR
        || c == GUNI_LB_LF || c == GUNI_LB_NL || c == GUNI_LB_ZW) {
      return 0;
    }
  }

  // LB15c: `SP ÷ IS NU`, which is what makes "subtract .5" break before the
  // decimal mark rather than after the space.
  if (a == GUNI_LB_SP && b == GUNI_LB_IS) {
    LbChar after = lb_next_base(text, right.end);
    if (after.present && after.value == GUNI_LB_NU) {
      return 1;
    }
  }
  if (b == GUNI_LB_IS) {
    return 0; // LB15d
  }

  // LB16: `(CL | CP) SP* × NS`.
  if (b == GUNI_LB_NS) {
    size_t before_spaces = lb_skip_back(text, at, GUNI_LB_SP);
    LbChar anchor = lb_prev(text, before_spaces);
    if (anchor.present && (anchor.value == GUNI_LB_CL || anchor.value == GUNI_LB_CP)) {
      return 0;
    }
  }

  // LB17: `B2 SP* × B2`.
  if (b == GUNI_LB_B2) {
    size_t before_spaces = lb_skip_back(text, at, GUNI_LB_SP);
    LbChar anchor = lb_prev(text, before_spaces);
    if (anchor.present && anchor.value == GUNI_LB_B2) {
      return 0;
    }
  }

  if (a == GUNI_LB_SP) {
    return 1; // LB18
  }
  if (b == GUNI_LB_QU && !lb_initial_quote(right)) {
    return 0; // LB19: `x [QU - \p{Pi}]`
  }
  if (a == GUNI_LB_QU && !lb_final_quote(left)) {
    return 0; // LB19: `[QU - \p{Pf}] x`
  }

  // LB19a: a quote is not broken from a neighbour unless East Asian
  // characters surround it.
  if (lb_quote(b)) {
    if (!east_asian(left.codepoint)) {
      return 0;
    }
    LbChar after = lb_next_base(text, right.end);
    if (!after.present || !east_asian(after.codepoint)) {
      return 0;
    }
  }
  if (lb_quote(a)) {
    if (!east_asian(right.codepoint)) {
      return 0;
    }
    LbChar before = lb_prev(text, left.start);
    if (!before.present || !east_asian(before.codepoint)) {
      return 0;
    }
  }

  if (b == GUNI_LB_CB || a == GUNI_LB_CB) {
    return 1; // LB20
  }

  // LB20a: a word-initial hyphen keeps its word.
  if ((a == GUNI_LB_HY || a == GUNI_LB_HH) && (b == GUNI_LB_AL || b == GUNI_LB_HL)) {
    LbChar before = lb_prev(text, left.start);
    if (!before.present || before.value == GUNI_LB_BK || before.value == GUNI_LB_CR
        || before.value == GUNI_LB_LF || before.value == GUNI_LB_NL
        || before.value == GUNI_LB_SP || before.value == GUNI_LB_ZW
        || before.value == GUNI_LB_CB || before.value == GUNI_LB_GL) {
      return 0;
    }
  }

  // LB21a: `HL (HY | HH) x [^HL]` comes before LB21, which would otherwise
  // break after the hyphen.
  if ((a == GUNI_LB_HY || a == GUNI_LB_HH) && b != GUNI_LB_HL) {
    LbChar before = lb_prev(text, left.start);
    if (before.present && before.value == GUNI_LB_HL) {
      return 0;
    }
  }

  if (b == GUNI_LB_BA || b == GUNI_LB_HH || b == GUNI_LB_HY || b == GUNI_LB_NS || a == GUNI_LB_BB) {
    return 0; // LB21
  }
  if (a == GUNI_LB_SY && b == GUNI_LB_HL) {
    return 0; // LB21b
  }
  if (b == GUNI_LB_IN) {
    return 0; // LB22
  }
  if ((a == GUNI_LB_AL || a == GUNI_LB_HL) && b == GUNI_LB_NU) {
    return 0; // LB23
  }
  if (a == GUNI_LB_NU && (b == GUNI_LB_AL || b == GUNI_LB_HL)) {
    return 0; // LB23
  }
  if (a == GUNI_LB_PR && (b == GUNI_LB_ID || b == GUNI_LB_EB || b == GUNI_LB_EM)) {
    return 0; // LB23a
  }
  if ((a == GUNI_LB_ID || a == GUNI_LB_EB || a == GUNI_LB_EM) && b == GUNI_LB_PO) {
    return 0; // LB23a
  }
  if ((a == GUNI_LB_PR || a == GUNI_LB_PO) && (b == GUNI_LB_AL || b == GUNI_LB_HL)) {
    return 0; // LB24
  }
  if ((a == GUNI_LB_AL || a == GUNI_LB_HL) && (b == GUNI_LB_PR || b == GUNI_LB_PO)) {
    return 0; // LB24
  }

  // LB25, the number rule, in the order the annex lists its lines.
  if (b == GUNI_LB_PO || b == GUNI_LB_PR) {
    // `NU ( SY | IS )* (CL | CP)? x (PO | PR)`.
    size_t from = at;
    if (a == GUNI_LB_CL || a == GUNI_LB_CP) {
      from = left.start;
    }
    if (lb_number_before(text, from)) {
      return 0;
    }
  }
  if ((a == GUNI_LB_PO || a == GUNI_LB_PR) && b == GUNI_LB_OP) {
    // `(PO | PR) x OP IS? NU`.
    LbChar after = lb_next_base(text, right.end);
    if (after.present && after.value == GUNI_LB_IS) {
      after = lb_next_base(text, after.end);
    }
    if (after.present && after.value == GUNI_LB_NU) {
      return 0;
    }
  }
  if ((a == GUNI_LB_PO || a == GUNI_LB_PR || a == GUNI_LB_HY || a == GUNI_LB_IS) && b == GUNI_LB_NU) {
    return 0; // LB25
  }
  if (b == GUNI_LB_NU && lb_number_before(text, at)) {
    return 0; // LB25: `NU ( SY | IS )* x NU`
  }

  if (a == GUNI_LB_JL
      && (b == GUNI_LB_JL || b == GUNI_LB_JV || b == GUNI_LB_H2 || b == GUNI_LB_H3)) {
    return 0; // LB26
  }
  if ((a == GUNI_LB_JV || a == GUNI_LB_H2) && (b == GUNI_LB_JV || b == GUNI_LB_JT)) {
    return 0; // LB26
  }
  if ((a == GUNI_LB_JT || a == GUNI_LB_H3) && b == GUNI_LB_JT) {
    return 0; // LB26
  }
  if ((a == GUNI_LB_JL || a == GUNI_LB_JV || a == GUNI_LB_JT || a == GUNI_LB_H2 || a == GUNI_LB_H3)
      && b == GUNI_LB_PO) {
    return 0; // LB27
  }
  if (a == GUNI_LB_PR
      && (b == GUNI_LB_JL || b == GUNI_LB_JV || b == GUNI_LB_JT || b == GUNI_LB_H2
          || b == GUNI_LB_H3)) {
    return 0; // LB27
  }
  if ((a == GUNI_LB_AL || a == GUNI_LB_HL) && (b == GUNI_LB_AL || b == GUNI_LB_HL)) {
    return 0; // LB28
  }

  // LB28a, the Brahmic orthographic syllable. `[◌]` is one code point and
  // stands beside AK and AS in three of the four lines.
  {
    int left_ak = a == GUNI_LB_AK || a == GUNI_LB_AS
        || left.codepoint == DOTTED_CIRCLE;
    int right_ak = b == GUNI_LB_AK || b == GUNI_LB_AS
        || right.codepoint == DOTTED_CIRCLE;
    int right_dotted
        = b == GUNI_LB_AK || right.codepoint == DOTTED_CIRCLE;
    if (a == GUNI_LB_AP && right_ak) {
      return 0;
    }
    if (left_ak && (b == GUNI_LB_VF || b == GUNI_LB_VI)) {
      return 0;
    }
    if (a == GUNI_LB_VI && right_dotted) {
      LbChar before = lb_prev(text, left.start);
      int before_ak = before.present
          && (before.value == GUNI_LB_AK || before.value == GUNI_LB_AS
              || before.codepoint == DOTTED_CIRCLE);
      if (before_ak) {
        return 0;
      }
    }
    if (left_ak && right_ak) {
      LbChar after = lb_next_base(text, right.end);
      if (after.present && after.value == GUNI_LB_VF) {
        return 0;
      }
    }
  }

  if (a == GUNI_LB_IS && (b == GUNI_LB_AL || b == GUNI_LB_HL)) {
    return 0; // LB29
  }
  if ((a == GUNI_LB_AL || a == GUNI_LB_HL || a == GUNI_LB_NU) && b == GUNI_LB_OP
      && !east_asian(right.codepoint)) {
    return 0; // LB30
  }
  if (a == GUNI_LB_CP && !east_asian(left.codepoint)
      && (b == GUNI_LB_AL || b == GUNI_LB_HL || b == GUNI_LB_NU)) {
    return 0; // LB30
  }
  if (a == GUNI_LB_RI && b == GUNI_LB_RI
      && lb_even_regional_indicators(text, left.start)) {
    return 0; // LB30a
  }
  if (a == GUNI_LB_EB && b == GUNI_LB_EM) {
    return 0; // LB30b
  }
  if (pictographic_unassigned(left.codepoint) && b == GUNI_LB_EM) {
    return 0; // LB30b
  }

  return 1; // LB31
}

// --------------------------------------------------------------------------
// The entry points
// --------------------------------------------------------------------------

/** Line_Break, with LB1 applied the way this text asked for. */
static uint32_t lb_of(const Text * text, uint32_t codepoint) {
  return (uint32_t)guni_line_break_resolve(guni_line_break(codepoint),
      guni_general_category(codepoint), text->tailoring);
}

size_t guni_break_text_length(const GUNI_BreakText * text) {
  return (text != NULL) ? text->length : 0;
}

bool guni_break_text_at(const GUNI_BreakText * text, size_t offset,
    uint32_t * cp_out, size_t * next_out) {
  if (text == NULL || cp_out == NULL) {
    return false;
  }
  size_t end = 0;
  if (!at_next(text, offset, cp_out, &end)) {
    return false;
  }
  if (next_out != NULL) {
    *next_out = end;
  }
  return true;
}

/**
 * The maximal run of unresolved `SA` characters containing @p position, if
 * @p position is strictly inside one.
 *
 * Asked of the *unresolved* class, because LB1 has already turned `SA` into
 * `AL` or `CM` by the time the rules see it - which is exactly why a provider
 * is needed and exactly why it cannot be consulted from inside them.
 */
static int sa_run_around(const Text * text, size_t position, size_t * start_out,
    size_t * end_out) {
  uint32_t codepoint = 0;
  size_t bound = 0;
  if (!at_next(text, position, &codepoint, &bound)
      || guni_line_break(codepoint) != GUNI_LB_SA) {
    return 0;
  }
  size_t start = 0;
  if (!at_prev(text, position, &codepoint, &start)
      || guni_line_break(codepoint) != GUNI_LB_SA) {
    return 0;
  }
  /* Walk both ways to the ends of the run. */
  size_t scan = start;
  for (;;) {
    size_t previous = 0;
    if (!at_prev(text, scan, &codepoint, &previous)
        || guni_line_break(codepoint) != GUNI_LB_SA) {
      break;
    }
    scan = previous;
  }
  *start_out = scan;
  scan = position;
  for (;;) {
    uint32_t next = 0;
    size_t end = 0;
    if (!at_next(text, scan, &next, &end)
        || guni_line_break(next) != GUNI_LB_SA) {
      break;
    }
    scan = end;
  }
  *end_out = scan;
  return 1;
}

/** The one place the four rule engines are dispatched to. */
static bool break_at(const Text * text, GUNI_BreakKind kind, size_t position) {
  if (position > text->length) {
    return false;
  }
  /* An empty text has no boundary of any kind. Neither standard says so -
   * both break at offset 0 - but there are no characters, so there is nothing
   * for a boundary to fall between, and it is what regex answers and what
   * Perl answers. */
  if (text->length == 0) {
    return false;
  }
  /* A byte in the middle of a UTF-8 character is not a boundary. Without this
   * the walk either side of it decodes two ill-formed fragments, compares
   * their replacement characters, and reports a boundary between the halves of
   * one character - which a caller mapping clusters to glyphs would believe.
   * The test is "is this a continuation byte", which is exact for well-formed
   * text and is the right answer for ill-formed text too: a stray
   * continuation byte is stepped over as its own character by at_next(), so
   * the only positions this refuses are inside a sequence that really is
   * one. */
  if (text->codepoints == NULL && position > 0 && position < text->length
      && ((unsigned char)text->utf8[position] & 0xC0u) == 0x80u) {
    return false;
  }

  uint32_t before = 0;
  uint32_t after = 0;
  size_t before_start = 0;
  size_t after_end = 0;
  int has_before = at_prev(text, position, &before, &before_start);
  int has_after = at_next(text, position, &after, &after_end);

  if (!has_before) {
    /* The start. UAX #29 breaks here (GB1, WB1, SB1); UAX #14 never does
     * (LB2). */
    return kind != GUNI_BREAK_LINE;
  }
  if (!has_after) {
    return true; /* the end: GB2, WB2, SB2, LB3 */
  }

  switch (kind) {
    case GUNI_BREAK_GRAPHEME:
      return grapheme_break(text, position, before, before_start, after) != 0;
    case GUNI_BREAK_WORD:
      return word_break(text, position, before, after, after_end) != 0;
    case GUNI_BREAK_SENTENCE:
      return sentence_break(text, position, before, after) != 0;
    case GUNI_BREAK_LINE: {
      /* The provider first, and only strictly inside a run of SA characters:
       * a dictionary knows things the rules cannot, and the rules have
       * already lost the SA class to LB1 by the time they run. */
      if (text->provider != NULL && text->provider->sa_break_at != NULL) {
        size_t start = 0;
        size_t end = 0;
        if (sa_run_around(text, position, &start, &end)
            && text->provider->sa_break_at(text->provider->ctx, text, start,
                end, position)) {
          return true;
        }
      }
      return line_break(text, position) != 0;
    }
    case GUNI_BREAK_KIND_COUNT:
    default:
      return false;
  }
}

/** A Text from the public options and a buffer. */
static Text make_text(const GUNI_BreakOptions * options, const char * utf8,
    const uint32_t * codepoints, size_t length) {
  Text text;
  text.utf8 = utf8;
  text.codepoints = codepoints;
  text.length = ((utf8 == NULL && codepoints == NULL)) ? 0 : length;
  text.tailoring = (options != NULL) ? options->tailoring
                                     : GUNI_LINE_BREAK_STRICT;
  text.provider = (options != NULL) ? options->provider : NULL;
  return text;
}

static GUNI_BreakKind kind_of(const GUNI_BreakOptions * options) {
  if (options == NULL) {
    return GUNI_BREAK_GRAPHEME;
  }
  return (options->kind < GUNI_BREAK_KIND_COUNT) ? options->kind
                                                 : GUNI_BREAK_KIND_COUNT;
}

bool guni_break_at(const GUNI_BreakOptions * options, const char * text,
    size_t len, size_t position) {
  Text subject = make_text(options, text, NULL, len);
  return break_at(&subject, kind_of(options), position);
}

bool guni_break_at_codepoints(const GUNI_BreakOptions * options,
    const uint32_t * text, size_t len, size_t position) {
  Text subject = make_text(options, NULL, text, len);
  return break_at(&subject, kind_of(options), position);
}

void guni_break_iter_init(GUNI_BreakIter * iter,
    const GUNI_BreakOptions * options, const char * text, size_t len) {
  if (iter == NULL) {
    return;
  }
  memset(iter, 0, sizeof(*iter));
  iter->utf8 = text;
  iter->length = (text == NULL) ? 0 : len;
  if (options != NULL) {
    iter->options = *options;
  }
}

void guni_break_iter_init_codepoints(GUNI_BreakIter * iter,
    const GUNI_BreakOptions * options, const uint32_t * text, size_t len) {
  if (iter == NULL) {
    return;
  }
  memset(iter, 0, sizeof(*iter));
  iter->codepoints = text;
  iter->length = (text == NULL) ? 0 : len;
  if (options != NULL) {
    iter->options = *options;
  }
}

bool guni_break_iter_next(GUNI_BreakIter * iter, size_t * position_out) {
  if (iter == NULL || position_out == NULL) {
    return false;
  }
  Text subject = make_text(&iter->options, iter->utf8, iter->codepoints,
      iter->length);
  GUNI_BreakKind kind = kind_of(&iter->options);
  /* The iterator is the point query asked at each character boundary, so the
   * two cannot disagree: there is one rule engine and this walks it. The
   * design had it the other way round - the iterator as the primitive - and
   * the reason it is this way is that the rules are written as "is there a
   * boundary between these two characters", which is the point query. */
  while (iter->position <= subject.length) {
    size_t position = iter->position;
    /* Advance to the next character boundary before answering, so that a
     * caller who never stops gets a strictly increasing sequence. */
    if (position < subject.length) {
      uint32_t codepoint = 0;
      size_t end = position;
      if (at_next(&subject, position, &codepoint, &end) && end > position) {
        iter->position = end;
      }
      else {
        iter->position = position + 1;
      }
    }
    else {
      iter->position = position + 1;
    }
    if (break_at(&subject, kind, position)) {
      *position_out = position;
      return true;
    }
  }
  return false;
}

static GUNI_Result break_all(const GUNI_BreakOptions * options,
    const char * utf8, const uint32_t * codepoints, size_t len, size_t * out,
    size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  GUNI_BreakIter iter;
  if (codepoints != NULL) {
    guni_break_iter_init_codepoints(&iter, options, codepoints, len);
  }
  else {
    guni_break_iter_init(&iter, options, utf8, len);
  }
  size_t count = 0;
  bool overflow = false;
  size_t position = 0;
  while (guni_break_iter_next(&iter, &position)) {
    if (count < cap) {
      out[count] = position;
    }
    else {
      overflow = true;
    }
    ++count;
  }
  *out_len = count;
  return overflow ? GUNI_ERR_LIMIT : GUNI_OK;
}

GUNI_Result guni_break_all(const GUNI_BreakOptions * options, const char * text,
    size_t len, size_t * out, size_t cap, size_t * out_len) {
  if (text == NULL && len != 0) {
    return GUNI_ERR_INVALID;
  }
  return break_all(options, text, NULL, len, out, cap, out_len);
}

GUNI_Result guni_break_all_codepoints(const GUNI_BreakOptions * options,
    const uint32_t * text, size_t len, size_t * out, size_t cap,
    size_t * out_len) {
  if (text == NULL && len != 0) {
    return GUNI_ERR_INVALID;
  }
  return break_all(options, NULL, text, len, out, cap, out_len);
}
