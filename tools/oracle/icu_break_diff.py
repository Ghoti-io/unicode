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
TAILORINGS = ["default", "strict", "normal", "loose"]

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
        for _ in range(cases):
            length = rng.randint(min_len, max_len)
            text = "".join(chr(flat[rng.randrange(len(flat))])
                           for _ in range(length))
            hexed = binascii.hexlify(text.encode("utf-8")).decode().upper()
            for tailoring in tailorings:
                requests.append((kind, hexed, tailoring))
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
                        requests.append((kind, hexed, tailoring))
    return requests, representatives


def ask(argv, requests, who, environment=None):
    """Send every request to one side, and read back one framed answer each."""
    body = "".join("%s\t%s\t%s\n" % request for request in requests)
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


def explain(kind, tailoring, text, ours, theirs, script, line_break):
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
    han, mark, ell1, ell2, thai = 0x4E00, 0x30FD, 0x2026, 0x22EF, 0x0E01
    marks = chr(han) + chr(mark) + chr(han)
    inseparable = chr(ell1) + chr(ell2)
    doubled_han = chr(han) * 2
    doubled_thai = chr(thai) * 2
    return [
        ("ICU breaks before an iteration mark, in loose",
         "line", "loose", marks, "6 9", "3 6 9", True),
        ("ICU breaks between two Inseparables, in loose",
         "line", "loose", inseparable, "6", "3 6", True),
        ("ICU splits a Han run in word breaking",
         "word", "default", doubled_han, "0 3 6", "0 6", True),
        ("ICU resolves a Complex_Context run with a dictionary",
         "word", "default", doubled_thai, "0 3 6", "0 6", True),
        # Everything below is a defect wearing a known divergence's clothes.
        ("we break before an iteration mark and ICU does not",
         "line", "loose", marks, "3 6 9", "6 9", False),
        ("ICU breaks before an iteration mark in strict, not loose",
         "line", "strict", marks, "6 9", "3 6 9", False),
        ("we break between two Inseparables and ICU does not",
         "line", "loose", inseparable, "3 6", "6", False),
        ("ICU breaks between two Inseparables in normal, not loose",
         "line", "normal", inseparable, "6", "3 6", False),
        ("a line-break difference beside a Han character",
         "line", "default", marks, "6 9", "3 6 9", False),
        ("a grapheme difference beside a Han character",
         "grapheme", "default", doubled_han, "0 3 6", "0 6", False),
        ("a known divergence and a defect in the same case",
         "line", "loose", marks, "6 9", "3 5 6 9", False),
    ]


def self_test(sweep):
    """Check every guard on `explain()`. Returns the number that misbehaved."""
    script = property_of(sweep, "Script")
    line_break = property_of(sweep, "Line_Break")
    wrong = 0
    for label, kind, tailoring, text, ours, theirs, expected in self_test_cases():
        reason = explain(kind, tailoring, text, ours, theirs, script, line_break)
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
                 sum(1 for case in self_test_cases() if case[6]),
                 sum(1 for case in self_test_cases() if not case[6])))
    return wrong


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
    if args.kind:
        known = [kind for kind, _prop in KINDS]
        unknown = [kind for kind in args.kind if kind not in known]
        if unknown:
            raise SystemExit("not a break kind: %s" % ", ".join(unknown))
        requests = [r for r in requests if r[0] in args.kind]
    if not requests:
        raise SystemExit("no requests, which is not a pass")

    for kind, prop in KINDS:
        if args.kind and kind not in args.kind:
            continue
        by_class, flat = pools[kind]
        print("pool for %-9s %3d values of %-22s %4d codepoints"
              % (kind, len(by_class), prop, len(flat)))
    print()

    environment = dict(os.environ)
    environment["GUNI_BREAK_DUMP"] = "stdin"
    environment.setdefault("GUNI_TEST_DATA", os.path.join(ROOT, "tests", "data"))
    mine = ask([segment], requests, "this library", environment)

    # The driver is compiled inside the image, against that image's ICU, on
    # every run. It costs a couple of seconds once per gate - the alternative
    # is a binary built at image-build time from a source the image would then
    # have to contain a copy of, which is a second thing to keep in step with
    # this file. `exec` replaces the shell so that stdin reaches the driver.
    theirs = ask(oracle_env.command(ORACLE, ["sh", "-c",
        "g++ -O1 -o /tmp/icu_break %s/tools/oracle/icu_break.cpp"
        " -licuuc -licui18n && exec /tmp/icu_break" % ROOT]),
        requests, "icu")

    script = property_of(sweep, "Script")
    line_break = property_of(sweep, "Line_Break", absent="Unknown")
    tally = {}
    examples = {}
    explained_by = {}
    for request, ours, yours in zip(requests, mine, theirs):
        kind, hexed, tailoring = request
        key = (kind, tailoring)
        agreed, explained, unexplained = tally.get(key, (0, 0, 0))
        if ours == yours:
            tally[key] = (agreed + 1, explained, unexplained)
            continue
        text = binascii.unhexlify(hexed).decode("utf-8")
        reason = explain(kind, tailoring, text, ours, yours, script, line_break)
        if reason is not None:
            tally[key] = (agreed, explained + 1, unexplained)
            counts = explained_by.setdefault(key, {})
            counts[reason] = counts.get(reason, 0) + 1
            continue
        tally[key] = (agreed, explained, unexplained + 1)
        examples.setdefault(key, []).append((text, ours, yours))

    total_unexplained = 0
    total_explained = 0
    for key in sorted(tally):
        agreed, explained, unexplained = tally[key]
        total_unexplained += unexplained
        total_explained += explained
        label = key[0] if key[0] != "line" else "%s/%s" % key
        print("  %-16s %6d agreed %5d explained %5d UNEXPLAINED  %s"
              % (label, agreed, explained, unexplained,
                 "ok" if unexplained == 0 else "DIFFERS"))
        for reason, count in sorted(explained_by.get(key, {}).items()):
            print("      %5d  %s" % (count, reason))
        for text, ours, yours in examples.get(key, [])[:args.show]:
            print("      %s" % " ".join("U+%04X" % ord(ch) for ch in text))
            print("        ours %s" % (ours or "(none)"))
            print("        icu  %s" % (yours or "(none)"))

    compared = sum(sum(entry) for entry in tally.values())
    print()
    if compared == 0:
        print("nothing was compared, which is not a pass")
        return 1
    print("%d comparisons over %d strings: %d explained, %d unexplained"
          % (compared, len(requests), total_explained, total_unexplained))
    print()
    print("An explained difference is one of the two documented divergences at"
          " the top of")
    print("this file, checked offset by offset rather than case by case - a case"
          " where one")
    print("offset is a known divergence and another is a defect is not"
          " explained. Nothing")
    print("is excluded from the pool: the characters that diverge stay in it, so"
          " that a")
    print("difference of any other shape at those characters still fails.")
    return 1 if total_unexplained else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
