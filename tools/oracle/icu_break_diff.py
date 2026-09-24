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
"""Differential: this library's segmentation against ICU's.

The Consortium's four conformance files gate the four algorithms in `make test`
and they are the authority. This is the second opinion, and for segmentation it
is the *only* one available - `unicodedata` exposes no boundary function at all,
so where the property tables have CPython to answer for them, UAX #29 and UAX
#14 have had nothing but the conformance files. design.md section 12.1 names
this gate and section 12.4 the shape.

**Why a conformance file is not enough on its own.** `GraphemeBreakTest.txt` and
its three companions are tables of *pairs* with a small amount of context. They
say nothing about a boundary four characters into a string of nine, and the
rules that decide those - GB9c's prepend context, WB4's ignore rule, the LB25
number sequences, the regional-indicator pair count in GB12/GB13 - are exactly
the ones an implementation gets subtly wrong while passing every pair. Random
strings over a class-stratified pool reach them; a pair table cannot.

**Measured complete**, 2026-09-24, by `--exhaustive` at ICU 78.3: every codepoint
in three contexts, all four algorithms, all three LB1 resolutions - 23,353,344
comparisons, **3,067 explained and 0 unexplained**. The 3,067 are the two
divergences at the top of this file and nothing else: 12 iteration-mark cases and
6 inseparable-pair cases in `loose`, and 3,049 dictionary cases in `word`.
Grapheme, sentence and every other line tailoring agree on all 3,336,192.

**The reference links only ICU.** `icu_break.cpp` is compiled inside
`containers/icu/`, against that image's ICU and nothing else, so the oracle
cannot reach the implementation it answers for. ICU 78.3 carries Unicode 17.0,
an exact match for `tools/ucd/UCD_VERSION`, so unlike the CPython differential
there is no version skew to excuse a disagreement and no advisory mode: a
difference here is a defect in one of the two libraries.

**The pool is stratified by break class, not uniform over the codespace.** A
uniform sample of 1,114,112 codepoints is 73% unassigned and would spend its
whole budget on `Other`/`XX`. The pool is built by reading this library's own
sweep dumps for Grapheme_Cluster_Break, Word_Break, Sentence_Break and
Line_Break and taking representatives of *every value of every one* - so every
class an implementation can confuse with another is in play. Using our own
tables to choose the sample is not circular: the tables decide which characters
get asked about, and ICU decides the answer.

Usage:
    make check-oracle-icu
    tools/oracle/icu_break_diff.py --cases 4000 --seed 7
    tools/oracle/icu_break_diff.py --kind word --show 20

Exit status is 1 on any difference, and 1 if it compared nothing.
"""

import argparse
import binascii
import bisect
import os
import random
import subprocess
import sys

MAX_CODEPOINT = 0x10FFFF

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

sys.path.insert(0, HERE)
import oracle_env

ORACLE = "icu"

# The four algorithms, and the property whose values stratify the pool for each.
KINDS = [
    ("grapheme", "Grapheme_Cluster_Break"),
    ("word", "Word_Break"),
    ("sentence", "Sentence_Break"),
    ("line", "Line_Break"),
]

# For line breaking, every LB1 resolution plus the unqualified default. The
# three named ones are this library's reason to exist (design.md section 2, M2)
# and ICU spells them as locale keywords, so this is the only place the two
# libraries' tailoring axes can be checked against each other at all.
# The three CSS values, named explicitly. **"default" is deliberately not here.**
# On this library's side it means the zero value, which is `strict`; on ICU's it
# means "whatever this locale's default is", which under `ja` is not strict. The
# two were the same question only while the locale was root, and comparing them
# with a language set reported 35 differences that were a disagreement about what
# "default" means rather than about any rule.
TAILORINGS = ["strict", "normal", "loose"]

# The writing system, which CSS Text's four conditional tailorings need and which
# ICU takes as a language. Asking root for a language-conditional rule and
# reporting that ICU lacks it would be asking the wrong question, so the driver
# maps neutral to root, chinese to `zh` and japanese to `ja`.
SYSTEMS = ["neutral", "chinese", "japanese"]

# `anywhere` is deliberately absent from TAILORINGS: ICU has no `@lb=anywhere`,
# so there is nothing to compare against. It is gated by the unit tests instead,
# where its definition - the grapheme cluster boundaries, less the start of text
# - is checkable without an oracle.

# CSS Text's `line-break: loose` is LB1's `CJ`-to-`ID` resolution *plus*
# tailorings of its own. ICU implements those; this library implements LB1
# (design.md section 2, M2) and offers nothing else on that axis. So ICU's loose
# permits breaks this library does not, in exactly two places.
#
# **This list is complete, and that was established rather than assumed.** All
# 1,114,112 codepoints were asked in two contexts - before and after a Han
# character - under all three tailorings, and the only characters where ICU's
# loose departs from its strict are the 60 `CJ` codepoints, which are LB1 and
# which this library agrees with, and these six. A first attempt probed only the
# *endpoints* of each Line_Break run and reported five, missing U+309D because it
# sits in a run's interior: the same shape of blindness one level down.
CSS_LOOSE_ITERATION_MARKS = {
    0x3005,  # IDEOGRAPHIC ITERATION MARK
    0x303B,  # VERTICAL IDEOGRAPHIC ITERATION MARK
    0x309D,  # HIRAGANA ITERATION MARK
    0x309E,  # HIRAGANA VOICED ITERATION MARK
    0x30FD,  # KATAKANA ITERATION MARK
    0x30FE,  # KATAKANA VOICED ITERATION MARK
}

