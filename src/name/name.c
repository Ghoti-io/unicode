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
 * UAX #44 character names, tier 1.
 *
 * Two things worth knowing before reading. **Names are decoded, not stored**:
 * a name is a run of word numbers with a separator bit, so every function here
 * that produces a name assembles it into a buffer. And **the loose matching is
 * one function**, mirrored exactly in the generator, because the reverse index
 * is sorted by the loose form and a query normalised differently would binary
 * search past its answer.
 */

#include <ghoti.io/unicode/char.h>
#include <ghoti.io/unicode/name.h>
#include <string.h>
#include "tables/name_tables.h"

/* Standard section 3.12's Hangul constants, again: name.c computes syllable
 * names and norm.c computes their decompositions, and neither is a table. */
#define HANGUL_SBASE UINT32_C(0xAC00)
#define HANGUL_LCOUNT 19u
#define HANGUL_VCOUNT 21u
#define HANGUL_TCOUNT 28u
#define HANGUL_NCOUNT (HANGUL_VCOUNT * HANGUL_TCOUNT)

/**
 * UAX #44-LM2's loose matching, into a caller's buffer.
 *
 * Ignore case, whitespace, underscores and **medial** hyphens, where medial
 * means a hyphen with a real character either side. The generator's
 * loose_name() is the same rule, and it has to be: the reverse index is sorted
 * by this form.
 *
 * @return The normalised length, or (size_t)-1 when the name cannot match -
 *         too long, or empty.
 */
#define GUNI_LOOSE_MAX (GUNI_NAME_MAX_LENGTH + 1)

static size_t loose(const char * name, size_t len, char * out) {
  if (name == NULL || len > GUNI_NAME_MAX_LENGTH) {
    return (size_t)-1;
  }
  /* Two passes, against two different strings, and the generator's
   * loose_name() does the same thing for the same reasons:
   *
   *   * "medial" is judged on the *original* name, where the spaces are still
   *     there. A hyphen with a space or an underscore beside it is not medial;
   *     U+11C88 is MARCHEN LETTER -A and U+11C8F is MARCHEN LETTER A, and
   *     judging medial-ness after the spaces are gone makes them one name.
   *   * the exception is judged on the *folded* form, because a caller may hand
   *     over a name that is already folded. U+116C is HANGUL JUNGSEONG OE and
   *     U+1180 is HANGUL JUNGSEONG O-E, and the Standard keeps that hyphen.
   */
  char folded[GUNI_LOOSE_MAX];
  bool medial[GUNI_LOOSE_MAX];
  size_t count = 0;
  for (size_t index = 0; index < len; ++index) {
    char character = name[index];
    if (character == ' ' || character == '_') {
      continue;
    }
    if (count + 1 >= GUNI_LOOSE_MAX) {
      /* Unreachable: the length was bounded above and folding only shrinks.
       * Here so that the two bounds are not one assumption. */
      return (size_t)-1;
    }
    char before = (index > 0) ? name[index - 1] : '\0';
    char after = (index + 1 < len) ? name[index + 1] : '\0';
    bool separator_before = before == '\0' || before == ' ' || before == '_'
        || before == '-';
    bool separator_after = after == '\0' || after == ' ' || after == '_'
        || after == '-';
    medial[count] = character == '-' && !separator_before && !separator_after;
    if (character >= 'A' && character <= 'Z') {
      character = (char)(character - 'A' + 'a');
    }
    folded[count++] = character;
  }
  folded[count] = '\0';

  static const char exception[] = "hanguljungseongo-e";
  if (count == sizeof(exception) - 1
      && memcmp(folded, exception, count) == 0) {
    memcpy(out, folded, count + 1);
    return count;
  }

  size_t written = 0;
  for (size_t index = 0; index < count; ++index) {
    if (!medial[index]) {
      out[written++] = folded[index];
    }
  }
  out[written] = '\0';
  return (written == 0) ? (size_t)-1 : written;
}

/**
 * The loose form of a *fragment* - an algorithmic name's prefix, or a jamo
 * short name - which is not a name and has no medial hyphens to preserve.
 *
 * "CJK UNIFIED IDEOGRAPH-" ends in a hyphen, which is not medial in the
 * fragment and is medial in the name the fragment starts, so loose() gives the
 * two different answers and the prefix never matches. That cost the whole CJK
 * reverse lookup.
 */
