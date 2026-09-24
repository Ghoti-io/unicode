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
 * UAX #9, implemented rule by rule with the rule numbers in the comments,
 * because that is the only way this code can be read against the Standard -
 * and reading it against the Standard is how it gets fixed when the Standard
 * changes, which it does most years.
 *
 * Two structural decisions worth knowing before reading:
 *
 * **X9's removed characters are not removed.** The embeddings, overrides and
 * PDFs, and every Boundary_Neutral, are left in place and skipped by an index
 * list per isolating run sequence. Physically removing them would mean
 * rebuilding the text and mapping every position back afterwards; skipping
 * them means the rules run over exactly the characters the Standard says they
 * run over, and the positions are still the caller's.
 *
 * **The rules run over an index list, not over the text.** An isolating run
 * sequence (BD13) is a chain of level runs that can jump over isolated
 * content, so its characters are not contiguous. Materialising the list is
 * what makes W1-W7, N0-N2 and I1-I2 read like the Standard's own prose: each
 * one is a loop over positions 0..n of the sequence.
 */

#include <ghoti.io/unicode/bidi.h>
#include <string.h>
#include "../char/tables/tables.h"

/**
 * The working state: eleven bytes per character, from the stack or the heap.
 *
 * `embedding` is separate from the caller's `levels` for a reason that cost a
 * debugging session: rule X10 computes each isolating run sequence's sos and
 * eos from the levels rules X1 to X9 assigned, and rules I1 and I2 *raise*
 * those levels as each sequence is resolved. Resolving into the same array
 * that X10 reads makes the sos of a later sequence depend on the resolution of
 * an earlier one, which is wrong for about one case in fifteen hundred in
 * BidiCharacterTest.txt - and right for all the simple ones.
 */
typedef struct {
  uint8_t * classes;   ///< The class of each character, as the rules rewrite it.
  uint8_t * embedding; ///< The level rules X1 to X9 gave it. Never rewritten.
  uint32_t * matching; ///< Isolate initiator to its PDI, and PDI to initiator.
  uint32_t * sequence; ///< Scratch for one isolating run sequence's indices.
  size_t length;
} Work;

static bool is_isolate_initiator(uint8_t class_) {
  return class_ == GUNI_BIDI_LRI || class_ == GUNI_BIDI_RLI
      || class_ == GUNI_BIDI_FSI;
}

/** The characters rule X9 removes: the embeddings, the overrides, and BN. */
static bool is_removed_by_x9(uint8_t class_) {
  return class_ == GUNI_BIDI_RLE || class_ == GUNI_BIDI_LRE
      || class_ == GUNI_BIDI_RLO || class_ == GUNI_BIDI_LRO
      || class_ == GUNI_BIDI_PDF || class_ == GUNI_BIDI_BN;
}

/** BD14's "NI": the neutral and isolate formatting classes. */
static bool is_neutral_or_isolate(uint8_t class_) {
  return class_ == GUNI_BIDI_B || class_ == GUNI_BIDI_S
      || class_ == GUNI_BIDI_WS || class_ == GUNI_BIDI_ON
      || class_ == GUNI_BIDI_FSI || class_ == GUNI_BIDI_LRI
      || class_ == GUNI_BIDI_RLI || class_ == GUNI_BIDI_PDI;
}

/**
 * Rule BD9: which PDI matches which isolate initiator.
 *
 * Filled both ways - initiator to PDI and PDI to initiator - because P2, X10
 * and the FSI resolution each need one of the two directions, and computing
 * either on demand is a forward scan that an input of ten thousand isolates
 * turns into a quadratic one.
 */
static void match_isolates(Work * work, const uint32_t * text) {
  (void)text;
  size_t length = work->length;
  for (size_t index = 0; index < length; ++index) {
    work->matching[index] = (uint32_t)length;
  }
  /* A stack of open initiators. Its depth is the nesting depth, which the
   * text can make as deep as it likes: BD9 has no limit, and only X5a-X5c's
   * max_depth does. So the scan uses the matching array itself as the stack,
   * which cannot overflow because there is one slot per character. */
  uint32_t * stack = work->sequence;
  size_t depth = 0;
  for (size_t index = 0; index < length; ++index) {
    uint8_t class_ = work->classes[index];
    if (is_isolate_initiator(class_)) {
      stack[depth++] = (uint32_t)index;
    }
    else if (class_ == GUNI_BIDI_PDI && depth != 0) {
      size_t initiator = stack[--depth];
      work->matching[initiator] = (uint32_t)index;
      work->matching[index] = (uint32_t)initiator;
    }
  }
}

/**
 * Rules P2 and P3 over a range: the first strong class, skipping isolates.
 *
 * Also rule X5c, which is the same question asked about an FSI's contents.
 */
