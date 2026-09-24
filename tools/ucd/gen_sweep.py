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
"""Generate the exhaustive sweep fixture: the oracle make test checks against.

This is a **second implementation** of reading the UCD, and that is the whole
point of it (documentation/design.md section 12.1). gen_tables.py emits the
tables the library ships; this script re-reads the same files with its own
parser - range lists and a linear merge, rather than an array per property -
and writes down, for every property, a checksum of the property's value at
every one of the 1,114,112 codepoints. tests/unit/test_sweep.cpp recomputes
those checksums from the compiled library and compares.

What that gate catches, which nothing else does:

  * one flipped entry in one generated table, on a clone with no network and
    no Python: the checksum for that property fails and names it;
  * a trie whose blocks are shared wrongly: the derived partition fails
    before any property does;
  * the runs table disagreeing with the trie, which is the claim set.h's
    correctness rests on;
  * a parsing mistake in gen_tables.py, because the two parsers would have to
    make the same mistake - and they read the fields differently on purpose.

The fixture is checksums rather than the whole sweep in text, because the
whole sweep in text is a third copy of the data and the committed tables are
already the reviewable one: on a Unicode upgrade the diff to read is
src/char/tables/props_data.c's, not a fixture's. When a checksum does fail,
"--property NAME" prints this script's answer as ranges and the test's
GUNI_SWEEP_DUMP prints the library's, so localising a divergence is a diff of
two streams rather than a rebuild with printfs.
"""

import argparse
import os
import re
import sys

MAX_CODEPOINT = 0x10FFFF
FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK64 = (1 << 64) - 1


def fnv1a(text):
    """FNV-1a over UTF-8 bytes. The same arithmetic as the C side's."""
    hash_value = FNV_OFFSET
    for byte in text.encode("utf-8"):
        hash_value = ((hash_value ^ byte) * FNV_PRIME) & MASK64
    return hash_value


# ---------------------------------------------------------------------------
# Reading the UCD, the other way round
#
# gen_tables.py fills an array of 1,114,112 entries per property. This reads
# each file into a list of (first, last, value) and resolves by walking the
# list. Two implementations that share a parser share its bugs, so these two
# deliberately do not: the field splitting, the @missing handling and the
# alias resolution are written again here rather than imported.
# ---------------------------------------------------------------------------

def data_lines(path):
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            body, _, _comment = raw.partition("#")
            body = body.strip()
            if body:
                yield body


def comment_lines(path):
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            if raw.lstrip().startswith("#"):
                yield raw.strip()


def split_range(text):
    text = text.strip()
    halves = text.split("..")
    first = int(halves[0], 16)
    last = int(halves[-1], 16)
    return first, last


class Ranges:
    """A property as (first, last, value) ranges plus a default.

    The two lists are not one list: every @missing line applies before every
    data line, in file order within each. Merging them and sorting by the
    first codepoint would let a block-wide @missing override a data line that
    starts before the block - which is not the file's meaning, and is the kind
    of ordering bug that shows up as a handful of wrong codepoints.
    """

    def __init__(self, default):
        self.default = default
        self.missing = []
        self.data = []

    def add_missing(self, first, last, value):
        self.missing.append((first, last, value))

    def add(self, first, last, value):
        self.data.append((first, last, value))

    def resolve(self, canonical=None):
        """The value of every codepoint, as a list."""
        table = [self.default] * (MAX_CODEPOINT + 1)
        for first, last, value in self.missing + self.data:
            if canonical is not None:
                value = canonical(value)
            for cp in range(first, min(last, MAX_CODEPOINT) + 1):
                table[cp] = value
        if canonical is not None:
            table = [canonical(self.default) if value == self.default else value
                     for value in table]
        return table


MISSING = re.compile(r"@missing:\s*([0-9A-Fa-f]+)\.\.([0-9A-Fa-f]+)\s*;\s*([^#]*)")


def read_ranged(path, default, field=1, only=None, value_field=None):
    """A range;value file, @missing lines included.

    A file that holds several properties spells its @missing lines with the
    property name first - "@missing: 0000..10FFFF; NFKC_QC; Yes" - so reading
    the whole tail as the value gives every codepoint the value
    "NFKC_QC; Yes". That is what happened the first time this ran, and the
    test caught it because the library said "Yes": one of the two sides being
    wrong is the only outcome this arrangement cannot hide.
    """
    ranges = Ranges(default)
    for line in comment_lines(path):
        match = MISSING.search(line)
        if not match:
            continue
        parts = [part.strip() for part in match.group(3).split(";")]
        if only is not None:
            if len(parts) < 2 or parts[0] not in only:
                continue
            value = parts[1]
        else:
            value = parts[0]
        if value.startswith("<"):
            continue
        ranges.add_missing(int(match.group(1), 16), int(match.group(2), 16), value)
    for line in data_lines(path):
        columns = [column.strip() for column in line.split(";")]
        if only is not None and (len(columns) < 2 or columns[1] not in only):
            continue
        index = value_field if value_field is not None else field
        if len(columns) <= index:
            continue
        first, last = split_range(columns[0])
        ranges.add(first, last, columns[index])
    return ranges