# The other half of CSS loose: "breaks are allowed between inseparable
# characters". UAX #14's LB22 forbids a break before `IN` unconditionally, and
# this library applies it in every tailoring.
#
# Narrow, and measured to be narrow: `IN x IN` differs and `ID x IN`, `IN x ID`,
# `AL x IN` and `IN x AL` all agree in all three tailorings. So the divergence is
# between two Inseparables and not "before an Inseparable", and an explanation
# written the looser way would have covered four pairs it has no business
# covering. This is why the pairwise mode exists: random strings found the
# iteration marks and never this, because two `IN` characters landing adjacent is
# rare, and the pairwise sweep asks every ordered pair once.
CSS_LOOSE_INSEPARABLE_PAIR = ("Inseparable", "Inseparable")

# ICU segments some text with a dictionary rather than with the UAX #29 rules,
# and inside such a run it is answering a different question: it joins two Han
# characters into one word where WB999 breaks them, and splits a run of
# Katakana where WB13 joins it. This library implements the rules and offers the
# dictionary as a provider seam instead (design.md section 9,
# GUNI_BreakProvider).
#
# **Word breaking only.** The first version of this applied to line breaking as
# well, on the assumption that ICU's dictionary would show there too, and that
# was an assumption rather than a measurement - a costly one, because Han and
# Kana are a large share of any pool and the explanation would have absorbed any
# defect adjacent to one of them. Every codepoint was doubled and asked under all
# four kinds: the divergence is in `word` and in nothing else. Grapheme,
# sentence and line agree everywhere.
#
# Two predicates, because the measurement showed two populations:
#
#   Complex_Context      Myanmar, Thai, Lao, Khmer, Tai Tham, Tai Viet, Tai Le,
#                        New Tai Lue, Ahom - 11 scripts, uniformly identified by
#                        Line_Break SA, which is how UAX #29 itself describes
#                        the case. A script list would have named seven of them
#                        and silently stopped covering the other four.
#   Han, Hiragana,       ICU's CJK dictionary. Not expressible as a Line_Break
#   Katakana             class: the divergent characters span ID, CJ, AL, NS and
#                        CM.
DICTIONARY_LINE_BREAK = "Complex_Context"
DICTIONARY_SCRIPTS = {"Han", "Hiragana", "Katakana"}

# ICU implements the *older* CSS Text definition of `line-break: normal`, under
# which a break before class CJ was allowed. The current specification forbids it
# for normal and strict alike, and says so in its own change log: "Disallowed
# breaks before small kana in line-break: normal" (CSSWG issue 10363, changed
# after the September 2024 Candidate Recommendation Draft). So this library
# forbids it and ICU 78.3 allows it, and the disagreement is a dated
# specification change rather than a defect in either.
#
# This library used to allow it too, which is how the differential found it: the
# correction made a previously-agreeing pair disagree.
CSS_NORMAL_SMALL_KANA = "Conditional_Japanese_Starter"

# The hyphen rule is conditioned on the *preceding character's class* being ID,
# not on the writing system - so this library applies it in neutral text. ICU
# applies it only when a language is set: under `ja` the two agree exactly, and
# in root ICU declines. Measured both ways, which is what distinguishes "ICU
# gates this on language" from "ICU does not implement this".
CSS_LOOSE_HYPHENS = {0x2010, 0x2013}

# And ICU applies that rule to **all eleven** codepoints of class HH - U+058A,
# U+05BE, U+1400, U+2010, U+2012, U+2013, U+2E17, U+2E40, U+2E5D, U+10D6E,
# U+10EAD - where CSS names two. Broader than the specification's minimum, which
# the specification explicitly permits: "the precise set of rules in effect for
# each of loose, normal, and strict is up to the UA", and "UAs can add additional
# distinctions between strict/normal/loose modes". So neither side is wrong and
# this library implements the required set.
CSS_HYPHEN_CLASS = "Unambiguous_Hyphen"

# **What the prefix and suffix tailorings lift is the one thing CSS does not
# say**, and the two libraries chose differently. This library lifts LB23a and
# LB25 - the rules that keep a prefix or suffix with its number or ideograph,
# which is the pairing the tailorings are about - and nothing else. ICU also
# lifts LB24, which is about letters: it breaks `PR x AL` and `AL x PO` where
# this library does not.
#
# Neither is wrong. The specification says outright that "the precise set of
# rules in effect for each of loose, normal, and strict is up to the UA", and
# taking "breaks after prefixes are allowed" at its word would break a currency
# sign from its digits, which is what scoping it to LB23a and LB25 avoids.
#
# Measured, after the scoping: of eight probe pairs around a wide prefix and a
# wide suffix, seven agree with ICU and this is the one that does not.
CSS_PREFIX_CLASS = "Prefix_Numeric"
CSS_SUFFIX_CLASS = "Postfix_Numeric"
CSS_LETTER_CLASSES = {"Alphabetic", "Hebrew_Letter"}

# The classes CSS's prefix and suffix rules *do* cover: LB23a's ideographs,
# LB25's numbers and LB27's Korean syllable blocks. A missing break beside one of
# these is this library's defect, not ICU being broad, so the broad explanation
# below must refuse them.
CSS_PAIRED_CLASSES = {"Ideographic", "E_Base", "E_Modifier", "Numeric",
                      "JL", "JV", "JT", "H2", "H3"}

# ICU's Chinese and Japanese locales allow a break beside the two directional
# double quotation marks, in **every** tailoring, where UAX #14's LB19 and LB19a
# forbid it. Neither UAX #14 nor CSS's line-break property asks for this: it is a
# language convention, which is the kind of thing design.md section 9 routes
# through GUNI_BreakProvider rather than into the rule engine.
#
# Exactly these two of class QU's 39 codepoints, measured one by one - the
# straight quote U+0022 is not tailored, and neither are the single curly quotes.
# Not conditioned on the neighbour either: `QU x AL` diverges as well as
# `ID x QU`.
ICU_CJK_QUOTES = {0x201C, 0x201D}
CSS_WIDE_WIDTHS = {"Ambiguous", "Fullwidth", "Wide"}