static uint8_t paragraph_level_of(const Work * work, size_t start, size_t end) {
  for (size_t index = start; index < end; ++index) {
    uint8_t class_ = work->classes[index];
    if (is_isolate_initiator(class_)) {
      /* P2: skip to the matching PDI, or to the end if there is none. */
      size_t match = work->matching[index];
      index = (match < end) ? match : end;
      continue;
    }
    if (class_ == GUNI_BIDI_L) {
      return 0;
    }
    if (class_ == GUNI_BIDI_R || class_ == GUNI_BIDI_AL) {
      return 1;
    }
  }
  return 0; /* P3 */
}

/* The directional status stack of rules X1-X8. max_depth + 2 entries, which
 * is what the Standard's own pseudocode uses. */
typedef struct {
  uint8_t level;
  uint8_t override_; ///< GUNI_BIDI_ON for neutral, or L or R.
  bool isolate;
} StatusEntry;

/** Rules X1 to X8: the explicit levels, and the levels of everything else. */
static void resolve_explicit(Work * work, uint8_t paragraph_level) {
  uint8_t * levels = work->embedding;
  StatusEntry stack[GUNI_BIDI_MAX_DEPTH + 2];
  size_t depth = 0;
  stack[depth].level = paragraph_level;
  stack[depth].override_ = GUNI_BIDI_ON;
  stack[depth].isolate = false;
  ++depth;

  size_t overflow_isolate = 0;
  size_t overflow_embedding = 0;
  size_t valid_isolate = 0;

  for (size_t index = 0; index < work->length; ++index) {
    uint8_t class_ = work->classes[index];
    switch (class_) {
      case GUNI_BIDI_RLE:
      case GUNI_BIDI_LRE:
      case GUNI_BIDI_RLO:
      case GUNI_BIDI_LRO: {
        /* X2 to X5. The embedding itself keeps the level it had, and X9 will
         * have it skipped; giving it the new level would show up in the
         * levels array as a character at a level nothing else has. */
        levels[index] = stack[depth - 1].level;
        bool rtl = (class_ == GUNI_BIDI_RLE || class_ == GUNI_BIDI_RLO);
        uint8_t next = rtl
            ? (uint8_t)((stack[depth - 1].level + 1) | 1)
            : (uint8_t)((stack[depth - 1].level + 2) & ~1);
        if (next <= GUNI_BIDI_MAX_DEPTH && overflow_isolate == 0
            && overflow_embedding == 0) {
          stack[depth].level = next;
          stack[depth].override_ = (class_ == GUNI_BIDI_RLO)
              ? GUNI_BIDI_R
              : ((class_ == GUNI_BIDI_LRO) ? GUNI_BIDI_L : GUNI_BIDI_ON);
          stack[depth].isolate = false;
          ++depth;
        }
        else if (overflow_isolate == 0) {
          ++overflow_embedding;
        }
        break;
      }
      case GUNI_BIDI_RLI:
      case GUNI_BIDI_LRI:
      case GUNI_BIDI_FSI: {
        /* X5a, X5b, X5c. An FSI is an RLI or an LRI depending on its own
         * contents, which is rules P2 and P3 asked about a substring. */
        bool rtl;
        if (class_ == GUNI_BIDI_FSI) {
          size_t match = work->matching[index];
          size_t end = (match < work->length) ? match : work->length;
          rtl = paragraph_level_of(work, index + 1, end) == 1;
        }
        else {
          rtl = (class_ == GUNI_BIDI_RLI);
        }
        /* The isolate initiator is given the current level, and the current
         * override applies to it: it is not removed by X9, so it takes part
         * in the rules that follow as an NI. */
        levels[index] = stack[depth - 1].level;
        if (stack[depth - 1].override_ != GUNI_BIDI_ON) {
          work->classes[index] = stack[depth - 1].override_;
        }
        uint8_t next = rtl
            ? (uint8_t)((stack[depth - 1].level + 1) | 1)
            : (uint8_t)((stack[depth - 1].level + 2) & ~1);
        if (next <= GUNI_BIDI_MAX_DEPTH && overflow_isolate == 0
            && overflow_embedding == 0) {
          ++valid_isolate;
          stack[depth].level = next;
          stack[depth].override_ = GUNI_BIDI_ON;
          stack[depth].isolate = true;
          ++depth;
        }
        else {
          ++overflow_isolate;
        }
        break;
      }
      case GUNI_BIDI_PDI: {
        /* X6a. */
        if (overflow_isolate > 0) {
          --overflow_isolate;
        }
        else if (valid_isolate != 0) {
          overflow_embedding = 0;
          while (!stack[depth - 1].isolate) {
            --depth;
          }
          --depth;
          --valid_isolate;
        }
        levels[index] = stack[depth - 1].level;
        if (stack[depth - 1].override_ != GUNI_BIDI_ON) {
          work->classes[index] = stack[depth - 1].override_;
        }
        break;
      }
      case GUNI_BIDI_PDF: {
        /* X7. Like the embeddings, X9 removes it. */
        levels[index] = stack[depth - 1].level;
        if (overflow_isolate > 0) {
          /* nothing */
        }
        else if (overflow_embedding > 0) {
          --overflow_embedding;
        }
        else if (!stack[depth - 1].isolate && depth >= 2) {
          --depth;
        }
        break;
      }
      case GUNI_BIDI_BN: {
        /* X6 lists the types it applies to as "all types besides B, BN, RLE,
         * LRE, RLO, LRO, PDF, RLI, LRI, FSI, PDI", and BN is in that list: a
         * Boundary_Neutral takes the current level but an active override must
         * not rewrite its class. Rewriting it made it R or L, which took it
         * out of the set X9 removes and put it into the rules as a strong
         * character - wrong for 25 of BidiTest.txt's 770,241 cases, all of
         * them a BN inside an override. */
        levels[index] = stack[depth - 1].level;
        break;
      }
      case GUNI_BIDI_B: {
        /* X8. A paragraph separator is at the paragraph level, and ends
         * everything: it can only be the last character of a paragraph. */
        depth = 1;
        overflow_isolate = 0;
        overflow_embedding = 0;
        valid_isolate = 0;
        levels[index] = paragraph_level;
        break;
      }
      default: {
        /* X6. */
        levels[index] = stack[depth - 1].level;
        if (stack[depth - 1].override_ != GUNI_BIDI_ON) {
          work->classes[index] = stack[depth - 1].override_;
        }
        break;
      }
    }
  }
}