def read_aliases(ucd):
    """(value alias -> long name) per property key, and property long names."""
    values = {}
    for line in data_lines(os.path.join(ucd, "PropertyValueAliases.txt")):
        columns = [column.strip() for column in line.split(";")]
        key = columns[0]
        if key == "ccc":
            if len(columns) < 4:
                continue
            long_name = columns[3]
            spellings = [columns[1], columns[2], columns[3]] + columns[4:]
        else:
            if len(columns) < 3:
                continue
            long_name = columns[2]
            spellings = columns[1:]
        for spelling in spellings:
            if spelling:
                values.setdefault(key, {})[spelling] = long_name
    properties = {}
    for line in data_lines(os.path.join(ucd, "PropertyAliases.txt")):
        columns = [column.strip() for column in line.split(";")]
        if len(columns) >= 2:
            properties[columns[0]] = columns[-1]
            for spelling in columns:
                properties.setdefault(spelling, columns[-1])
    return values, properties


# ---------------------------------------------------------------------------
# What is swept
#
# Every property the library answers for, by the long name guni_property_name()
# reports, with the value spelled the way guni_value_name() spells it. The two
# sides agree on the *text*, never on a number: a fixture that named enum
# values could not catch a renumbering, which is one of the things it is for.
# ---------------------------------------------------------------------------

ENUM_FILES = (
    # long property name, file, default, record-resident
    ("General_Category", "DerivedGeneralCategory.txt", "Cn", True),
    ("Script", "Scripts.txt", "Unknown", True),
    ("Bidi_Class", "DerivedBidiClass.txt", "Left_To_Right", True),
    ("East_Asian_Width", "EastAsianWidth.txt", "N", True),
    ("Line_Break", "LineBreak.txt", "XX", True),
    ("Grapheme_Cluster_Break", "GraphemeBreakProperty.txt", "Other", True),
    ("Word_Break", "WordBreakProperty.txt", "Other", True),
    ("Sentence_Break", "SentenceBreakProperty.txt", "Other", True),
    ("Joining_Type", "DerivedJoiningType.txt", "Non_Joining", True),
    ("Joining_Group", "DerivedJoiningGroup.txt", "No_Joining_Group", True),
    ("Indic_Syllabic_Category", "IndicSyllabicCategory.txt", "Other", True),
    ("Indic_Positional_Category", "IndicPositionalCategory.txt", "NA", True),
    ("Vertical_Orientation", "VerticalOrientation.txt", "R", True),
    ("Hangul_Syllable_Type", "HangulSyllableType.txt", "NA", True),
    ("Decomposition_Type", "DerivedDecompositionType.txt", "None", True),
    ("Numeric_Type", "DerivedNumericType.txt", "None", True),
)

ENUM_KEYS = {
    "General_Category": "gc", "Script": "sc", "Bidi_Class": "bc",
    "East_Asian_Width": "ea", "Line_Break": "lb",
    "Grapheme_Cluster_Break": "GCB", "Word_Break": "WB",
    "Sentence_Break": "SB", "Joining_Type": "jt", "Joining_Group": "jg",
    "Indic_Syllabic_Category": "InSC", "Indic_Positional_Category": "InPC",
    "Vertical_Orientation": "vo", "Hangul_Syllable_Type": "hst",
    "Decomposition_Type": "dt", "Numeric_Type": "nt",
    "Indic_Conjunct_Break": "InCB",
}

BINARY_SOURCES = (
    ("PropList.txt", None),
    ("DerivedCoreProperties.txt", None),
    ("DerivedNormalizationProps.txt", (
        "Changes_When_NFKC_Casefolded", "Full_Composition_Exclusion",
        "Expands_On_NFC", "Expands_On_NFD", "Expands_On_NFKC",
        "Expands_On_NFKD")),
    ("DerivedBinaryProperties.txt", None),
    ("emoji-data.txt", None),
)

