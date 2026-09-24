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
 * The four normalisation forms on text nobody chose, checked against the
 * properties UAX #15 guarantees rather than against expected output.
 *
 * The first byte selects the form, per CONVENTIONS.md section 7, so one corpus
 * exercises all four. What is asserted:
 *
 *   * **idempotence**: normalising the output again changes nothing. This is
 *     the property that catches a composer that leaves a composable pair, and
 *     it needs no expected answer;
 *   * **the output is reported as normalised** by guni_is_normalized(), which
 *     ties the two halves of the module together: an is_normalized() that
 *     disagreed with normalize() would let a caller loop forever;
 *   * **the bounds hold**: the output never exceeds the generated expansion
 *     constant, which is what every caller sizes a buffer from;
 *   * **the composed form is no longer than the decomposed one**, because
 *     composition never grows a sequence - the assumption the buffer contract
 *     in norm.h rests on;
 *   * **the UTF-8 and codepoint entry points agree**, since only one of them
 *     chunks and the chunking is the part with no conformance file;
 *   * **canonical order is a fixed point** of itself.
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <ghoti.io/unicode/unicode.h>

namespace {

std::vector<uint32_t> normalize(GUNI_NormForm form,
    const std::vector<uint32_t> & input) {
  std::vector<uint32_t> out(input.size() * GUNI_NORM_MAX_EXPANSION_NFKD + 1);
  size_t written = 0;
  if (guni_normalize(form, input.data(), input.size(), nullptr, out.data(),
          out.size(), &written)
      != GUNI_OK) {
    std::abort(); /* a buffer sized from the constant must always be enough */
  }
  if (written > input.size() * GUNI_NORM_MAX_EXPANSION_NFKD) {
    std::abort();
  }
  out.resize(written);
  return out;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }
  const GUNI_NormForm form = static_cast<GUNI_NormForm>(data[0] % GUNI_NORM_FORM_COUNT);
  const char * text = reinterpret_cast<const char *>(data + 1);
  const size_t length = size - 1;

  /* The input as codepoints, through the decoder's replacing policy so that
   * any bytes at all become text. */
  std::vector<uint32_t> input;
  size_t count = 0;
  if (guni_utf8_count(text, length, GUNI_INVALID_REPLACE, &count) != GUNI_OK) {
    std::abort();
  }
  input.resize(count);
  size_t written = 0;
  if (guni_utf8_to_codepoints(text, length, GUNI_INVALID_REPLACE, input.data(),
          input.size(), &written)
      != GUNI_OK
      || written != count) {
    std::abort();
  }

  std::vector<uint32_t> once = normalize(form, input);
  std::vector<uint32_t> twice = normalize(form, once);
  if (once != twice) {
    std::abort(); /* not idempotent */
  }

  bool normalised = false;
  if (guni_is_normalized(form, once.data(), once.size(), nullptr, &normalised)
      != GUNI_OK) {
    std::abort();
  }
  if (!normalised) {
    std::abort(); /* normalize() and is_normalized() disagree */
  }

  /* Composition never grows a sequence. */
  const bool compatibility = (form == GUNI_NFKC || form == GUNI_NFKD);
  const GUNI_NormForm decomposing = compatibility ? GUNI_NFKD : GUNI_NFD;
  const GUNI_NormForm composing = compatibility ? GUNI_NFKC : GUNI_NFC;
  if (normalize(composing, input).size() > normalize(decomposing, input).size()) {
    std::abort();
  }

  /* Canonical order is its own fixed point. */
  std::vector<uint32_t> ordered = once;
  if (!ordered.empty()) {
    guni_canonical_order(ordered.data(), ordered.size());
    std::vector<uint32_t> again = ordered;
    guni_canonical_order(again.data(), again.size());
    if (ordered != again) {
      std::abort();
    }
  }

  /* The UTF-8 entry point, against the codepoint one. Only the UTF-8 path
   * chunks at normalisation boundaries, and no conformance file covers the
   * chunking. */
  std::vector<char> encoded(input.size() * GUNI_UTF8_MAX_LENGTH + 1);
  size_t encoded_length = 0;
  if (guni_utf8_from_codepoints(input.data(), input.size(), GUNI_INVALID_REFUSE,
          encoded.data(), encoded.size(), &encoded_length)
      != GUNI_OK) {
    std::abort();
  }
  std::vector<char> bytes(
      encoded_length * GUNI_NORM_MAX_EXPANSION_NFKD + 4);
  size_t bytes_length = 0;
  GUNI_Result result = guni_normalize_utf8(form, encoded.data(), encoded_length,
      GUNI_INVALID_REFUSE, nullptr, bytes.data(), bytes.size(), &bytes_length);
  if (result == GUNI_OK) {
    std::vector<char> expected(once.size() * GUNI_UTF8_MAX_LENGTH + 1);
    size_t expected_length = 0;
    if (guni_utf8_from_codepoints(once.data(), once.size(), GUNI_INVALID_REFUSE,
            expected.data(), expected.size(), &expected_length)
        != GUNI_OK) {
      std::abort();
    }
    if (bytes_length != expected_length
        || std::memcmp(bytes.data(), expected.data(), bytes_length) != 0) {
      std::abort(); /* the two entry points disagree */
    }
  }
  else if (result != GUNI_ERR_LIMIT) {
    std::abort(); /* the only other answer is a run with no boundary in it */
  }

  /* The stream-safe transform makes text stream-safe, and leaves text that
   * already is alone. */
  std::vector<uint32_t> safe(input.size() * 2 + 1);
  size_t safe_length = 0;
  if (guni_stream_safe(input.data(), input.size(), nullptr, safe.data(),
          safe.size(), &safe_length)
      != GUNI_OK) {
    std::abort();
  }
  safe.resize(safe_length);
  if (!guni_is_stream_safe(safe.data(), safe.size(), nullptr)) {
    std::abort();
  }
  if (guni_is_stream_safe(input.data(), input.size(), nullptr)
      && safe != input) {
    std::abort(); /* changed text that did not need changing */
  }
  return 0;
}