/**
 * Does the character at @p index continue an isolating run sequence rather
 * than start one?
 *
 * A PDI belongs to the sequence its isolate initiator is in. "Belongs to"
 * is not the same as "has a matching initiator": the initiator has to be at
 * this level, for the reason the chain in resolve_sequences() explains. A PDI
 * whose initiator is at another level - which a paragraph separator inside the
 * isolate produces - starts its own sequence, and without this it would be in
 * no sequence at all.
 */
static bool continues_a_sequence(const Work * work, size_t index) {
  if (work->classes[index] != GUNI_BIDI_PDI) {
    return false;
  }
  size_t initiator = work->matching[index];
  if (initiator >= work->length) {
    return false;
  }
  return work->embedding[initiator] == work->embedding[index];
}

/** The next character X9 does not remove, at or after @p from. */
static size_t next_kept(const Work * work, size_t from) {
  while (from < work->length && is_removed_by_x9(work->classes[from])) {
    ++from;
  }
  return from;
}

/** L if the level is even, R if it is odd. */
static uint8_t direction_of_level(uint8_t level) {
  return (level & 1) ? GUNI_BIDI_R : GUNI_BIDI_L;
}

/**
 * Rule N0 over one isolating run sequence: the paired brackets.
 *
 * BD16's stack is 63 entries by the Standard's own statement, and when it
 * fills the rule stops for the rest of the sequence - which is a bound the
 * Standard gives precisely so that an implementation does not need an
 * allocator here.
 */
#define BRACKET_STACK_SIZE 63

/** Canonicalise a bracket for BD16's canonical-equivalence clause. */
static uint32_t canonical_bracket(uint32_t cp) {
  /* U+2329 and U+232A are canonically equivalent to U+3008 and U+3009, and
   * BD16 says to treat them as the same bracket. Going through the
   * decomposition table rather than hard-coding the pair means a future
   * singleton equivalence is picked up by regenerating the tables. */
  const GuniPropRecord * record = guni_record(cp);
  if (record->dt != GUNI_DT_CANONICAL) {
    return cp;
  }
  size_t low = 0;
  size_t high = GUNI_DECOMP_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (cp < guni_decomp_codepoint[middle]) {
      high = middle;
    }
    else if (cp > guni_decomp_codepoint[middle]) {
      low = middle + 1;
    }
    else {
      if (guni_decomp_nfd_length[middle] == 1) {
        return guni_decomp_pool[guni_decomp_nfd_offset[middle]];
      }
      return cp;
    }
  }
  return cp;
}

/** The strong class N0 and N1 see: EN and AN count as R. */
static uint8_t strong_class(uint8_t class_) {
  if (class_ == GUNI_BIDI_EN || class_ == GUNI_BIDI_AN) {
    return GUNI_BIDI_R;
  }
  if (class_ == GUNI_BIDI_L || class_ == GUNI_BIDI_R) {
    return class_;
  }
  return GUNI_BIDI_ON;
}