def dump_property(binary, name):
    """This library's value for one property, as (first, last, value) runs."""
    environment = dict(os.environ)
    environment["GUNI_SWEEP_DUMP"] = name
    environment.setdefault("GUNI_TEST_DATA", os.path.join(ROOT, "tests", "data"))
    finished = subprocess.run([binary], env=environment, capture_output=True,
                              text=True, check=True)
    runs = []
    for line in finished.stdout.splitlines():
        bounds, _space, value = line.partition(" ")
        low, _dots, high = bounds.partition("..")
        try:
            runs.append((int(low, 16), int(high, 16), value))
        except ValueError:
            raise SystemExit(
                "%s did not dump %s - it printed %r.\n"
                "GUNI_SWEEP_DUMP lives in testSweep; a test binary that does "
                "not know the variable runs its tests instead, and its output "
                "parses as nothing." % (os.path.basename(binary), name, line))
    if not runs:
        raise SystemExit("%s dumped no runs for %s" % (binary, name))
    return runs


def pool_for(binary, prop, per_class, rng):
    """Representatives of every value of `prop`.

    `per_class` codepoints per value, drawn from the runs that carry it rather
    than from the first one - a class whose first range is a single control
    character would otherwise be represented only by that character, and the
    rules that treat it specially would be the only ones exercised.
    """
    by_value = {}
    for first, last, value in dump_property(binary, prop):
        by_value.setdefault(value, []).append((first, last))
    chosen = {}
    for value, ranges in sorted(by_value.items()):
        picks = []
        for _ in range(per_class):
            first, last = ranges[rng.randrange(len(ranges))]
            cp = rng.randint(first, last)
            if 0xD800 <= cp <= 0xDFFF:
                # A lone surrogate is not text: it cannot be encoded as UTF-8,
                # so neither side could be asked about it and a request
                # containing one would test the protocol rather than the
                # algorithm.
                continue
            picks.append(cp)
        if picks:
            chosen[value] = picks
    return chosen


def generate(sweep, cases, seed, per_class, min_len, max_len):
    """The request list, and the pool it was drawn from.

    `sweep` is testSweep, which is where GUNI_SWEEP_DUMP lives - the property
    tables and the boundaries come from two different test binaries, and asking
    the wrong one for the pool is how the first version of this failed.
    """
    rng = random.Random(seed)
    requests = []
    pools = {}
    for kind, prop in KINDS:
        pool_by_class = pool_for(sweep, prop, per_class, rng)
        flat = [cp for picks in pool_by_class.values() for cp in picks]
        pools[kind] = (pool_by_class, flat)
        tailorings = TAILORINGS if kind == "line" else ["default"]
        systems = SYSTEMS if kind == "line" else ["neutral"]
        for _ in range(cases):
            length = rng.randint(min_len, max_len)
            text = "".join(chr(flat[rng.randrange(len(flat))])
                           for _ in range(length))
            hexed = binascii.hexlify(text.encode("utf-8")).decode().upper()
            for tailoring in tailorings:
                for system in systems:
                    requests.append((kind, hexed, tailoring, system))
    return requests, pools


def pairwise_requests(sweep, prop, kind, tailorings, rng, per_class):
    """Every ordered pair of one property's values, as two-character strings.

    The random pool finds divergences at the rate they occur in random text,
    which for a tailoring that touches two classes is a slow trickle - the first
    run surfaced the Japanese iteration marks, the next 22 cases of one single
    class pair, and neither told me whether there were others. This asks every
    ordered pair once and answers that question completely: 48 Line_Break
    classes is 2,304 pairs, which is cheaper than one random run.

    design.md section 12.3 already asks for a pairwise Line_Break sweep as a
    *regression record* against this library's own past answers
    (tests/data/break/linebreak-pairs.txt). This is the same shape pointed at an
    oracle instead, which is the thing that record cannot be: it can only say
    that an answer changed, never that it was wrong.
    """
    by_value = {}
    for first, last, value in dump_property(sweep, prop):
        by_value.setdefault(value, []).append((first, last))
    representatives = {}
    for value, ranges in sorted(by_value.items()):
        for _ in range(per_class):
            first, last = ranges[rng.randrange(len(ranges))]
            cp = rng.randint(first, last)
            if 0xD800 <= cp <= 0xDFFF:
                continue
            representatives.setdefault(value, []).append(cp)
    requests = []
    for left_value in sorted(representatives):
        for right_value in sorted(representatives):
            for left in representatives[left_value][:1]:
                for right in representatives[right_value][:1]:
                    text = chr(left) + chr(right)
                    hexed = binascii.hexlify(
                        text.encode("utf-8")).decode().upper()
                    for tailoring in tailorings:
                        for system in (SYSTEMS if kind == "line"
                                       else ["neutral"]):
                            requests.append((kind, hexed, tailoring, system))
    return requests, representatives


# The contexts an exhaustive sweep places each codepoint in. Two characters is
# enough to reach the pair rules and is what the divergence list was established
# over; a longer frame multiplies the cost without adding a rule the random mode
# does not already reach.
#
# `doubled` is the context a dictionary acts in - a run of one script - and is
# what showed that ICU's dictionary divergence is word-only. The two Han frames
# are what showed that the `loose` divergence is exactly six codepoints.
EXHAUSTIVE_CONTEXTS = [
    ("doubled", lambda cp: chr(cp) * 2),
    ("after Han", lambda cp: "\u4e00" + chr(cp)),
    ("before Han", lambda cp: chr(cp) + "\u4e00"),
]


