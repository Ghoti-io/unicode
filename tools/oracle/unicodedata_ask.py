#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-only
#
# Copyright (C) 2026 Corey Pennycuff
#
# This file is part of Ghoti.io Unicode.
#
# Ghoti.io Unicode is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License version 3 as
# published by the Free Software Foundation.
"""CPython's `unicodedata` behind a batch protocol, so that it can be pinned.

`unicodedata_diff.py` used to `import unicodedata` in its own process. That is
the one shape of oracle that cannot be pinned at all - "the reference" is
whichever interpreter happened to run the tool - and this library was paying for
it in a triage list of disagreements that were all the reference being two
Unicode releases behind the tables it was checking.

The fix is a process boundary, not a faster loop. `notes/suite/CONTAINERS.md`
measured the same move in `regex` and found the subprocess *faster* than
in-process on the host (0.97s against 1.16s over 120,000 cases) and 0.7s slower
containerised: what makes a differential slow is a process per *case*, and this
is a process per *run*.

Protocol. One request per line on stdin, each the name of a property. For each,
one framed answer on stdout:

    <property><TAB><lines>
    <first>[..<last>]<TAB><value>          x <lines>

The name is echoed in the frame header and the count is exact, so a parent that
loses sync - which is a live risk, because the engine on this machine prints a
banner and a driver that merged the streams would read it as data - finds out
rather than absorbing it. Runs are maximal and in ascending order, which is the
same shape `GUNI_SWEEP_DUMP` writes, so both sides of the comparison speak one
format.

`-` as a value means **the reference declined to give one**, and what that means
is the property's business, not this file's:

  * `Bidi_Class` - CPython answers `""` for a codepoint its tables do not cover,
    which is not a disagreement about the value. The parent counts it as not
    comparable.
  * `Numeric_Value` and `Name` - `numeric()` and `name()` raise for a codepoint
    that has neither, and "this character has no numeric value" is a real answer
    that can agree or disagree with ours.

One sentinel with two readings is deliberate: the alternative is this file
deciding what a missing answer implies, which is the judgement the parent makes
and documents.

What is *not* here: any decision about what is comparable. The Hangul
decomposition exclusion and the DerivedAge filter are the parent's, because they
are judgements about the comparison rather than answers from the reference. This
file only reports what CPython says.
"""

import sys
import unicodedata
from fractions import Fraction

MAX_CODEPOINT = 0x10FFFF


def general_category(ch):
    return unicodedata.category(ch)


def bidi_class(ch):
    # "" is CPython saying it has no data for this codepoint, which the parent
    # must not score as a disagreement about the value.
    return unicodedata.bidirectional(ch) or None


def east_asian_width(ch):
    return unicodedata.east_asian_width(ch)


def combining_class(ch):
    return str(unicodedata.combining(ch))


def bidi_mirrored(ch):
    return "Y" if unicodedata.mirrored(ch) else "N"


def decomposition_type(ch):
    """The UCD's own tag text, lowercased, or `none`, or `canonical`.

    `unicodedata.decomposition()` returns what UnicodeData.txt records:
    `<noBreak> 0020` for a compatibility mapping with a tag, `0041 0301` with no
    tag for a canonical one, and nothing at all for a character that does not
    decompose. The long alias of a value is that tag text, which is what this
    emits - the short aliases would compare `nb` against `nobreak`.
    """
    text = unicodedata.decomposition(ch)
    if not text:
        return "none"
    if text.startswith("<"):
        return text[1:text.index(">")].lower()
    return "canonical"


def numeric_value(ch):
    """The exact rational, as `p/q`.

    `unicodedata.numeric()` hands back a float, so the UCD's 1/3 arrives as
    0.3333333333333333 and the rational has to be reconstructed. The parent
    compares rationals rather than floats, because a float comparison of a third
    is a coin toss.
    """
    try:
        value = unicodedata.numeric(ch)
    except ValueError:
        return None
    return str(Fraction(value).limit_denominator(1000000))


def character_name(ch):
    try:
        return unicodedata.name(ch)
    except ValueError:
        return None


PROPERTIES = {
    "General_Category": general_category,
    "Bidi_Class": bidi_class,
    "East_Asian_Width": east_asian_width,
    "Canonical_Combining_Class": combining_class,
    "Bidi_Mirrored": bidi_mirrored,
    "Decomposition_Type": decomposition_type,
    "Numeric_Value": numeric_value,
    "Name": character_name,
}


def runs(ask):
    """Maximal ascending runs of one value, as (first, last, text)."""
    out = []
    first = 0
    previous = ask(chr(0))
    for cp in range(1, MAX_CODEPOINT + 1):
        # Lone surrogates included, deliberately. The first draft of this
        # skipped them on the assumption that CPython would refuse one, and
        # that would have dropped 2,048 real comparisons per property while
        # reporting them as "not comparable" - a sweep that cannot see,
        # returning clean. It refuses nothing: category() answers Cs,
        # bidirectional() answers L, and only name() and numeric() raise, which
        # they do for any codepoint that has neither.
        value = ask(chr(cp))
        if value != previous:
            out.append((first, cp - 1, previous))
            first = cp
            previous = value
    out.append((first, MAX_CODEPOINT, previous))
    return out


def main(argv):
    if len(argv) > 1 and argv[1] == "--version":
        sys.stdout.write("Python %s, unicodedata %s\n"
                         % (sys.version.split()[0], unicodedata.unidata_version))
        return 0
    write = sys.stdout.write
    for line in sys.stdin:
        name = line.strip()
        if not name:
            continue
        if name == "version":
            write("version\t1\nunidata\t%s\n" % unicodedata.unidata_version)
            sys.stdout.flush()
            continue
        ask = PROPERTIES.get(name)
        if ask is None:
            # A frame with a count of zero, rather than silence: the parent is
            # counting lines and a request that produced nothing at all would
            # desynchronise every answer after it.
            write("%s\t0\n" % name)
            sys.stdout.flush()
            sys.stderr.write("unicodedata_ask: no such property %r\n" % name)
            continue
        table = runs(ask)
        write("%s\t%d\n" % (name, len(table)))
        for first, last, value in table:
            bounds = ("%04X" % first) if first == last \
                else ("%04X..%04X" % (first, last))
            write("%s\t%s\n" % (bounds, "-" if value is None else value))
        sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
