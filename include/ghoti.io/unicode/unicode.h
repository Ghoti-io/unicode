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
 * Umbrella header for the Ghoti.io Unicode library: every tier-0 header.
 * A consumer that wants a higher tier includes that tier's header itself,
 * which is what keeps the tier boundary visible at the include line. See
 * documentation/design.md.
 */

#ifndef GHOTI_IO_GUNI_UNICODE_H
#define GHOTI_IO_GUNI_UNICODE_H

#include <ghoti.io/unicode/allocator.h>
#include <ghoti.io/unicode/bidi.h>
#include <ghoti.io/unicode/break.h>
#include <ghoti.io/unicode/case.h>
#include <ghoti.io/unicode/char.h>
#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/enums.h>
#include <ghoti.io/unicode/norm.h>
#include <ghoti.io/unicode/script.h>
#include <ghoti.io/unicode/set.h>
#include <ghoti.io/unicode/utf.h>
#include <ghoti.io/unicode/macros.h>

#endif // GHOTI_IO_GUNI_UNICODE_H
