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
 * Result strings, default limits, and the version.
 */

#include <ghoti.io/unicode/macros.h>
#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/libver.h>

const char * guni_result_string(GUNI_Result result) {
  switch (result) {
    case GUNI_OK:
      return "No error";
    case GUNI_ERR_IO:
      return "I/O error";
    case GUNI_ERR_FORMAT:
      return "Not a recognised format";
    case GUNI_ERR_UNSUPPORTED:
      return "Unsupported feature";
    case GUNI_ERR_LIMIT:
      return "Limit exceeded";
    case GUNI_ERR_CORRUPT:
      return "Corrupt data";
    case GUNI_ERR_OOM:
      return "Out of memory";
    case GUNI_ERR_INVALID:
      return "Invalid argument";
    case GUNI_ERR_INTERNAL:
      return "Internal error";
    case GUNI_RESULT_COUNT:
    default:
      return "Unknown error";
  }
}

void guni_limits_default(GUNI_Limits * limits) {
  if (!limits) {
    return;
  }

  *limits = (GUNI_Limits) {
    .max_text_bytes = (size_t)64 * 1024 * 1024,
    .max_bidi_depth = 125,
    .max_nonstarters = 30,
  };
}
