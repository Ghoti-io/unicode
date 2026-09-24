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
 * ICU's `ubrk_*` behind the differential's batch protocol.
 *
 * **This file links ICU and nothing else**, which is the point of it and is why
 * it lives here rather than in tests/. design.md section 12.4 states the rule
 * and `regex`'s `pcre2_match` is the precedent: an oracle that could reach the
 * implementation it answers for is not an oracle. It is compiled inside the
 * image in containers/icu/, against that image's ICU, so the only Unicode
 * tables in the process are ICU's.
 *
 * Protocol, identical to testSegment's GUNI_BREAK_DUMP=stdin:
 *
 *     request   <kind>\t<hex of the UTF-8 bytes>[\t<tailoring>]
 *     answer    <kind>\t<hex>\t<space-separated byte offsets>
 *
 * The answer echoes the request because that is the framing: one answer per
 * request would let a stray line shift every answer after it and be absorbed,
 * and the engine on this machine writes a banner on every invocation.
 *
 * Two conversions this side has to do, and they are the whole of why a driver
 * is needed rather than a one-line ICU call:
 *
 *  1. **UTF-16 to UTF-8 offsets.** ICU counts UTF-16 code units and this
 *     library counts UTF-8 bytes (design.md section 4.1). A prefix table is
 *     built once per case rather than re-encoding per boundary.
 *
 *  2. **Position 0.** UAX #29 makes the start of text a boundary (GB1, WB1,
 *     SB1) and UAX #14 does not (LB2: sot x). This library reports it
 *     accordingly - included for the three UAX #29 kinds, excluded for line -
 *     while `ubrk_first()` returns 0 for every type, because it is the
 *     iterator's starting position rather than a claim about the text. So 0 is
 *     dropped **for line breaks only**. That is a normalisation and therefore a
 *     place a real difference could be hidden, so it is exactly one offset in
 *     exactly one kind, and nothing else about either side's answer is touched.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