QUICK_CHECKS = {
    "NFC_Quick_Check": "NFC_QC",
    "NFD_Quick_Check": "NFD_QC",
    "NFKC_Quick_Check": "NFKC_QC",
    "NFKD_Quick_Check": "NFKD_QC",
}


def sweep(ucd_dir):
    """{property long name: [value text per codepoint]}, and which are in the record."""
    value_aliases, _property_names = read_aliases(ucd_dir)
    out = {}
    in_record = set()

    def canonical_for(key):
        table = value_aliases.get(key, {})
        return lambda value: table.get(value, value)

    for long_name, filename, default, resident in ENUM_FILES:
        key = ENUM_KEYS[long_name]
        ranges = read_ranged(os.path.join(ucd_dir, filename), default)
        out[long_name] = ranges.resolve(canonical_for(key))
        if resident:
            in_record.add(long_name)

    # Indic_Conjunct_Break: a three-field line in a file of two-field ones.
    ranges = read_ranged(os.path.join(ucd_dir, "DerivedCoreProperties.txt"),
                         "None", only=("InCB",), value_field=2)
    out["Indic_Conjunct_Break"] = ranges.resolve(canonical_for("InCB"))
    in_record.add("Indic_Conjunct_Break")

    # Canonical_Combining_Class, as the number it is.
    ranges = read_ranged(os.path.join(ucd_dir, "DerivedCombiningClass.txt"), "0")
    out["Canonical_Combining_Class"] = [
        str(int(value)) if value.isdigit() else "0"
        for value in ranges.resolve()]
    in_record.add("Canonical_Combining_Class")

    for long_name, key in QUICK_CHECKS.items():
        ranges = read_ranged(os.path.join(ucd_dir, "DerivedNormalizationProps.txt"),
                             "Y", only=(key,), value_field=2)
        table = {"Y": "Yes", "N": "No", "M": "Maybe"}
        out[long_name] = [table.get(value, value) for value in ranges.resolve()]
        in_record.add(long_name)

    # Binary properties: "Yes" where listed, "No" everywhere else. Every
    # property named in each file is taken, which is how a property added to
    # the UCD shows up as an unknown name in the test rather than as silence.
    _values, property_names = read_aliases(ucd_dir)
    for filename, only in BINARY_SOURCES:
        path = os.path.join(ucd_dir, filename)
        rows = {}
        for line in data_lines(path):
            columns = [column.strip() for column in line.split(";")]
            if len(columns) != 2:
                continue  # three-field lines here are other properties
            name = property_names.get(columns[1], columns[1])
            if only is not None and columns[1] not in only:
                continue
            first, last = split_range(columns[0])
            rows.setdefault(name, []).append((first, last))
        for name, spans in rows.items():
            table = ["No"] * (MAX_CODEPOINT + 1)
            for first, last in spans:
                for cp in range(first, min(last, MAX_CODEPOINT) + 1):
                    table[cp] = "Yes"
            out[name] = table
            in_record.add(name)

    # The three the Standard defines as rules rather than as file lines.
    out["Any"] = ["Yes"] * (MAX_CODEPOINT + 1)
    out["ASCII"] = ["Yes" if cp < 0x80 else "No" for cp in range(MAX_CODEPOINT + 1)]
    out["Assigned"] = ["No" if value == "Unassigned" else "Yes"
                       for value in out["General_Category"]]
    in_record.update(("Any", "ASCII", "Assigned"))

    # Script_Extensions: set-valued, so the text is the sorted set. UAX #24's
    # default is the codepoint's own Script.
    scx = list(out["Script"])
    for line in data_lines(os.path.join(ucd_dir, "ScriptExtensions.txt")):
        columns = [column.strip() for column in line.split(";")]
        if len(columns) < 2:
            continue
        canonical = canonical_for("sc")
        names = ",".join(sorted(canonical(token) for token in columns[1].split()))
        first, last = split_range(columns[0])
        for cp in range(first, min(last, MAX_CODEPOINT) + 1):
            scx[cp] = names
    out["Script_Extensions"] = scx
    in_record.add("Script_Extensions")

    # Block, which is a range table rather than a field of the record.
    blocks = ["No_Block"] * (MAX_CODEPOINT + 1)
    for line in data_lines(os.path.join(ucd_dir, "Blocks.txt")):
        columns = [column.strip() for column in line.split(";")]
        first, last = split_range(columns[0])
        for cp in range(first, min(last, MAX_CODEPOINT) + 1):
            blocks[cp] = columns[1]
    out["Block"] = blocks

    # Numeric_Value, as the exact rational the library reports.
    numeric = ["None"] * (MAX_CODEPOINT + 1)
    for line in data_lines(os.path.join(ucd_dir, "DerivedNumericValues.txt")):
        columns = [column.strip() for column in line.split(";")]
        text = columns[3] if len(columns) > 3 and columns[3] else columns[1]
        if "/" not in text:
            text = "%d/1" % int(text.split(".")[0])
        first, last = split_range(columns[0])
        for cp in range(first, min(last, MAX_CODEPOINT) + 1):
            numeric[cp] = text
    out["Numeric_Value"] = numeric

    return out, in_record


