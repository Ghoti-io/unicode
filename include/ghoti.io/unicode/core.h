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
 * Core types, result codes, limits and the version for the Ghoti.io Unicode
 * library.
 */

#ifndef GHOTI_IO_GUNI_CORE_H
#define GHOTI_IO_GUNI_CORE_H

#include <ghoti.io/unicode/allocator.h>
#include <ghoti.io/unicode/macros.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result code for unicode library operations.
 *
 * The fixed vocabulary of CONVENTIONS.md section 5. Zero is success and
 * GUNI_RESULT_COUNT closes the enum so that a test can check the string table
 * is complete.
 */
typedef enum {
  GUNI_OK = 0,          ///< Operation succeeded.
  GUNI_ERR_IO,          ///< I/O error (read/write/seek failed).
  GUNI_ERR_FORMAT,      ///< Not a format this library recognises.
  GUNI_ERR_UNSUPPORTED, ///< This format, but a feature not implemented.
  GUNI_ERR_LIMIT,       ///< A GUNI_Limits field was exceeded.
  GUNI_ERR_CORRUPT,     ///< This format, but the bytes are wrong.
  GUNI_ERR_OOM,         ///< The allocator returned NULL.
  GUNI_ERR_INVALID,     ///< A caller-supplied argument is wrong.
  GUNI_ERR_INTERNAL,    ///< The library's own invariant failed.
  GUNI_RESULT_COUNT
} GUNI_Result;

/**
 * @brief Convert a result code to a human-readable string.
 *
 * The returned string is statically allocated and must not be freed.
 *
 * @param result The result code.
 * @return A description of the result code, never NULL.
 */
GUNI_API const char * guni_result_string(GUNI_Result result);

/**
 * @brief Caps applied while walking text, so that a hostile or
 * enormous input cannot make the library allocate or loop without bound.
 * See documentation/design.md section 13.2.
 */
typedef struct GUNI_Limits {
  /**
   * Longest text any one call walks, in bytes. A caller that hands over
   * more gets @ref GUNI_ERR_LIMIT rather than a walk that takes a minute.
   * Default 64 MiB.
   */
  size_t max_text_bytes;
  /**
   * The embedding depth of UAX #9. Fixed at 125 by the Standard and not
   * raisable: a larger value would not be the Unicode Bidirectional
   * Algorithm. The field exists so that the limit is stated in one place and
   * a test can read it.
   */
  size_t max_bidi_depth;
  /**
   * UAX #15 section 13's Stream-Safe Text Format bound: at most this many
   * non-starters in a row. 30, per the Standard.
   */
  size_t max_nonstarters;
} GUNI_Limits;

/**
 * @brief Fill in the default limits.
 *
 * @param limits Structure to populate. NULL is ignored.
 */
GUNI_API void guni_limits_default(GUNI_Limits * limits);

/**
 * @brief This build's version, as the string the Makefile generated.
 *
 * "0.0.0", or "0.0.0-dev" for a build with an overridden BRANCH. The same
 * string is GUNI_VERSION_STRING at compile time; this is the one the linked
 * library reports, which is the one that matters when the two differ.
 *
 * @return A static string, never NULL.
 */
GUNI_API const char * guni_version_string(void);

/**
 * @brief This build's version, packed as GUNI_MAKE_VERSION() packs it.
 *
 * @return `(major << 16) | (minor << 8) | patch`.
 */
GUNI_API unsigned guni_version_number(void);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GUNI_CORE_H