static size_t loose_fragment(const char * text, char * out) {
  size_t written = 0;
  for (size_t index = 0; text[index] != '\0'; ++index) {
    char character = text[index];
    if (character == ' ' || character == '_' || character == '-') {
      continue;
    }
    if (character >= 'A' && character <= 'Z') {
      character = (char)(character - 'A' + 'a');
    }
    if (written + 1 >= GUNI_LOOSE_MAX) {
      /* Unreachable: every fragment is a prefix or a jamo short name, and the
       * longest is twenty-two characters. */
      return (size_t)-1;
    }
    out[written++] = character;
  }
  out[written] = '\0';
  return written;
}

/** Decode a token run into @p out, reporting the length it needs. */
static GUNI_Result decode_tokens(const uint16_t * tokens, size_t first,
    size_t last, char * out, size_t cap, size_t * out_len) {
  size_t needed = 0;
  for (size_t index = first; index < last; ++index) {
    if (index != first) {
      ++needed; /* the separator */
    }
    needed += strlen(guni_name_words[tokens[index] & GUNI_NAME_WORD_MASK]);
  }
  *out_len = needed;
  if (needed + 1 > cap) {
    return GUNI_ERR_LIMIT;
  }
  size_t written = 0;
  for (size_t index = first; index < last; ++index) {
    if (index != first) {
      out[written++] = (tokens[index] & GUNI_NAME_HYPHEN) ? '-' : ' ';
    }
    const char * word = guni_name_words[tokens[index] & GUNI_NAME_WORD_MASK];
    size_t length = strlen(word);
    memcpy(out + written, word, length);
    written += length;
  }
  out[written] = '\0';
  return GUNI_OK;
}

/** Decode entry @p index of the name table. */
static GUNI_Result decode_name(size_t index, char * out, size_t cap,
    size_t * out_len) {
  return decode_tokens(guni_name_tokens, guni_name_offsets[index],
      guni_name_offsets[index + 1], out, cap, out_len);
}

/** The algorithmic range containing @p cp, or GUNI_NAME_RANGE_COUNT. */
static size_t range_of(uint32_t cp) {
  for (size_t index = 0; index < GUNI_NAME_RANGE_COUNT; ++index) {
    if (cp >= guni_name_range_first[index] && cp <= guni_name_range_last[index]) {
      return index;
    }
  }
  return GUNI_NAME_RANGE_COUNT;
}

/** The name of a Hangul syllable, spelled from its jamo. */
static GUNI_Result hangul_name(uint32_t cp, const char * prefix, char * out,
    size_t cap, size_t * out_len) {
  uint32_t index = cp - HANGUL_SBASE;
  const char * lead = guni_jamo_lead[index / HANGUL_NCOUNT];
  const char * vowel = guni_jamo_vowel[(index % HANGUL_NCOUNT) / HANGUL_TCOUNT];
  const char * trail = guni_jamo_trail[index % HANGUL_TCOUNT];
  size_t needed = strlen(prefix) + strlen(lead) + strlen(vowel) + strlen(trail);
  *out_len = needed;
  if (needed + 1 > cap) {
    return GUNI_ERR_LIMIT;
  }
  size_t written = 0;
  const char * parts[4] = {prefix, lead, vowel, trail};
  for (size_t index = 0; index < 4; ++index) {
    size_t length = strlen(parts[index]);
    memcpy(out + written, parts[index], length);
    written += length;
  }
  out[written] = '\0';
  return GUNI_OK;
}

GUNI_Result guni_name(uint32_t cp, char * out, size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  size_t range = range_of(cp);
  if (range != GUNI_NAME_RANGE_COUNT) {
    if (guni_name_range_hangul[range]) {
      return hangul_name(cp, guni_name_range_prefix[range], out, cap, out_len);
    }
    /* The prefix and the codepoint in hex, at least four digits. */
    char digits[8];
    size_t count = 0;
    uint32_t value = cp;
    do {
      static const char hex[] = "0123456789ABCDEF";
      digits[count++] = hex[value & 0xFu];
      value >>= 4;
    } while (value != 0);
    while (count < 4) {
      /* Unreachable with today's data: the lowest algorithmic range starts at
       * U+3400, so every codepoint in one has at least four hex digits. The
       * Standard writes these names with a minimum of four, and a future range
       * below U+1000 would need the padding rather than a new bug. */
      digits[count++] = '0';
    }
    const char * prefix = guni_name_range_prefix[range];
    size_t prefix_length = strlen(prefix);
    *out_len = prefix_length + count;
    if (*out_len + 1 > cap) {
      return GUNI_ERR_LIMIT;
    }
    memcpy(out, prefix, prefix_length);
    for (size_t index = 0; index < count; ++index) {
      out[prefix_length + index] = digits[count - 1 - index];
    }
    out[prefix_length + count] = '\0';
    return GUNI_OK;
  }

  size_t low = 0;
  size_t high = GUNI_NAME_PRIMARY_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (cp < guni_name_primary_cp[middle]) {
      high = middle;
    }
    else if (cp > guni_name_primary_cp[middle]) {
      low = middle + 1;
    }
    else {
      return decode_name(guni_name_primary_index[middle], out, cap, out_len);
    }
  }
  /* No name of its own. A control character's name is an alias, which is a
   * different question and a different function. */
  return GUNI_ERR_INVALID;
}

