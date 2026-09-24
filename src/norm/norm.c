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
 * UAX #15, and the one decision the whole file turns on: **the ordering has
 * to finish before the composition starts.**
 *
 * A normaliser that composes as it goes is wrong, and wrong in a way that
 * passes casual testing. Take U+0041, then a mark of class 230, then a mark
 * of class 220. Compose eagerly and the 230 mark is absorbed into the A
 * before the 220 mark arrives; the correct answer orders 220 before 230 and
 * composes A with the 220 mark instead. So this file decomposes and orders
 * into the output buffer, and then composes in place over what it wrote -
 * two passes, and a buffer that has to hold the intermediate.
 *
 * Everything is in place or in a fixed stack buffer. Nothing allocates.
 */

#include <ghoti.io/unicode/norm.h>
#include <string.h>
#include "../char/tables/tables.h"

/* Standard section 3.12's Hangul constants. Spelled once, here, because the
 * arithmetic appears in four functions and a transcription error in one of
 * them would be a bug in one direction only. */
#define HANGUL_SBASE UINT32_C(0xAC00)
#define HANGUL_LBASE UINT32_C(0x1100)
#define HANGUL_VBASE UINT32_C(0x1161)
#define HANGUL_TBASE UINT32_C(0x11A7)
#define HANGUL_LCOUNT 19u
#define HANGUL_VCOUNT 21u
#define HANGUL_TCOUNT 28u
#define HANGUL_NCOUNT (HANGUL_VCOUNT * HANGUL_TCOUNT) /* 588 */
#define HANGUL_SCOUNT (HANGUL_LCOUNT * HANGUL_NCOUNT) /* 11172 */

/// U+034F COMBINING GRAPHEME JOINER, what the stream-safe transform inserts.
#define GUNI_CGJ UINT32_C(0x034F)

/**
 * The working buffer for the chunked entry points, in codepoints.
 *
 * Big enough that no natural text reaches it: a chunk is one run between
 * normalisation boundaries, and in real text a boundary falls at nearly every
 * character. Small enough to be a stack frame.
 */
#define GUNI_NORM_CHUNK 1024

static const GUNI_Limits * effective_limits(const GUNI_Limits * limits,
    GUNI_Limits * storage) {
  if (limits != NULL) {
    return limits;
  }
  guni_limits_default(storage);
  return storage;
}

/** The row for a codepoint in the decomposition table, or -1. */
static size_t decomposition_row(uint32_t cp) {
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
      return middle;
    }
  }
  return GUNI_DECOMP_COUNT;
}

/**
 * Decompose one codepoint into @p buffer, which must hold
 * GUNI_NORM_MAX_EXPANSION_NFKD.
 *
 * The lookup is gated by Decomposition_Type, which is a field of the property
 * record: the 1,108,198 codepoints that do not decompose cost one trie lookup
 * and no search at all.
 */
static size_t decompose_into(uint32_t cp, bool compatibility,
    uint32_t * buffer) {
  const GuniPropRecord * record = guni_record(cp);
  if (record->dt == GUNI_DT_NONE) {
    buffer[0] = cp;
    return 1;
  }
  if (record->hst == GUNI_HST_LV || record->hst == GUNI_HST_LVT) {
    /* Arithmetic, not a table: Standard section 3.12. */
    uint32_t index = cp - HANGUL_SBASE;
    uint32_t lead = HANGUL_LBASE + index / HANGUL_NCOUNT;
    uint32_t vowel = HANGUL_VBASE + (index % HANGUL_NCOUNT) / HANGUL_TCOUNT;
    uint32_t trail = index % HANGUL_TCOUNT;
    buffer[0] = lead;
    buffer[1] = vowel;
    if (trail == 0) {
      return 2;
    }
    buffer[2] = HANGUL_TBASE + trail;
    return 3;
  }
  if (!compatibility && record->dt != GUNI_DT_CANONICAL) {
    buffer[0] = cp;
    return 1;
  }
  size_t row = decomposition_row(cp);
  if (row == GUNI_DECOMP_COUNT) {
    /* Unreachable: Decomposition_Type and the decomposition table come from
     * one generator run over one file. Here because the alternative to a
     * guard is indexing the table with a row that is not in it. */
    /* Decomposition_Type says there is one and the table does not have it,
     * which would be a generator bug rather than an input one. Answering with
     * the codepoint keeps the caller's text intact either way. */
    buffer[0] = cp;
    return 1;
  }
  uint16_t offset = compatibility
      ? guni_decomp_nfkd_offset[row]
      : guni_decomp_nfd_offset[row];
  uint8_t length = compatibility
      ? guni_decomp_nfkd_length[row]
      : guni_decomp_nfd_length[row];
  if (length == 0) {
    buffer[0] = cp;
    return 1;
  }
  memcpy(buffer, &guni_decomp_pool[offset], length * sizeof(uint32_t));
  return length;
}