static void resolve_brackets(Work * work, const uint32_t * text,
    const uint32_t * sequence, size_t count, uint8_t level, uint8_t sos,
    const uint8_t * original) {
  struct {
    uint32_t bracket; ///< The expected closing bracket, canonicalised.
    uint32_t position; ///< Its position in the sequence.
  } stack[BRACKET_STACK_SIZE];
  size_t depth = 0;
  uint32_t openers[BRACKET_STACK_SIZE];
  uint32_t closers[BRACKET_STACK_SIZE];
  size_t pairs = 0;

  for (size_t index = 0; index < count; ++index) {
    size_t at = sequence[index];
    if (work->classes[at] != GUNI_BIDI_ON) {
      continue; /* BD14/BD15: only a bracket whose class is still ON */
    }
    bool opening = false;
    uint32_t paired = guni_bidi_paired_bracket(text[at], &opening);
    if (paired == 0) {
      continue;
    }
    if (opening) {
      if (depth == BRACKET_STACK_SIZE) {
        /* BD16: stop processing for the remainder of the sequence. */
        break;
      }
      stack[depth].bracket = canonical_bracket(paired);
      stack[depth].position = (uint32_t)index;
      ++depth;
      continue;
    }
    uint32_t closing = canonical_bracket(text[at]);
    for (size_t probe = depth; probe > 0; --probe) {
      if (stack[probe - 1].bracket != closing) {
        continue;
      }
      if (pairs < BRACKET_STACK_SIZE) {
        openers[pairs] = stack[probe - 1].position;
        closers[pairs] = (uint32_t)index;
        ++pairs;
      }
      depth = probe - 1;
      break;
    }
  }

  /* N0 wants the pairs in the order of their opening bracket. */
  for (size_t outer = 1; outer < pairs; ++outer) {
    uint32_t opener = openers[outer];
    uint32_t closer = closers[outer];
    size_t inner = outer;
    while (inner > 0 && openers[inner - 1] > opener) {
      openers[inner] = openers[inner - 1];
      closers[inner] = closers[inner - 1];
      --inner;
    }
    openers[inner] = opener;
    closers[inner] = closer;
  }

  uint8_t embedding = direction_of_level(level);
  uint8_t opposite = (embedding == GUNI_BIDI_L) ? GUNI_BIDI_R : GUNI_BIDI_L;

  for (size_t pair = 0; pair < pairs; ++pair) {
    bool found_embedding = false;
    bool found_opposite = false;
    for (size_t index = openers[pair] + 1; index < closers[pair]; ++index) {
      uint8_t strong = strong_class(work->classes[sequence[index]]);
      if (strong == embedding) {
        found_embedding = true;
        break;
      }
      if (strong == opposite) {
        found_opposite = true;
      }
    }
    uint8_t decision = GUNI_BIDI_ON;
    if (found_embedding) {
      decision = embedding; /* N0 b */
    }
    else if (found_opposite) {
      /* N0 c: the established context before the opening bracket. */
      uint8_t context = sos;
      for (size_t before = openers[pair]; before > 0; --before) {
        uint8_t strong = strong_class(work->classes[sequence[before - 1]]);
        if (strong != GUNI_BIDI_ON) {
          context = strong;
          break;
        }
      }
      decision = (context == opposite) ? opposite : embedding;
    }
    if (decision == GUNI_BIDI_ON) {
      continue; /* N0 d: leave it to N1 and N2 */
    }
    work->classes[sequence[openers[pair]]] = decision;
    work->classes[sequence[closers[pair]]] = decision;
    /* And the NSMs that followed either bracket before W1 ran. */
    for (size_t side = 0; side < 2; ++side) {
      size_t from = (side == 0) ? openers[pair] : closers[pair];
      for (size_t index = from + 1; index < count; ++index) {
        if (original[sequence[index]] != GUNI_BIDI_NSM) {
          break;
        }
        work->classes[sequence[index]] = decision;
      }
    }
  }
}