def exhaustive_chunks(kinds, chunk, upto=MAX_CODEPOINT):
    """Every codepoint, in every context, in blocks small enough to hold.

    **This is what entitles the divergence list to say "complete".** The guard
    suite in --self-test-only is a floor: its cases are ones I thought of, and it
    caught the bug it caught because that bug happened to share a shape with a
    case already written. A divergence nobody imagined walks straight through it.
    Only a sweep that asks about every codepoint can say the list is finished, and
    it is the thing to re-run when the ICU pin moves - a new ICU is new
    tailorings, and the explanation table is written against the ones measured
    here.

    Yielded in chunks because the whole sweep is tens of millions of requests and
    the request list is text: 23 million lines does not want to be one list.

    `upto` stops early. A sweep that takes tens of minutes and can only be run
    whole is one nobody debugs and nobody bisects - the first attempt to check
    this code path was a 22-minute run of `--kind line` that printed nothing for
    its first minute because stdout block-buffers into a pipe, which is not how
    to find out whether the plumbing works.
    """
    for base in range(0, upto + 1, chunk):
        requests = []
        for cp in range(base, min(base + chunk, upto + 1)):
            if 0xD800 <= cp <= 0xDFFF:
                # Not text: a lone surrogate cannot be encoded as UTF-8, so
                # neither side can be asked about it.
                continue
            for _label, frame in EXHAUSTIVE_CONTEXTS:
                hexed = binascii.hexlify(
                    frame(cp).encode("utf-8")).decode().upper()
                for kind in kinds:
                    tailorings = TAILORINGS if kind == "line" else ["default"]
                    systems = SYSTEMS if kind == "line" else ["neutral"]
                    for tailoring in tailorings:
                        for system in systems:
                            requests.append((kind, hexed, tailoring, system))
        if requests:
            yield base, requests


def ask(argv, requests, who, environment=None):
    """Send every request to one side, and read back one framed answer each."""
    body = "".join("%s\t%s\t%s\t%s\n" % request for request in requests)
    child = subprocess.Popen(argv, stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, text=True, env=environment)
    out, _ = child.communicate(body)
    if child.returncode != 0:
        raise SystemExit("%s exited %d" % (who, child.returncode))
    lines = out.splitlines()
    if len(lines) != len(requests):
        # Show the first line, because the count alone sent me looking for a
        # pipe deadlock when what had happened was that testSegment ran its
        # gtest suite: GUNI_BREAK_DUMP was not in the environment this passes,
        # so the binary did the other thing it does and 34 lines of test output
        # arrived. A frame that catches the desync should also say what it saw.
        raise SystemExit("%s: asked %d and %d answers arrived; the first is %r"
                         % (who, len(requests), len(lines),
                            lines[0] if lines else ""))
    answers = []
    for request, line in zip(requests, lines):
        fields = line.split("\t")
        if len(fields) != 3:
            raise SystemExit("%s: not three fields: %r" % (who, line))
        # The echo is the framing: a stray line - the engine's banner, say -
        # would otherwise shift every answer after it and be absorbed.
        if fields[0] != request[0] or fields[1] != request[1]:
            raise SystemExit("%s: asked %s/%s and the answer echoes %s/%s"
                             % (who, request[0], request[1][:24], fields[0],
                                fields[1][:24]))
        answers.append(fields[2])
    return answers


def property_of(sweep, prop, absent="Unknown"):
    """A function from codepoint to one property's value, from our own tables.

    Used to *classify a difference*, never to decide one. Which characters get
    asked about, and which divergence a difference is filed under, come from this
    library's tables; every answer being compared comes from the two
    implementations.
    """
    runs = dump_property(sweep, prop)
    starts = [first for first, _last, _value in runs]
    values = [value for _first, _last, value in runs]

    def lookup(cp):
        index = bisect.bisect_right(starts, cp) - 1
        return values[index] if index >= 0 else absent

    return lookup


def characters_at(text, offsets):
    """The codepoints ending at and starting at each byte offset.

    A boundary is between two characters, so a difference *at* an offset is a
    claim about the pair, and both halves have to be looked at.
    """
    starts = {}
    at = 0
    for ch in text:
        starts[at] = ch
        at += len(ch.encode("utf-8"))
    ends = {}
    at = 0
    for ch in text:
        at += len(ch.encode("utf-8"))
        ends[at] = ch
    out = []
    for offset in offsets:
        out.append((ends.get(offset), starts.get(offset)))
    return out