GUNI_Result guni_decompose(uint32_t cp, bool compatibility, uint32_t * out,
    size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)) {
    return GUNI_ERR_INVALID;
  }
  uint32_t buffer[GUNI_NORM_MAX_EXPANSION_NFKD];
  size_t length = decompose_into(cp, compatibility, buffer);
  *out_len = length;
  if (length > cap) {
    return GUNI_ERR_LIMIT;
  }
  memcpy(out, buffer, length * sizeof(uint32_t));
  return GUNI_OK;
}

uint32_t guni_compose(uint32_t first, uint32_t second) {
  /* Hangul first, by arithmetic. L + V and LV + T are 11,172 pairs that a
   * table would carry for nothing. */
  uint32_t lead = first - HANGUL_LBASE;
  if (lead < HANGUL_LCOUNT) {
    uint32_t vowel = second - HANGUL_VBASE;
    if (vowel < HANGUL_VCOUNT) {
      return HANGUL_SBASE + (lead * HANGUL_VCOUNT + vowel) * HANGUL_TCOUNT;
    }
  }
  uint32_t syllable = first - HANGUL_SBASE;
  if (syllable < HANGUL_SCOUNT && syllable % HANGUL_TCOUNT == 0) {
    uint32_t trail = second - HANGUL_TBASE;
    if (trail > 0 && trail < HANGUL_TCOUNT) {
      return first + trail;
    }
  }

  uint64_t key = GUNI_COMPOSE_KEY(first, second);
  size_t low = 0;
  size_t high = GUNI_COMPOSE_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (key < guni_compose_key[middle]) {
      high = middle;
    }
    else if (key > guni_compose_key[middle]) {
      low = middle + 1;
    }
    else {
      return guni_compose_value[middle];
    }
  }
  return 0;
}

GUNI_QuickCheck guni_quick_check(GUNI_NormForm form, uint32_t cp) {
  static const GUNI_Property properties[GUNI_NORM_FORM_COUNT] = {
    GUNI_PROPERTY_NFC_QUICK_CHECK, GUNI_PROPERTY_NFD_QUICK_CHECK,
    GUNI_PROPERTY_NFKC_QUICK_CHECK, GUNI_PROPERTY_NFKD_QUICK_CHECK};
  if ((unsigned)form >= GUNI_NORM_FORM_COUNT) {
    return GUNI_QC_NO;
  }
  return (GUNI_QuickCheck)guni_property_value(cp, properties[form]);
}

GUNI_QuickCheck guni_quick_check_text(GUNI_NormForm form,
    const uint32_t * text, size_t len) {
  if ((unsigned)form >= GUNI_NORM_FORM_COUNT || (text == NULL && len != 0)) {
    return GUNI_QC_NO;
  }
  /* UAX #15 section 9's algorithm: a class that decreases without an
   * intervening starter is out of canonical order, so the text is not
   * normalised whatever the quick-check properties say. */
  uint8_t last_class = 0;
  GUNI_QuickCheck answer = GUNI_QC_YES;
  for (size_t index = 0; index < len; ++index) {
    uint32_t cp = text[index];
    uint8_t combining = guni_combining_class(cp);
    if (last_class > combining && combining != 0) {
      return GUNI_QC_NO;
    }
    GUNI_QuickCheck check = guni_quick_check(form, cp);
    if (check == GUNI_QC_NO) {
      return GUNI_QC_NO;
    }
    if (check == GUNI_QC_MAYBE) {
      answer = GUNI_QC_MAYBE;
    }
    last_class = combining;
  }
  return answer;
}