bool guni_has_name(uint32_t cp) {
  size_t length = 0;
  return guni_name(cp, NULL, 0, &length) != GUNI_ERR_INVALID;
}

/** The first alias of @p cp, or GUNI_NAME_ALIAS_COUNT. */
static size_t first_alias(uint32_t cp) {
  size_t low = 0;
  size_t high = GUNI_NAME_ALIAS_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (guni_name_alias_cp[middle] < cp) {
      low = middle + 1;
    }
    else {
      high = middle;
    }
  }
  return low;
}

size_t guni_name_alias_count(uint32_t cp) {
  size_t index = first_alias(cp);
  size_t count = 0;
  while (index + count < GUNI_NAME_ALIAS_COUNT
      && guni_name_alias_cp[index + count] == cp) {
    ++count;
  }
  return count;
}

GUNI_Result guni_name_alias(uint32_t cp, size_t index,
    GUNI_NameAliasKind * kind_out, char * out, size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  size_t first = first_alias(cp);
  if (first + index >= GUNI_NAME_ALIAS_COUNT
      || guni_name_alias_cp[first + index] != cp) {
    return GUNI_ERR_INVALID;
  }
  if (kind_out != NULL) {
    *kind_out = (GUNI_NameAliasKind)guni_name_alias_kind[first + index];
  }
  return decode_name(guni_name_alias_index[first + index], out, cap, out_len);
}

/**
 * The entry whose loose name is @p key, or GUNI_NAME_COUNT.
 *
 * A binary search over the loose-name order, decoding each candidate. Decoding
 * per comparison costs sixteen decodes for a 40,951-entry table, which is
 * nothing for a lookup that happens once per pattern - and it is what keeps the
 * loose forms from being a second copy of every name.
 */
static size_t find_loose(const char * key) {
  size_t low = 0;
  size_t high = GUNI_NAME_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    size_t entry = guni_name_loose_order[middle];
    char name[GUNI_NAME_MAX_LENGTH + 1];
    char folded[GUNI_LOOSE_MAX];
    size_t length = 0;
    if (decode_name(entry, name, sizeof(name), &length) != GUNI_OK
        || loose(name, length, folded) == (size_t)-1) {
      /* Unreachable: every name fits GUNI_NAME_MAX_LENGTH by construction and
       * none of them normalises away to nothing. */
      return GUNI_NAME_COUNT;
    }
    int order = strcmp(folded, key);
    if (order < 0) {
      low = middle + 1;
    }
    else if (order > 0) {
      high = middle;
    }
    else {
      return entry;
    }
  }
  return GUNI_NAME_COUNT;
}