/** Rules W1 to W7, N0 to N2 and I1 to I2 over one isolating run sequence. */
static void resolve_sequence(Work * work, const uint32_t * text,
    const uint32_t * sequence, size_t count, uint8_t level, uint8_t sos,
    uint8_t eos, const uint8_t * original, uint8_t * levels) {
  if (count == 0) {
    /* Unreachable: every sequence starts with a character X9 keeps, so it has
     * at least one. Here because every loop below would read sequence[0]. */
    return;
  }

  /* W1: NSM takes the class of the previous character, or sos at the start;
   * after an isolate initiator or a PDI it becomes ON. */
  uint8_t previous = sos;
  for (size_t index = 0; index < count; ++index) {
    size_t at = sequence[index];
    if (work->classes[at] == GUNI_BIDI_NSM) {
      work->classes[at] = (is_isolate_initiator(previous)
                              || previous == GUNI_BIDI_PDI)
          ? GUNI_BIDI_ON
          : previous;
    }
    previous = work->classes[at];
  }

  /* W2: EN becomes AN when the last strong class before it is AL. */
  uint8_t last_strong = sos;
  for (size_t index = 0; index < count; ++index) {
    size_t at = sequence[index];
    uint8_t class_ = work->classes[at];
    if (class_ == GUNI_BIDI_EN && last_strong == GUNI_BIDI_AL) {
      work->classes[at] = GUNI_BIDI_AN;
    }
    else if (class_ == GUNI_BIDI_L || class_ == GUNI_BIDI_R
        || class_ == GUNI_BIDI_AL) {
      last_strong = class_;
    }
  }

  /* W3: AL becomes R. */
  for (size_t index = 0; index < count; ++index) {
    if (work->classes[sequence[index]] == GUNI_BIDI_AL) {
      work->classes[sequence[index]] = GUNI_BIDI_R;
    }
  }

  /* W4: a single ES between two ENs, or a single CS between two numbers of
   * the same kind, becomes that kind. */
  for (size_t index = 1; index + 1 < count; ++index) {
    uint8_t class_ = work->classes[sequence[index]];
    uint8_t before = work->classes[sequence[index - 1]];
    uint8_t after = work->classes[sequence[index + 1]];
    if (class_ == GUNI_BIDI_ES && before == GUNI_BIDI_EN
        && after == GUNI_BIDI_EN) {
      work->classes[sequence[index]] = GUNI_BIDI_EN;
    }
    else if (class_ == GUNI_BIDI_CS && before == after
        && (before == GUNI_BIDI_EN || before == GUNI_BIDI_AN)) {
      work->classes[sequence[index]] = before;
    }
  }

  /* W5: a run of ETs adjacent to an EN becomes EN. */
  for (size_t index = 0; index < count; ++index) {
    if (work->classes[sequence[index]] != GUNI_BIDI_ET) {
      continue;
    }
    size_t end = index;
    while (end < count && work->classes[sequence[end]] == GUNI_BIDI_ET) {
      ++end;
    }
    bool adjacent = (index > 0
                        && work->classes[sequence[index - 1]] == GUNI_BIDI_EN)
        || (end < count && work->classes[sequence[end]] == GUNI_BIDI_EN);
    if (adjacent) {
      for (size_t at = index; at < end; ++at) {
        work->classes[sequence[at]] = GUNI_BIDI_EN;
      }
    }
    index = end - 1;
  }

  /* W6: what is left of ET, ES and CS becomes ON. */
  for (size_t index = 0; index < count; ++index) {
    uint8_t class_ = work->classes[sequence[index]];
    if (class_ == GUNI_BIDI_ET || class_ == GUNI_BIDI_ES
        || class_ == GUNI_BIDI_CS) {
      work->classes[sequence[index]] = GUNI_BIDI_ON;
    }
  }

  /* W7: EN becomes L when the last strong class before it is L. */
  last_strong = sos;
  for (size_t index = 0; index < count; ++index) {
    size_t at = sequence[index];
    uint8_t class_ = work->classes[at];
    if (class_ == GUNI_BIDI_EN && last_strong == GUNI_BIDI_L) {
      work->classes[at] = GUNI_BIDI_L;
    }
    else if (class_ == GUNI_BIDI_L || class_ == GUNI_BIDI_R) {
      last_strong = class_;
    }
  }

  /* N0: the paired brackets, before the neutrals are swept up. */
  resolve_brackets(work, text, sequence, count, level, sos, original);

  /* N1: a run of NIs between two classes of the same direction takes that
   * direction, with EN and AN counting as R, and sos and eos at the edges.
   * N2: whatever is left takes the embedding direction. */
  uint8_t embedding = direction_of_level(level);
  for (size_t index = 0; index < count; ++index) {
    if (!is_neutral_or_isolate(work->classes[sequence[index]])) {
      continue;
    }
    size_t end = index;
    while (end < count
        && is_neutral_or_isolate(work->classes[sequence[end]])) {
      ++end;
    }
    uint8_t before = (index > 0)
        ? strong_class(work->classes[sequence[index - 1]])
        : sos;
    uint8_t after = (end < count)
        ? strong_class(work->classes[sequence[end]])
        : eos;
    uint8_t decision = (before == after && before != GUNI_BIDI_ON)
        ? before
        : embedding;
    for (size_t at = index; at < end; ++at) {
      work->classes[sequence[at]] = decision;
    }
    index = end - 1;
  }

  /* I1 and I2: the implicit levels. */
  for (size_t index = 0; index < count; ++index) {
    size_t at = sequence[index];
    uint8_t class_ = work->classes[at];
    if ((level & 1) == 0) {
      if (class_ == GUNI_BIDI_R) {
        levels[at] = (uint8_t)(level + 1);
      }
      else if (class_ == GUNI_BIDI_AN || class_ == GUNI_BIDI_EN) {
        levels[at] = (uint8_t)(level + 2);
      }
      else {
        levels[at] = level;
      }
    }
    else {
      if (class_ == GUNI_BIDI_L || class_ == GUNI_BIDI_AN
          || class_ == GUNI_BIDI_EN) {
        levels[at] = (uint8_t)(level + 1);
      }
      else {
        levels[at] = level;
      }
    }
  }
}

/**
 * Rule X10: build every isolating run sequence and resolve it.
 *
 * BD13's construction: a sequence starts at a level run whose first character
 * is not a PDI that matches an initiator, and continues through the level run
 * after the matching PDI of whatever isolate initiator ends the current run.
 */