def explain(kind, tailoring, system, text, ours, theirs, script, line_break,
        east_asian_width):
    """Why the two sides differ here, or None if nothing accounts for it.

    Every difference is reduced to the set of offsets the two sides disagree
    about, and *each one* has to be accounted for. A case where one offset is a
    known divergence and another is a real defect must not be filed under the
    known divergence, which is what checking the case as a whole would do.
    """
    mine = set(ours.split()) if ours else set()
    yours = set(theirs.split()) if theirs else set()
    reasons = set()
    for offset_text in sorted(mine ^ yours, key=int):
        offset = int(offset_text)
        theirs_only = offset_text in yours
        before, after = characters_at(text, [offset])[0]

        # ICU allows a break before class CJ under `normal`; the current CSS
        # forbids it. Only under `normal` - strict resolves CJ to NS on both
        # sides and loose resolves it to ID on both - and at a boundary with a CJ
        # character on **either** side, because what differs is the class the two
        # libraries resolved it to, and that changes the pair whichever side it
        # sits on. The first version of this checked only the right-hand
        # character and left `CJ x PO` unexplained, which is the same difference
        # seen from the other end.
        # The two sides are not symmetric, and the guard suite is what forced
        # that out. With CJ on the **right** this library resolves it to NS and
        # LB21 forbids a break before NS, so it can never be the side with the
        # extra boundary: ours-only there would be a defect. With CJ on the
        # **left** the resolution changes the left-hand class and the direction
        # then depends on the other character - `NS x PO` breaks where `ID x PO`
        # does not - so both directions are legitimate.
        if (kind == "line" and tailoring == "normal" and theirs_only
                and after is not None
                and line_break(ord(after)) == CSS_NORMAL_SMALL_KANA):
            reasons.add("ICU implements the pre-2024 CSS normal, which resolved"
                        " small kana to ID")
            continue
        if (kind == "line" and tailoring == "normal" and before is not None
                and line_break(ord(before)) == CSS_NORMAL_SMALL_KANA):
            reasons.add("ICU implements the pre-2024 CSS normal, which resolved"
                        " small kana to ID")
            continue

        # ICU's hyphen rule covers all of class HH; CSS names two codepoints.
        # Scoped tightly: loose, ICU's extra boundary, a language set (ICU gates
        # it on one), and an HH character CSS does *not* name - so a difference at
        # U+2010 or U+2013 is never filed here.
        # ICU's hyphen rule covers all of class HH; CSS names two codepoints.
        # **Either side of the character**, because ICU's treatment shows on both:
        # having broken before an HH character it keeps it attached to what
        # follows, so the same divergence appears as an ICU-only boundary before
        # it and an ours-only boundary after it. The first version checked only
        # the boundary before, and a random string containing `ID HH AI` was left
        # unexplained on its second offset.
        #
        # Scoped to loose, a language being set, and an HH character CSS does not
        # name - nine codepoints - so a difference at U+2010 or U+2013 is never
        # filed here.
        if (kind == "line" and tailoring == "loose" and system != "neutral"
                and ((theirs_only and after is not None
                          and line_break(ord(after)) == CSS_HYPHEN_CLASS
                          and ord(after) not in CSS_LOOSE_HYPHENS)
                     or (not theirs_only and before is not None
                          and line_break(ord(before)) == CSS_HYPHEN_CLASS
                          and ord(before) not in CSS_LOOSE_HYPHENS))):
            reasons.add("ICU applies the hyphen rule to all of class HH; CSS"
                        " names U+2010 and U+2013")
            continue

        # The hyphens after an ID character: ours, and only where ICU has no
        # language to gate it on. Under zh or ja the two agree, so an unexplained
        # difference there is a real one.
        # `line_break()` reads the raw table, and this rule is about the class
        # *after* LB1 - under loose, class CJ resolves to ID. Reading the raw
        # class left `U+308E U+2010` unexplained, U+308E being small kana that
        # becomes Ideographic exactly here.
        if (kind == "line" and tailoring == "loose" and not theirs_only
                and system == "neutral"
                and after is not None and ord(after) in CSS_LOOSE_HYPHENS
                and before is not None
                and line_break(ord(before)) in ("Ideographic",
                        CSS_NORMAL_SMALL_KANA)):
            reasons.add("ICU gates the hyphen-after-ID rule on language; CSS"
                        " gates it on the preceding class")
            continue

        if (kind == "line" and tailoring == "loose" and theirs_only
                and after is not None
                and ord(after) in CSS_LOOSE_ITERATION_MARKS):
            reasons.add("CSS loose breaks before Japanese iteration marks")
            continue

        if (kind == "line" and tailoring == "loose" and theirs_only
                and before is not None and after is not None
                and (line_break(ord(before)), line_break(ord(after)))
                    == CSS_LOOSE_INSEPARABLE_PAIR):
            reasons.add("CSS loose breaks between inseparable characters")
            continue

        # **ICU's prefix and suffix tailorings are broader than CSS's, in both
        # directions and across classes**, and this is one statement rather than
        # three because measuring it three ways produced three partial ones. What
        # CSS asks for is a break *before* a wide PO and *after* a wide PR, and
        # only where the other character is a number or ideograph - the pairing
        # LB23a, LB25 and LB27 exist to protect. ICU also breaks before a wide
        # PR, after a wide PO, and beside letters and symbols: asking every
        # left-hand class against a wide PR and a wide PO gave 41 classes and 36
        # where CSS's reading gives far fewer.
        #
        # Permitted: "the precise set of rules in effect for each of loose,
        # normal, and strict is up to the UA."
        #
        # Scope and its cost, stated because this is the broadest explanation
        # here: loose only, Chinese or Japanese only, ICU's extra boundary only,
        # and immediately beside one of the nineteen wide PR or PO codepoints. It
        # would absorb a defect in which *this library failed* to break beside one
        # of those nineteen under loose in zh/ja, so the rules CSS does require
        # there are asserted case by case in test_segment.cpp rather than left to
        # this differential.
        if (kind == "line" and tailoring == "loose" and theirs_only
                and system != "neutral"):
            pair = (before, after)
            wide_fix = [n for n, ch in enumerate(pair) if ch is not None
                        and line_break(ord(ch)) in (CSS_PREFIX_CLASS,
                                                    CSS_SUFFIX_CLASS)
                        and east_asian_width(ord(ch)) in CSS_WIDE_WIDTHS]
            # ...and the character on the other side is NOT one CSS's own rule
            # pairs with. If it is, CSS requires the break and a missing one is a
            # defect here. The guard suite caught this: the broad form absorbed
            # `Hangul x wide PO`, which LB27 makes this library's business.
            others = [pair[1 - n] for n in wide_fix]
            if wide_fix and not any(ch is not None
                    and line_break(ord(ch)) in CSS_PAIRED_CLASSES
                    for ch in others):
                reasons.add("ICU's prefix and suffix tailorings are broader than"
                            " CSS's, which are about numbers and ideographs")
                continue

        if (kind == "line" and theirs_only and system != "neutral"
                and ((before is not None and ord(before) in ICU_CJK_QUOTES)
                     or (after is not None and ord(after) in ICU_CJK_QUOTES))):
            reasons.add("ICU's zh/ja locales break beside U+201C and U+201D;"
                        " UAX #14 LB19 forbids it and CSS does not ask")
            continue

        if kind == "word":
            complex_context = [ch for ch in (before, after) if ch is not None
                               and line_break(ord(ch)) == DICTIONARY_LINE_BREAK]
            if complex_context:
                reasons.add("ICU resolves Complex_Context with a dictionary"
                            " (%s)" % "/".join(sorted(
                                {script(ord(ch)) for ch in complex_context})))
                continue
            cjk = [ch for ch in (before, after) if ch is not None
                   and script(ord(ch)) in DICTIONARY_SCRIPTS]
            if cjk:
                reasons.add("ICU segments %s with a dictionary"
                            % "/".join(sorted({script(ord(ch)) for ch in cjk})))
                continue

        return None  # this offset is not accounted for, so the case is not
    return ", ".join(sorted(reasons)) if reasons else None