namespace {

/** The ICU break type for a protocol kind name, or a count for "no such". */
UBreakIteratorType type_for(const std::string & name, bool * ok) {
  *ok = true;
  if (name == "grapheme") {
    return UBRK_CHARACTER;
  }
  if (name == "word") {
    return UBRK_WORD;
  }
  if (name == "sentence") {
    return UBRK_SENTENCE;
  }
  if (name == "line") {
    return UBRK_LINE;
  }
  *ok = false;
  return UBRK_CHARACTER;
}

/**
 * The locale to open the iterator in.
 *
 * The **root** locale, so that no language's tailoring is in play and the
 * comparison is against the algorithm rather than against a locale's
 * preferences - which is what this library implements, with tailorings arriving
 * through a provider instead (design.md section 9).
 *
 * For line breaking the LB1 resolution of `CJ` is a locale keyword in ICU, and
 * the three values are the same three CSS names this library uses. Which of
 * them ICU's *unqualified* root locale corresponds to is a measured question
 * and not one to assume: the differential asks all three.
 */
std::string locale_for(UBreakIteratorType type, const std::string & tailoring) {
  if (type != UBRK_LINE || tailoring == "default") {
    return std::string("");
  }
  return std::string("@lb=") + tailoring;
}

bool decode_hex(const std::string & hex, std::string * out) {
  if (hex.size() % 2 != 0) {
    return false;
  }
  out->clear();
  for (size_t at = 0; at + 1 < hex.size(); at += 2) {
    const std::string byte = hex.substr(at, 2);
    char * end = nullptr;
    const unsigned long value = std::strtoul(byte.c_str(), &end, 16);
    if (end == nullptr || *end != '\0') {
      return false;
    }
    out->push_back(static_cast<char>(value));
  }
  return true;
}

/**
 * UTF-16 index to UTF-8 byte offset, for every index in the string.
 *
 * Built by re-encoding each code point rather than by trusting a formula, so
 * that a surrogate pair contributes one four-byte character and both of its
 * UTF-16 indices map somewhere defined.
 */
std::vector<size_t> utf8_offsets(const UChar * utf16, int32_t length) {
  std::vector<size_t> out(static_cast<size_t>(length) + 1, 0);
  size_t bytes = 0;
  int32_t at = 0;
  while (at < length) {
    UChar32 cp = 0;
    const int32_t before = at;
    U16_NEXT(utf16, at, length, cp);
    size_t width = 1;
    if (cp >= 0x10000) {
      width = 4;
    }
    else if (cp >= 0x800) {
      width = 3;
    }
    else if (cp >= 0x80) {
      width = 2;
    }
    for (int32_t index = before; index < at; ++index) {
      /* Both halves of a surrogate pair map to the character's start, which is
       * the only defined answer: ICU never reports a boundary between them. */
      out[static_cast<size_t>(index)] = bytes;
    }
    bytes += width;
  }
  out[static_cast<size_t>(length)] = bytes;
  return out;
}

int answer(const std::string & kind, const std::string & hex,
    const std::string & tailoring) {
  bool known = false;
  const UBreakIteratorType type = type_for(kind, &known);
  if (!known) {
    std::fprintf(stderr, "icu_break: %s is not a kind\n", kind.c_str());
    return 2;
  }
  std::string utf8;
  if (!decode_hex(hex, &utf8)) {
    std::fprintf(stderr, "icu_break: %s is not hex\n", hex.c_str());
    return 2;
  }

  UErrorCode status = U_ZERO_ERROR;
  std::vector<UChar> utf16(utf8.size() * 2 + 2);
  int32_t utf16_length = 0;
  u_strFromUTF8(utf16.data(), static_cast<int32_t>(utf16.size()),
      &utf16_length, utf8.data(), static_cast<int32_t>(utf8.size()), &status);
  if (U_FAILURE(status)) {
    /* Invalid UTF-8 is a thing the parent must be able to see rather than a
     * silently empty answer. It should never arrive - the generator emits only
     * valid text - so if it does, that is itself the finding. */
    std::printf("%s\t%s\tnot-utf8 %s\n", kind.c_str(), hex.c_str(),
        u_errorName(status));
    return 0;
  }

  const std::string locale = locale_for(type, tailoring);
  status = U_ZERO_ERROR;
  UBreakIterator * iter = ubrk_open(type, locale.c_str(), utf16.data(),
      utf16_length, &status);
  if (U_FAILURE(status) || iter == nullptr) {
    std::printf("%s\t%s\trefused %s\n", kind.c_str(), hex.c_str(),
        u_errorName(status));
    return 0;
  }

  const std::vector<size_t> offsets = utf8_offsets(utf16.data(), utf16_length);
  std::string out;
  for (int32_t at = ubrk_first(iter); at != UBRK_DONE;
       at = ubrk_next(iter)) {
    if (type == UBRK_LINE && at == 0) {
      continue; /* LB2: the start of text is not a break opportunity. */
    }
    if (!out.empty()) {
      out.push_back(' ');
    }
    char number[32];
    std::snprintf(number, sizeof(number), "%zu",
        offsets[static_cast<size_t>(at)]);
    out += number;
  }
  ubrk_close(iter);
  std::printf("%s\t%s\t%s\n", kind.c_str(), hex.c_str(), out.c_str());
  return 0;
}

} // namespace

int main(int argc, char ** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("ICU %s, Unicode %s\n", U_ICU_VERSION, U_UNICODE_VERSION);
    return 0;
  }
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      continue;
    }
    const size_t first = line.find('\t');
    if (first == std::string::npos) {
      std::fprintf(stderr, "icu_break: no tab in %s\n", line.c_str());
      return 2;
    }
    const size_t second = line.find('\t', first + 1);
    const std::string kind = line.substr(0, first);
    const std::string hex = (second == std::string::npos)
        ? line.substr(first + 1)
        : line.substr(first + 1, second - first - 1);
    const std::string tailoring = (second == std::string::npos)
        ? std::string("default")
        : line.substr(second + 1);
    const int result = answer(kind, hex, tailoring);
    if (result != 0) {
      return result;
    }
  }
  std::fflush(stdout);
  return 0;
}
