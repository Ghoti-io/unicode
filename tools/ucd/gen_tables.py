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
"""Generate every Unicode table this library ships, from the UCD.

Run it as ``make gen-ucd-tables``; ``make check-ucd-tables`` runs it into a
scratch directory and fails if a committed file differs by a byte.

The shape of what it emits, and why (documentation/design.md section 5.3):

  * **One record per distinct property tuple.** Every enumerated property and
    every binary property of a codepoint is a field of one ``GuniRecord``.
    UCD 17.0.0 has 1,114,112 codepoints and 2,369 distinct records, because
    properties correlate: a codepoint's script almost determines its bidi
    class, its line-break class and its Indic categories. Storing the tuple
    once and pointing at it is what makes every property cost the same
    lookup and keeps the whole thing near 200 KB.

  * **A two-stage trie** maps a codepoint to a record index: stage 1 is
    indexed by ``cp >> 6``, stage 2 by the low six bits, and identical
    64-codepoint blocks are shared. Three dependent loads, no branch. The
    block size is 64 because it measured smallest: 32 costs more stage-1
    entries than it saves in blocks, 128 and 256 lose dedup faster than they
    save index. That measurement is in the commit that added this file.

  * **A run table** - maximal ranges over which the record index is constant
    - is emitted from the same map, and is what ``set.h`` enumerates to
    answer "every codepoint whose script is Greek" without a second copy of
    the data existing (design.md section 4.2, amended: the range lists are
    materialised from the runs rather than committed per value, so the two
    call shapes are not two sources and cannot drift). The sweep test
    asserts the trie and the runs agree at all 1,114,112 codepoints anyway,
    because "cannot drift by construction" is a claim and the claim is
    cheap to check.

  * **Enum values are append-only.** Every generated enum is read back from
    the committed header before it is written, existing members keep their
    values, and a member that the new UCD no longer defines is kept rather
    than dropped. A renumbering would silently change the meaning of a value
    a consumer stored (design.md section 2, M12), so the generator refuses
    to emit one.
"""

import argparse
import os
import re
import sys
from collections import defaultdict

MAX_CODEPOINT = 0x10FFFF
NUM_CODEPOINTS = MAX_CODEPOINT + 1
BLOCK_SHIFT = 6
BLOCK_SIZE = 1 << BLOCK_SHIFT

LICENSE_NOTICE = """\
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
"""


def generated_notice(version, source):
    return (
        "/*\n"
        " * GENERATED FILE - DO NOT EDIT.\n"
        " *\n"
        " * Written by tools/ucd/gen_tables.py from the Unicode Character\n"
        " * Database version %s. Regenerate with \"make gen-ucd-tables\";\n"
        " * \"make check-ucd-tables\" fails if this file and the generator\n"
        " * disagree. Its content is derived from %s.\n"
        " */\n" % (version, source)
    )


# ---------------------------------------------------------------------------
# Reading the UCD
#
# Every file in the UCD is one of three shapes, and these four functions read
# all of them. Fields are ";"-separated, "#" starts a comment, and a first
# field is either "XXXX" or "XXXX..YYYY". The "@missing" lines are comments
# that carry data: they give the value for every codepoint the file does not
# list, and DerivedBidiClass.txt has twenty-nine of them, one per block whose
# unassigned codepoints are right-to-left. Reading those is the difference
# between R and L for every unassigned codepoint in the Hebrew block.
# ---------------------------------------------------------------------------

MISSING_RE = re.compile(r"^#\s*@missing:\s*([0-9A-Fa-f]{4,6})\.\.([0-9A-Fa-f]{4,6})\s*;\s*(.*)$")


def parse_range(field):
    field = field.strip()
    if ".." in field:
        first, last = field.split("..")
        return int(first, 16), int(last, 16)
    value = int(field, 16)
    return value, value


def read_records(path):
    """Yield the ";"-split fields of every data line, comments stripped."""
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            yield [field.strip() for field in line.split(";")]


def read_missing(path):
    """Yield (first, last, value) for every @missing line, in file order."""
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            match = MISSING_RE.match(line.strip())
            if match:
                value = match.group(3).split("#", 1)[0].strip()
                out.append((int(match.group(1), 16), int(match.group(2), 16), value))
    return out


def read_enumerated(path, default, column=1, values_filter=None):
    """Read a range;value file into a list of NUM_CODEPOINTS values.

    @missing lines are applied first, in file order, so that a later one
    overrides an earlier one for the range they share; then the data lines.
    """
    table = [default] * NUM_CODEPOINTS
    for first, last, value in read_missing(path):
        if value.startswith("<"):
            continue  # "<script>", "<code point>": a rule, not a value.
        for cp in range(first, min(last, MAX_CODEPOINT) + 1):
            table[cp] = value
    for fields in read_records(path):
        if len(fields) <= column:
            continue
        value = fields[column]
        if values_filter is not None and value not in values_filter:
            continue
        first, last = parse_range(fields[0])
        for cp in range(first, min(last, MAX_CODEPOINT) + 1):
            table[cp] = value
    return table


def read_binary(path, wanted):
    """Read a range;property file into {property: set-as-bool-list}."""
    out = {name: bytearray(NUM_CODEPOINTS) for name in wanted}
    for fields in read_records(path):
        if len(fields) < 2:
            continue
        name = fields[1]
        if name not in out:
            continue
        first, last = parse_range(fields[0])
        row = out[name]
        for cp in range(first, min(last, MAX_CODEPOINT) + 1):
            row[cp] = 1
    return out


# ---------------------------------------------------------------------------
# The property registry
#
# This is the list design.md section 5.3 promises: adding a property is a new
# row here plus an accessor in char.h, and nothing else. The trie, the runs,
# the enum, the name tables, the sweep fixture and the agreement test all
# read this list.
#
# "key" is the property's short name as PropertyAliases.txt spells it, which
# is also how PropertyValueAliases.txt keys its value aliases. "field" is the
# C struct member; "bits" is how wide that member is, and the generator fails
# if a value does not fit, because a silently truncated property is the kind
# of defect that reads as a Unicode bug for years.
# ---------------------------------------------------------------------------


class EnumProperty:
    def __init__(self, key, filename, default, ctype, prefix, field, bits,
                 accessor, brief, column=1):
        self.key = key
        self.filename = filename
        self.default = default
        self.ctype = ctype
        self.prefix = prefix
        self.field = field
        self.bits = bits
        self.accessor = accessor
        self.brief = brief
        self.column = column
        self.table = None      # filled by load(): cp -> value string
        self.values = None     # filled by assign_enums(): value string -> int
        self.long_names = None # value string -> long alias


ENUM_PROPERTIES = [
    EnumProperty("gc", "DerivedGeneralCategory.txt", "Cn",
                 "GUNI_GeneralCategory", "GUNI_GC", "gc", 8,
                 "guni_general_category", "General_Category"),
    EnumProperty("sc", "Scripts.txt", "Unknown",
                 "GUNI_Script", "GUNI_SCRIPT", "script", 16,
                 "guni_script", "Script"),
    EnumProperty("bc", "DerivedBidiClass.txt", "Left_To_Right",
                 "GUNI_BidiClass", "GUNI_BIDI", "bidi_class", 8,
                 "guni_bidi_class", "Bidi_Class"),
    EnumProperty("ea", "EastAsianWidth.txt", "N",
                 "GUNI_EastAsianWidth", "GUNI_EAW", "eaw", 8,
                 "guni_east_asian_width", "East_Asian_Width"),
    EnumProperty("lb", "LineBreak.txt", "XX",
                 "GUNI_LineBreak", "GUNI_LB", "line_break", 8,
                 "guni_line_break", "Line_Break (unresolved: LB1 is the caller's)"),
    EnumProperty("GCB", "GraphemeBreakProperty.txt", "Other",
                 "GUNI_GraphemeClusterBreak", "GUNI_GCB", "gcb", 8,
                 "guni_grapheme_cluster_break", "Grapheme_Cluster_Break"),
    EnumProperty("WB", "WordBreakProperty.txt", "Other",
                 "GUNI_WordBreak", "GUNI_WB", "wb", 8,
                 "guni_word_break", "Word_Break"),
    EnumProperty("SB", "SentenceBreakProperty.txt", "Other",
                 "GUNI_SentenceBreak", "GUNI_SB", "sb", 8,
                 "guni_sentence_break", "Sentence_Break"),
    EnumProperty("jt", "DerivedJoiningType.txt", "Non_Joining",
                 "GUNI_JoiningType", "GUNI_JT", "joining_type", 8,
                 "guni_joining_type", "Joining_Type"),
    EnumProperty("jg", "DerivedJoiningGroup.txt", "No_Joining_Group",
                 "GUNI_JoiningGroup", "GUNI_JG", "joining_group", 16,
                 "guni_joining_group", "Joining_Group"),
    EnumProperty("InSC", "IndicSyllabicCategory.txt", "Other",
                 "GUNI_IndicSyllabicCategory", "GUNI_INSC", "insc", 8,
                 "guni_indic_syllabic_category", "Indic_Syllabic_Category"),
    EnumProperty("InPC", "IndicPositionalCategory.txt", "Not_Applicable",
                 "GUNI_IndicPositionalCategory", "GUNI_INPC", "inpc", 8,
                 "guni_indic_positional_category", "Indic_Positional_Category"),
    EnumProperty("vo", "VerticalOrientation.txt", "R",
                 "GUNI_VerticalOrientation", "GUNI_VO", "vo", 8,
                 "guni_vertical_orientation", "Vertical_Orientation"),
    EnumProperty("hst", "HangulSyllableType.txt", "Not_Applicable",
                 "GUNI_HangulSyllableType", "GUNI_HST", "hst", 8,
                 "guni_hangul_syllable_type", "Hangul_Syllable_Type"),
    EnumProperty("dt", "DerivedDecompositionType.txt", "None",
                 "GUNI_DecompositionType", "GUNI_DT", "dt", 8,
                 "guni_decomposition_type", "Decomposition_Type"),
    EnumProperty("nt", "DerivedNumericType.txt", "None",
                 "GUNI_NumericType", "GUNI_NT", "nt", 8,
                 "guni_numeric_type", "Numeric_Type"),
]

# Indic_Conjunct_Break and the four quick-check properties are enumerated too,
# but they live inside a file of binary properties rather than one of their
# own: InCB is a three-field line in DerivedCoreProperties.txt and the QC
# properties are three-field lines in DerivedNormalizationProps.txt. They get
# the same treatment through a different reader.
INCB = EnumProperty("InCB", "DerivedCoreProperties.txt", "None",
                    "GUNI_IndicConjunctBreak", "GUNI_INCB", "incb", 8,
                    "guni_indic_conjunct_break", "Indic_Conjunct_Break", column=2)

QUICK_CHECKS = ("NFC_QC", "NFD_QC", "NFKC_QC", "NFKD_QC")

# The binary properties, in the file each is defined by. The order within a
# file does not matter; the enum is numbered by the order of this list, and
# that order is frozen by the committed header from here on.
BINARY_FILES = (
    ("PropList.txt", (
        "ASCII_Hex_Digit", "Bidi_Control", "Dash", "Deprecated", "Diacritic",
        "Extender", "Hex_Digit", "Hyphen", "IDS_Binary_Operator",
        "IDS_Trinary_Operator", "IDS_Unary_Operator", "ID_Compat_Math_Continue",
        "ID_Compat_Math_Start", "Ideographic", "Join_Control",
        "Logical_Order_Exception", "Modifier_Combining_Mark",
        "Noncharacter_Code_Point", "Other_Alphabetic",
        "Other_Default_Ignorable_Code_Point", "Other_Grapheme_Extend",
        "Other_ID_Continue", "Other_ID_Start", "Other_Lowercase", "Other_Math",
        "Other_Uppercase", "Pattern_Syntax", "Pattern_White_Space",
        "Prepended_Concatenation_Mark", "Quotation_Mark", "Radical",
        "Regional_Indicator", "Sentence_Terminal", "Soft_Dotted",
        "Terminal_Punctuation", "Unified_Ideograph", "Variation_Selector",
        "White_Space",
    )),
    ("DerivedCoreProperties.txt", (
        "Alphabetic", "Case_Ignorable", "Cased", "Changes_When_Casefolded",
        "Changes_When_Casemapped", "Changes_When_Lowercased",
        "Changes_When_Titlecased", "Changes_When_Uppercased",
        "Default_Ignorable_Code_Point", "Grapheme_Base", "Grapheme_Extend",
        "Grapheme_Link", "ID_Continue", "ID_Start", "Lowercase", "Math",
        "Uppercase", "XID_Continue", "XID_Start",
    )),
    ("DerivedNormalizationProps.txt", (
        "Changes_When_NFKC_Casefolded", "Full_Composition_Exclusion",
        "Expands_On_NFC", "Expands_On_NFD", "Expands_On_NFKC",
        "Expands_On_NFKD",
    )),
    ("DerivedBinaryProperties.txt", ("Bidi_Mirrored",)),
    ("emoji-data.txt", (
        "Emoji", "Emoji_Component", "Emoji_Modifier", "Emoji_Modifier_Base",
        "Emoji_Presentation", "Extended_Pictographic",
    )),
)

# Binary properties this library derives rather than reads. Each is a rule of
# the Standard rather than a line in a file, and each is one a consumer asked
# for: "Assigned" is what a regex engine's \p{Assigned} is, and "ASCII" and
# "Any" are the two blocks UTS #18 requires by name.
DERIVED_BINARY = ("Any", "ASCII", "Assigned")

# The single-letter General_Category groups. PropertyValueAliases.txt lists
# them beside the real values, distinguished only by a "#"-comment naming the
# values they cover, so they are excluded by name here and emitted as masks
# instead (\p{L} is a mask test, not an equality test).
GC_GROUP_ALIASES = {"C", "L", "LC", "M", "N", "P", "S", "Z"}

GC_GROUPS = {
    "C": ("Cc", "Cf", "Cn", "Co", "Cs"),
    "L": ("Ll", "Lm", "Lo", "Lt", "Lu"),
    "LC": ("Ll", "Lt", "Lu"),
    "M": ("Mc", "Me", "Mn"),
    "N": ("Nd", "Nl", "No"),
    "P": ("Pc", "Pd", "Pe", "Pf", "Pi", "Po", "Ps"),
    "S": ("Sc", "Sk", "Sm", "So"),
    "Z": ("Zl", "Zp", "Zs"),
}


def read_value_aliases(path):
    """{property key: [(short, long, [other aliases]), ...]}, in file order.

    Canonical_Combining_Class is the one property whose lines have four
    fields rather than three, because its values are numbers: the number is
    field 1 and the short and long names are one column further right. The
    file says so in a comment and every parser that ignores the comment reads
    "NR" as ccc's long name.
    """
    out = defaultdict(list)
    for fields in read_records(path):
        if fields[0] == "ccc":
            if len(fields) < 4:
                continue
            out["ccc"].append((fields[2], fields[3], [fields[1]] + [f for f in fields[4:] if f]))
            continue
        if len(fields) < 3:
            continue
        key, short, long_name = fields[0], fields[1], fields[2]
        extra = [f for f in fields[3:] if f]
        out[key].append((short, long_name, extra))
    return out


def read_property_aliases(path):
    """{property key: [every spelling of its name]}, in file order."""
    out = {}
    for fields in read_records(path):
        if len(fields) < 2:
            continue
        out[fields[0]] = [f for f in fields if f]
    return out