# The explainer's own test, and it is not optional.
#
# `explain()` is the one piece of this gate whose failure is silent: an
# explanation that is too broad turns a defect into a green line, which is the
# exact failure the whole oracle directory exists to avoid. The first version of
# the dictionary rule applied to line breaking as well as word breaking, and
# these cases are what caught it - it was absorbing "a defect beside a Han
# character" in every kind.
#
# Each case is (label, kind, tailoring, text, ours, theirs, expect_explained).
def self_test_cases():
    """(label, kind, tailoring, system, text, ours, theirs, expect_explained)."""
    han, mark, ell1, ell2, thai = 0x4E00, 0x30FD, 0x2026, 0x22EF, 0x0E01
    kana, hyphen = 0x3041, 0x2010
    marks = chr(han) + chr(mark) + chr(han)
    inseparable = chr(ell1) + chr(ell2)
    small_kana = chr(han) + chr(kana) + chr(han)
    hyphenated = chr(han) + chr(hyphen) + chr(han)
    doubled_han = chr(han) * 2
    doubled_thai = chr(thai) * 2
    return [
        ("ICU breaks before an iteration mark, in loose",
         "line", "loose", "neutral", marks, "6 9", "3 6 9", True),
        ("ICU breaks between two Inseparables, in loose",
         "line", "loose", "neutral", inseparable, "6", "3 6", True),
        ("ICU breaks before small kana in normal, as pre-2024 CSS did",
         "line", "normal", "neutral", small_kana, "6 9", "3 6 9", True),
        ("we break before a hyphen after ID and ICU has no language to gate on",
         "line", "loose", "neutral", hyphenated, "3 6 9", "6 9", True),
        ("ICU splits a Han run in word breaking",
         "word", "default", "neutral", doubled_han, "0 3 6", "0 6", True),
        ("ICU resolves a Complex_Context run with a dictionary",
         "word", "default", "neutral", doubled_thai, "0 3 6", "0 6", True),
        # Everything below is a defect wearing a known divergence's clothes.
        ("we break before an iteration mark and ICU does not",
         "line", "loose", "neutral", marks, "3 6 9", "6 9", False),
        ("ICU breaks before an iteration mark in strict, not loose",
         "line", "strict", "neutral", marks, "6 9", "3 6 9", False),
        ("we break between two Inseparables and ICU does not",
         "line", "loose", "neutral", inseparable, "3 6", "6", False),
        ("ICU breaks between two Inseparables in normal, not loose",
         "line", "normal", "neutral", inseparable, "6", "3 6", False),
        ("ICU breaks before small kana in STRICT, which no CSS ever allowed",
         "line", "strict", "neutral", small_kana, "6 9", "3 6 9", False),
        ("we break before small kana in normal, the wrong direction",
         "line", "normal", "neutral", small_kana, "3 6 9", "6 9", False),
        ("the hyphen rule, but under ja where ICU agrees - so a real difference",
         "line", "loose", "japanese", hyphenated, "3 6 9", "6 9", False),
        ("the hyphen rule in the wrong direction",
         "line", "loose", "neutral", hyphenated, "6 9", "3 6 9", False),
        ("a line-break difference beside a Han character",
         "line", "default", "neutral", marks, "6 9", "3 6 9", False),
        ("a grapheme difference beside a Han character",
         "grapheme", "default", "neutral", doubled_han, "0 3 6", "0 6", False),
        ("a known divergence and a defect in the same case",
         "line", "loose", "neutral", marks, "6 9", "3 5 6 9", False),
        ("ICU breaks beside U+201C in Japanese, in every tailoring",
         "line", "strict", "japanese", chr(han) + chr(0x201C), "6", "3 6", True),
        ("the same in neutral, where ICU's locale rule does not apply",
         "line", "strict", "neutral", chr(han) + chr(0x201C), "6", "3 6", False),
        ("the same but ours-only, the wrong direction",
         "line", "strict", "japanese", chr(han) + chr(0x201C), "3 6", "6", False),
        ("the STRAIGHT quote, which ICU does not tailor",
         "line", "strict", "japanese", chr(han) + chr(0x0022), "4", "3 4", False),
    ] + [
        # The small-kana divergence with CJ on the left rather than the right,
        # which the first explainer could not see.
        ("CJ on the left, in normal",
         "line", "normal", "neutral", chr(kana) + chr(0x0025), "3 4", "4", True),
        ("CJ on the left, but in strict where both resolve NS",
         "line", "strict", "neutral", chr(kana) + chr(0x0025), "3 4", "4", False),
        # ICU's broader hyphen class: U+058A is HH and CSS does not name it.
        ("ICU breaks before U+058A, an HH character CSS does not name",
         "line", "loose", "japanese", chr(han) + chr(0x058A), "5", "3 5", True),
        ("we break after U+05BE, the same divergence from the other side",
         "line", "loose", "japanese", chr(0x05BE) + chr(0x0041), "2 3", "3", True),
        ("we break after U+2010, which CSS names - so not this rule",
         "line", "loose", "japanese", chr(0x2010) + chr(0x0041), "3 4", "4", False),
        ("we break after U+05BE in neutral, where ICU's rule does not apply",
         "line", "loose", "neutral", chr(0x05BE) + chr(0x0041), "2 3", "3", False),
        ("the same in neutral, where ICU has no language to gate on",
         "line", "loose", "neutral", chr(han) + chr(0x058A), "5", "3 5", False),
        ("ICU breaks before U+2010, which CSS *does* name, so not this rule",
         "line", "loose", "japanese", hyphenated, "6 9", "3 6 9", False),
        # The hyphen rule with CJ on the left, which resolves to ID under loose.
        ("we break before U+2010 after small kana, which loose makes ID",
         "line", "loose", "neutral", chr(0x308E) + chr(0x2010), "3 6", "6", True),
        # ICU's extra break before a wide prefix, and its near-misses.
        ("ICU breaks before a wide prefix, in Japanese loose",
         "line", "loose", "japanese", chr(0x0041) + chr(0xFFE5), "4", "1 4", True),
        ("ICU breaks after a wide prefix beside a letter, in Japanese loose",
         "line", "loose", "japanese", chr(0xFFE5) + chr(0x0041), "4", "3 4", True),
        ("ICU lifts LB24 beside a wide suffix, in Japanese loose",
         "line", "loose", "japanese", chr(0x0041) + chr(0xFF05), "4", "1 4", True),
        ("the same in neutral, where the tailoring does not apply at all",
         "line", "loose", "neutral", chr(0x0041) + chr(0xFFE5), "4", "1 4", False),
        ("the same but ours-only, the wrong direction",
         "line", "loose", "japanese", chr(0x0041) + chr(0xFFE5), "1 4", "4", False),
        ("a NARROW prefix, which the width condition excludes",
         "line", "loose", "japanese", chr(0x0041) + chr(0x0024), "2", "1 2", False),

        ("Hangul before a wide suffix, which LB27 makes ours too - a real"
         " difference if it reappears",
         "line", "loose", "japanese", chr(0xAC00) + chr(0xFF05), "6", "3 6", False),
    ]