static void resolve_sequences(Work * work, const uint32_t * text,
    uint8_t paragraph_level, uint8_t * levels, const uint8_t * original) {
  size_t length = work->length;
  const uint8_t * embedding = work->embedding;
  for (size_t start = 0; start < length; ++start) {
    if (is_removed_by_x9(work->classes[start])) {
      continue;
    }
    /* Is this the start of a level run? */
    size_t previous = start;
    bool is_run_start = true;
    while (previous > 0) {
      --previous;
      if (is_removed_by_x9(work->classes[previous])) {
        continue;
      }
      is_run_start = embedding[previous] != embedding[start];
      break;
    }
    if (!is_run_start) {
      continue;
    }
    if (continues_a_sequence(work, start)) {
      continue;
    }

    uint8_t level = embedding[start];
    size_t count = 0;
    size_t at = start;
    size_t last = start;
    bool open_isolate = false;
    for (;;) {
      /* Walk one level run. */
      while (at < length) {
        if (is_removed_by_x9(work->classes[at])) {
          ++at;
          continue;
        }
        if (embedding[at] != level) {
          break;
        }
        work->sequence[count++] = (uint32_t)at;
        last = at;
        ++at;
      }
      /* BD13: continue after the matching PDI when this run ends with an
       * isolate initiator that has one. */
      if (!is_isolate_initiator(work->classes[last])) {
        open_isolate = false;
        break;
      }
      size_t match = work->matching[last];
      /* BD13: the sequence continues only when the matching PDI is the first
       * character of the *next level run* - which means at this level. A
       * paragraph separator inside an isolate breaks that: rule X8 resets the
       * directional status stack, so the PDI comes back at the paragraph level
       * while its initiator is deeper, and the PDI is then not part of this
       * sequence at all.
       *
       * Without the level test this loop never terminated: the inner walk
       * added nothing, `last` stayed the isolate initiator, and the chain
       * jumped to the same PDI for ever. Four characters were enough -
       * U+202B U+2066 U+000A U+2069 - and the fuzzer found it in a minute,
       * having been given the whole of BidiTest.txt to disagree with first.
       * The input is out of contract, because rule P1 makes a paragraph
       * separator the last character of its paragraph and this one is in the
       * middle, but a library must not hang on input it was not promised.
       *
       * The `match > last` test cannot fail - BD9 matches forwards - and is
       * here so that forward progress is a property of this loop rather than
       * of a function three hundred lines away. */
      if (match >= length || embedding[match] != level || match <= last) {
        open_isolate = true; /* eos comes from the paragraph level */
        break;
      }
      at = match;
    }

    /* X10: sos and eos, from the levels either side of the sequence. */
    size_t before = start;
    uint8_t before_level = paragraph_level;
    while (before > 0) {
      --before;
      if (is_removed_by_x9(work->classes[before])) {
        continue;
      }
      before_level = embedding[before];
      break;
    }
    uint8_t sos_level = (before_level > level) ? before_level : level;
    uint8_t eos_level;
    if (open_isolate) {
      eos_level = (paragraph_level > level) ? paragraph_level : level;
    }
    else {
      size_t after = next_kept(work, last + 1);
      uint8_t after_level = (after < length) ? embedding[after] : paragraph_level;
      eos_level = (after_level > level) ? after_level : level;
    }

    resolve_sequence(work, text, work->sequence, count, level,
        direction_of_level(sos_level), direction_of_level(eos_level), original,
        levels);
  }
}

/** Rule L1: separators and trailing whitespace go back to the paragraph level. */
static void reset_whitespace(const uint8_t * original, size_t length,
    uint8_t paragraph_level, uint8_t * levels) {
  /* L1 reads the *original* classes, not the resolved ones, which is the
   * whole subtlety: by this point every neutral has been given a direction,
   * and a rule that looked at the resolved class would reset nothing. */
  bool trailing = true;
  for (size_t index = length; index > 0; --index) {
    uint8_t class_ = original[index - 1];
    if (class_ == GUNI_BIDI_B || class_ == GUNI_BIDI_S) {
      levels[index - 1] = paragraph_level;
      trailing = true;
    }
    else if (trailing
        && (class_ == GUNI_BIDI_WS || is_isolate_initiator(class_)
            || class_ == GUNI_BIDI_PDI)) {
      levels[index - 1] = paragraph_level;
    }
    else if (is_removed_by_x9(class_)) {
      /* X9's removed characters do not end a trailing run: UAX #9 section 5.2
       * says to treat them as the whitespace they sit in. */
    }
    else {
      trailing = false;
    }
  }
}

/**
 * The levels of the characters X9 removed.
 *
 * UAX #9 section 5.2: give each the level of the character before it, so that
 * a caller iterating levels alongside text sees no discontinuity. No
 * conformance file checks these - BidiTest.txt writes "x" for them - which is
 * exactly why they need a rule rather than whatever was left in the array.
 */
static void level_removed_characters(const Work * work, uint8_t paragraph_level,
    uint8_t * levels) {
  uint8_t previous = paragraph_level;
  for (size_t index = 0; index < work->length; ++index) {
    if (is_removed_by_x9(work->classes[index])) {
      levels[index] = previous;
    }
    else {
      previous = levels[index];
    }
  }
}