# ---------------------------------------------------------------------------
# Enum numbering, which is append-only
# ---------------------------------------------------------------------------

ENUM_BLOCK_RE = re.compile(r"typedef enum \{(.*?)\n\} (GUNI_[A-Za-z]+);", re.S)
ENUM_MEMBER_RE = re.compile(r"^\s*(GUNI_[A-Z0-9_]+)\s*=\s*(\d+)\s*,", re.M)


def c_identifier(text):
    """A value alias as a C identifier fragment: upper case, _ for the rest."""
    out = re.sub(r"[^0-9A-Za-z]+", "_", text).upper().strip("_")
    if out and out[0].isdigit():
        out = "N" + out
    return out


def read_committed_enums(path):
    """{ctype: {member: value}} from an already-generated header.

    This is the record that makes the numbering append-only (M12). It is the
    committed header rather than a side file precisely because the header is
    what a consumer compiled against.
    """
    if not os.path.exists(path):
        return {}
    text = open(path, encoding="utf-8").read()
    out = {}
    for body, ctype in ENUM_BLOCK_RE.findall(text):
        out[ctype] = {m: int(v) for m, v in ENUM_MEMBER_RE.findall(body)}
    return out


class EnumNumbering:
    """Frozen numbering for one generated enum."""

    def __init__(self, ctype, prefix):
        self.ctype = ctype
        self.prefix = prefix
        self.values = {}        # canonical long name -> int
        self.retired = {}       # member -> int, kept from the committed header
        self.order = []         # canonical long names, in numbering order

    def member(self, long_name):
        return "%s_%s" % (self.prefix, c_identifier(long_name))

    def assign(self, ordered_long_names, committed):
        """Give every name a value, keeping every value the header already has."""
        prior = committed.get(self.ctype, {})
        used = set(prior.values())
        by_member = {self.member(name): name for name in ordered_long_names}
        for name in ordered_long_names:
            member = self.member(name)
            if member in prior:
                self.values[name] = prior[member]
        nxt = 0
        for name in ordered_long_names:
            if name in self.values:
                continue
            while nxt in used:
                nxt += 1
            self.values[name] = nxt
            used.add(nxt)
        # A member the committed header has and the new UCD does not is kept,
        # not dropped: a consumer may have stored the value, and Unicode has
        # renamed a property value before (Script=Qaai became Inherited).
        for member, value in sorted(prior.items(), key=lambda kv: kv[1]):
            if member not in by_member:
                self.retired[member] = value
        self.order = sorted(self.values, key=lambda n: self.values[n])
        return self

    def check(self, committed):
        """Fail loudly if the numbering is not one a consumer can rely on.

        What keeps the numbering append-only is that assign() *adopts* every
        value the committed header already gives - so "an existing member
        changed value" is not something this can catch, because it is not
        something that can happen while the header is being read. Saying
        otherwise would be a gate that cannot fail.

        What can happen, and is what this checks:

          * the committed header names one value twice, by a bad hand-edit or
            a merge. Adopting it would silently collapse two property values
            into one - two scripts that compare equal - and every table built
            from it would be wrong in a way no test of the tables could see,
            because the tables would agree with the header;
          * a value too wide for the record field it is stored in, which
            truncates and reads as a Unicode bug for years;
          * a member the new UCD no longer defines, which is kept rather than
            dropped, and is reported so that the reviewer knows.
        """
        collisions = {}
        for name, value in self.values.items():
            collisions.setdefault(value, []).append(self.member(name))
        for value, members in sorted(collisions.items()):
            if len(members) > 1:
                raise SystemExit(
                    "%s: %s all have the value %d. The committed header is "
                    "where this numbering lives (design.md section 2, M12), "
                    "and a duplicate in it collapses two property values into "
                    "one. Fix include/ghoti.io/unicode/enums.h, or delete the "
                    "enum from it to renumber deliberately."
                    % (self.ctype, ", ".join(sorted(members)), value))
        for member, value in self.retired.items():
            if value in collisions:
                raise SystemExit(
                    "%s: retired member %s shares the value %d with %s. A "
                    "retired value is never reused."
                    % (self.ctype, member, value,
                       ", ".join(collisions[value])))

    @property
    def count(self):
        return (max(list(self.values.values()) + list(self.retired.values())) + 1
                if self.values or self.retired else 0)


# ---------------------------------------------------------------------------
# Loading every property
# ---------------------------------------------------------------------------

class Ucd:
    """Every property table this generator reads, keyed the way C wants it."""

    def __init__(self, ucd_dir, version):
        self.dir = ucd_dir
        self.version = version
        self.value_aliases = read_value_aliases(self.path("PropertyValueAliases.txt"))
        self.property_aliases = read_property_aliases(self.path("PropertyAliases.txt"))
        self.canonical = {}   # (key, any alias) -> canonical long name
        self.long_of = {}     # key -> {canonical long: (short, [extra])}
        for key, entries in self.value_aliases.items():
            self.long_of[key] = {}
            for short, long_name, extra in entries:
                for alias in [short, long_name] + extra:
                    self.canonical[(key, alias)] = long_name
                self.long_of[key][long_name] = (short, extra)
        # A second, loosely-keyed view of the same thing, for the one join
        # that cannot be exact.
        #
        # **Blocks are named by two files that spell the long name
        # differently.** Blocks.txt says `0370..03FF; Greek and Coptic` with
        # spaces; PropertyValueAliases.txt says
        # `blk; Greek ; Greek_And_Coptic` with underscores. Everything else
        # here takes both sides of the join from PropertyValueAliases, so an
        # exact lookup works and this view is never reached; for `blk` the
        # left side comes from Blocks.txt and an exact lookup finds nothing.
        #
        # What that cost was every short block alias, all 347 of them, of
        # which 143 differ from the long name - `Greek`, `Greek_Ext`, and
        # `ASCII` for `Basic_Latin`. `guni_value_by_name(BLOCK, "Greek")`
        # was GUNI_ERR_INVALID while `sc=Grek`, `gc=Lu` and `lb=AL` all
        # resolved, and the alias pass that should have caught it *does*
        # cover blocks and had simply never succeeded for one.
        #
        # UAX #44-LM3 says these spellings are one name, so joining them
        # loosely is what the standard asks for rather than a workaround.
        self.long_of_loose = {}
        for key, table in self.long_of.items():
            self.long_of_loose[key] = {
                loose_name(long_name): value for long_name, value in table.items()
            }
        self.properties = []
        self.load()

    def alias_of(self, key, long_name):
        """`(short, [extra])` for one value, joined loosely where it must be.

        Exact first, so that a property whose two sides already agree is
        unaffected and this stays a widening rather than a change.
        """
        table = self.long_of.get(key, {})
        if long_name in table:
            return table[long_name]
        return self.long_of_loose.get(key, {}).get(loose_name(long_name),
                                                   (None, []))

    def path(self, name):
        return os.path.join(self.dir, name)

    def canonicalise(self, key, value):
        return self.canonical.get((key, value), value)

    def load(self):
        for prop in ENUM_PROPERTIES:
            raw = read_enumerated(self.path(prop.filename),
                                  prop.default, column=prop.column)
            prop.table = [self.canonicalise(prop.key, v) for v in raw]
            self.properties.append(prop)

        # InCB and the quick checks: an enumerated property inside a file of
        # binary ones, so the value lives in the third field and every other
        # line in the file has to be ignored.
        for prop, names in ((INCB, ("InCB",)),):
            table = [self.canonicalise(prop.key, prop.default)] * NUM_CODEPOINTS
            for fields in read_records(self.path(prop.filename)):
                if len(fields) < 3 or fields[1] not in names:
                    continue
                first, last = parse_range(fields[0])
                value = self.canonicalise(prop.key, fields[2])
                for cp in range(first, min(last, MAX_CODEPOINT) + 1):
                    table[cp] = value
            prop.table = table
            self.properties.append(prop)

        self.quick_check = {}
        for name in QUICK_CHECKS:
            self.quick_check[name] = ["Yes"] * NUM_CODEPOINTS
        for fields in read_records(self.path("DerivedNormalizationProps.txt")):
            if len(fields) < 3 or fields[1] not in self.quick_check:
                continue
            first, last = parse_range(fields[0])
            value = {"N": "No", "M": "Maybe", "Y": "Yes"}[fields[2]]
            row = self.quick_check[fields[1]]
            for cp in range(first, min(last, MAX_CODEPOINT) + 1):
                row[cp] = value

        # Canonical combining class is a number, not an enum: 0 to 254, with
        # names for the ones that have them. It is stored as the number.
        self.ccc = [0] * NUM_CODEPOINTS
        for fields in read_records(self.path("DerivedCombiningClass.txt")):
            first, last = parse_range(fields[0])
            value = int(fields[1])
            for cp in range(first, min(last, MAX_CODEPOINT) + 1):
                self.ccc[cp] = value

        self.binary = {}
        for filename, names in BINARY_FILES:
            self.binary.update(read_binary(self.path(filename), names))
        assigned = bytearray(NUM_CODEPOINTS)
        gc = self.property("gc").table
        for cp in range(NUM_CODEPOINTS):
            assigned[cp] = 0 if gc[cp] == "Unassigned" else 1
        self.binary["Assigned"] = assigned
        any_cp = bytearray(b"\x01" * NUM_CODEPOINTS)
        self.binary["Any"] = any_cp
        ascii_only = bytearray(NUM_CODEPOINTS)
        for cp in range(0x80):
            ascii_only[cp] = 1
        self.binary["ASCII"] = ascii_only
        self.binary_names = []
        for _filename, names in BINARY_FILES:
            self.binary_names.extend(names)
        self.binary_names.extend(DERIVED_BINARY)

        self.decomposition = self.load_decompositions()
        self.case = self.load_case()
        self.names = self.load_names()
        self.mirroring = self.load_mirroring()
        self.brackets = self.load_brackets()
        self.scx = self.load_script_extensions()
        self.blocks = self.load_blocks()
        self.numeric = self.load_numeric()

    def property(self, key):
        for prop in ENUM_PROPERTIES + [INCB]:
            if prop.key == key:
                return prop
        raise KeyError(key)

    def load_decompositions(self):
        """Full canonical and compatibility decompositions, and the pairs
        that compose.

        Recursive at generation time, so that the library never recurses:
        UnicodeData.txt gives one step and the Standard defines the mapping as
        the fixed point of applying it, so the fixed point is what is stored.
        A decomposition that did not terminate would hang the generator rather
        than the library, which is the right place for it to hang.

        Hangul is not here: Standard section 3.12 gives it as arithmetic over
        11,172 syllables, and a table would be 11,172 entries of something a
        dozen lines of C computes.
        """
        one_step = {}
        compat_tag = {}
        for fields in read_records(self.path("UnicodeData.txt")):
            if len(fields) < 6 or not fields[5]:
                continue
            cp = int(fields[0], 16)
            text = fields[5]
            if text.startswith("<"):
                tag, _sep, rest = text.partition(">")
                compat_tag[cp] = tag[1:]
                one_step[cp] = [int(part, 16) for part in rest.split()]
            else:
                one_step[cp] = [int(part, 16) for part in text.split()]

        canonical = {cp: seq for cp, seq in one_step.items() if cp not in compat_tag}

        def expand(cp, compatibility, seen):
            """The fixed point, with a cycle guard the data should never need."""
            source = one_step if compatibility else canonical
            if cp not in source:
                return [cp]
            if cp in seen:
                raise SystemExit("decomposition of U+%04X is cyclic" % cp)
            out = []
            for part in source[cp]:
                out.extend(expand(part, compatibility, seen | {cp}))
            return out

        nfd = {}
        nfkd = {}
        for cp in sorted(one_step):
            if cp in canonical:
                full = expand(cp, False, set())
                if full != [cp]:
                    nfd[cp] = full
            full = expand(cp, True, set())
            if full != [cp]:
                nfkd[cp] = full

        # Composition: a canonical decomposition of exactly two codepoints,
        # whose codepoint is not excluded. Singletons - one codepoint -
        # never compose, which is what Full_Composition_Exclusion's
        # "singleton decomposition" clause says, and the derived property
        # already includes them.
        excluded = read_binary(self.path("DerivedNormalizationProps.txt"),
                               ("Full_Composition_Exclusion",))
        exclusion = excluded["Full_Composition_Exclusion"]
        pairs = {}
        for cp, seq in canonical.items():
            if len(seq) != 2 or exclusion[cp]:
                continue
            pairs[(seq[0], seq[1])] = cp
        return {"nfd": nfd, "nfkd": nfkd, "pairs": pairs, "tag": compat_tag}

    def load_case(self):
        """Simple mappings, full mappings, the conditions, and the fold orbits.

        Four files' worth of one subject. The simple mappings are three fields
        of UnicodeData.txt; the folds are CaseFolding.txt's C, F, S and T
        statuses; the full mappings and every condition are SpecialCasing.txt.
        The conditions are the part every toupper() gets wrong (design.md
        section 2, M13) and there are only sixteen lines of them, which is why
        they are a table here and rules in case.c rather than anything cleverer.
        """
        simple = {}
        for fields in read_records(self.path("UnicodeData.txt")):
            if len(fields) < 15:
                continue
            cp = int(fields[0], 16)
            upper = int(fields[12], 16) if fields[12] else 0
            lower = int(fields[13], 16) if fields[13] else 0
            title = int(fields[14], 16) if fields[14] else 0
            if upper or lower or title:
                simple[cp] = {"upper": upper, "lower": lower, "title": title}

        folds = {}
        full_folds = {}
        turkic_folds = {}
        for fields in read_records(self.path("CaseFolding.txt")):
            if len(fields) < 3:
                continue
            cp = int(fields[0], 16)
            status = fields[1]
            mapping = [int(part, 16) for part in fields[2].split()]
            if status == "C":
                folds[cp] = mapping[0]
                if len(mapping) != 1:
                    raise SystemExit("common fold of U+%04X is not one codepoint" % cp)
            elif status == "S":
                folds[cp] = mapping[0]
            elif status == "F":
                full_folds[cp] = mapping
            elif status == "T":
                turkic_folds[cp] = mapping

        full = {}       # cp -> {upper, lower, title} as sequences
        conditional = []  # (cp, language, condition, upper, lower, title)
        for line in open(self.path("SpecialCasing.txt"), encoding="utf-8"):
            body = line.split("#", 1)[0].strip()
            if not body:
                continue
            fields = [field.strip() for field in body.split(";")]
            if len(fields) < 4:
                continue
            cp = int(fields[0], 16)
            lower = [int(part, 16) for part in fields[1].split()]
            title = [int(part, 16) for part in fields[2].split()]
            upper = [int(part, 16) for part in fields[3].split()]
            condition = fields[4] if len(fields) > 4 and fields[4] else ""
            if condition:
                words = condition.split()
                language = words[0] if words[0] in ("lt", "tr", "az") else ""
                rule = " ".join(words[1:] if language else words)
                conditional.append((cp, language, rule, upper, lower, title))
            else:
                full[cp] = {"upper": upper, "lower": lower, "title": title}

        # The fold orbits: every codepoint that folds to the same value, which
        # is what a case-insensitive character class needs. The value itself is
        # a member of its own orbit when it folds to itself.
        orbit_of = {}
        for cp in range(NUM_CODEPOINTS):
            value = folds.get(cp, cp)
            orbit_of.setdefault(value, set()).add(cp)
            orbit_of[value].add(value)
        orbits = {value: sorted(members) for value, members in orbit_of.items()
                  if len(members) > 1}
        return {"simple": simple, "folds": folds, "full_folds": full_folds,
                "turkic_folds": turkic_folds, "full": full,
                "conditional": conditional, "orbits": orbits}

    def load_names(self):
        """Every name, alias and named sequence, and the algorithmic families.

        Four sources and one rule:

        - `UnicodeData.txt` field 1, less the `<...>` rows. Those are range
          endpoints and control characters: the ranges are algorithmic and the
          controls have no name of their own, only aliases.
        - `NameAliases.txt`, all five types, with their types kept - a caller
          asking "what is U+0000 called" wants NULL, and a caller asking what
          kind of name that is wants to know it is a control alias.
        - `NamedSequences.txt`, which are names for sequences and so cannot go
          in the same table as names for codepoints.
        - `Jamo.txt`, for the Hangul syllable names, which are computed.

        The algorithmic families are read from the `<Label, First>` /
        `<Label, Last>` pairs rather than written down, so that a new CJK
        extension block arrives with the next regeneration instead of being
        noticed later. Surrogates and private use are in the same shape and
        are deliberately absent: they have no names at all.
        """
        names = []
        seen = {}

        def add(name, codepoint):
            # A name that resolved to two codepoints would make the reverse
            # lookup arbitrary, so it is an error rather than last-one-wins.
            if name in seen:
                if seen[name] != codepoint:
                    raise SystemExit("name %r maps to both U+%04X and U+%04X"
                                     % (name, seen[name], codepoint))
                return
            seen[name] = codepoint
            names.append((name, codepoint))

        primary = {}
        for fields in read_records(self.path("UnicodeData.txt")):
            if len(fields) < 2 or not fields[1] or fields[1].startswith("<"):
                continue
            cp = int(fields[0], 16)
            primary[cp] = fields[1]
            add(fields[1], cp)

        aliases = []
        for fields in read_records(self.path("NameAliases.txt")):
            if len(fields) < 3:
                continue
            cp = int(fields[0], 16)
            aliases.append((cp, fields[1], fields[2]))
            add(fields[1], cp)

        sequences = []
        for fields in read_records(self.path("NamedSequences.txt")):
            if len(fields) < 2:
                continue
            sequences.append((fields[0],
                              [int(part, 16) for part in fields[1].split()]))

        jamo = {}
        for fields in read_records(self.path("Jamo.txt")):
            if len(fields) < 2:
                continue
            jamo[int(fields[0], 16)] = fields[1]

        labels = {
            "CJK Ideograph": "CJK UNIFIED IDEOGRAPH-",
            "Tangut Ideograph": "TANGUT IDEOGRAPH-",
            "Hangul Syllable": "HANGUL SYLLABLE ",
        }
        ranges = []
        first = None
        for fields in read_records(self.path("UnicodeData.txt")):
            if len(fields) < 2 or not fields[1].startswith("<"):
                continue
            label = fields[1].strip("<>")
            if label.endswith(", First"):
                first = (int(fields[0], 16), label[: -len(", First")])
                continue
            if not label.endswith(", Last") or first is None:
                continue
            start, stem_name = first
            first = None
            for stem, prefix in labels.items():
                if stem_name == stem or stem_name.startswith(stem + " "):
                    ranges.append((start, int(fields[0], 16), prefix,
                                   stem == "Hangul Syllable"))
                    break
        ranges.sort()

        names.sort(key=lambda pair: pair[0].encode("ascii"))
        return {"names": names, "primary": primary, "aliases": aliases,
                "sequences": sequences, "jamo": jamo, "ranges": ranges}

    def load_mirroring(self):
        """Bidi_Mirroring_Glyph: what UAX #9's rule L4 substitutes."""
        out = {}
        for fields in read_records(self.path("BidiMirroring.txt")):
            if len(fields) < 2 or not fields[1]:
                continue
            out[int(fields[0], 16)] = int(fields[1], 16)
        return out

    def load_brackets(self):
        """BidiBrackets.txt: the paired bracket and whether it opens or closes.

        BD14 and BD15, which rule N0 reads. The canonical-equivalence clause -
        U+2329 is to be treated as U+3008 - is not applied here: it is applied
        in bidi.c through the decomposition table, because doing it in the
        table would lose the distinction a caller may want to see.
        """
        out = {}
        for fields in read_records(self.path("BidiBrackets.txt")):
            if len(fields) < 3:
                continue
            out[int(fields[0], 16)] = (int(fields[1], 16), fields[2])
        return out

    def load_script_extensions(self):
        """cp -> tuple of canonical script long names, defaulting to Script.

        UAX #24: the default is the codepoint's own Script value, so every
        codepoint has a non-empty set and no caller has to special-case the
        absence of one.
        """
        script = self.property("sc").table
        explicit = [None] * NUM_CODEPOINTS
        for fields in read_records(self.path("ScriptExtensions.txt")):
            first, last = parse_range(fields[0])
            names = tuple(sorted(self.canonicalise("sc", token)
                                 for token in fields[1].split()))
            for cp in range(first, min(last, MAX_CODEPOINT) + 1):
                explicit[cp] = names
        return [explicit[cp] or (script[cp],) for cp in range(NUM_CODEPOINTS)]

    def load_blocks(self):
        out = []
        for fields in read_records(self.path("Blocks.txt")):
            first, last = parse_range(fields[0])
            out.append((first, last, fields[1]))
        return out

    def load_numeric(self):
        """(first, last, numerator, denominator) runs, sorted."""
        entries = []
        for fields in read_records(self.path("DerivedNumericValues.txt")):
            first, last = parse_range(fields[0])
            # Field 3 is the exact rational ("-1/2", "1000000000000"); field
            # 1 is a decimal rendering of it and is not used, because
            # parsing "1000000000000.0" as a float and truncating is how a
            # value above 2**53 loses its last digits silently.
            text = fields[3] if len(fields) > 3 and fields[3] else fields[1]
            if "/" in text:
                num, den = text.split("/")
                numerator, denominator = int(num), int(den)
            else:
                numerator, denominator = int(text.split(".")[0]), 1
                if text.split(".")[1:] not in ([], ["0"]):
                    raise SystemExit("non-integral numeric value %r" % text)
            entries.append((first, last, numerator, denominator))
        entries.sort()
        return entries