def self_test(sweep):
    """Check every guard on `explain()`. Returns the number that misbehaved."""
    script = property_of(sweep, "Script")
    line_break = property_of(sweep, "Line_Break")
    east_asian_width = property_of(sweep, "East_Asian_Width")
    wrong = 0
    for (label, kind, tailoring, system, text, ours, theirs,
            expected) in self_test_cases():
        reason = explain(kind, tailoring, system, text, ours, theirs, script,
                         line_break, east_asian_width)
        got = reason is not None
        if got != expected:
            wrong += 1
            print("  WRONG  %s" % label)
            print("         expected %s, got %r"
                  % ("an explanation" if expected else "no explanation", reason))
    if wrong:
        print("  %d of %d explainer cases misbehaved" % (wrong,
            len(self_test_cases())))
    else:
        print("explainer: %d cases, %d explained and %d refused, as intended"
              % (len(self_test_cases()),
                 sum(1 for case in self_test_cases() if case[7]),
                 sum(1 for case in self_test_cases() if not case[7])))
    return wrong


class Tally:
    """The running comparison, so that no mode counts differently.

    The exhaustive mode compares in chunks and every other mode compares one
    list, and that is exactly the shape where two copies of the counting drift
    apart. There is one copy, and `add()` is it.
    """

    def __init__(self, script, line_break, east_asian_width, keep_examples):
        self.script = script
        self.line_break = line_break
        self.east_asian_width = east_asian_width
        self.keep_examples = keep_examples
        self.counts = {}
        self.examples = {}
        self.explained_by = {}
        self.requests = 0

    def add(self, requests, mine, theirs):
        for request, ours, yours in zip(requests, mine, theirs):
            kind, hexed, tailoring, system = request
            key = (kind, tailoring, system)
            self.requests += 1
            agreed, explained, unexplained = self.counts.get(key, (0, 0, 0))
            if ours == yours:
                self.counts[key] = (agreed + 1, explained, unexplained)
                continue
            text = binascii.unhexlify(hexed).decode("utf-8")
            reason = explain(kind, tailoring, system, text, ours, yours,
                             self.script, self.line_break, self.east_asian_width)
            if reason is not None:
                self.counts[key] = (agreed, explained + 1, unexplained)
                counts = self.explained_by.setdefault(key, {})
                counts[reason] = counts.get(reason, 0) + 1
                continue
            self.counts[key] = (agreed, explained, unexplained + 1)
            held = self.examples.setdefault(key, [])
            if len(held) < self.keep_examples:
                held.append((text, ours, yours))

    def report(self):
        """Print every line and return the number of unexplained differences."""
        total_unexplained = 0
        total_explained = 0
        for key in sorted(self.counts):
            agreed, explained, unexplained = self.counts[key]
            total_unexplained += unexplained
            total_explained += explained
            label = key[0] if key[0] != "line" else "%s/%s/%s" % key
            print("  %-30s %7d agreed %6d explained %6d UNEXPLAINED  %s"
                  % (label, agreed, explained, unexplained,
                     "ok" if unexplained == 0 else "DIFFERS"))
            for reason, count in sorted(self.explained_by.get(key, {}).items()):
                print("      %6d  %s" % (count, reason))
            for text, ours, yours in self.examples.get(key, []):
                print("      %s" % " ".join("U+%04X" % ord(ch) for ch in text))
                print("        ours %s" % (ours or "(none)"))
                print("        icu  %s" % (yours or "(none)"))

        compared = sum(sum(entry) for entry in self.counts.values())
        print()
        if compared == 0:
            print("nothing was compared, which is not a pass")
            return 1
        print("%d comparisons over %d requests: %d explained, %d unexplained"
              % (compared, self.requests, total_explained, total_unexplained))
        print()
        print("An explained difference is one of the documented divergences at"
              " the top of this")
        print("file, checked offset by offset rather than case by case - a case"
              " where one")
        print("offset is a known divergence and another is a defect is not"
              " explained. Nothing")
        print("is excluded from the pool: the characters that diverge stay in"
              " it, so that a")
        print("difference of any other shape at those characters still fails.")
        return 1 if total_unexplained else 0