def runs_of(values):
    """(first, last, value) for the maximal ranges of constant value."""
    out = []
    start = 0
    for cp in range(1, MAX_CODEPOINT + 2):
        if cp > MAX_CODEPOINT or values[cp] != values[start]:
            out.append((start, cp - 1, values[start]))
            start = cp
    return out


def canonical_text(runs):
    """The text a checksum is taken over. The C side writes the same bytes."""
    return "".join("%06X..%06X %s\n" % (first, last, value)
                   for first, last, value in runs)


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ucd")
    parser.add_argument("--out", default=root)
    parser.add_argument("--version")
    parser.add_argument("--property",
                        help="print this property's ranges instead of writing "
                             "the fixture, to diff against GUNI_SWEEP_DUMP")
    args = parser.parse_args(argv[1:])

    version = args.version or open(os.path.join(here, "UCD_VERSION"),
                                   encoding="utf-8").read().strip()
    ucd_dir = args.ucd or os.path.join(root, "third_party", "ucd", version)
    if not os.path.isdir(ucd_dir):
        raise SystemExit("%s does not exist; run tools/ucd/fetch.sh" % ucd_dir)

    values, in_record = sweep(ucd_dir)

    if args.property:
        if args.property not in values:
            raise SystemExit("no property %r; have %s"
                             % (args.property, ", ".join(sorted(values))))
        sys.stdout.write(canonical_text(runs_of(values[args.property])))
        return 0

    # The partition: the boundaries of the record, which is every property the
    # record holds. set.h's runs table has to reproduce it exactly.
    boundary = bytearray(MAX_CODEPOINT + 2)
    boundary[0] = 1
    for name in sorted(in_record):
        table = values[name]
        for cp in range(1, MAX_CODEPOINT + 1):
            if table[cp] != table[cp - 1]:
                boundary[cp] = 1
    partition = [cp for cp in range(MAX_CODEPOINT + 1) if boundary[cp]]

    path = os.path.join(args.out, "tests", "data", "sweep", "%s.sums" % version)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as out:
        out.write("# Exhaustive property sweep for UCD %s.\n" % version)
        out.write("#\n"
                  "# GENERATED by tools/ucd/gen_sweep.py, which parses the UCD\n"
                  "# independently of tools/ucd/gen_tables.py. This file is the\n"
                  "# oracle tests/unit/test_sweep.cpp checks the compiled tables\n"
                  "# against, over all 1,114,112 codepoints, with no network and no\n"
                  "# Python at test time (documentation/design.md section 12.1).\n"
                  "#\n"
                  "# Each line is a property, the number of maximal ranges over which\n"
                  "# its value is constant, and FNV-1a-64 over the text of those\n"
                  "# ranges: \"%06X..%06X <value long name>\\n\" per range. The value is\n"
                  "# spelled, never numbered, so that a renumbered enum fails here.\n"
                  "#\n"
                  "# \"partition\" is the record's own boundary set - every property the\n"
                  "# record holds - hashed as \"%06X\\n\" per boundary. The library's runs\n"
                  "# table must reproduce it exactly, which is what set.h's agreement\n"
                  "# with char.h rests on.\n"
                  "#\n"
                  "# To localise a failure:\n"
                  "#   tools/ucd/gen_sweep.py --property Script > /tmp/oracle\n"
                  "#   GUNI_SWEEP_DUMP=Script build/.../testSweep > /tmp/ours\n"
                  "#   diff /tmp/oracle /tmp/ours\n")
        out.write("version %s\n" % version)
        text = "".join("%06X\n" % cp for cp in partition)
        out.write("partition %d %016X\n" % (len(partition), fnv1a(text)))
        for name in sorted(values):
            runs = runs_of(values[name])
            out.write("%s %d %016X\n" % (name, len(runs), fnv1a(canonical_text(runs))))
    sys.stderr.write("%s: %d properties, %d partition boundaries\n"
                     % (path, len(values), len(partition)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