GUNI_QuickCheck guni_quick_check_utf8(GUNI_NormForm form, const char * text,
    size_t len) {
  if ((unsigned)form >= GUNI_NORM_FORM_COUNT || (text == NULL && len != 0)) {
    return GUNI_QC_NO;
  }
  uint8_t last_class = 0;
  GUNI_QuickCheck answer = GUNI_QC_YES;
  size_t offset = 0;
  while (offset < len) {
    uint32_t cp = 0;
    bool valid = false;
    size_t used = guni_utf8_decode(text + offset, len - offset, &cp, &valid);
    offset += used;
    if (!valid) {
      /* Ill-formed bytes are not in any normalisation form, because they are
       * not text. Saying MAYBE would send the caller to normalise something
       * that cannot be normalised. */
      return GUNI_QC_NO;
    }
    uint8_t combining = guni_combining_class(cp);
    if (last_class > combining && combining != 0) {
      return GUNI_QC_NO;
    }
    GUNI_QuickCheck check = guni_quick_check(form, cp);
    if (check == GUNI_QC_NO) {
      return GUNI_QC_NO;
    }
    if (check == GUNI_QC_MAYBE) {
      answer = GUNI_QC_MAYBE;
    }
    last_class = combining;
  }
  return answer;
}

void guni_canonical_order(uint32_t * text, size_t len) {
  if (text == NULL) {
    return;
  }
  /* An insertion sort, which is what the Standard describes: the runs are
   * short - two marks is a long one - and it is stable, so two marks of the
   * same class keep the order the text had. A faster sort that was not stable
   * would change text that is already normalised. */
  for (size_t index = 1; index < len; ++index) {
    uint32_t cp = text[index];
    uint8_t combining = guni_combining_class(cp);
    if (combining == 0) {
      continue; /* a starter ends the run; nothing moves across it */
    }
    size_t position = index;
    while (position > 0) {
      uint8_t previous = guni_combining_class(text[position - 1]);
      if (previous == 0 || previous <= combining) {
        break;
      }
      text[position] = text[position - 1];
      --position;
    }
    text[position] = cp;
  }
}

/**
 * Compose a canonically ordered sequence in place, returning the new length.
 *
 * UAX #15's Canonical Composition Algorithm. "Blocked" is the whole subtlety:
 * a character can only compose with the last starter if no character between
 * them has a class greater than or equal to its own, which is what keeps
 * A + ring-above + acute from composing the acute onto the A.
 */
static size_t compose_in_place(uint32_t * text, size_t len) {
  if (len == 0) {
    return 0;
  }
  size_t starter = 0;       /* index of the last starter in the output */
  bool have_starter = guni_combining_class(text[0]) == 0;
  size_t written = 1;
  uint8_t last_class = guni_combining_class(text[0]);
  for (size_t index = 1; index < len; ++index) {
    uint32_t cp = text[index];
    uint8_t combining = guni_combining_class(cp);
    if (have_starter && (last_class < combining || last_class == 0)) {
      uint32_t composite = guni_compose(text[starter], cp);
      if (composite != 0) {
        text[starter] = composite;
        /* last_class is deliberately not updated: the composed character is
         * gone from the sequence, so what blocks the next one is still the
         * class of the last character actually kept. */
        continue;
      }
    }
    if (combining == 0) {
      starter = written;
      have_starter = true;
    }
    last_class = combining;
    text[written++] = cp;
  }
  return written;
}

/**
 * Is @p cp a normalisation boundary for @p form?
 *
 * A starter whose quick-check property is YES: the text before it and the
 * text from it normalise independently, because YES means it never composes
 * with what precedes it and a starter means nothing reorders across it. This
 * is what lets the UTF-8 entry points work in a fixed buffer.
 */
static bool is_boundary(GUNI_NormForm form, uint32_t cp) {
  return guni_combining_class(cp) == 0 && guni_quick_check(form, cp) == GUNI_QC_YES;
}

/**
 * Decompose @p text into @p out with canonical ordering applied, writing at
 * most @p cap and always reporting the length needed.
 *
 * The ordering is done as the codepoints are appended - each non-starter
 * bubbles back past the trailing marks with a higher class - so there is no
 * separate sort pass and no second buffer. Counting continues past the
 * overflow so that the caller learns the whole requirement from one call.
 */
static size_t decompose_all(GUNI_NormForm form, const uint32_t * text,
    size_t len, uint32_t * out, size_t cap, bool * overflow) {
  bool compatibility = (form == GUNI_NFKC || form == GUNI_NFKD);
  size_t written = 0;
  for (size_t index = 0; index < len; ++index) {
    uint32_t buffer[GUNI_NORM_MAX_EXPANSION_NFKD];
    size_t count = decompose_into(text[index], compatibility, buffer);
    for (size_t part = 0; part < count; ++part) {
      if (written >= cap) {
        *overflow = true;
        ++written;
        continue;
      }
      uint32_t cp = buffer[part];
      uint8_t combining = guni_combining_class(cp);
      size_t position = written;
      if (combining != 0) {
        while (position > 0) {
          uint8_t previous = guni_combining_class(out[position - 1]);
          if (previous == 0 || previous <= combining) {
            break;
          }
          out[position] = out[position - 1];
          --position;
        }
      }
      out[position] = cp;
      ++written;
    }
  }
  return written;
}