def icu_argv():
    """The reference, compiled inside its image against that image's ICU.

    A couple of seconds once per run. The alternative is a binary built at
    image-build time from a copy of this source the image would have to carry,
    which is a second thing to keep in step with this file.
    `exec` replaces the shell so that stdin reaches the driver.
    """
    return oracle_env.command(ORACLE, ["sh", "-c",
        "g++ -O1 -o /tmp/icu_break %s/tools/oracle/icu_break.cpp"
        " -licuuc -licui18n && exec /tmp/icu_break" % ROOT])


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build", default=os.path.join(ROOT, "build", "linux",
                                                        "release", "apps"),
                        help="where the test binaries are")
    parser.add_argument("--cases", type=int, default=2000,
                        help="strings per break kind")
    parser.add_argument("--seed", type=int, default=20260924,
                        help="the seed, so a run is reproducible")
    parser.add_argument("--per-class", type=int, default=3,
                        help="codepoints sampled per break-property value")
    parser.add_argument("--min-len", type=int, default=2)
    parser.add_argument("--max-len", type=int, default=12)
    parser.add_argument("--kind", action="append",
                        help="compare only these kinds (may be repeated)")
    parser.add_argument("--show", type=int, default=6,
                        help="differing cases to print per kind")
    parser.add_argument("--exhaustive", action="store_true",
                        help="every codepoint in every context, instead of a "
                             "sample: tens of minutes, and the thing that "
                             "entitles the divergence list to claim it is "
                             "complete. Re-run it when the ICU pin moves")
    parser.add_argument("--chunk", type=lambda text: int(text, 0),
                        default=0x10000,
                        help="codepoints per batch in --exhaustive")
    parser.add_argument("--upto", type=lambda text: int(text, 0),
                        default=MAX_CODEPOINT,
                        help="stop --exhaustive at this codepoint, for a quick "
                             "check or a bisect; the completeness claim needs "
                             "the default")
    parser.add_argument("--self-test-only", action="store_true",
                        help="check the explainer's guards and stop; needs no "
                             "container")
    parser.add_argument("--pairwise", action="store_true",
                        help="every ordered pair of every break class, instead "
                             "of random strings: exhaustive over pairs where "
                             "the random pool is a sample")
    args = parser.parse_args(argv[1:])

    segment = os.path.join(args.build, "testSegment")
    sweep = os.path.join(args.build, "testSweep")
    for binary in (segment, sweep):
        if not os.path.exists(binary):
            raise SystemExit("%s is not built; run make test first" % binary)

    # Before anything is compared: an explainer that has grown too broad would
    # report a clean run over a defect, so it is checked first and the gate stops
    # if it is wrong. Needs no container.
    if self_test(sweep):
        return 1
    if args.self_test_only:
        return 0
    print()

    environment = dict(os.environ)
    environment["GUNI_BREAK_DUMP"] = "stdin"
    environment.setdefault("GUNI_TEST_DATA", os.path.join(ROOT, "tests", "data"))
    script = property_of(sweep, "Script")
    line_break = property_of(sweep, "Line_Break", absent="Unknown")
    east_asian_width = property_of(sweep, "East_Asian_Width")
    tally = Tally(script, line_break, east_asian_width, args.show)

    known = [kind for kind, _prop in KINDS]
    if args.kind:
        unknown = [kind for kind in args.kind if kind not in known]
        if unknown:
            raise SystemExit("not a break kind: %s" % ", ".join(unknown))
    kinds = [kind for kind in known if not args.kind or kind in args.kind]

    if args.exhaustive:
        contexts = ", ".join(label for label, _frame in EXHAUSTIVE_CONTEXTS)
        print("exhaustive: U+0000..U+%04X in %d contexts (%s), kinds %s"
              % (args.upto, len(EXHAUSTIVE_CONTEXTS), contexts, "/".join(kinds)))
        if args.upto < MAX_CODEPOINT:
            print("PARTIAL: --upto stops short of U+10FFFF, so this run does not"
                  " support the completeness claim")
        else:
            print("this is what lets the divergence list say \"complete\"; it"
                  " takes tens of minutes")
        print(flush=True)
        for base, requests in exhaustive_chunks(kinds, args.chunk, args.upto):
            mine = ask([segment], requests, "this library", environment)
            theirs = ask(icu_argv(), requests, "icu")
            tally.add(requests, mine, theirs)
            sys.stderr.write("  through U+%05X, %d requests, %d unexplained\n"
                % (min(base + args.chunk, args.upto), tally.requests,
                   sum(entry[2] for entry in tally.counts.values())))
            sys.stderr.flush()
        return tally.report()

    if args.pairwise:
        rng = random.Random(args.seed)
        requests = []
        pools = {}
        for kind, prop in KINDS:
            tailorings = TAILORINGS if kind == "line" else ["default"]
            part, representatives = pairwise_requests(sweep, prop, kind,
                tailorings, rng, args.per_class)
            requests += part
            pools[kind] = (representatives,
                           [cp for picks in representatives.values()
                            for cp in picks])
    else:
        requests, pools = generate(sweep, args.cases, args.seed, args.per_class,
                                   args.min_len, args.max_len)
    requests = [request for request in requests if request[0] in kinds]
    if not requests:
        raise SystemExit("no requests, which is not a pass")

    for kind, prop in KINDS:
        if kind not in kinds:
            continue
        by_class, flat = pools[kind]
        print("pool for %-9s %3d values of %-22s %4d codepoints"
              % (kind, len(by_class), prop, len(flat)))
    print()

    mine = ask([segment], requests, "this library", environment)
    theirs = ask(icu_argv(), requests, "icu")
    tally.add(requests, mine, theirs)
    return tally.report()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