static GUNI_Result run(const uint32_t * text, size_t len,
    GUNI_BidiDirection direction, uint8_t * levels, uint8_t * classes,
    uint32_t * matching, uint32_t * sequence, uint8_t * original,
    uint8_t * embedding, uint8_t * paragraph_level_out) {
  Work work;
  work.classes = classes;
  work.embedding = embedding;
  work.matching = matching;
  work.sequence = sequence;
  work.length = len;

  for (size_t index = 0; index < len; ++index) {
    uint8_t class_ = (uint8_t)guni_bidi_class(text[index]);
    classes[index] = class_;
    original[index] = class_;
    embedding[index] = 0;
  }

  match_isolates(&work, text);

  uint8_t paragraph_level;
  switch (direction) {
    case GUNI_BIDI_RTL:
      paragraph_level = 1;
      break;
    case GUNI_BIDI_AUTO:
      paragraph_level = paragraph_level_of(&work, 0, len);
      break;
    case GUNI_BIDI_LTR:
    default:
      paragraph_level = 0;
      break;
  }

  resolve_explicit(&work, paragraph_level);
  /* Every character starts at the level X1-X9 gave it; the sequence
   * resolution overwrites the ones that are in a sequence, which is all of
   * them except the characters X9 removed. */
  memcpy(levels, embedding, len);
  resolve_sequences(&work, text, paragraph_level, levels, original);
  reset_whitespace(original, len, paragraph_level, levels);
  level_removed_characters(&work, paragraph_level, levels);

  if (paragraph_level_out != NULL) {
    *paragraph_level_out = paragraph_level;
  }
  return GUNI_OK;
}

static GUNI_Result check_arguments(const uint32_t * text, size_t len,
    const GUNI_Limits * limits, const uint8_t * levels, size_t cap) {
  if ((text == NULL && len != 0) || (levels == NULL && len != 0)) {
    return GUNI_ERR_INVALID;
  }
  if (cap < len) {
    return GUNI_ERR_INVALID;
  }
  GUNI_Limits storage;
  if (limits == NULL) {
    guni_limits_default(&storage);
    limits = &storage;
  }
  if (limits->max_bidi_depth != GUNI_BIDI_MAX_DEPTH) {
    /* The Standard fixes it at 125, and its own conformance data depends on
     * the overflow behaviour at exactly that value. A caller who raised it
     * would get an implementation that agrees with nothing else. */
    return GUNI_ERR_LIMIT;
  }
  if (len > limits->max_text_bytes / 4) {
    return GUNI_ERR_LIMIT;
  }
  if (len > UINT32_MAX) {
    /* Positions are stored as uint32_t in the working arrays, which is nine
     * bytes per character rather than thirteen. A paragraph of four billion
     * characters is refused rather than silently truncated. */
    return GUNI_ERR_LIMIT;
  }
  return GUNI_OK;
}

GUNI_Result guni_bidi_levels(const uint32_t * text, size_t len,
    GUNI_BidiDirection direction, const GUNI_Limits * limits, uint8_t * levels,
    size_t cap, uint8_t * paragraph_level_out) {
  GUNI_Result result = check_arguments(text, len, limits, levels, cap);
  if (result != GUNI_OK) {
    return result;
  }
  if (len == 0) {
    if (paragraph_level_out != NULL) {
      *paragraph_level_out = (direction == GUNI_BIDI_RTL) ? 1 : 0;
    }
    return GUNI_OK;
  }
  if (len > GUNI_BIDI_MAX_STACK_LENGTH) {
    return GUNI_ERR_LIMIT;
  }
  uint8_t classes[GUNI_BIDI_MAX_STACK_LENGTH];
  uint8_t original[GUNI_BIDI_MAX_STACK_LENGTH];
  uint8_t embedding[GUNI_BIDI_MAX_STACK_LENGTH];
  uint32_t matching[GUNI_BIDI_MAX_STACK_LENGTH];
  uint32_t sequence[GUNI_BIDI_MAX_STACK_LENGTH];
  return run(text, len, direction, levels, classes, matching, sequence,
      original, embedding, paragraph_level_out);
}