GUNI_Result guni_normalize(GUNI_NormForm form, const uint32_t * text,
    size_t len, const GUNI_Limits * limits, uint32_t * out, size_t cap,
    size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)
      || (text == NULL && len != 0)
      || (unsigned)form >= GUNI_NORM_FORM_COUNT) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  GUNI_Limits storage;
  const GUNI_Limits * active = effective_limits(limits, &storage);
  if (len > active->max_text_bytes / 4) {
    return GUNI_ERR_LIMIT;
  }

  bool overflow = false;
  size_t written = decompose_all(form, text, len, out, cap, &overflow);
  if (overflow) {
    /* The decomposed length, which is what the buffer has to hold even for
     * the composing forms: see the header. */
    *out_len = written;
    return GUNI_ERR_LIMIT;
  }
  if (form == GUNI_NFC || form == GUNI_NFKC) {
    written = compose_in_place(out, written);
  }
  *out_len = written;
  return GUNI_OK;
}


/**
 * Where the next chunk of UTF-8 ends.
 *
 * Decodes forward from @p start, stopping at the last **normalisation
 * boundary** it saw before running out of room, so that the chunk can be
 * normalised without seeing what follows. One helper rather than one per
 * entry point: two pieces of code deciding the same boundary is how they come
 * to decide it differently.
 *
 * @param count_out Receives how many codepoints the chunk holds.
 * @param truncated_out Receives true when there was no boundary to stop at,
 *        which is the refusal case.
 * @param invalid_out Receives true when the policy is REFUSE and the bytes
 *        are ill-formed.
 * @return The offset just past the chunk.
 */
static size_t next_chunk_utf8(GUNI_NormForm form, const char * text, size_t len,
    size_t start, size_t max_codepoints, GUNI_Invalid policy,
    size_t * count_out, bool * truncated_out, bool * invalid_out) {
  size_t count = 0;
  size_t boundary_offset = 0;
  size_t boundary_count = 0;
  size_t scan = start;
  *count_out = 0;
  *truncated_out = false;
  *invalid_out = false;
  while (scan < len && count < max_codepoints) {
    uint32_t cp = 0;
    bool valid = false;
    size_t used = guni_utf8_decode(text + scan, len - scan, &cp, &valid);
    if (!valid) {
      if (policy == GUNI_INVALID_REFUSE) {
        *invalid_out = true;
        return scan;
      }
      if (policy == GUNI_INVALID_SKIP) {
        scan += used;
        continue;
      }
      cp = GUNI_REPLACEMENT_CHARACTER;
    }
    if (count != 0 && is_boundary(form, cp)) {
      boundary_offset = scan;
      boundary_count = count;
    }
    ++count;
    scan += used;
  }
  if (scan < len) {
    if (boundary_count == 0) {
      *truncated_out = true;
      return start;
    }
    *count_out = boundary_count;
    return boundary_offset;
  }
  *count_out = count;
  return scan;
}

/** Decode exactly @p count codepoints of @p text into @p out. */
static void decode_chunk(const char * text, size_t len, GUNI_Invalid policy,
    uint32_t * out, size_t count) {
  size_t offset = 0;
  size_t written = 0;
  while (offset < len && written < count) {
    uint32_t cp = 0;
    bool valid = false;
    size_t used = guni_utf8_decode(text + offset, len - offset, &cp, &valid);
    offset += used;
    if (!valid) {
      if (policy == GUNI_INVALID_SKIP) {
        continue;
      }
      cp = GUNI_REPLACEMENT_CHARACTER;
    }
    out[written++] = cp;
  }
}

/**
 * Normalise one chunk of codepoints: decompose, order, and compose for the
 * composing forms. The one place the order of those three is written down.
 */