# ---------------------------------------------------------------------------
# Building the records, the trie and the runs
# ---------------------------------------------------------------------------

QC_ORDER = ("NFC_QC", "NFD_QC", "NFKC_QC", "NFKD_QC")
QC_VALUES = {"Yes": 0, "No": 1, "Maybe": 2}


class Tables:
    def __init__(self, ucd, committed):
        self.ucd = ucd
        self.version = ucd.version
        self.numbering = {}
        self.assign_enums(committed)
        self.build_scx_pool()
        self.build_records()
        self.build_trie()
        self.build_runs()
        self.build_decompositions()
        self.build_case()
        self.build_script_runs()
        self.build_names()

    # -- enums -------------------------------------------------------------

    def assign_enums(self, committed):
        for prop in self.ucd.properties:
            present = sorted({v for v in prop.table})
            known = [long_name for long_name in self.ucd.long_of.get(prop.key, {})
                     if not (prop.key == "gc"
                             and self.ucd.long_of[prop.key][long_name][0] in GC_GROUP_ALIASES)]
            for value in present:
                if value not in known:
                    known.append(value)
            default_long = self.ucd.canonicalise(prop.key, prop.default)
            ordered = ([default_long] +
                       sorted(name for name in known if name != default_long))
            numbering = EnumNumbering(prop.ctype, prop.prefix).assign(ordered, committed)
            numbering.check(committed)
            self.numbering[prop.ctype] = numbering
            prop.values = numbering.values
            limit = 1 << prop.bits
            for name, value in numbering.values.items():
                if value >= limit:
                    raise SystemExit(
                        "%s: %s = %d does not fit in the %d-bit record field %r. "
                        "Widen the field; a truncated property reads as a "
                        "Unicode bug." % (prop.ctype, numbering.member(name),
                                          value, prop.bits, prop.field))

        block_names = [name for _f, _l, name in self.ucd.blocks]
        numbering = EnumNumbering("GUNI_Block", "GUNI_BLOCK")
        numbering.assign(["No_Block"] + block_names, committed)
        numbering.check(committed)
        self.numbering["GUNI_Block"] = numbering
        self.block_numbering = numbering

        numbering = EnumNumbering("GUNI_BinaryProperty", "GUNI_PROP")
        numbering.assign(list(self.ucd.binary_names), committed)
        numbering.check(committed)
        self.numbering["GUNI_BinaryProperty"] = numbering
        self.binary_numbering = numbering
        if numbering.count > 96:
            raise SystemExit("more than 96 binary properties: widen "
                             "GuniPropRecord.flags and the GUNI_PROP_WORDS "
                             "macro together.")

    # -- Script_Extensions -------------------------------------------------

    def build_scx_pool(self):
        """One shared pool of sets; identical sets are stored once."""
        script = self.ucd.property("sc")
        pool = []
        offsets = {}
        for cp in range(NUM_CODEPOINTS):
            names = self.ucd.scx[cp]
            if names in offsets:
                continue
            offsets[names] = len(pool)
            pool.append(len(names))
            pool.extend(sorted(script.values[name] for name in names))
        if len(pool) > 0xFFFF:
            raise SystemExit("Script_Extensions pool exceeds a uint16 offset")
        self.scx_pool = pool
        self.scx_offsets = offsets

    # -- records -----------------------------------------------------------

    def build_records(self):
        ucd = self.ucd
        props = ucd.properties
        flag_words = (self.binary_numbering.count + 31) // 32
        self.flag_words = flag_words
        binary_bits = [(ucd.binary[name], self.binary_numbering.values[name])
                       for name in ucd.binary_names]
        records = {}
        index = [0] * NUM_CODEPOINTS
        for cp in range(NUM_CODEPOINTS):
            fields = tuple(prop.values[prop.table[cp]] for prop in props)
            qc = 0
            for shift, name in enumerate(QC_ORDER):
                qc |= QC_VALUES[ucd.quick_check[name][cp]] << (2 * shift)
            flags = [0] * flag_words
            for row, bit in binary_bits:
                if row[cp]:
                    flags[bit >> 5] |= 1 << (bit & 31)
            key = fields + (ucd.ccc[cp], qc, self.scx_offsets[ucd.scx[cp]]) + tuple(flags)
            slot = records.get(key)
            if slot is None:
                slot = len(records)
                records[key] = slot
            index[cp] = slot
        if len(records) > 0xFFFF:
            raise SystemExit("more than 65535 distinct property records: "
                             "widen the trie's value type")
        self.records = list(records)
        self.index = index

    # -- trie --------------------------------------------------------------

    def build_trie(self):
        blocks = {}
        stage1 = []
        for base in range(0, NUM_CODEPOINTS, BLOCK_SIZE):
            block = tuple(self.index[base:base + BLOCK_SIZE])
            slot = blocks.get(block)
            if slot is None:
                slot = len(blocks)
                blocks[block] = slot
            stage1.append(slot)
        self.stage1 = stage1
        self.stage2 = [value for block in blocks for value in block]

    # -- decompositions ----------------------------------------------------

    def build_decompositions(self):
        """One shared pool, and a sorted index into it.

        A sorted array with a binary search rather than a trie, because the
        lookup is already gated: Decomposition_Type is a field of the record,
        so a codepoint with no decomposition - which is 1,108,000 of them -
        never reaches the search at all. Only the 6,000 that have one pay 13
        iterations, and they pay it once per codepoint per normalisation and
        not per glyph per frame.

        Sequences are shared: a codepoint whose canonical and compatibility
        decompositions are equal - most of them - stores one.
        """
        data = self.ucd.decomposition
        pool = []
        offsets = {}

        def intern(sequence):
            key = tuple(sequence)
            if key not in offsets:
                offsets[key] = len(pool)
                pool.extend(key)
            return offsets[key]

        codepoints = sorted(set(data["nfd"]) | set(data["nfkd"]))
        rows = []
        for cp in codepoints:
            nfd = data["nfd"].get(cp)
            nfkd = data["nfkd"].get(cp)
            rows.append((
                cp,
                intern(nfd) if nfd else 0, len(nfd) if nfd else 0,
                intern(nfkd) if nfkd else 0, len(nfkd) if nfkd else 0))
        if len(pool) > 0xFFFF:
            raise SystemExit("decomposition pool exceeds a uint16 offset")
        self.decomp_pool = pool
        self.decomp_rows = rows
        self.compose = sorted((first, second, composite)
                              for (first, second), composite
                              in self.ucd.decomposition["pairs"].items())
        self.max_expansion = {
            "NFD": max((len(v) for v in data["nfd"].values()), default=1),
            "NFKD": max((len(v) for v in data["nfkd"].values()), default=1),
        }
        # NFC and NFKC can leave marks the composition could not absorb, so
        # their bound is the decomposed bound: composition never grows a
        # sequence, and a caller sizing a buffer from these is safe for the
        # intermediate as well as the result.
        self.max_expansion["NFC"] = self.max_expansion["NFD"]
        self.max_expansion["NFKC"] = self.max_expansion["NFKD"]
        witness = max(data["nfkd"], key=lambda cp: len(data["nfkd"][cp]))
        self.max_expansion_witness = witness

    # -- case --------------------------------------------------------------

    def build_case(self):
        """One sorted table over every codepoint with any case data.

        Gated at run time by Changes_When_Casemapped or
        Changes_When_Casefolded, which are bits of the property record, so a
        codepoint with no case data costs one trie lookup and no search.
        """
        case = self.ucd.case
        pool = []
        offsets = {}

        def intern(sequence):
            key = tuple(sequence)
            if not key:
                return (0, 0)
            if key not in offsets:
                offsets[key] = len(pool)
                pool.extend(key)
            return (offsets[key], len(key))

        codepoints = sorted(set(case["simple"]) | set(case["folds"])
                            | set(case["full"]) | set(case["full_folds"]))
        rows = []
        for cp in codepoints:
            simple = case["simple"].get(cp, {})
            full = case["full"].get(cp, {})
            simple_values = [
                simple.get("upper", 0), simple.get("lower", 0),
                simple.get("title", 0), case["folds"].get(cp, 0)]
            full_values = [
                intern(full.get("upper", [])), intern(full.get("lower", [])),
                intern(full.get("title", [])),
                intern(case["full_folds"].get(cp, []))]
            rows.append((cp, simple_values, full_values))
        self.case_rows = rows

        conditional = []
        for cp, language, condition, upper, lower, title in case["conditional"]:
            if condition not in CASE_CONDITIONS:
                raise SystemExit(
                    "SpecialCasing.txt condition %r is not one case.c knows. "
                    "Add it to CASE_CONDITIONS here and to the switch in "
                    "case.c; a dropped condition is a case mapping that is "
                    "wrong in one language and right everywhere else."
                    % condition)
            if language not in CASE_LANGUAGES:
                raise SystemExit("unknown SpecialCasing language %r" % language)
            conditional.append((cp, CASE_LANGUAGES[language],
                                CASE_CONDITIONS[condition], intern(upper),
                                intern(lower), intern(title)))
        self.case_conditional = conditional

        self.turkic_folds = sorted(
            (cp, mapping[0]) for cp, mapping in case["turkic_folds"].items()
            if len(mapping) == 1)
        if len(self.turkic_folds) != len(case["turkic_folds"]):
            raise SystemExit("a Turkic fold is more than one codepoint")

        orbit_pool = []
        orbit_rows = []
        for value in sorted(case["orbits"]):
            members = case["orbits"][value]
            orbit_rows.append((value, len(orbit_pool), len(members)))
            orbit_pool.extend(members)
        self.orbit_rows = orbit_rows
        self.orbit_pool = orbit_pool

        widest = 1
        for cp, _simple, full in rows:
            for _offset, length in full:
                widest = max(widest, length)
        for entry in conditional:
            for _offset, length in entry[3:]:
                widest = max(widest, length)
        self.case_max_expansion = widest
        if len(pool) > 0xFFFF or len(orbit_pool) > 0xFFFF:
            raise SystemExit("a case pool exceeds a uint16 offset")
        self.case_pool = pool

    # -- names (tier 1) ----------------------------------------------------

    def build_names(self):
        """Word-dictionary encoding of the names, and the reverse index.

        Unicode names are a small vocabulary repeated endlessly - 18,349
        distinct words across 40,951 names and 1,044,804 bytes of text - so
        storing the words once and the names as word numbers costs about a
        third of what storing the strings costs.

        A token is a word number in the low 15 bits and, in bit 15, the
        separator that *precedes* it: set for "-" and clear for a space. The
        first token of a name has no separator and the bit is clear. Two
        separators are enough because no Unicode name contains anything else,
        which is checked here rather than assumed - a name with an apostrophe
        would silently lose it.

        The design is regex's, with its round-trip check: the encoding is only
        useful if it decodes back, and the sort order the lookup relies on is
        the order of the original strings, so a lossy encoding does not merely
        lose the name it mangled - it invalidates the search for its
        neighbours.
        """
        data = self.ucd.names
        vocabulary = {}
        order = []

        def word_number(word):
            if word not in vocabulary:
                vocabulary[word] = len(order)
                order.append(word)
            return vocabulary[word]

        tokens = []
        offsets = []
        codepoints = []
        for name, codepoint in data["names"]:
            for character in name:
                if not (character.isupper() or character.isdigit()
                        or character in " -"):
                    raise SystemExit("name %r has an unexpected character %r"
                                     % (name, character))
            offsets.append(len(tokens))
            hyphen = False
            for piece in re.split(r"([ -])", name):
                if piece == " ":
                    hyphen = False
                    continue
                if piece == "-":
                    hyphen = True
                    continue
                # An empty piece is what re.split yields between two adjacent
                # separators, and nineteen Unicode names have a pair - the UCD
                # spells U+11A0A "ZANABAZAR SQUARE LETTER -A". Skipping them
                # would drop one separator of the two.
                number = word_number(piece)
                if number >= 0x8000:
                    raise SystemExit("more than 32767 distinct words in names")
                tokens.append(number | (0x8000 if hyphen else 0))
                hyphen = False
            codepoints.append(codepoint)
        offsets.append(len(tokens))

        for index, (name, _codepoint) in enumerate(data["names"]):
            decoded = []
            for position in range(offsets[index], offsets[index + 1]):
                token = tokens[position]
                if position != offsets[index]:
                    decoded.append("-" if token & 0x8000 else " ")
                decoded.append(order[token & 0x7FFF])
            if "".join(decoded) != name:
                raise SystemExit("name %r encodes to %r"
                                 % (name, "".join(decoded)))

        self.name_words = order
        self.name_tokens = tokens
        self.name_offsets = offsets
        self.name_codepoints = codepoints
        self.name_longest = max(len(name) for name, _cp in data["names"])

        # The reverse index: the entries in loose-name order, so that a lookup
        # is a binary search. Kept as a permutation rather than a second copy
        # of the names.
        loose_of = [loose_name(name) for name, _cp in data["names"]]
        collisions = {}
        for index, key in enumerate(loose_of):
            if key in collisions:
                other = collisions[key]
                if codepoints[other] != codepoints[index]:
                    raise SystemExit(
                        "loose matching makes %r and %r the same name, for "
                        "U+%04X and U+%04X. UAX #44-LM2 has one exception and "
                        "it is already applied; a second one needs a decision."
                        % (data["names"][other][0], data["names"][index][0],
                           codepoints[other], codepoints[index]))
            collisions[key] = index
        self.name_loose_order = sorted(range(len(loose_of)),
                                       key=lambda index: loose_of[index])

        # The primary names, by codepoint, for the forward lookup.
        primary = sorted(data["primary"])
        index_by_name = {name: index for index, (name, _cp)
                         in enumerate(data["names"])}
        self.name_primary_cp = primary
        self.name_primary_index = [index_by_name[data["primary"][cp]]
                                   for cp in primary]

        # The aliases, by codepoint, with their kinds.
        kinds = ("correction", "control", "alternate", "figment",
                 "abbreviation")
        alias_rows = []
        for cp, name, kind in data["aliases"]:
            if kind not in kinds:
                raise SystemExit(
                    "NameAliases.txt type %r is not one name.c knows; add it "
                    "to GUNI_NameAliasKind and to this list together." % kind)
            alias_rows.append((cp, kinds.index(kind), index_by_name[name]))
        alias_rows.sort()
        self.name_aliases = alias_rows

        # The named sequences: names for sequences, so a table of their own.
        sequence_tokens = []
        sequence_offsets = []
        sequence_pool = []
        sequence_pool_offsets = []
        sequence_names = sorted(data["sequences"], key=lambda row: loose_name(row[0]))
        for name, points in sequence_names:
            sequence_offsets.append(len(sequence_tokens))
            hyphen = False
            for piece in re.split(r"([ -])", name):
                if piece == " ":
                    hyphen = False
                    continue
                if piece == "-":
                    hyphen = True
                    continue
                sequence_tokens.append(word_number(piece)
                                       | (0x8000 if hyphen else 0))
                hyphen = False
            sequence_pool_offsets.append(len(sequence_pool))
            sequence_pool.extend(points)
        sequence_offsets.append(len(sequence_tokens))
        sequence_pool_offsets.append(len(sequence_pool))
        self.sequence_tokens = sequence_tokens
        self.sequence_offsets = sequence_offsets
        self.sequence_pool = sequence_pool
        self.sequence_pool_offsets = sequence_pool_offsets
        self.sequence_longest = max((len(points) for _n, points in sequence_names),
                                    default=0)

        # The algorithmic families, and the jamo short names the Hangul rule
        # spells syllables with.
        self.name_ranges = data["ranges"]
        jamo = data["jamo"]
        self.jamo_lead = [jamo.get(0x1100 + index, "") for index in range(19)]
        self.jamo_vowel = [jamo.get(0x1161 + index, "") for index in range(21)]
        self.jamo_trail = [""] + [jamo.get(0x11A7 + index, "")
                                  for index in range(1, 28)]

    # -- script runs -------------------------------------------------------

    def build_script_runs(self):
        """One augmented bitset per distinct Script_Extensions set.

        Indexed by the set's offset in the Script_Extensions pool, which is
        already a field of the property record - so a script-run check is the
        record lookup it was going to do anyway plus one array index, with no
        second search.
        """
        script = self.ucd.property("sc")
        total = script.values and max(script.values.values()) + 1
        virtual_base = total
        bits = virtual_base + len(VIRTUAL_SCRIPTS)
        words = (bits + 63) // 64
        self.script_run_words = words
        self.script_run_bits = bits

        sets = []
        index_of = {}
        by_offset = [0] * len(self.scx_pool)
        for names, offset in self.scx_offsets.items():
            members = set(names)
            for name in names:
                for extra in SCRIPT_AUGMENTATIONS.get(name, ()):
                    members.add(extra)
            mask = [0] * words
            for name in sorted(members):
                if name in VIRTUAL_SCRIPTS:
                    bit = virtual_base + VIRTUAL_SCRIPTS.index(name)
                else:
                    bit = script.values[name]
                mask[bit // 64] |= 1 << (bit % 64)
            key = tuple(mask)
            if key not in index_of:
                index_of[key] = len(sets)
                sets.append(mask)
            by_offset[offset] = index_of[key]
        self.script_run_sets = sets
        self.script_run_by_offset = by_offset

        # The decimal-digit blocks: each is ten consecutive codepoints, which
        # the UCD has always arranged and which is asserted rather than assumed
        # - a version that stopped doing it would otherwise produce a table
        # that mis-groups digits silently.
        gc = self.ucd.property("gc").table
        zeros = []
        cp = 0
        while cp <= MAX_CODEPOINT:
            if gc[cp] != "Decimal_Number":
                cp += 1
                continue
            numeric = None
            for first, last, numerator, denominator in self.ucd.numeric:
                if first <= cp <= last:
                    numeric = (numerator, denominator)
                    break
            if numeric != (0, 1):
                cp += 1
                continue
            for offset in range(10):
                if cp + offset > MAX_CODEPOINT \
                        or gc[cp + offset] != "Decimal_Number":
                    raise SystemExit(
                        "the decimal digits at U+%04X are not ten consecutive "
                        "Nd codepoints; the script-run digit rule assumes they "
                        "are" % cp)
            zeros.append(cp)
            cp += 10
        self.digit_zeros = zeros

    # -- runs --------------------------------------------------------------

    def build_runs(self):
        runs = []
        previous = None
        for cp in range(NUM_CODEPOINTS):
            if self.index[cp] != previous:
                previous = self.index[cp]
                runs.append((cp, previous))
        self.runs = runs


# ---------------------------------------------------------------------------
# Every property, as something addressable by name
#
# set.h answers "which codepoints have Script=Greek" and takes the property
# and the value as enum values a caller can also look up by name. That needs
# one enum over *all* properties, including the ones that are not a field of
# the record - Block is a range table, Canonical_Combining_Class is a number,
# Script_Extensions is a set - so each carries a "kind" saying how its value
# is got at, and the generic extractor in set.c switches on the kind rather
# than on the property. Adding a property does not touch that switch.
# ---------------------------------------------------------------------------

# UTS #39 section 5.1's augmentation, which makes a three-way script mix fall
# out of an ordinary set intersection.
#
# Han is written alongside Hiragana and Katakana in Japanese, alongside Hangul
# in Korean, and alongside Bopomofo in Taiwanese Mandarin - but a string mixing
# Hangul, Bopomofo and Han is none of the three. Adding Han to Hiragana's set
# and Hiragana to Han's would make that string a run, because each pair would
# intersect. UTS #39 instead invents three scripts that the participating
# characters all name, so the three-way case needs no rule of its own.
#
# They are not UCD script values and are deliberately not members of
# GUNI_Script: they live only in the script-run bitsets, as the three bits
# above the real scripts.
SCRIPT_AUGMENTATIONS = {
    "Han": ("Japanese", "Korean", "HanBopomofo"),
    "Hiragana": ("Japanese",),
    "Katakana": ("Japanese",),
    "Hangul": ("Korean",),
    "Bopomofo": ("HanBopomofo",),
}

VIRTUAL_SCRIPTS = ("Japanese", "Korean", "HanBopomofo")

# SpecialCasing.txt's conditions, which case.c implements. A condition the
# UCD adds and this list does not know stops the generator: silently dropping
# one would be a case mapping that is wrong in one language and right
# everywhere else, which is the hardest kind of defect to notice.
CASE_CONDITIONS = {
    "": "GUNI_CASE_COND_NONE",
    "Final_Sigma": "GUNI_CASE_COND_FINAL_SIGMA",
    "After_Soft_Dotted": "GUNI_CASE_COND_AFTER_SOFT_DOTTED",
    "More_Above": "GUNI_CASE_COND_MORE_ABOVE",
    "After_I": "GUNI_CASE_COND_AFTER_I",
    "Not_Before_Dot": "GUNI_CASE_COND_NOT_BEFORE_DOT",
}

CASE_LANGUAGES = {
    "": "GUNI_LANG_NONE",
    "tr": "GUNI_LANG_TURKIC",
    "az": "GUNI_LANG_TURKIC",
    "lt": "GUNI_LANG_LITHUANIAN",
}

KIND_ENUM = "GUNI_PROP_KIND_ENUM"
KIND_BINARY = "GUNI_PROP_KIND_BINARY"
KIND_CCC = "GUNI_PROP_KIND_CCC"
KIND_BLOCK = "GUNI_PROP_KIND_BLOCK"
KIND_SCX = "GUNI_PROP_KIND_SCX"
KIND_QC = "GUNI_PROP_KIND_QC"

KINDS = (KIND_ENUM, KIND_BINARY, KIND_CCC, KIND_BLOCK, KIND_SCX, KIND_QC)

EXTRA_PROPERTIES = (
    # key, long name, kind, the C type of its values
    ("blk", "Block", KIND_BLOCK, "GUNI_Block"),
    ("ccc", "Canonical_Combining_Class", KIND_CCC, "uint8_t"),
    ("scx", "Script_Extensions", KIND_SCX, "GUNI_Script"),
    ("NFC_QC", "NFC_Quick_Check", KIND_QC, "GUNI_QuickCheck"),
    ("NFD_QC", "NFD_Quick_Check", KIND_QC, "GUNI_QuickCheck"),
    ("NFKC_QC", "NFKC_Quick_Check", KIND_QC, "GUNI_QuickCheck"),
    ("NFKD_QC", "NFKD_Quick_Check", KIND_QC, "GUNI_QuickCheck"),
)


class PropertyEntry:
    """One row of the table set.h looks properties up in."""

    def __init__(self, key, long_name, kind, ctype, prop=None, bit=None,
                 qc_index=None):
        self.key = key
        self.long_name = long_name
        self.kind = kind
        self.ctype = ctype
        self.prop = prop            # the EnumProperty, for KIND_ENUM
        self.bit = bit              # the flag bit, for KIND_BINARY
        self.qc_index = qc_index    # 0..3, for KIND_QC
        self.value_names = []       # value -> long name ("" where none)
        self.aliases = []           # (loose name, value)


def build_property_entries(ucd, tables):
    entries = []
    for prop in ucd.properties:
        entry = PropertyEntry(prop.key, ucd.property_aliases[prop.key][-1],
                              KIND_ENUM, prop.ctype, prop=prop)
        numbering = tables.numbering[prop.ctype]
        entry.value_names = [""] * numbering.count
        for name, value in numbering.values.items():
            entry.value_names[value] = name
        entries.append(entry)
    for key, long_name, kind, ctype in EXTRA_PROPERTIES:
        entry = PropertyEntry(key, long_name, kind, ctype,
                              qc_index=(QC_ORDER.index(key) if kind == KIND_QC else None))
        if kind == KIND_BLOCK:
            entry.value_names = [""] * tables.block_numbering.count
            for name, value in tables.block_numbering.values.items():
                entry.value_names[value] = name
        elif kind == KIND_CCC:
            entry.value_names = [""] * 255
            for short, long_alias, extra in ucd.value_aliases.get("ccc", []):
                number = int(extra[0])
                if number < 255:
                    entry.value_names[number] = long_alias
        elif kind == KIND_SCX:
            script = tables.numbering["GUNI_Script"]
            entry.value_names = [""] * script.count
            for name, value in script.values.items():
                entry.value_names[value] = name
        elif kind == KIND_QC:
            entry.value_names = ["Yes", "No", "Maybe"]
        entries.append(entry)
    for name in ucd.binary_names:
        entry = PropertyEntry(name, name, KIND_BINARY, "bool",
                              bit=tables.binary_numbering.values[name])
        entry.value_names = ["No", "Yes"]
        entries.append(entry)
    return entries


def loose_name(name):
    """UAX #44-LM2: ignore case, whitespace, underscores and **medial** hyphens.

    Three things have to be right at once, and they are measured against two
    different strings, which is why this is longer than the rule sounds:

    - **"medial" is judged on the original name.** A hyphen with a space or an
      underscore beside it is not medial. Nineteen names have one - U+11C88 is
      MARCHEN LETTER -A - and dropping it makes MARCHEN LETTER A, which is
      U+11C8F. Judging medial-ness after the spaces are gone collapses them.
    - **the exception is judged on the folded form.** The Standard keeps the
      hyphen in U+1180 HANGUL JUNGSEONG O-E, because U+116C is HANGUL
      JUNGSEONG OE - and a caller may hand over a name that is already folded,
      so the comparison cannot depend on the spaces still being there.
    - an underscore counts as whitespace, which is what makes
      `LATIN_CAPITAL_LETTER_A` resolve.
    """
    folded = []
    medial = []
    for index, character in enumerate(name):
        if character in " _":
            continue
        before = name[index - 1] if index > 0 else ""
        after = name[index + 1] if index + 1 < len(name) else ""
        separator = (" ", "_", "-", "")
        folded.append(character.lower())
        medial.append(character == "-" and before not in separator
                      and after not in separator)
    text = "".join(folded)
    if text == "hanguljungseongo-e":
        return text
    return "".join(character for character, is_medial in zip(folded, medial)
                   if not is_medial)


def loose(text):
    """UAX #44-LM3: ignore case, whitespace, '_' and '-' in a property or
    value name. Applied to both the table and the query, so that
    "Line_Break" and "linebreak" are one key."""
    return re.sub(r"[\s_-]+", "", text).lower()


def build_alias_tables(ucd, tables, entries):
    """(property aliases, value aliases) as sorted lists for binary search."""
    # PropertyAliases.txt is keyed by the short name ("Alpha ; Alphabetic"),
    # and a binary property's entry here is keyed by its long name, so the
    # short spelling has to be looked up the other way round. Without this,
    # guni_property_by_name("Alpha") failed while ("Alphabetic") worked -
    # which no consumer would have reported as anything but "the property
    # does not exist".
    by_any_spelling = {}
    for key, spellings in ucd.property_aliases.items():
        for spelling in spellings:
            by_any_spelling.setdefault(spelling, key)

    property_aliases = []
    for index, entry in enumerate(entries):
        key = entry.key if entry.key in ucd.property_aliases \
            else by_any_spelling.get(entry.long_name, entry.key)
        spellings = set(ucd.property_aliases.get(key, [entry.key]))
        spellings.add(entry.long_name)
        spellings.add(entry.key)
        for spelling in spellings:
            property_aliases.append((loose(spelling), index))
    by_name = defaultdict(set)
    for name, index in property_aliases:
        by_name[name].add(index)
    for name, indices in by_name.items():
        if len(indices) > 1:
            raise SystemExit("property spelling %r names %d properties; loose "
                             "matching would have to guess" % (name, len(indices)))
    property_aliases = sorted(set(property_aliases))

    value_aliases = []
    for index, entry in enumerate(entries):
        spellings = defaultdict(set)
        if entry.kind in (KIND_ENUM, KIND_SCX, KIND_BLOCK):
            key = "sc" if entry.kind == KIND_SCX else entry.key
            for value, long_name in enumerate(entry.value_names):
                if not long_name:
                    continue
                spellings[value].add(long_name)
                short, extra = ucd.alias_of(key, long_name)
                if short:
                    spellings[value].add(short)
                for alias in extra:
                    spellings[value].add(alias)
        elif entry.kind == KIND_CCC:
            for value, long_name in enumerate(entry.value_names):
                spellings[value].add(str(value))
                if long_name:
                    spellings[value].add(long_name)
                    short, extra = ucd.long_of.get("ccc", {}).get(long_name, (None, []))
                    if short:
                        spellings[value].add(short)
        else:
            # Binary and quick-check values: "Yes"/"No"/"Maybe" with the
            # Y/N/T/F/M aliases the UCD gives every binary property.
            for value, long_name in enumerate(entry.value_names):
                spellings[value].add(long_name)
            if entry.kind == KIND_BINARY:
                spellings[0].update(("N", "F", "False"))
                spellings[1].update(("Y", "T", "True"))
            else:
                spellings[0].update(("Y",))
                spellings[1].update(("N",))
                spellings[2].update(("M",))
        seen = {}
        for value, names in spellings.items():
            for name in names:
                key = loose(name)
                if key in seen and seen[key] != value:
                    raise SystemExit(
                        "%s: %r names both value %d and value %d"
                        % (entry.long_name, name, seen[key], value))
                seen[key] = value
                value_aliases.append((key, index, value))
    return property_aliases, sorted(set(value_aliases))


# ---------------------------------------------------------------------------
# Emission
# ---------------------------------------------------------------------------

def open_out(out_dir, relative):
    path = os.path.join(out_dir, relative)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    return open(path, "w", encoding="utf-8", newline="\n")


def c_string(text):
    out = text.replace("\\", "\\\\").replace('"', '\\"')
    return '"%s"' % out


def emit_array(out, values, per_line, formatter=str):
    line = []
    for value in values:
        line.append(formatter(value))
        if len(line) == per_line:
            out.write("  %s,\n" % ", ".join(line))
            line = []
    if line:
        out.write("  %s,\n" % ", ".join(line))


def record_layout(ucd):
    """The GuniPropRecord field order, widest first so that it has no holes.

    Emitted from the registry rather than written out, so that a new property
    cannot be added to one of the two and not the other - which would put
    every field after it one column out and read as a corrupt table.
    """
    wide = [p for p in ucd.properties if p.bits == 16]
    narrow = [p for p in ucd.properties if p.bits == 8]
    fields = [("flags", "uint32_t", "flags[GUNI_PROP_FLAG_WORDS]", None)]
    for prop in wide:
        fields.append((prop.field, "uint16_t", prop.field, prop))
    fields.append(("scx", "uint16_t", "scx", None))
    for prop in narrow:
        fields.append((prop.field, "uint8_t", prop.field, prop))
    fields.append(("ccc", "uint8_t", "ccc", None))
    fields.append(("qc", "uint8_t", "qc", None))
    return fields


def emit_enums(ucd, tables, entries, out_dir):
    version = ucd.version
    major, minor, patch = (int(part) for part in version.split("."))
    with open_out(out_dir, "include/ghoti.io/unicode/enums.h") as out:
        out.write(LICENSE_NOTICE)
        out.write("\n")
        out.write(generated_notice(version, "PropertyAliases.txt, "
                                            "PropertyValueAliases.txt and Blocks.txt"))
        out.write("""
/**
 * @file
 *
 * Every enumerated property value the Unicode Character Database defines,
 * as a C enum.
 *
 * Values are **append-only**: a member keeps its value for the life of the
 * library, a member the current UCD no longer defines is kept rather than
 * dropped, and the generator refuses to emit a renumbering
 * (documentation/design.md section 2, M12). A consumer may therefore store a
 * GUNI_Script in a file and read it back after a Unicode upgrade.
 *
 * Both spellings of every value are here: the long alias as the canonical
 * member and the UCD's short alias beside it where they differ, so that
 * GUNI_GC_LU and GUNI_GC_UPPERCASE_LETTER are the same constant.
 */

#ifndef GHOTI_IO_GUNI_ENUMS_H
#define GHOTI_IO_GUNI_ENUMS_H

#include <ghoti.io/unicode/macros.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

""")
        out.write("/// The Unicode version every table here was generated from.\n")
        out.write("#define GUNI_UCD_VERSION_STRING %s\n" % c_string(version))
        out.write("/// The same, packed as GUNI_MAKE_VERSION() packs a version.\n")
        out.write("#define GUNI_UCD_VERSION_NUMBER GUNI_MAKE_VERSION(%d, %d, %d)\n"
                  % (major, minor, patch))
        out.write("#define GUNI_UCD_VERSION_MAJOR %d\n" % major)
        out.write("#define GUNI_UCD_VERSION_MINOR %d\n" % minor)
        out.write("#define GUNI_UCD_VERSION_PATCH %d\n" % patch)

        for prop in ucd.properties:
            numbering = tables.numbering[prop.ctype]
            out.write("\n/**\n * @brief %s.\n */\ntypedef enum {\n" % prop.brief)
            key = prop.key
            # One member per identifier. Decomposition_Type gives both "Can"
            # and "can" as spellings of Canonical, which differ only in case
            # and so are one C identifier; emitting both is a redeclaration.
            emitted = set()
            for name in numbering.order:
                member = numbering.member(name)
                out.write("  %s = %d,\n" % (member, numbering.values[name]))
                emitted.add(member)
                short, extra = ucd.long_of.get(key, {}).get(name, (None, []))
                for alias in ([short] if short else []) + list(extra):
                    alias_member = "%s_%s" % (prop.prefix, c_identifier(alias))
                    if alias_member not in emitted:
                        out.write("  %s = %s,\n" % (alias_member, member))
                        emitted.add(alias_member)
            for member, value in sorted(numbering.retired.items(), key=lambda kv: kv[1]):
                out.write("  %s = %d, /* not in UCD %s; kept, never reused */\n"
                          % (member, value, version))
            out.write("} %s;\n" % prop.ctype)
            out.write("/// One past the largest %s.\n#define %s_COUNT %d\n"
                      % (prop.ctype, prop.prefix, numbering.count))

        # General_Category groups. \p{L} is a mask test rather than an
        # equality test, and the mask is generated because the membership of
        # each group is the UCD's to decide.
        out.write("\n/**\n * @brief The most codepoints one codepoint becomes "
                  "under each form.\n *\n"
                  " * Generated from the data, not stated: a caller sizes a buffer as\n"
                  " * `length * GUNI_NORM_MAX_EXPANSION_NFKD` and needs no preflight, and a\n"
                  " * future Unicode that exceeded one of these changes the constant rather\n"
                  " * than silently overflowing that caller (design.md section 6.3). The\n"
                  " * NFKD witness is U+%04X.\n */\n" % tables.max_expansion_witness)
        for form in ("NFD", "NFC", "NFKD", "NFKC"):
            out.write("#define GUNI_NORM_MAX_EXPANSION_%s %d\n"
                      % (form, tables.max_expansion[form]))
        out.write("\n/**\n * @brief The longest character name, in bytes, "
                  "without its NUL.\n *\n"
                  " * Generated from the data: a caller sizes a buffer as\n"
                  " * GUNI_NAME_MAX_LENGTH + 1 and never asks twice.\n */\n")
        out.write("#define GUNI_NAME_MAX_LENGTH %d\n" % tables.name_longest)
        out.write("\n/// @brief The most codepoints a named sequence has.\n")
        out.write("#define GUNI_SEQUENCE_MAX_LENGTH %d\n" % tables.sequence_longest)

        out.write("\n/**\n * @brief Words of bitset in a script-run check.\n *\n"
                  " * One bit per script, plus three for UTS #39 section 5.1's\n"
                  " * augmented scripts - Japanese, Korean and HanBopomofo - which are\n"
                  " * not Unicode script values and exist only so that a three-way mix\n"
                  " * of Han, Hangul and Bopomofo falls out of an ordinary set\n"
                  " * intersection. Generated, because it follows from how many scripts\n"
                  " * there are.\n */\n")
        out.write("#define GUNI_SCRIPT_RUN_WORDS %d\n" % tables.script_run_words)

        out.write("\n/**\n * @brief The most codepoints one codepoint becomes "
                  "under a full case mapping.\n *\n"
                  " * Generated from SpecialCasing.txt and CaseFolding.txt, not "
                  "stated: a\n * caller sizes a buffer as `length * "
                  "GUNI_CASE_MAX_EXPANSION` and needs no\n * preflight.\n */\n")
        out.write("#define GUNI_CASE_MAX_EXPANSION %d\n"
                  % tables.case_max_expansion)

        gc = tables.numbering["GUNI_GeneralCategory"]
        out.write("\n/**\n * @brief The single-letter General_Category groups, as masks.\n"
                  " *\n"
                  " * `\\\\p{L}` asks whether a codepoint's category is any of five, and\n"
                  " * these are how that question is spelled: `GUNI_GC_MASK(gc) &\n"
                  " * GUNI_GC_MASK_L`. The membership of each group is the UCD's, so the\n"
                  " * masks are generated rather than written.\n */\n")
        out.write("#define GUNI_GC_MASK(gc) ((uint32_t)1u << (unsigned)(gc))\n")
        for group, members in GC_GROUPS.items():
            parts = []
            for short in members:
                long_name = ucd.canonicalise("gc", short)
                parts.append("GUNI_GC_MASK(%s)" % gc.member(long_name))
            out.write("#define GUNI_GC_MASK_%s (%s)\n" % (group, " | ".join(parts)))

        out.write("\n/**\n * @brief The language-sensitive case rules "
                  "SpecialCasing.txt defines.\n *\n"
                  " * Not a locale: an argument. setlocale() is process-wide state that\n"
                  " * changes how a library behaves (design.md section 2, M7), and the\n"
                  " * Turkish dotless i is the case where that state silently corrupts\n"
                  " * data. Generated, because the languages are the UCD's: a new one\n"
                  " * appends a member here rather than needing a hand-edit.\n */\n")
        out.write("typedef enum {\n"
                  "  GUNI_LANG_NONE = 0,   ///< The language-neutral mappings.\n"
                  "  GUNI_LANG_TURKIC = 1, ///< Turkish and Azerbaijani: the dotless i.\n"
                  "  GUNI_LANG_LITHUANIAN = 2 ///< Lithuanian: the retained dot above.\n"
                  "} GUNI_CaseTailoring;\n")

        out.write("\n/**\n * @brief UAX #15's quick-check answer.\n *\n"
                  " * MAYBE means the full algorithm has to run; it is never a guess.\n */\n")
        out.write("typedef enum {\n"
                  "  GUNI_QC_YES = 0,\n"
                  "  GUNI_QC_NO = 1,\n"
                  "  GUNI_QC_MAYBE = 2\n"
                  "} GUNI_QuickCheck;\n")

        block = tables.block_numbering
        out.write("\n/**\n * @brief Block, from Blocks.txt.\n */\ntypedef enum {\n")
        # The short alias beside the long member, which this enum did not
        # have and which the file header at the top of this page promises
        # for every enumerated value: "the long alias as the canonical
        # member and the UCD's short alias beside it where they differ".
        # Blocks were the one kind that did not keep it - GUNI_BLOCK_ASCII
        # did not exist beside GUNI_BLOCK_BASIC_LATIN - for the same
        # two-file spelling mismatch that cost the name lookups, so the
        # join is Ucd.alias_of() here too.
        #
        # Additive: an alias member takes an existing member's value and
        # never introduces or renumbers one, so the append-only rule in
        # design.md section 2 (M12) is untouched.
        block_emitted = set()
        for name in block.order:
            member = block.member(name)
            out.write("  %s = %d,\n" % (member, block.values[name]))
            block_emitted.add(member)
        for name in block.order:
            member = block.member(name)
            short, extra = ucd.alias_of("blk", name)
            for alias in ([short] if short else []) + list(extra):
                alias_member = "GUNI_BLOCK_%s" % c_identifier(alias)
                if alias_member not in block_emitted:
                    out.write("  %s = %s,\n" % (alias_member, member))
                    block_emitted.add(alias_member)
        for member, value in sorted(block.retired.items(), key=lambda kv: kv[1]):
            out.write("  %s = %d, /* not in UCD %s; kept, never reused */\n"
                      % (member, value, version))
        out.write("} GUNI_Block;\n")
        out.write("/// One past the largest GUNI_Block.\n#define GUNI_BLOCK_COUNT %d\n"
                  % block.count)

        binary = tables.binary_numbering
        out.write("\n/**\n * @brief A binary property, as a bit position.\n *\n"
                  " * Every binary property of a codepoint is one bit of one word, so\n"
                  " * guni_has_property() costs the same lookup as every other property\n"
                  " * rather than a search of a per-property table.\n */\ntypedef enum {\n")
        for name in binary.order:
            out.write("  %s = %d,\n" % (binary.member(name), binary.values[name]))
        for member, value in sorted(binary.retired.items(), key=lambda kv: kv[1]):
            out.write("  %s = %d, /* not in UCD %s; kept, never reused */\n"
                      % (member, value, version))
        out.write("} GUNI_BinaryProperty;\n")
        out.write("/// One past the largest GUNI_BinaryProperty.\n"
                  "#define GUNI_PROP_COUNT %d\n" % binary.count)

        prop_numbering = tables.numbering["GUNI_Property"]
        out.write("\n/**\n * @brief Every property this library can be asked for by name.\n *\n"
                  " * The enumerated properties, the binary properties, and the four\n"
                  " * that are neither a field of the record nor a bit of it: Block is a\n"
                  " * range table, Canonical_Combining_Class is a number,\n"
                  " * Script_Extensions is a set, and the quick checks are two bits\n"
                  " * each. set.h takes one of these and a value.\n */\ntypedef enum {\n")
        for name in prop_numbering.order:
            out.write("  %s = %d,\n" % (prop_numbering.member(name),
                                        prop_numbering.values[name]))
        for member, value in sorted(prop_numbering.retired.items(), key=lambda kv: kv[1]):
            out.write("  %s = %d, /* not in UCD %s; kept, never reused */\n"
                      % (member, value, version))
        out.write("} GUNI_Property;\n")
        out.write("/// One past the largest GUNI_Property.\n"
                  "#define GUNI_PROPERTY_COUNT %d\n" % prop_numbering.count)

        out.write("\n/**\n * @brief How a property's value is got at.\n *\n"
                  " * set.c switches on this rather than on the property, which is what\n"
                  " * makes adding a property a one-row change (design.md section 5.3).\n"
                  " */\ntypedef enum {\n")
        for index, kind in enumerate(KINDS):
            out.write("  %s = %d,\n" % (kind, index))
        out.write("} GUNI_PropertyKind;\n")

        out.write("""
#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GUNI_ENUMS_H
""")


def emit_tables_header(ucd, tables, entries, out_dir):
    layout = record_layout(ucd)
    with open_out(out_dir, "src/char/tables/tables.h") as out:
        out.write(LICENSE_NOTICE)
        out.write("\n")
        out.write(generated_notice(ucd.version, "the whole UCD"))
        out.write("""
/**
 * @file
 *
 * The generated property tables, and the one lookup every accessor in
 * char.c goes through.
 *
 * Internal: not installed, not part of the API. The shape is
 * documentation/design.md section 4.2 - a two-stage trie to a shared record
 * for the point query, and a run table for set.h's bulk enumeration, both
 * from the same map so that they cannot disagree.
 */

#ifndef GHOTI_IO_GUNI_CHAR_TABLES_H
#define GHOTI_IO_GUNI_CHAR_TABLES_H

#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/enums.h>
#include <ghoti.io/unicode/macros.h>
#include <stddef.h>
#include <stdint.h>

""")
        out.write("/// Words of binary-property bits in a record.\n")
        out.write("#define GUNI_PROP_FLAG_WORDS %d\n" % tables.flag_words)
        out.write("/// Codepoints per stage-2 block; 64 measured smallest.\n")
        out.write("#define GUNI_PROP_STAGE1_SHIFT %d\n" % BLOCK_SHIFT)
        out.write("#define GUNI_PROP_STAGE2_MASK %d\n" % (BLOCK_SIZE - 1))
        out.write("#define GUNI_PROP_RECORD_COUNT %d\n" % len(tables.records))
        out.write("#define GUNI_PROP_STAGE1_COUNT %d\n" % len(tables.stage1))
        out.write("#define GUNI_PROP_STAGE2_COUNT %d\n" % len(tables.stage2))
        out.write("#define GUNI_PROP_RUN_COUNT %d\n" % len(tables.runs))
        out.write("#define GUNI_SCX_POOL_COUNT %d\n" % len(tables.scx_pool))
        out.write("#define GUNI_BLOCK_RANGE_COUNT %d\n" % len(ucd.blocks))
        out.write("#define GUNI_NUMERIC_RANGE_COUNT %d\n" % len(ucd.numeric))
        out.write("#define GUNI_DECOMP_COUNT %d\n" % len(tables.decomp_rows))
        out.write("#define GUNI_DECOMP_POOL_COUNT %d\n" % len(tables.decomp_pool))
        out.write("#define GUNI_COMPOSE_COUNT %d\n" % len(tables.compose))
        out.write("/* UTS #39 script runs. GUNI_SCRIPT_RUN_WORDS is in enums.h:\n"
                  " * GUNI_ScriptRun holds the bitset, so its width is public. */\n")
        out.write("#define GUNI_SCRIPT_RUN_SET_COUNT %d\n"
                  % len(tables.script_run_sets))
        out.write("#define GUNI_DIGIT_ZERO_COUNT %d\n" % len(tables.digit_zeros))
        out.write("#define GUNI_CASE_COUNT %d\n" % len(tables.case_rows))
        out.write("#define GUNI_CASE_POOL_COUNT %d\n" % len(tables.case_pool))
        out.write("#define GUNI_CASE_CONDITIONAL_COUNT %d\n"
                  % len(tables.case_conditional))
        out.write("#define GUNI_TURKIC_FOLD_COUNT %d\n" % len(tables.turkic_folds))
        out.write("#define GUNI_ORBIT_COUNT %d\n" % len(tables.orbit_rows))
        out.write("#define GUNI_ORBIT_POOL_COUNT %d\n" % len(tables.orbit_pool))
        out.write("/* The four columns of every case table, in this order. */\n")
        out.write("#define GUNI_CASE_UPPER 0\n")
        out.write("#define GUNI_CASE_LOWER 1\n")
        out.write("#define GUNI_CASE_TITLE 2\n")
        out.write("#define GUNI_CASE_FOLD 3\n")
        out.write("#define GUNI_MIRROR_COUNT %d\n" % len(ucd.mirroring))
        out.write("#define GUNI_BRACKET_COUNT %d\n" % len(ucd.brackets))
        out.write("/* A composition key: the two codepoints, 21 bits each. */\n")
        out.write("#define GUNI_COMPOSE_KEY(first, second) "
                  "(((uint64_t)(first) << 21) | (uint64_t)(second))\n")
        out.write("#define GUNI_PROPERTY_ALIAS_COUNT %d\n" % len(tables.property_aliases))
        out.write("#define GUNI_VALUE_ALIAS_COUNT %d\n" % len(tables.value_aliases))

        out.write("""
/**
 * @brief Every property of a codepoint, in one record.
 *
 * 2,369 of these cover all 1,114,112 codepoints, because properties
 * correlate: a codepoint's script very nearly determines its bidi class, its
 * line-break class and its Indic categories. Fields are widest-first so the
 * struct has no padding holes.
 */
typedef struct {
""")
        for _name, ctype, declarator, prop in layout:
            comment = ""
            if prop is not None:
                comment = "  ///< %s\n" % prop.brief
            out.write("  %s %s;%s" % (ctype, declarator, comment or "\n"))
        out.write("} GuniPropRecord;\n")

        out.write("""
extern const GuniPropRecord guni_prop_records[GUNI_PROP_RECORD_COUNT];
extern const GuniPropRecord guni_prop_record_default;
extern const uint16_t guni_prop_stage1[GUNI_PROP_STAGE1_COUNT];
extern const uint16_t guni_prop_stage2[GUNI_PROP_STAGE2_COUNT];
extern const uint32_t guni_prop_run_first[GUNI_PROP_RUN_COUNT];
extern const uint16_t guni_prop_run_record[GUNI_PROP_RUN_COUNT];
extern const uint16_t guni_scx_pool[GUNI_SCX_POOL_COUNT];

extern const uint32_t guni_block_first[GUNI_BLOCK_RANGE_COUNT];
extern const uint32_t guni_block_last[GUNI_BLOCK_RANGE_COUNT];
extern const uint16_t guni_block_id[GUNI_BLOCK_RANGE_COUNT];

/* Decompositions. The pool holds every canonical and compatibility
 * decomposition, shared where they are equal; the row arrays are sorted by
 * codepoint. Hangul is absent on purpose: Standard section 3.12 gives it as
 * arithmetic (norm.c), and a table would be 11,172 entries of what a dozen
 * lines compute. */
extern const uint32_t guni_decomp_pool[GUNI_DECOMP_POOL_COUNT];
extern const uint32_t guni_decomp_codepoint[GUNI_DECOMP_COUNT];
extern const uint16_t guni_decomp_nfd_offset[GUNI_DECOMP_COUNT];
extern const uint8_t guni_decomp_nfd_length[GUNI_DECOMP_COUNT];
extern const uint16_t guni_decomp_nfkd_offset[GUNI_DECOMP_COUNT];
extern const uint8_t guni_decomp_nfkd_length[GUNI_DECOMP_COUNT];

/* Canonical composition: the pairs that compose, sorted by a packed key so
 * that the lookup is one binary search. A canonical decomposition of exactly
 * two codepoints whose codepoint is not Full_Composition_Exclusion. */
extern const uint64_t guni_compose_key[GUNI_COMPOSE_COUNT];
extern const uint32_t guni_compose_value[GUNI_COMPOSE_COUNT];

/* UTS #39 script runs. One augmented bitset per distinct Script_Extensions
 * set, indexed by that set's offset in guni_scx_pool - which is a field of the
 * property record, so a check costs no second search. */
extern const uint64_t
    guni_script_run_sets[GUNI_SCRIPT_RUN_SET_COUNT][GUNI_SCRIPT_RUN_WORDS];
extern const uint16_t guni_script_run_by_offset[GUNI_SCX_POOL_COUNT];

/* The first codepoint of each block of ten decimal digits. */
extern const uint32_t guni_digit_zeros[GUNI_DIGIT_ZERO_COUNT];

/* Case mappings. One sorted table over every codepoint with any, gated by
 * Changes_When_Casemapped and Changes_When_Casefolded in the property record,
 * so a codepoint with none costs no search. A simple mapping of 0 means the
 * identity: 0 is not a mapping target, so it is unambiguous. */
extern const uint32_t guni_case_codepoint[GUNI_CASE_COUNT];
extern const uint32_t guni_case_simple[GUNI_CASE_COUNT][4];
extern const uint16_t guni_case_full_offset[GUNI_CASE_COUNT][4];
extern const uint8_t guni_case_full_length[GUNI_CASE_COUNT][4];
extern const uint32_t guni_case_pool[GUNI_CASE_POOL_COUNT];

/** SpecialCasing.txt's conditions, which case.c implements one by one. */
typedef enum {
  GUNI_CASE_COND_NONE = 0,
  GUNI_CASE_COND_FINAL_SIGMA,
  GUNI_CASE_COND_AFTER_SOFT_DOTTED,
  GUNI_CASE_COND_MORE_ABOVE,
  GUNI_CASE_COND_AFTER_I,
  GUNI_CASE_COND_NOT_BEFORE_DOT
} GuniCaseCondition;

/** One conditional mapping: sixteen lines of SpecialCasing.txt. */
typedef struct {
  uint32_t codepoint;
  uint8_t language;  ///< A GUNI_CaseTailoring.
  uint8_t condition; ///< A GuniCaseCondition.
  uint16_t offset[4];
  uint8_t length[4];
} GuniCaseConditional;

extern const GuniCaseConditional
    guni_case_conditional[GUNI_CASE_CONDITIONAL_COUNT];

/* CaseFolding.txt's T status: the two codepoints a Turkic fold differs on. */
extern const uint32_t guni_turkic_fold_from[GUNI_TURKIC_FOLD_COUNT];
extern const uint32_t guni_turkic_fold_to[GUNI_TURKIC_FOLD_COUNT];

/* The fold orbits: every codepoint that folds to the same value, keyed by
 * that value. What a case-insensitive character class needs. */
extern const uint32_t guni_orbit_value[GUNI_ORBIT_COUNT];
extern const uint16_t guni_orbit_offset[GUNI_ORBIT_COUNT];
extern const uint8_t guni_orbit_length[GUNI_ORBIT_COUNT];
extern const uint32_t guni_orbit_pool[GUNI_ORBIT_POOL_COUNT];

/* UAX #9's rule L4: the mirrored glyph, and BD14/BD15's bracket pairs for
 * rule N0. Sorted by codepoint; both are small enough that a binary search is
 * the whole implementation. */
extern const uint32_t guni_mirror_from[GUNI_MIRROR_COUNT];
extern const uint32_t guni_mirror_to[GUNI_MIRROR_COUNT];
extern const uint32_t guni_bracket_from[GUNI_BRACKET_COUNT];
extern const uint32_t guni_bracket_pair[GUNI_BRACKET_COUNT];
/** 1 for an opening bracket, 2 for a closing one. */
extern const uint8_t guni_bracket_kind[GUNI_BRACKET_COUNT];

extern const uint32_t guni_numeric_first[GUNI_NUMERIC_RANGE_COUNT];
extern const uint32_t guni_numeric_last[GUNI_NUMERIC_RANGE_COUNT];
extern const int64_t guni_numeric_numerator[GUNI_NUMERIC_RANGE_COUNT];
extern const uint32_t guni_numeric_denominator[GUNI_NUMERIC_RANGE_COUNT];

/** @brief One row of the table a property name is looked up in. */
typedef struct {
  const char * name;                ///< The long alias.
  const char * const * value_names; ///< Value to long alias; "" where none.
  uint16_t value_count;             ///< One past the largest value.
  uint8_t kind;                     ///< A GUNI_PropertyKind.
  uint8_t bit;                      ///< The GUNI_BinaryProperty, for BINARY.
  uint8_t qc_index;                 ///< 0..3 selecting the form, for QC.
} GuniPropertyInfo;

extern const GuniPropertyInfo guni_property_info[GUNI_PROPERTY_COUNT];

/** @brief A loose-matched spelling of a property name. */
typedef struct {
  const char * name;
  uint16_t property;
} GuniPropertyAlias;

/** @brief A loose-matched spelling of one property's value. */
typedef struct {
  const char * name;
  uint16_t property;
  uint16_t value;
} GuniValueAlias;

extern const GuniPropertyAlias guni_property_aliases[GUNI_PROPERTY_ALIAS_COUNT];
extern const GuniValueAlias guni_value_aliases[GUNI_VALUE_ALIAS_COUNT];

/**
 * @brief The record for a codepoint. Three dependent loads, no branch on the
 * fast path.
 *
 * Defined for every uint32_t: above the last codepoint the Standard defines,
 * the answer is the all-defaults record, so that no input indexes a table
 * (design.md section 4.4). That record is a synthesised one rather than some
 * unassigned codepoint's, because an unassigned codepoint is still a
 * codepoint and answers yes to Any.
 */
static inline const GuniPropRecord * guni_record(uint32_t cp) {
  if (cp > GUNI_MAX_CODEPOINT) {
    return &guni_prop_record_default;
  }
  return &guni_prop_records[guni_prop_stage2[
      ((size_t)guni_prop_stage1[cp >> GUNI_PROP_STAGE1_SHIFT]
       << GUNI_PROP_STAGE1_SHIFT)
      + (cp & GUNI_PROP_STAGE2_MASK)]];
}

/**
 * @brief The value of any property of a record, as an unsigned integer.
 *
 * Generated, so that the switch over properties is never hand-maintained.
 * Returns 0 for a property whose value is not in the record - Block, whose
 * value is a range lookup - and set.c handles those by kind.
 */
uint32_t guni_record_property_value(const GuniPropRecord * record,
    GUNI_Property property);

#endif // GHOTI_IO_GUNI_CHAR_TABLES_H
""")


def emit_props_data(ucd, tables, entries, out_dir):
    layout = record_layout(ucd)
    with open_out(out_dir, "src/char/tables/props_data.c") as out:
        out.write(LICENSE_NOTICE)
        out.write("\n")
        out.write(generated_notice(ucd.version, "every property file in the UCD"))
        out.write("\n#include \"tables.h\"\n")
        out.write("\n/* Field order: %s. */\n"
                  % ", ".join(name for name, _c, _d, _p in layout))
        out.write("const GuniPropRecord guni_prop_records[GUNI_PROP_RECORD_COUNT] = {\n")
        props = ucd.properties
        wide = [p for p in props if p.bits == 16]
        narrow = [p for p in props if p.bits == 8]
        offset = {prop.key: index for index, prop in enumerate(props)}
        for key in tables.records:
            values = key[:len(props)]
            ccc, qc, scx = key[len(props):len(props) + 3]
            flags = key[len(props) + 3:]
            cells = ["{%s}" % ", ".join("0x%08Xu" % word for word in flags)]
            cells.extend(str(values[offset[prop.key]]) for prop in wide)
            cells.append(str(scx))
            cells.extend(str(values[offset[prop.key]]) for prop in narrow)
            cells.append(str(ccc))
            cells.append(str(qc))
            out.write("  {%s},\n" % ", ".join(cells))
        out.write("};\n")

        default_cells = ["{%s}" % ", ".join(["0x00000000u"] * tables.flag_words)]
        default_values = {}
        for prop in props:
            default_values[prop.key] = prop.values[ucd.canonicalise(prop.key, prop.default)]
        default_cells.extend(str(default_values[prop.key]) for prop in wide)
        default_cells.append(str(tables.scx_offsets[("Unknown",)]))
        default_cells.extend(str(default_values[prop.key]) for prop in narrow)
        default_cells.append("0")
        default_cells.append("0")
        out.write("\n/* The answer above the last codepoint: every property's default,\n"
                  " * no binary property set, Script_Extensions the singleton {Unknown}. */\n")
        out.write("const GuniPropRecord guni_prop_record_default = {%s};\n"
                  % ", ".join(default_cells))

        out.write("\nconst uint16_t guni_prop_stage1[GUNI_PROP_STAGE1_COUNT] = {\n")
        emit_array(out, tables.stage1, 16)
        out.write("};\n")
        out.write("\nconst uint16_t guni_prop_stage2[GUNI_PROP_STAGE2_COUNT] = {\n")
        emit_array(out, tables.stage2, 16)
        out.write("};\n")
        out.write("\n/* The maximal ranges over which the record is constant. set.h walks\n"
                  " * these; the sweep test asserts they agree with the trie everywhere. */\n")
        out.write("const uint32_t guni_prop_run_first[GUNI_PROP_RUN_COUNT] = {\n")
        emit_array(out, [first for first, _rec in tables.runs], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint16_t guni_prop_run_record[GUNI_PROP_RUN_COUNT] = {\n")
        emit_array(out, [rec for _first, rec in tables.runs], 16)
        out.write("};\n")
        out.write("\n/* Script_Extensions sets: a count followed by that many GUNI_Script\n"
                  " * values, sorted. Every codepoint has one, defaulting to the\n"
                  " * singleton of its own Script (UAX #24), so no caller special-cases\n"
                  " * the absence of a set. */\n")
        out.write("const uint16_t guni_scx_pool[GUNI_SCX_POOL_COUNT] = {\n")
        emit_array(out, tables.scx_pool, 16)
        out.write("};\n")

        out.write("\nuint32_t guni_record_property_value(const GuniPropRecord * record,\n"
                  "    GUNI_Property property) {\n"
                  "  switch (property) {\n")
        prop_numbering = tables.numbering["GUNI_Property"]
        for entry in entries:
            member = prop_numbering.member(entry.long_name)
            if entry.kind == KIND_ENUM:
                out.write("    case %s: return record->%s;\n" % (member, entry.prop.field))
            elif entry.kind == KIND_CCC:
                out.write("    case %s: return record->ccc;\n" % member)
            elif entry.kind == KIND_QC:
                out.write("    case %s: return (record->qc >> %d) & 3u;\n"
                          % (member, 2 * entry.qc_index))
            elif entry.kind == KIND_BINARY:
                out.write("    case %s: return (record->flags[%d] >> %d) & 1u;\n"
                          % (member, entry.bit >> 5, entry.bit & 31))
        out.write("    /* Unreachable from inside the library: every property is a case\n"
                  "     * above except Block and Script_Extensions, which char.c and set.c\n"
                  "     * answer before they get here. C needs the arm; coverage names it. */\n"
                  "    default: return 0;\n"
                  "  }\n"
                  "}\n")


def emit_misc_data(ucd, tables, out_dir):
    with open_out(out_dir, "src/char/tables/misc_data.c") as out:
        out.write(LICENSE_NOTICE)
        out.write("\n")
        out.write(generated_notice(ucd.version,
                                  "Blocks.txt and DerivedNumericValues.txt"))
        out.write("\n#include \"tables.h\"\n")
        out.write("\n/* Blocks are contiguous by definition and there are few of them, so\n"
                  " * they are a sorted range table rather than a field of the record:\n"
                  " * putting them in the record would fragment it, because a block\n"
                  " * boundary almost never coincides with a property change. */\n")
        out.write("const uint32_t guni_block_first[GUNI_BLOCK_RANGE_COUNT] = {\n")
        emit_array(out, [first for first, _l, _n in ucd.blocks], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_block_last[GUNI_BLOCK_RANGE_COUNT] = {\n")
        emit_array(out, [last for _f, last, _n in ucd.blocks], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint16_t guni_block_id[GUNI_BLOCK_RANGE_COUNT] = {\n")
        emit_array(out, [tables.block_numbering.values[name]
                         for _f, _l, name in ucd.blocks], 12)
        out.write("};\n")

        out.write("\n/* Numeric_Value as an exact rational. The numerator is 64-bit\n"
                  " * because the UCD contains 10^12, which a double truncates. */\n")
        out.write("const uint32_t guni_numeric_first[GUNI_NUMERIC_RANGE_COUNT] = {\n")
        emit_array(out, [e[0] for e in ucd.numeric], 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_numeric_last[GUNI_NUMERIC_RANGE_COUNT] = {\n")
        emit_array(out, [e[1] for e in ucd.numeric], 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst int64_t guni_numeric_numerator[GUNI_NUMERIC_RANGE_COUNT] = {\n")
        emit_array(out, [e[2] for e in ucd.numeric], 8, lambda v: "INT64_C(%d)" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_numeric_denominator[GUNI_NUMERIC_RANGE_COUNT] = {\n")
        emit_array(out, [e[3] for e in ucd.numeric], 12, lambda v: "%du" % v)
        out.write("};\n")


def emit_norm_data(ucd, tables, out_dir):
    with open_out(out_dir, "src/norm/tables/norm_data.c") as out:
        out.write(LICENSE_NOTICE)
        out.write("\n")
        out.write(generated_notice(ucd.version,
                                  "UnicodeData.txt field 5 and "
                                  "DerivedNormalizationProps.txt"))
        out.write("\n#include \"../../char/tables/tables.h\"\n")
        out.write("\n/* Every decomposition, fully expanded at generation time so that the\n"
                  " * library never recurses. Shared where a codepoint's canonical and\n"
                  " * compatibility decompositions are the same sequence. */\n")
        out.write("const uint32_t guni_decomp_pool[GUNI_DECOMP_POOL_COUNT] = {\n")
        emit_array(out, tables.decomp_pool, 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_decomp_codepoint[GUNI_DECOMP_COUNT] = {\n")
        emit_array(out, [row[0] for row in tables.decomp_rows], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint16_t guni_decomp_nfd_offset[GUNI_DECOMP_COUNT] = {\n")
        emit_array(out, [row[1] for row in tables.decomp_rows], 12)
        out.write("};\n")
        out.write("\nconst uint8_t guni_decomp_nfd_length[GUNI_DECOMP_COUNT] = {\n")
        emit_array(out, [row[2] for row in tables.decomp_rows], 20)
        out.write("};\n")
        out.write("\nconst uint16_t guni_decomp_nfkd_offset[GUNI_DECOMP_COUNT] = {\n")
        emit_array(out, [row[3] for row in tables.decomp_rows], 12)
        out.write("};\n")
        out.write("\nconst uint8_t guni_decomp_nfkd_length[GUNI_DECOMP_COUNT] = {\n")
        emit_array(out, [row[4] for row in tables.decomp_rows], 20)
        out.write("};\n")
        out.write("\n/* The pairs that compose, by packed key. */\n")
        out.write("const uint64_t guni_compose_key[GUNI_COMPOSE_COUNT] = {\n")
        emit_array(out, tables.compose, 4,
                   lambda row: "GUNI_COMPOSE_KEY(0x%06Xu, 0x%06Xu)" % (row[0], row[1]))
        out.write("};\n")
        out.write("\nconst uint32_t guni_compose_value[GUNI_COMPOSE_COUNT] = {\n")
        emit_array(out, tables.compose, 8, lambda row: "0x%06Xu" % row[2])
        out.write("};\n")


def emit_case_data(ucd, tables, out_dir):
    with open_out(out_dir, "src/case/tables/case_data.c") as out:
        out.write(LICENSE_NOTICE)
        out.write("\n")
        out.write(generated_notice(ucd.version, "UnicodeData.txt, SpecialCasing.txt and CaseFolding.txt"))
        out.write("\n#include \"../../char/tables/tables.h\"\n")
        out.write("\n/* Every codepoint with a case mapping, sorted. */\n")
        out.write("const uint32_t guni_case_codepoint[GUNI_CASE_COUNT] = {\n")
        emit_array(out, [row[0] for row in tables.case_rows], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\n/* Simple mappings: upper, lower, title, fold. 0 is the "
                  "identity. */\n")
        out.write("const uint32_t guni_case_simple[GUNI_CASE_COUNT][4] = {\n")
        for _cp, simple, _full in tables.case_rows:
            out.write("  {%s},\n" % ", ".join("0x%06Xu" % v for v in simple))
        out.write("};\n")
        out.write("\n/* Full mappings, into guni_case_pool. Length 0 means the "
                  "simple mapping stands. */\n")
        out.write("const uint16_t guni_case_full_offset[GUNI_CASE_COUNT][4] = {\n")
        for _cp, _simple, full in tables.case_rows:
            out.write("  {%s},\n" % ", ".join(str(offset) for offset, _l in full))
        out.write("};\n")
        out.write("\nconst uint8_t guni_case_full_length[GUNI_CASE_COUNT][4] = {\n")
        for _cp, _simple, full in tables.case_rows:
            out.write("  {%s},\n" % ", ".join(str(length) for _o, length in full))
        out.write("};\n")
        out.write("\nconst uint32_t guni_case_pool[GUNI_CASE_POOL_COUNT] = {\n")
        emit_array(out, tables.case_pool, 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\n/* The sixteen conditional lines of SpecialCasing.txt. */\n")
        out.write("const GuniCaseConditional\n"
                  "    guni_case_conditional[GUNI_CASE_CONDITIONAL_COUNT] = {\n")
        for cp, language, condition, upper, lower, title in tables.case_conditional:
            out.write("  {0x%06Xu, %s, %s, {%d, %d, %d, 0}, {%d, %d, %d, 0}},\n"
                      % (cp, language, condition, upper[0], lower[0], title[0],
                         upper[1], lower[1], title[1]))
        out.write("};\n")
        out.write("\nconst uint32_t guni_turkic_fold_from[GUNI_TURKIC_FOLD_COUNT] = {\n")
        emit_array(out, [cp for cp, _to in tables.turkic_folds], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_turkic_fold_to[GUNI_TURKIC_FOLD_COUNT] = {\n")
        emit_array(out, [to for _cp, to in tables.turkic_folds], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\n/* The fold orbits, keyed by the value folded to. */\n")
        out.write("const uint32_t guni_orbit_value[GUNI_ORBIT_COUNT] = {\n")
        emit_array(out, [row[0] for row in tables.orbit_rows], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint16_t guni_orbit_offset[GUNI_ORBIT_COUNT] = {\n")
        emit_array(out, [row[1] for row in tables.orbit_rows], 12)
        out.write("};\n")
        out.write("\nconst uint8_t guni_orbit_length[GUNI_ORBIT_COUNT] = {\n")
        emit_array(out, [row[2] for row in tables.orbit_rows], 20)
        out.write("};\n")
        out.write("\nconst uint32_t guni_orbit_pool[GUNI_ORBIT_POOL_COUNT] = {\n")
        emit_array(out, tables.orbit_pool, 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")



def emit_script_data(ucd, tables, out_dir):
    with open_out(out_dir, "src/script/tables/script_data.c") as out:
        out.write(LICENSE_NOTICE)
        out.write("\n")
        out.write(generated_notice(ucd.version, "ScriptExtensions.txt and DerivedGeneralCategory.txt"))
        out.write("\n#include \"../../char/tables/tables.h\"\n")
        out.write("\n/* The augmented script sets, by Script_Extensions pool "
                  "offset. */\n")
        out.write("const uint64_t\n"
                  "    guni_script_run_sets[GUNI_SCRIPT_RUN_SET_COUNT]"
                  "[GUNI_SCRIPT_RUN_WORDS] = {\n")
        for mask in tables.script_run_sets:
            out.write("  {%s},\n"
                      % ", ".join("UINT64_C(0x%016X)" % word for word in mask))
        out.write("};\n")
        out.write("\nconst uint16_t guni_script_run_by_offset"
                  "[GUNI_SCX_POOL_COUNT] = {\n")
        emit_array(out, tables.script_run_by_offset, 16)
        out.write("};\n")
        out.write("\n/* The first codepoint of each block of ten decimal "
                  "digits. */\n")
        out.write("const uint32_t guni_digit_zeros[GUNI_DIGIT_ZERO_COUNT] = {\n")
        emit_array(out, tables.digit_zeros, 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")



def emit_bidi_data(ucd, tables, out_dir):
    with open_out(out_dir, "src/bidi/tables/bidi_data.c") as out:
        out.write(LICENSE_NOTICE)
        out.write("\n")
        out.write(generated_notice(ucd.version, "BidiMirroring.txt and BidiBrackets.txt"))
        out.write("\n#include \"../../char/tables/tables.h\"\n")
        mirroring = sorted(ucd.mirroring.items())
        out.write("\n/* Bidi_Mirroring_Glyph, for rule L4. */\n")
        out.write("const uint32_t guni_mirror_from[GUNI_MIRROR_COUNT] = {\n")
        emit_array(out, [cp for cp, _to in mirroring], 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_mirror_to[GUNI_MIRROR_COUNT] = {\n")
        emit_array(out, [to for _cp, to in mirroring], 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")
        brackets = sorted(ucd.brackets.items())
        out.write("\n/* BidiBrackets.txt, for rule N0. */\n")
        out.write("const uint32_t guni_bracket_from[GUNI_BRACKET_COUNT] = {\n")
        emit_array(out, [cp for cp, _pair in brackets], 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_bracket_pair[GUNI_BRACKET_COUNT] = {\n")
        emit_array(out, [pair[0] for _cp, pair in brackets], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint8_t guni_bracket_kind[GUNI_BRACKET_COUNT] = {\n")
        emit_array(out, [1 if pair[1] == "o" else 2 for _cp, pair in brackets], 20)
        out.write("};\n")



def emit_name_data(ucd, tables, out_dir):
    """The character names: tier 1, in their own directory and header.

    More than half the generated bulk of this library, wanted by one consumer,
    and behind a header nothing in tier 0 includes - which make-layering
    enforces (design.md section 3).
    """
    with open_out(out_dir, "src/name/tables/name_tables.h") as out:
        out.write(LICENSE_NOTICE)
        out.write("\n")
        out.write(generated_notice(ucd.version,
                                  "UnicodeData.txt, NameAliases.txt, "
                                  "NamedSequences.txt and Jamo.txt"))
        out.write("""
/**
 * @file
 *
 * The generated name tables. **Tier 1**: internal, and not included by
 * anything in tier 0.
 *
 * A token is a word number in the low 15 bits and, in bit 15, the separator
 * that precedes it - set for "-" and clear for a space. 18,457 words and
 * 162,649 tokens encode 40,951 names that are 1,044,804 bytes as text.
 */

#ifndef GHOTI_IO_GUNI_NAME_TABLES_H
#define GHOTI_IO_GUNI_NAME_TABLES_H

#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/macros.h>
#include <stddef.h>
#include <stdint.h>

""")
        out.write("#define GUNI_NAME_WORD_COUNT %d\n" % len(tables.name_words))
        out.write("#define GUNI_NAME_TOKEN_COUNT %d\n" % len(tables.name_tokens))
        out.write("#define GUNI_NAME_COUNT %d\n" % (len(tables.name_offsets) - 1))
        out.write("#define GUNI_NAME_PRIMARY_COUNT %d\n"
                  % len(tables.name_primary_cp))
        out.write("#define GUNI_NAME_ALIAS_COUNT %d\n" % len(tables.name_aliases))
        out.write("#define GUNI_NAME_RANGE_COUNT %d\n" % len(tables.name_ranges))
        out.write("#define GUNI_SEQUENCE_COUNT %d\n"
                  % (len(tables.sequence_offsets) - 1))
        out.write("#define GUNI_SEQUENCE_TOKEN_COUNT %d\n"
                  % len(tables.sequence_tokens))
        out.write("#define GUNI_SEQUENCE_POOL_COUNT %d\n"
                  % len(tables.sequence_pool))
        out.write("/* The separator bit of a token, and the word-number mask. */\n")
        out.write("#define GUNI_NAME_HYPHEN 0x8000u\n")
        out.write("#define GUNI_NAME_WORD_MASK 0x7FFFu\n")
        out.write("""
extern const char * const guni_name_words[GUNI_NAME_WORD_COUNT];
extern const uint16_t guni_name_tokens[GUNI_NAME_TOKEN_COUNT];
extern const uint32_t guni_name_offsets[GUNI_NAME_COUNT + 1];
extern const uint32_t guni_name_codepoints[GUNI_NAME_COUNT];
/** The name entries in loose-name order, for the reverse lookup. */
extern const uint32_t guni_name_loose_order[GUNI_NAME_COUNT];

/** The codepoints with a name of their own, and which entry it is. */
extern const uint32_t guni_name_primary_cp[GUNI_NAME_PRIMARY_COUNT];
extern const uint32_t guni_name_primary_index[GUNI_NAME_PRIMARY_COUNT];

/** The aliases, sorted by codepoint. */
extern const uint32_t guni_name_alias_cp[GUNI_NAME_ALIAS_COUNT];
extern const uint8_t guni_name_alias_kind[GUNI_NAME_ALIAS_COUNT];
extern const uint32_t guni_name_alias_index[GUNI_NAME_ALIAS_COUNT];

/** The families whose names are computed: CJK, Tangut, Hangul syllables. */
extern const uint32_t guni_name_range_first[GUNI_NAME_RANGE_COUNT];
extern const uint32_t guni_name_range_last[GUNI_NAME_RANGE_COUNT];
extern const char * const guni_name_range_prefix[GUNI_NAME_RANGE_COUNT];
/** 1 for the Hangul rule, which spells jamo rather than hex. */
extern const uint8_t guni_name_range_hangul[GUNI_NAME_RANGE_COUNT];

/** The jamo short names, by index within their part of a syllable. */
extern const char * const guni_jamo_lead[19];
extern const char * const guni_jamo_vowel[21];
extern const char * const guni_jamo_trail[28];

/** Named sequences, in loose-name order. */
extern const uint16_t guni_sequence_tokens[GUNI_SEQUENCE_TOKEN_COUNT];
extern const uint32_t guni_sequence_offsets[GUNI_SEQUENCE_COUNT + 1];
extern const uint32_t guni_sequence_pool[GUNI_SEQUENCE_POOL_COUNT];
extern const uint32_t guni_sequence_pool_offsets[GUNI_SEQUENCE_COUNT + 1];

#endif // GHOTI_IO_GUNI_NAME_TABLES_H
""")

    with open_out(out_dir, "src/name/tables/name_data.c") as out:
        out.write(LICENSE_NOTICE)
        out.write("\n")
        out.write(generated_notice(ucd.version,
                                  "UnicodeData.txt, NameAliases.txt, "
                                  "NamedSequences.txt and Jamo.txt"))
        out.write("\n#include \"name_tables.h\"\n")
        out.write("\nconst char * const guni_name_words[GUNI_NAME_WORD_COUNT] = {\n")
        emit_array(out, tables.name_words, 6, c_string)
        out.write("};\n")
        out.write("\nconst uint16_t guni_name_tokens[GUNI_NAME_TOKEN_COUNT] = {\n")
        emit_array(out, tables.name_tokens, 12, lambda v: "0x%04Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_name_offsets[GUNI_NAME_COUNT + 1] = {\n")
        emit_array(out, tables.name_offsets, 12)
        out.write("};\n")
        out.write("\nconst uint32_t guni_name_codepoints[GUNI_NAME_COUNT] = {\n")
        emit_array(out, tables.name_codepoints, 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_name_loose_order[GUNI_NAME_COUNT] = {\n")
        emit_array(out, tables.name_loose_order, 12)
        out.write("};\n")
        out.write("\nconst uint32_t guni_name_primary_cp[GUNI_NAME_PRIMARY_COUNT] = {\n")
        emit_array(out, tables.name_primary_cp, 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_name_primary_index"
                  "[GUNI_NAME_PRIMARY_COUNT] = {\n")
        emit_array(out, tables.name_primary_index, 12)
        out.write("};\n")
        out.write("\nconst uint32_t guni_name_alias_cp[GUNI_NAME_ALIAS_COUNT] = {\n")
        emit_array(out, [row[0] for row in tables.name_aliases], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint8_t guni_name_alias_kind[GUNI_NAME_ALIAS_COUNT] = {\n")
        emit_array(out, [row[1] for row in tables.name_aliases], 20)
        out.write("};\n")
        out.write("\nconst uint32_t guni_name_alias_index"
                  "[GUNI_NAME_ALIAS_COUNT] = {\n")
        emit_array(out, [row[2] for row in tables.name_aliases], 12)
        out.write("};\n")
        out.write("\nconst uint32_t guni_name_range_first[GUNI_NAME_RANGE_COUNT] = {\n")
        emit_array(out, [row[0] for row in tables.name_ranges], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_name_range_last[GUNI_NAME_RANGE_COUNT] = {\n")
        emit_array(out, [row[1] for row in tables.name_ranges], 8,
                   lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst char * const guni_name_range_prefix"
                  "[GUNI_NAME_RANGE_COUNT] = {\n")
        emit_array(out, [row[2] for row in tables.name_ranges], 3, c_string)
        out.write("};\n")
        out.write("\nconst uint8_t guni_name_range_hangul[GUNI_NAME_RANGE_COUNT] = {\n")
        emit_array(out, [1 if row[3] else 0 for row in tables.name_ranges], 20)
        out.write("};\n")
        for name, values in (("lead", tables.jamo_lead),
                             ("vowel", tables.jamo_vowel),
                             ("trail", tables.jamo_trail)):
            out.write("\nconst char * const guni_jamo_%s[%d] = {\n"
                      % (name, len(values)))
            emit_array(out, values, 8, c_string)
            out.write("};\n")
        out.write("\nconst uint16_t guni_sequence_tokens"
                  "[GUNI_SEQUENCE_TOKEN_COUNT] = {\n")
        emit_array(out, tables.sequence_tokens, 12, lambda v: "0x%04Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_sequence_offsets[GUNI_SEQUENCE_COUNT + 1] = {\n")
        emit_array(out, tables.sequence_offsets, 12)
        out.write("};\n")
        out.write("\nconst uint32_t guni_sequence_pool[GUNI_SEQUENCE_POOL_COUNT] = {\n")
        emit_array(out, tables.sequence_pool, 8, lambda v: "0x%06Xu" % v)
        out.write("};\n")
        out.write("\nconst uint32_t guni_sequence_pool_offsets"
                  "[GUNI_SEQUENCE_COUNT + 1] = {\n")
        emit_array(out, tables.sequence_pool_offsets, 12)
        out.write("};\n")


def emit_names_data(ucd, tables, entries, out_dir):
    prop_numbering = tables.numbering["GUNI_Property"]
    with open_out(out_dir, "src/char/tables/names_data.c") as out:
        out.write(LICENSE_NOTICE)
        out.write("\n")
        out.write(generated_notice(ucd.version,
                                  "PropertyAliases.txt and PropertyValueAliases.txt"))
        out.write("\n#include \"tables.h\"\n")
        out.write("\n/* Every value's long alias, for guni_value_name() and for the\n"
                  " * sweep fixture's canonical text form. \"\" where the UCD defines no\n"
                  " * value with that number, which happens for Canonical_Combining_Class:\n"
                  " * the class is a number 0..254 and only 58 of them are named. */\n")
        for index, entry in enumerate(entries):
            out.write("\nstatic const char * const guni_value_names_%d[] = {\n" % index)
            emit_array(out, entry.value_names, 4, c_string)
            out.write("};\n")
        out.write("\nconst GuniPropertyInfo guni_property_info[GUNI_PROPERTY_COUNT] = {\n")
        rows = {}
        for index, entry in enumerate(entries):
            rows[prop_numbering.values[entry.long_name]] = (
                "  [%s] = {%s, guni_value_names_%d, %d, %s, %d, %d},\n"
                % (prop_numbering.member(entry.long_name), c_string(entry.long_name),
                   index, len(entry.value_names), entry.kind,
                   entry.bit if entry.bit is not None else 0,
                   entry.qc_index if entry.qc_index is not None else 0))
        for value in sorted(rows):
            out.write(rows[value])
        out.write("};\n")

        out.write("\n/* Loose-matched (UAX #44-LM3) property spellings, sorted, for\n"
                  " * binary search: case, whitespace, '_' and '-' are already removed\n"
                  " * from the stored form and guni_property_by_name() removes them from\n"
                  " * the query. */\n")
        out.write("const GuniPropertyAlias guni_property_aliases"
                  "[GUNI_PROPERTY_ALIAS_COUNT] = {\n")
        for name, index in tables.property_aliases:
            out.write("  {%s, %s},\n"
                      % (c_string(name),
                         prop_numbering.member(entries[index].long_name)))
        out.write("};\n")
        out.write("\nconst GuniValueAlias guni_value_aliases[GUNI_VALUE_ALIAS_COUNT] = {\n")
        for name, index, value in tables.value_aliases:
            out.write("  {%s, %s, %d},\n"
                      % (c_string(name),
                         prop_numbering.member(entries[index].long_name), value))
        out.write("};\n")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ucd", help="the unpacked UCD directory")
    parser.add_argument("--out", default=root,
                        help="where to write; the repository root by default")
    parser.add_argument("--version", help="override tools/ucd/UCD_VERSION")
    args = parser.parse_args(argv[1:])

    version = args.version or open(os.path.join(here, "UCD_VERSION"),
                                   encoding="utf-8").read().strip()
    ucd_dir = args.ucd or os.path.join(root, "third_party", "ucd", version)
    if not os.path.isdir(ucd_dir):
        raise SystemExit("%s does not exist. Run tools/ucd/fetch.sh first; the "
                         "UCD is not committed (design.md section 5.1)." % ucd_dir)

    # The committed header is where the enum numbering lives, and it is read
    # from the repository even when writing elsewhere: "make check-ucd-tables"
    # generates into a scratch directory and must reproduce the committed
    # numbering, not invent a fresh one.
    committed = read_committed_enums(
        os.path.join(root, "include", "ghoti.io", "unicode", "enums.h"))

    sys.stderr.write("reading UCD %s from %s\n" % (version, ucd_dir))
    ucd = Ucd(ucd_dir, version)
    tables = Tables(ucd, committed)
    entries = build_property_entries(ucd, tables)
    numbering = EnumNumbering("GUNI_Property", "GUNI_PROPERTY")
    numbering.assign([entry.long_name for entry in entries], committed)
    numbering.check(committed)
    tables.numbering["GUNI_Property"] = numbering
    tables.property_aliases, tables.value_aliases = build_alias_tables(
        ucd, tables, entries)

    emit_enums(ucd, tables, entries, args.out)
    emit_tables_header(ucd, tables, entries, args.out)
    emit_props_data(ucd, tables, entries, args.out)
    emit_misc_data(ucd, tables, args.out)
    emit_norm_data(ucd, tables, args.out)
    emit_case_data(ucd, tables, args.out)
    emit_script_data(ucd, tables, args.out)
    emit_bidi_data(ucd, tables, args.out)
    emit_name_data(ucd, tables, args.out)
    emit_names_data(ucd, tables, entries, args.out)

    sys.stderr.write(
        "%d distinct records, %d stage-2 blocks, %d runs, %d scx words, "
        "%d properties, %d value spellings, %d decompositions in %d words, "
        "%d composition pairs, %d mirrors, %d brackets, %d case rows, "
        "%d orbits, %d script sets, %d digit blocks\n"
        % (len(tables.records), len(tables.stage2) // BLOCK_SIZE,
           len(tables.runs), len(tables.scx_pool), len(entries),
           len(tables.value_aliases), len(tables.decomp_rows),
           len(tables.decomp_pool), len(tables.compose), len(ucd.mirroring),
           len(ucd.brackets), len(tables.case_rows), len(tables.orbit_rows),
           len(tables.script_run_sets), len(tables.digit_zeros)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