/** Parse an algorithmic name: a prefix and hex, or the Hangul spelling. */
static bool parse_algorithmic(const char * key, size_t len, uint32_t * out) {
  for (size_t range = 0; range < GUNI_NAME_RANGE_COUNT; ++range) {
    char prefix[GUNI_LOOSE_MAX];
    size_t prefix_length = loose_fragment(guni_name_range_prefix[range], prefix);
    if (prefix_length == (size_t)-1 || len <= prefix_length
        || strncmp(key, prefix, prefix_length) != 0) {
      continue;
    }
    const char * tail = key + prefix_length;
    size_t tail_length = len - prefix_length;
    if (guni_name_range_hangul[range]) {
      /* The jamo spelling, longest lead and vowel first so that a trailing
       * consonant is not read as part of the vowel. */
      for (size_t lead = 0; lead < HANGUL_LCOUNT; ++lead) {
        char lead_key[GUNI_LOOSE_MAX];
        size_t lead_length = loose_fragment(guni_jamo_lead[lead], lead_key);
        if (lead_length == (size_t)-1 || lead_length > tail_length
            || strncmp(tail, lead_key, lead_length) != 0) {
          continue;
        }
        for (size_t vowel = 0; vowel < HANGUL_VCOUNT; ++vowel) {
          char vowel_key[GUNI_LOOSE_MAX];
          size_t vowel_length = loose_fragment(guni_jamo_vowel[vowel], vowel_key);
          if (vowel_length == (size_t)-1
              || lead_length + vowel_length > tail_length
              || strncmp(tail + lead_length, vowel_key, vowel_length) != 0) {
            continue;
          }
          size_t used = lead_length + vowel_length;
          for (size_t trail = 0; trail < HANGUL_TCOUNT; ++trail) {
            size_t trail_length = 0;
            if (trail != 0) {
              char trail_key[GUNI_LOOSE_MAX];
              trail_length = loose_fragment(guni_jamo_trail[trail], trail_key);
              if (trail_length == (size_t)-1
                  || used + trail_length != tail_length
                  || strncmp(tail + used, trail_key, trail_length) != 0) {
                continue;
              }
            }
            else if (used != tail_length) {
              continue;
            }
            uint32_t cp = HANGUL_SBASE
                + (uint32_t)((lead * HANGUL_VCOUNT + vowel) * HANGUL_TCOUNT
                    + trail);
            if (cp >= guni_name_range_first[range]
                && cp <= guni_name_range_last[range]) {
              *out = cp;
              return true;
            }
          }
        }
      }
      continue;
    }
    /* Hex, and it has to be all hex: "cjkunifiedideograph4e00zz" is not a
     * name, and a partial parse would resolve it. */
    uint32_t value = 0;
    for (size_t index = 0; index < tail_length; ++index) {
      char digit = tail[index];
      uint32_t nibble;
      if (digit >= '0' && digit <= '9') {
        nibble = (uint32_t)(digit - '0');
      }
      else if (digit >= 'a' && digit <= 'f') {
        nibble = (uint32_t)(digit - 'a' + 10);
      }
      else {
        value = 0;
        tail_length = 0;
        break;
      }
      if (value > (GUNI_MAX_CODEPOINT >> 4)) {
        value = 0;
        tail_length = 0;
        break;
      }
      value = (value << 4) | nibble;
    }
    if (tail_length != 0 && value >= guni_name_range_first[range]
        && value <= guni_name_range_last[range]) {
      *out = value;
      return true;
    }
  }
  return false;
}

GUNI_Result guni_codepoint_by_name(const char * name, size_t len,
    uint32_t * out) {
  if (out == NULL) {
    return GUNI_ERR_INVALID;
  }
  char key[GUNI_LOOSE_MAX];
  if (loose(name, len, key) == (size_t)-1) {
    return GUNI_ERR_INVALID;
  }
  size_t entry = find_loose(key);
  if (entry != GUNI_NAME_COUNT) {
    *out = guni_name_codepoints[entry];
    return GUNI_OK;
  }
  if (parse_algorithmic(key, strlen(key), out)) {
    return GUNI_OK;
  }
  return GUNI_ERR_INVALID;
}

GUNI_Result guni_named_sequence(const char * name, size_t len, uint32_t * out,
    size_t cap, size_t * out_len) {
  if (out_len == NULL || (out == NULL && cap != 0)) {
    return GUNI_ERR_INVALID;
  }
  *out_len = 0;
  char key[GUNI_LOOSE_MAX];
  if (loose(name, len, key) == (size_t)-1) {
    return GUNI_ERR_INVALID;
  }
  size_t low = 0;
  size_t high = GUNI_SEQUENCE_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    char text[GUNI_NAME_MAX_LENGTH + 1];
    char folded[GUNI_LOOSE_MAX];
    size_t length = 0;
    if (decode_tokens(guni_sequence_tokens, guni_sequence_offsets[middle],
            guni_sequence_offsets[middle + 1], text, sizeof(text), &length)
            != GUNI_OK
        || loose(text, length, folded) == (size_t)-1) {
      return GUNI_ERR_INVALID;
    }
    int order = strcmp(folded, key);
    if (order < 0) {
      low = middle + 1;
    }
    else if (order > 0) {
      high = middle;
    }
    else {
      size_t first = guni_sequence_pool_offsets[middle];
      size_t count = guni_sequence_pool_offsets[middle + 1] - first;
      *out_len = count;
      if (count > cap) {
        return GUNI_ERR_LIMIT;
      }
      memcpy(out, &guni_sequence_pool[first], count * sizeof(uint32_t));
      return GUNI_OK;
    }
  }
  return GUNI_ERR_INVALID;
}