static GUNI_Result normalize_chunk(GUNI_NormForm form, const uint32_t * text,
    size_t count, uint32_t * work, size_t work_cap, size_t * produced_out) {
  bool overflow = false;
  size_t produced = decompose_all(form, text, count, work, work_cap, &overflow);
  if (overflow) {
    return GUNI_ERR_LIMIT;
  }
  if (form == GUNI_NFC || form == GUNI_NFKC) {
    produced = compose_in_place(work, produced);
  }
  *produced_out = produced;
  return GUNI_OK;
}

GUNI_Result guni_normalize_utf8(GUNI_NormForm form, const char * text,
    size_t len, GUNI_Invalid policy, const GUNI_Limits * limits, char * out,
    size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)
      || (text == NULL && len != 0)
      || (unsigned)form >= GUNI_NORM_FORM_COUNT) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  GUNI_Limits storage;
  const GUNI_Limits * active = effective_limits(limits, &storage);
  if (len > active->max_text_bytes) {
    return GUNI_ERR_LIMIT;
  }

  uint32_t chunk[GUNI_NORM_CHUNK / 2];
  uint32_t work[GUNI_NORM_CHUNK];
  size_t offset = 0;
  size_t written = 0;
  bool overflow = false;

  while (offset < len) {
    size_t count = 0;
    bool truncated = false;
    bool invalid = false;
    size_t next = next_chunk_utf8(form, text, len, offset, GUNI_NORM_CHUNK / 2,
        policy, &count, &truncated, &invalid);
    if (invalid) {
      return GUNI_ERR_INVALID;
    }
    if (truncated) {
      /* A run of more than 512 codepoints with no normalisation boundary in
       * it. Natural text does not do this - a boundary falls at nearly every
       * character - and text that does is what guni_stream_safe() is for.
       * Refusing beats truncating, and beats growing a buffer whose size an
       * attacker chose. */
      return GUNI_ERR_LIMIT;
    }
    if (next <= offset && count == 0) {
      break; /* every remaining byte was skipped */
    }
    decode_chunk(text + offset, next - offset, policy, chunk, count);
    size_t produced = 0;
    GUNI_Result result = normalize_chunk(form, chunk, count, work,
        GUNI_NORM_CHUNK, &produced);
    if (result != GUNI_OK) {
      return result;
    }
    for (size_t index = 0; index < produced; ++index) {
      char bytes[GUNI_UTF8_MAX_LENGTH];
      size_t used = guni_utf8_encode(work[index], bytes);
      if (used == 0) {
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

GUNI_Result guni_is_normalized(GUNI_NormForm form, const uint32_t * text,
    size_t len, const GUNI_Limits * limits, bool * out) {
  if (out == NULL || (text == NULL && len != 0)
      || (unsigned)form >= GUNI_NORM_FORM_COUNT) {
    return GUNI_ERR_INVALID;
  }
  *out = false;
  GUNI_QuickCheck quick = guni_quick_check_text(form, text, len);
  if (quick == GUNI_QC_NO) {
    return GUNI_OK;
  }
  if (quick == GUNI_QC_YES) {
    *out = true;
    return GUNI_OK;
  }
  /* MAYBE: normalise a chunk at a time and compare. Same chunking as the
   * UTF-8 path, and the same refusal for a run with no boundary in it. */
  GUNI_Limits storage;
  const GUNI_Limits * active = effective_limits(limits, &storage);
  if (len > active->max_text_bytes / 4) {
    return GUNI_ERR_LIMIT;
  }
  uint32_t work[GUNI_NORM_CHUNK];
  size_t index = 0;
  while (index < len) {
    size_t take = 0;
    size_t last_boundary = 0;
    while (index + take < len && take < GUNI_NORM_CHUNK / 2) {
      if (take != 0 && is_boundary(form, text[index + take])) {
        last_boundary = take;
      }
      ++take;
    }
    if (index + take < len) {
      if (last_boundary == 0) {
        return GUNI_ERR_LIMIT;
      }
      take = last_boundary;
    }
    bool overflow = false;
    size_t produced = decompose_all(form, text + index, take, work,
        GUNI_NORM_CHUNK, &overflow);
    if (overflow) {
      /* Unreachable at today's window sizes, and not dead code: the chunk is
       * at most GUNI_NORM_CHUNK/2 codepoints and the text that gets this far
       * has no character whose quick-check property is No, which bounds the
       * expansion at two - so the output cannot exceed GUNI_NORM_CHUNK. The
       * two constants are independent, and a future one that made this
       * reachable would find the check rather than the overflow. */
      return GUNI_ERR_LIMIT;
    }
    if (form == GUNI_NFC || form == GUNI_NFKC) {
      produced = compose_in_place(work, produced);
    }
    if (produced != take
        || memcmp(work, text + index, take * sizeof(uint32_t)) != 0) {
      return GUNI_OK; /* *out is already false */
    }
    index += take;
  }
  *out = true;
  return GUNI_OK;
}

GUNI_Result guni_is_normalized_utf8(GUNI_NormForm form, const char * text,
    size_t len, const GUNI_Limits * limits, bool * out) {
  if (out == NULL || (text == NULL && len != 0)
      || (unsigned)form >= GUNI_NORM_FORM_COUNT) {
    return GUNI_ERR_INVALID;
  }
  *out = false;
  GUNI_QuickCheck quick = guni_quick_check_utf8(form, text, len);
  if (quick == GUNI_QC_NO) {
    return GUNI_OK;
  }
  if (quick == GUNI_QC_YES) {
    *out = true;
    return GUNI_OK;
  }
  GUNI_Limits storage;
  const GUNI_Limits * active = effective_limits(limits, &storage);
  if (len > active->max_text_bytes) {
    return GUNI_ERR_LIMIT;
  }
  /* MAYBE: normalise a chunk at a time and compare. The comparison is of
   * codepoints rather than of bytes because the chunking is already in
   * codepoints and UTF-8 is a bijection, so the two questions are one. */
  uint32_t chunk[GUNI_NORM_CHUNK / 2];
  uint32_t work[GUNI_NORM_CHUNK];
  size_t offset = 0;
  while (offset < len) {
    size_t count = 0;
    bool truncated = false;
    bool invalid = false;
    size_t next = next_chunk_utf8(form, text, len, offset, GUNI_NORM_CHUNK / 2,
        GUNI_INVALID_REFUSE, &count, &truncated, &invalid);
    if (invalid) {
      return GUNI_OK; /* ill-formed, so in no normalisation form */
    }
    if (truncated) {
      return GUNI_ERR_LIMIT;
    }
    decode_chunk(text + offset, next - offset, GUNI_INVALID_REFUSE, chunk,
        count);
    size_t produced = 0;
    GUNI_Result result = normalize_chunk(form, chunk, count, work,
        GUNI_NORM_CHUNK, &produced);
    if (result != GUNI_OK) {
      /* As in guni_is_normalized(): unreachable while the window sizes are
       * what they are, and present because they are independent. */
      return result;
    }
    if (produced != count
        || memcmp(work, chunk, count * sizeof(uint32_t)) != 0) {
      return GUNI_OK;
    }
    if (next <= offset) {
      return GUNI_ERR_INTERNAL;
    }
    offset = next;
  }
  *out = true;
  return GUNI_OK;
}

bool guni_is_stream_safe(const uint32_t * text, size_t len,
    const GUNI_Limits * limits) {
  if (text == NULL && len != 0) {
    return false;
  }
  GUNI_Limits storage;
  const GUNI_Limits * active = effective_limits(limits, &storage);
  size_t run = 0;
  for (size_t index = 0; index < len; ++index) {
    if (guni_combining_class(text[index]) == 0) {
      run = 0;
      continue;
    }
    if (++run > active->max_nonstarters) {
      return false;
    }
  }
  return true;
}

GUNI_Result guni_stream_safe(const uint32_t * text, size_t len,
    const GUNI_Limits * limits, uint32_t * out, size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)
      || (text == NULL && len != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  GUNI_Limits storage;
  const GUNI_Limits * active = effective_limits(limits, &storage);
  if (active->max_nonstarters == 0) {
    return GUNI_ERR_INVALID;
  }
  size_t written = 0;
  size_t run = 0;
  bool overflow = false;
  for (size_t index = 0; index < len; ++index) {
    uint32_t cp = text[index];
    if (guni_combining_class(cp) == 0) {
      run = 0;
    }
    else if (++run > active->max_nonstarters) {
      /* The joiner is a starter, so inserting it both ends the run and keeps
       * the text canonically equivalent for the Standard's purposes. */
      if (written < cap) {
        out[written] = GUNI_CGJ;
      }
      else {
        overflow = true;
      }
      ++written;
      run = 1;
    }
    if (written < cap) {
      out[written] = cp;
    }
    else {
      overflow = true;
    }
    ++written;
  }
  *out_len = written;
  return overflow ? GUNI_ERR_LIMIT : GUNI_OK;
}