GUNI_Result guni_bidi_levels_with_allocator(const uint32_t * text, size_t len,
    GUNI_BidiDirection direction, const GUNI_Limits * limits, uint8_t * levels,
    size_t cap, uint8_t * paragraph_level_out, GUNI_Allocator * allocator) {
  GUNI_Result result = check_arguments(text, len, limits, levels, cap);
  if (result != GUNI_OK) {
    return result;
  }
  if (len == 0) {
    if (paragraph_level_out != NULL) {
      *paragraph_level_out = (direction == GUNI_BIDI_RTL) ? 1 : 0;
    }
    return GUNI_OK;
  }
  const GUNI_Allocator * active = (allocator != NULL)
      ? allocator
      : guni_allocator_default();
  /* One allocation, not four: the three working arrays and the original
   * classes are one block, so a partial failure cannot leave some of them
   * allocated. */
  size_t bytes = len * (3 * sizeof(uint8_t) + 2 * sizeof(uint32_t));
  if (bytes / len != (3 * sizeof(uint8_t) + 2 * sizeof(uint32_t))) {
    return GUNI_ERR_LIMIT; /* the multiplication overflowed */
  }
  unsigned char * block =
      (unsigned char *)active->malloc_fn(active->ctx, bytes);
  if (block == NULL) {
    return GUNI_ERR_OOM;
  }
  uint32_t * matching = (uint32_t *)(void *)block;
  uint32_t * sequence = matching + len;
  uint8_t * classes = (uint8_t *)(sequence + len);
  uint8_t * original = classes + len;
  uint8_t * embedding = original + len;
  result = run(text, len, direction, levels, classes, matching, sequence,
      original, embedding, paragraph_level_out);
  active->free_fn(active->ctx, block);
  return result;
}

GUNI_Result guni_bidi_levels_utf8(const char * text, size_t len,
    GUNI_Invalid policy, GUNI_BidiDirection direction,
    const GUNI_Limits * limits, uint8_t * levels, size_t cap,
    size_t * count_out, uint8_t * paragraph_level_out) {
  if (count_out == NULL || (text == NULL && len != 0)) {
    return GUNI_ERR_INVALID;
  }
  *count_out = 0;
  uint32_t codepoints[GUNI_BIDI_MAX_STACK_LENGTH];
  size_t count = 0;
  size_t offset = 0;
  while (offset < len) {
    uint32_t cp = 0;
    bool valid = false;
    size_t used = guni_utf8_decode(text + offset, len - offset, &cp, &valid);
    offset += used;
    if (!valid) {
      if (policy == GUNI_INVALID_REFUSE) {
        return GUNI_ERR_INVALID;
      }
      if (policy == GUNI_INVALID_SKIP) {
        continue;
      }
    }
    if (count == GUNI_BIDI_MAX_STACK_LENGTH) {
      return GUNI_ERR_LIMIT;
    }
    codepoints[count++] = cp;
  }
  if (cap < count) {
    *count_out = count;
    return GUNI_ERR_LIMIT;
  }
  GUNI_Result result = guni_bidi_levels(codepoints, count, direction, limits,
      levels, cap, paragraph_level_out);
  if (result == GUNI_OK) {
    *count_out = count;
  }
  return result;
}

GUNI_Result guni_bidi_reorder(const uint8_t * levels, size_t len, size_t * out,
    size_t cap) {
  if ((levels == NULL && len != 0) || (out == NULL && len != 0) || cap < len) {
    return GUNI_ERR_INVALID;
  }
  for (size_t index = 0; index < len; ++index) {
    out[index] = index;
  }
  if (len == 0) {
    return GUNI_OK;
  }
  /* Rule L2: from the highest level down to the lowest odd level, reverse
   * every contiguous run at or above that level. Done on the permutation
   * rather than on the text, which is the same operation and leaves the
   * caller's text alone. */
  uint8_t highest = 0;
  uint8_t lowest_odd = (uint8_t)(GUNI_BIDI_MAX_DEPTH + 2);
  for (size_t index = 0; index < len; ++index) {
    if (levels[index] > highest) {
      highest = levels[index];
    }
    if ((levels[index] & 1) && levels[index] < lowest_odd) {
      lowest_odd = levels[index];
    }
  }
  for (uint8_t level = highest; level >= lowest_odd && level > 0; --level) {
    size_t index = 0;
    while (index < len) {
      if (levels[index] < level) {
        ++index;
        continue;
      }
      size_t end = index;
      while (end < len && levels[end] >= level) {
        ++end;
      }
      for (size_t left = index, right = end - 1; left < right; ++left, --right) {
        size_t swap = out[left];
        out[left] = out[right];
        out[right] = swap;
      }
      index = end;
    }
  }
  return GUNI_OK;
}

uint32_t guni_bidi_mirror(uint32_t cp) {
  size_t low = 0;
  size_t high = GUNI_MIRROR_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (cp < guni_mirror_from[middle]) {
      high = middle;
    }
    else if (cp > guni_mirror_from[middle]) {
      low = middle + 1;
    }
    else {
      return guni_mirror_to[middle];
    }
  }
  return cp;
}

uint32_t guni_bidi_paired_bracket(uint32_t cp, bool * opening_out) {
  size_t low = 0;
  size_t high = GUNI_BRACKET_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (cp < guni_bracket_from[middle]) {
      high = middle;
    }
    else if (cp > guni_bracket_from[middle]) {
      low = middle + 1;
    }
    else {
      if (opening_out != NULL) {
        *opening_out = guni_bracket_kind[middle] == 1;
      }
      return guni_bracket_pair[middle];
    }
  }
  return 0;
}
