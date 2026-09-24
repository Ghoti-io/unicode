# The design of ghoti.io-unicode

**Status:** phases A, B and C are built - every module in §3.1, both tiers,
the generator, the exhaustive sweep, and the conformance gates for
normalisation, bidi and all four segmentations. What is left is the
migrations, D to F, and they are the consumers' own commits. This page
says what will exist and why, so that the code can be judged against it
rather than the other way round. A change of mind lands here first, in the
same commit as the code that needs it (`CONVENTIONS.md` §9), and §16 marks
what is built. §17 lists what the code decided differently from this page,
and why. The workspace's `notes/suite/UNICODE-LIBRARY.md` records the
decision to build this library and the inventory that motivated it.

`unicode` is the suite's Unicode library: the Character Database as generated
tables, and the algorithms the Unicode Standard Annexes define over them -
normalisation, segmentation, line breaking, bidirectional ordering, case
mapping, and the properties a shaper needs. It exists because three libraries
here need this and two of them have already written it: `regex` carries a
full UAX #29 and UAX #14 implementation and 54,736 lines of generated tables;
`text` carries NFC and 7,341 lines of its own; `ctang` links ICU to get one
grapheme-cluster iterator; and `font`, the next library, would be the third
copy. The full inventory is in `notes/suite/UNICODE-LIBRARY.md` §1.

The prefix is `GUNI_` / `guni_`. The package is `ghoti.io-unicode-0`, the
include path `<ghoti.io/unicode/...>`. The include path sits beside ICU's
`<unicode/...>` in any file that has both, which `ctang` will until §16 phase
F; the `ghoti.io/` component keeps them apart at compile time and a reader has
to be told.

---

## 1. What the library is for

The brief is a correct, cross-platform, dependency-free C implementation of
the parts of Unicode that a text-processing library needs, that an enterprise
can build on, that every library in this suite uses rather than approximates,
and that no future library here has to write again. Each word is a mechanism:

| Property | Mechanism |
| --- | --- |
| **Correct** | Every table is generated from the Unicode Character Database at a pinned version, never typed (§5). Every algorithm is one the Standard specifies in a numbered annex and publishes a conformance file for, and that file is the gate (§12). Every property answer is checked **exhaustively** - all 1,114,112 codepoints - against a second source, not sampled (§12.1). |
| **Cross-platform** | Tier 0 (§3) touches no operating-system API, reads no locale, opens no file. It is pure functions over integers and bytes and behaves identically everywhere by construction. There is no tier that touches the OS at all. |
| **Dependency-free** | The only link dependency is `cutil`, for the allocator vtable, checked size arithmetic, and UTF-8/UTF-16 conversion. ICU is an *oracle* in the tests and is never linked (§12). CLDR is not shipped, not fetched, not read (§2 M6, §9). |
| **Enterprise-ready** | No global state, no `setlocale`, no environment read (§3.6). Every function is reentrant over immutable `const` tables (§13.3). The data that answered a question is queryable - `guni_ucd_version()` - because "which Unicode version classified this string" is an audit question (§5.4). Symbols are namespaced per `CONVENTIONS.md` §4, so two versions can be loaded in one process. |
| **Useful** | Three consumers exist today and their exact needs are enumerated in §11, with a fourth (`font`) designed against it. Both call shapes each consumer uses - set-oriented for a regex compiler, point-oriented for a shaper - come from one generator and are proven to agree (§4.2). |
| **Reusable** | The generator is the asset, not any particular table. Adding a property is a change to `tools/ucd/gen_tables.py` and nothing else (§5.3). That is the mechanism behind "no future library does this again". |

### 1.1 The shape is borrowed, deliberately

ICU's `uchar.h`, `unorm2.h`, `ubrk.h` and `ubidi.h` are the reference for
*what* a Unicode library exposes, and thirty years of consumers have found
their shape workable. Where this library departs from ICU, §2 names the
reason. Where it does not, the reader can assume the ICU semantics apply and
the ICU conformance behaviour is the target.

### 1.2 The threat model

Input to this library is text the caller got from somewhere else. It may be
invalid UTF-8, may contain surrogates, noncharacters, unassigned codepoints,
private-use codepoints, values above `0x10FFFF` if the caller decoded badly,
and sequences designed to make a normaliser or a bidi resolver allocate
without bound. Every function therefore defines its behaviour for **every**
32-bit input value (§4.4), every output is bounded by a documented expansion
factor (§6.3), and every algorithm with a recursion or a stack has its depth
fixed by the Standard and enforced (§7.2). Nothing here trusts its input, and
nothing here can be made to read past a buffer by any input.

---

## 2. Mistakes this library exists not to repeat

The suite's convention is that a rule names the defect it prevents. The first
five are this suite's own; the rest are the field's.

| # | The mistake | Where it happened | What `unicode` does instead |
| --- | --- | --- | --- |
| M1 | The same tables generated three times by three libraries, from three generators, with three version pins nothing checks | This suite: `regex` and `text` at UCD 17.0.0 by coincidence, `font` about to be the third | One generator, one pin, one committed output, and a suite-level check that every `UCD_VERSION` file agrees (§5.4) |
| M2 | A tailoring decision baked into a shared table at generation time | `regex`'s generator applies UAX #14's LB1 while generating: `CJ` becomes `NS`, which *is* CSS `line-break: strict`; a layout engine needs the other two | Tables carry the unresolved classes; LB1 is a function the caller applies with a policy (§7.3) |
| M3 | A UTF-16 API, so every UTF-8 program converts on the way in and out | ICU's `u_*` functions; `ctang`'s `u_strFromUTF8`/`u_strToUTF8` are half its ICU call sites | UTF-8 is the primary encoding; boundaries are byte offsets; a codepoint-array entry point stands beside it, and no UTF-16 API exists (§4.1) |
| M4 | A conformance gate that skips when its data is absent, with no second gate | `regex`'s `test_break.cpp` skips without `third_party/ucd/`; defensible there because the Perl differential also exists, indefensible for a shared owner | The conformance files are committed; the gate cannot skip (§12.2) |
| M5 | One consumer's call shape imposed on the next consumer | Would have been `font` looping a per-position query written for `regex` | Both shapes - point query and bulk iteration - are first-class and generated from one source (§4.2) |
| M6 | Character data and locale data in one library, so linking one means shipping the other | ICU: 30 MB of data, most of it CLDR, needed by nobody who wanted a grapheme iterator | Unicode Character Database only. Locale tailorings arrive through provider vtables (§9), as `chron` §8.5 and `text`'s regex provider already do. `chron` §14 states the position: shipping CLDR is shipping ICU's problem |
| M7 | Global mutable state: a process-wide locale, a default converter, a static break iterator | `setlocale`, `LC_CTYPE`-dependent `iswalpha`, ICU's default locale | No globals. No function reads the environment. State an algorithm needs lives in a caller-owned struct (§3.6) |
| M8 | Undefined behaviour on an out-of-range codepoint | `towupper(0x110000)`; table lookups indexed by unchecked input | Every function defines its answer for all of `uint32_t` (§4.4): the Standard's answer up to `0x10FFFF`, `GUNI_ERR_INVALID` or the identity above it, never a read past a table |
| M9 | Normalisation output that can grow without a stated bound | Callers guessing `2 * len` and overflowing on `U+FDFA` (NFKD: 18 codepoints) | Expansion factors per form are documented and checked (§6.3); output is caller-buffer with required-length reporting, `chron` §8.6's contract |
| M10 | Invalid UTF-8 silently replaced, or silently accepted | Every decoder that emits `U+FFFD` without being asked; every one that passes surrogates through | A `GUNI_Invalid` policy on every UTF-8 entry point; zero is `REFUSE` (§4.3) |
| M11 | A bidi resolver with no depth limit, or one that silently truncates | Pre-6.3 implementations without isolate handling; embedding stacks that overflow | UAX #9's `max_depth` of 125 is enforced as the Standard says, with the overflow behaviour the Standard specifies (§7.2) |
| M12 | Enum values renumbered between data versions, so a stored property value changes meaning | Any library whose script enum is regenerated from a sorted list | Generated enums append and never renumber; the committed diff on upgrade is the review artifact (§5.5) |
| M13 | Case mapping that is context-free when the Standard says it is not | `toupper('ß')` → `ß`; final sigma handled nowhere; Turkish `i` handled by locale side-effect | Full mappings with `SpecialCasing` conditions, context passed explicitly, and the three language-sensitive cases as an enum argument - which is UCD data, not CLDR (§8) |
| M14 | Line breaking that claims to handle Thai | UAX #14 assigns class `SA` and says "use a dictionary"; libraries that resolve `SA` to `AL` and say nothing | `SA` runs go to a provider; without one there is no interior break, and the header says so in those words (§9.1) |
| M15 | A "compatibility" character name lookup that cannot find `LATIN SMALL LETTER A` because of a hyphen | Implementations ignoring UAX #44-LM2 | Loose matching per UAX #44, as `regex` already does; kept in the tier-1 module (§10) |
| M16 | Half a megabyte of character names linked by a program that wanted a grapheme iterator | Any monolithic Unicode library | Tiers (§3): names are tier 1, in their own translation units, behind their own header, and `make check-layering` keeps tier 0 from reaching them |

---

## 3. Tiers and modules

Two tiers, split by what a consumer pays for. **Nothing in tier 0 includes a
tier-1 header**, and `make check-layering` greps for it and fails naming the
file, as `chron` does.

| Tier | Holds | Needs | Consumers |
| --- | --- | --- | --- |
| 0 | every property, every algorithm | nothing: no OS, no file, no locale | `text`, `regex`, `font`, `ctang` |
| 1 | character names, aliases, named sequences | nothing either - the split is about *size*, not dependencies | `regex` |

Tier 1 exists because `tables_names.c` is 31,603 lines, more than half the
generated bulk of everything being consolidated, and only `regex` has ever
wanted it. In a shared library everything ships, and demand-paged `.rodata`
means untouched tables cost address space rather than memory - but the split
is still worth enforcing, because the next consumer who *does* want names
should find them behind a header and not woven through `char.h`.

### 3.1 The modules

| Header | Module | Holds | Tier |
| --- | --- | --- | :-: |
| `core.h` | core | `GUNI_Result`, `GUNI_Limits`, the version query, `GUNI_Error` | 0 |
| `utf.h` | encoding | UTF-8 decode/encode with the `GUNI_Invalid` policy; codepoint iteration; what `cutil`'s `utf.h` does not cover | 0 |
| `char.h` | properties | general category, script, `Script_Extensions`, canonical combining class, East Asian Width, block, numeric type and value, the binary properties (`Alphabetic`, `White_Space`, `Extended_Pictographic`, ...), `Bidi_Class`, `Bidi_Mirrored` and the mirror, `Joining_Type`, `Joining_Group`, `Indic_Syllabic_Category`, `Indic_Positional_Category`, `Indic_Conjunct_Break`, `Vertical_Orientation`, the emoji properties, `Hangul_Syllable_Type`, `Line_Break` (unresolved), the four segmentation properties, decomposition type | 0 |
| `set.h` | sets | the same properties as **range lists** for a regex compiler: "every codepoint whose script is Greek"; property-name and value-name lookup with UAX #44 loose matching | 0 |
| `script.h` | script runs | script-run segmentation with `Script_Extensions` and paired-bracket handling: UTS #39's notion, which is also a shaper's itemiser | 0 |
| `case.h` | case | simple and full upper, lower, title, fold; `SpecialCasing` conditions; the fold orbits `regex` needs for case-insensitive classes | 0 |
| `norm.h` | normalisation | NFC, NFD, NFKC, NFKD; the quick-check properties; canonical ordering; Hangul composition and decomposition by algorithm | 0 |
| `break.h` | segmentation | UAX #29 grapheme, word and sentence boundaries; UAX #14 line-break opportunities with LB1 exposed; point query and iterator forms; the `SA` provider seam | 0 |
| `bidi.h` | bidirectional | UAX #9: paragraph level, resolved embedding levels, isolates, line reordering, mirroring | 0 |
| `name.h` | names | UAX #44 character names including the algorithmic ones, name aliases, named sequences; name → codepoint with loose matching | **1** |
| `unicode.h` | umbrella | everything in tier 0 | 0 |

`vim_class.c` and the ECMAScript legacy case rules stay in `regex`: they are
dialect features that happen to consume Unicode data, not Unicode services.
IDNA2008 and UTS #46 stay in `text` for now (§15.3).

---

## 4. The API contract

### 4.1 UTF-8 first, codepoints second, UTF-16 never

Every algorithm has a UTF-8 entry point that takes `const char *, size_t` and
reports positions as **byte offsets** into that buffer, and a codepoint entry
point that takes `const uint32_t *, size_t` and reports indices. `regex`'s
existing break API is already the first shape and `text`'s NFC is already the
second; both are kept. There is no UTF-16 entry point: `cutil`'s `utf.h`
converts, and a caller holding UTF-16 converts once at the edge rather than
this library carrying a third copy of every function.

Byte offsets are the natural currency of a layout engine's cluster map (a
character range in the source string maps to glyphs) and of a regex engine's
match positions, and they are what makes M3 go away.

### 4.2 Two call shapes, one source

A regex compiler asks *"which codepoints have Script=Greek"* and wants a list of
ranges to build a class from. A shaper asks *"what is the script of U+03B1"* a
million times a second and wants a constant-time answer. These are the same
data in two layouts:

- **`char.h` is point-oriented.** Every property is a field of one record,
  and one two-stage trie maps a codepoint to it: `cp >> 6` indexes stage 1,
  the low six bits index stage 2, and identical 64-codepoint blocks are
  shared. Three dependent loads, no branch. Measured at UCD 17.0.0: 1,114,112
  codepoints have **2,374 distinct records**, because properties correlate -
  a codepoint's script very nearly determines its bidi class, its line-break
  class and its Indic categories. Storing the tuple once is what makes every
  property cost the same lookup and keeps the whole thing to 316 KB of
  `.rodata` for 97 properties.
- **`set.h` is set-oriented**, and **materialises** its ranges rather than
  reading a committed array per value. The generator emits one more table
  from the same map: the **8,035 maximal runs** over which the record is
  constant. `guni_set_ranges()` filters those runs, merges the ones that
  touch, and follows the output contract of §4.5 - ask with a cap of 0 to
  learn the count, then ask again. A regex compiler pays one walk of 8,035
  entries per property per pattern.

This is a departure from the first draft of this section, which specified a
committed sorted `GUNI_Range` array per property value and a test to prove the
two layouts agreed. Materialising is better on both counts: the range arrays
would have been about 320 KB of a second copy of the data - which is M1 in
miniature - and "the point query and the set enumeration cannot disagree
because they are not two sources" became true of the construction rather than
a claim resting on a test.

The test still exists, because a claim that costs nothing to check is worth
checking: `tests/unit/test_sweep.cpp` walks all 1,114,112 codepoints asserting
that the trie's record is the record the runs table names, that every run is
maximal, and that every range `set.h` reports contains its endpoints and not
the codepoints either side of them.

Segmentation likewise has both shapes: `guni_break_at()` answers one position
(the `regex` shape, where the engine is already at an offset and asks whether
it is a boundary), and `guni_break_iter_*()` walks a buffer (the `font` shape,
where a paragraph is segmented once), with `guni_break_all()` for a caller
that wants the whole table at once - "shape once, break many".

**The point query is the primitive and the iterator is defined in terms of
it**, which is the opposite of what this section first said. The reason is
that the rules themselves are written as "is there a boundary between these
two characters": UAX #29 and UAX #14 are both stated that way, so an
implementation whose primitive is the walk has to invert every rule. One
engine either way, so neither can disagree with the other; this way the code
reads like the Standard.

### 4.3 Invalid input is a policy, and zero refuses

Every UTF-8 entry point takes a `GUNI_Invalid`:

```c
typedef enum {
  GUNI_INVALID_REFUSE = 0,   /* GUNI_ERR_INVALID, with the byte offset */
  GUNI_INVALID_REPLACE,      /* U+FFFD per the maximal-subpart practice */
  GUNI_INVALID_SKIP          /* drop the bytes; for diagnostics only */
} GUNI_Invalid;
```

Zero refuses, following `chron` §3.7: a caller who wants leniency writes the
word. `REPLACE` follows the Unicode Standard §3.9 "maximal subpart" recommendation
(the same one WHATWG and Python use), so that `\xE1\x80` followed by ASCII
produces one `U+FFFD`, not two - an implementation detail two libraries in the
field disagree on and this one pins.

Surrogate codepoints (`0xD800`-`0xDFFF`) encoded in UTF-8 are invalid, per the
Standard, and are handled by the same policy; a caller with CESU-8 or WTF-8
data converts at the edge.

### 4.4 Every function answers for every `uint32_t`

A property function called with any value up to `0x10FFFF` returns the
Standard's answer, which for unassigned codepoints is `Cn`, `Unknown`,
`Not_Reordered`, and so on - real values, not errors. Called with a value above
`0x10FFFF`, a property function returns the same answer as for an unassigned
codepoint and a mapping function returns its input; neither indexes a table
with it. Functions that return a `GUNI_Result` return `GUNI_ERR_INVALID` for
such a value. There is no input to any function in this library that produces
undefined behaviour, and `tests/unit/test_range.cpp` calls every property
function at `0`, `0x10FFFF`, `0x110000`, `0xFFFFFFFF` and every surrogate and
noncharacter to prove it.

### 4.5 Output contract

Functions whose output length the caller cannot know in advance -
normalisation, case mapping with full mappings, bidi reordering - follow
`chron` §8.6: write into a caller buffer, report the length the output needs,
return `GUNI_ERR_LIMIT` when the buffer is too small with `out_len` set to the
requirement, and leave the buffer's contents unspecified on failure. There is
no allocating variant in the first release. §6.3 gives the bounds a caller can
size a buffer from without a preflight call.

Functions over single codepoints - every property, every simple case mapping,
the mirror - are pure, take no allocator, touch no buffer, and cannot fail.
This is most of the library, and it is what makes tier 0 usable from a signal
handler or a JIT's runtime, should either ever want it.

### 4.6 Results

`GUNI_Result` is the fixed vocabulary of `CONVENTIONS.md` §5 - `OK`, `ERR_INVALID`,
`ERR_LIMIT`, `ERR_OOM`, `ERR_INTERNAL`, `ERR_UNSUPPORTED` - with no additions.
`ERR_IO`, `ERR_FORMAT` and `ERR_CORRUPT` are in the enum for uniformity and
nothing here returns them: the library reads no file and parses no format. A
`GUNI_Error` carries a byte offset and the codepoint at fault, as `chron`'s
`GCHRON_Error` carries a position (§7.3 there), because "invalid UTF-8
somewhere" is not a diagnostic.

---

## 5. The data

### 5.1 What is fetched and what is committed

The Unicode Character Database is fetched by `tools/ucd/fetch.sh` into
`third_party/ucd/<version>/`, which is gitignored, exactly as `regex` does
today. The **generated tables are committed**, under `src/*/tables/`, because
they are what a silent change would change the behaviour of, and a clone must
build without network access. The **conformance files are committed too**,
under `tests/data/ucd/<version>/`, which is the departure from `regex` (M4):
`GraphemeBreakTest.txt`, `WordBreakTest.txt`, `SentenceBreakTest.txt`,
`LineBreakTest.txt`, `NormalizationTest.txt`, `BidiTest.txt` and
`BidiCharacterTest.txt`, under the Unicode License v3 with its notice in
`tests/data/ucd/LICENSE`. A clone's `make test` runs every conformance gate
with no fetch and no skip.

Measured rather than estimated: the seven are **21 MB** on disk, of which git
stores 2.3 MB - they are repetitive text and compress by nine to one. Each
arrives in the commit that adds the runner that reads it, so that no data file
sits in the repository with nothing testing it.

The emoji data files (`emoji-data.txt`, `emoji-sequences.txt`,
`emoji-zwj-sequences.txt`, `emoji-variation-sequences.txt`) are published
alongside the UCD under `Public/emoji/` and `Public/<version>/ucd/emoji/`
rather than in it, under the same licence; `fetch.sh` fetches them with the
UCD and they are pinned to the same version, as they have been since Emoji 11
tracked Unicode 11.

### 5.2 `make check-ucd-tables`

Regenerates every table into a scratch directory and fails when any committed
file differs byte for byte, as `regex`'s `check-unicode-tables` and `text`'s
equivalent do today. It catches a generator changed without a regeneration, a
table edited by hand, and - on upgrade - produces the diff a reviewer reads.

**It is not in `TEST_GATES`**, which is a departure from this section's first
draft. It cannot be: it needs `third_party/ucd/`, which is fetched and not
committed, so in `TEST_GATES` it would either fail on every fresh clone or
skip when its data is absent - and a gate that skips when its data is absent
is M4, the mistake this library was partly built to stop repeating. The
resolution is not to weaken it but to have a second gate cover the same
ground from committed data: **`make test` checks the tables against
`tests/data/sweep/<version>.sums`** (§12.1), which a second, independent
parser produced. An edited table therefore fails on a clone with no network
and no Python; `check-ucd-tables` runs wherever the UCD is present and is
required before a release. Without the UCD it fails, loudly, naming
`tools/ucd/fetch.sh`.

### 5.3 The generator is the product

`tools/ucd/gen_tables.py` is one script that parses every UCD file this library
uses and emits every table. It is a direct descendant of `regex`'s
`gen_tables.py`, which already handles the `;`-separated range syntax, the
`@missing` lines, the derived-property files and the property-value alias
resolution, and which is the part of the consolidation that moves with the
least change.

The rule that makes "no future library does this again" true: **adding a
property is a change to the generator's property list and a new function in
`char.h`, and nothing else.** No new file format, no new emission path, no new
test harness - the exhaustive agreement test (§4.2) and the range fixture
(§12.1) pick the new property up from the same list. If adding a property ever
needs more than that, the generator has grown a special case and the special
case is the bug.

### 5.4 One version, one pin, queryable

`tools/ucd/UCD_VERSION` holds the version, `17.0.0` at the time of writing.
`fetch.sh` reads it, `gen_tables.py` embeds it, `guni_ucd_version()` returns it
as a string and `guni_ucd_version_number()` as `GUNI_MAKE_VERSION(17, 0, 0)`,
so that a consumer can log which Unicode version classified a string. That is
an audit answer, for the same reason `chron`'s tzdata version is one.

The suite-level check: `tools/check-ucd-pins.sh` in the workspace reads every
`UCD_VERSION` in every library that has one and fails if they differ. Today
that is `regex` and `text`; after §16 it is this library alone and the check is
trivially true, which is the point.

### 5.5 Upgrading Unicode

When Unicode 18 is published, the process is a checklist, and it is written
here so that it is followed rather than rediscovered:

1. Change `UCD_VERSION`; run `tools/ucd/fetch.sh`.
2. `make gen-ucd-tables`; read the diff. **Generated enums append and never
   renumber** (M12). The mechanism is that `include/ghoti.io/unicode/enums.h`
   *is* the record: the generator reads the committed header first and adopts
   every value it finds, so a new script lands on the first free number and
   nothing existing can move. A member the new UCD no longer defines is kept,
   with a comment, rather than dropped - Unicode has renamed a property value
   before. What the generator refuses is the two ways that record can be
   *invalid*: a value that appears twice, which would collapse two property
   values into one and which no test of the tables could catch because the
   tables would agree with the header; and a value too wide for the record
   field it is stored in, which would truncate. To renumber deliberately -
   which breaks every consumer that stored a value - the enum has to be
   deleted from the header by hand first.
3. Copy the new conformance files into `tests/data/ucd/<version>/`; delete the
   old directory.
4. `make test`. Every conformance gate runs against the new files. A UAX
   rule change - and there is one most years - fails here, in the algorithm
   that needs updating, with the failing line from the Standard's own file.
5. The exhaustive-sweep checksums (§12.1) *will* change; regenerate them and
   commit. The range fixture's diff is the human-readable record of what
   changed.
6. Bump `MINOR_VERSION`. A Unicode upgrade is an observable behaviour change
   in every consumer and is versioned as one.

---

## 6. Normalisation

### 6.1 The four forms and the algorithm

UAX #15's four forms - NFD, NFC, NFKD, NFKC - over the canonical and
compatibility decomposition mappings, the canonical combining class ordering,
the composition exclusions, and the algorithmic Hangul syllable
composition/decomposition (Standard §3.12). `text` has NFC today; the other
three are new, and NFD in particular is what a shaper needs to decompose a
precomposed character into base plus mark when a font has the mark and not
the composite.

### 6.2 Quick check

The `NFC_QC`, `NFD_QC`, `NFKC_QC` and `NFKD_QC` properties are exposed as
`guni_norm_quick_check(form, text)` returning `YES`, `NO` or `MAYBE` per UAX
#15 §9, so that a caller can skip normalising text that is already normalised -
which is nearly all text. A `MAYBE` means the full algorithm runs; the function
never guesses.

### 6.3 Bounds

The expansion of one codepoint under each form is bounded by the data, and the
bound is a generated constant a caller can size a buffer from without a
preflight:

| Form | Max codepoints out per codepoint in | Witness |
| --- | ---: | --- |
| NFD | 4 | `U+1F82` |
| NFC | 3 | composition can leave decomposed marks behind |
| NFKD | 18 | `U+FDFA` ARABIC LIGATURE SALLALLAHOU ALAYHE WASALLAM |
| NFKC | 18 | the same |

`GUNI_NORM_MAX_EXPANSION_NFD` and its three siblings are emitted by the
generator from the actual data and checked by a test that decomposes every
codepoint. `text`'s `nfc.c` today carries `4 * len` as a comment; here it is a
constant that would fail a build if the data ever exceeded it.

### 6.4 Stream-safe text, and where the working buffer comes from

UAX #15 §13's Stream-Safe Text Format - at most 30 non-starters in a row - is
what makes normalising a stream in bounded memory possible. The library
exposes the check and the transform (`guni_stream_safe()`).

**What was built, which is not quite what this section first said.** The
normaliser needs no working buffer at all, and so has no bound to state: it
decomposes and canonically orders *into the caller's output buffer*, inserting
each mark into the trailing run as it goes, and then composes in place over
what it wrote. The cost of that is one thing a caller has to know, and it is
in `norm.h`: the composing forms need the buffer to hold the **intermediate
decomposition**, which can be longer than the result, so `GUNI_ERR_LIMIT`
reports a sufficient length rather than the exact one and success reports the
exact one.

The one function with a fixed internal buffer is `guni_normalize_utf8()`,
because UTF-8 in and UTF-8 out cannot be reordered in place. It processes the
text between **normalisation boundaries** - a starter whose quick-check
property is `YES`, a position where the text before and after normalise
independently - with a 512-codepoint window. Real text has a boundary at
nearly every character. A run of more than 512 codepoints without one is
`GUNI_ERR_LIMIT`, and `guni_stream_safe()` is the documented way through;
refusing beats truncating, and beats growing a buffer whose size the input
chose. The codepoint entry point has no such limit, because it works in the
caller's buffer and there is nothing to overflow.

---

## 7. Segmentation, line breaking and bidi

### 7.1 UAX #29 and UAX #14

The grapheme, word and sentence boundary rules of UAX #29 and the line-break
opportunities of UAX #14, at the pinned Unicode version, exactly as
`regex/src/unicode/break.c` implements them today: the rule tables, the
`Extended_Pictographic` and regional-indicator handling, GB9c's
`Indic_Conjunct_Break`, the East Asian Width consultation in LB19a and LB30.
That code moves in essentially unchanged; what changes is described in the
next two subsections.

### 7.2 UAX #9

New. The bidirectional algorithm, Unicode 6.3 and later: paragraph level
determination (P1-P3), explicit embeddings and isolates (X1-X10) with the
`max_depth` of 125 and the overflow counters the Standard specifies, weak
types (W1-W7), neutrals and isolates (N0-N2, including the paired-bracket
algorithm of BD16), implicit levels (I1-I2), and the per-line steps (L1-L4):
trailing whitespace reset, reordering, and mirroring.

The API takes a paragraph as codepoints or UTF-8 and a requested direction
(`LTR`, `RTL`, or `AUTO` for P2/P3), writes one resolved level per input
character into a caller buffer, and separately reorders one line given the
levels and the line's bounds. Levels are kept rather than the reordered text,
because a layout engine reorders *glyph runs*, not characters, and needs the
levels to do it. `guni_bidi_mirror(cp)` is the `Bidi_Mirroring_Glyph` lookup
for L4.

Two conformance files gate it and both are exhaustive over their domain:
`BidiTest.txt` enumerates every sequence of bidi classes up to a length and
gives levels and reorderings for each of the three paragraph directions;
`BidiCharacterTest.txt` does the same over real codepoints, which is what
exercises the bracket-pair rule. Measured: **770,241 cases** from the first
and **91,707** from the second, and they earned their place immediately by
finding two defects that every hand-written test had passed:

- **rules I1 and I2 were resolving into the array rule X10 reads.** X10 takes
  each isolating run sequence's `sos` and `eos` from the levels rules X1-X9
  assigned, and I1/I2 *raise* those levels; resolving in place made one
  sequence's boundary depend on another's resolution. 61 of
  `BidiCharacterTest.txt`'s cases, and none of the obvious ones. The fix is a
  separate `embedding` array, which is why the resolver's working state is
  eleven bytes per character rather than ten.
- **rule X6 excludes `BN` and the code did not.** An active override was
  rewriting a Boundary_Neutral's class to `L` or `R`, which took it out of the
  set X9 removes and put it into the rules as a strong character. 25 of
  `BidiTest.txt`'s 770,241 cases, every one of them a `BN` inside an
  override.

The working state is a fixed buffer for paragraphs up to
`GUNI_BIDI_MAX_STACK_LENGTH` (1,024 characters) and comes from an allocator
beyond that, through `guni_bidi_levels_with_allocator()` - the one allocating
function in the library (§13.1). The fuzzer runs both and compares them, so
the two paths cannot drift.

### 7.3 LB1 is the caller's, and `SA` is a provider's

This is M2 and it is the one substantive change to the code that moves.

UAX #14's rule LB1 resolves five classes "that cannot be determined from the
character alone": `AI`, `SG` and `XX` become `AL`; `SA` becomes `CM` or `AL`
by general category; `CJ` becomes `NS` **or `ID`, at the implementation's
choice** - and that choice is what CSS's `line-break` property exposes as
`strict` (`NS`), `normal` and `loose` (`ID`, with further tailorings). `regex`
resolves all five at table-generation time and picks `NS`. Correct for its
`\b{lb}`, which follows Perl; wrong for a layout engine that must offer all
three.

So: the generated `Line_Break` table carries the raw classes.
`guni_lb_resolve(class, general_category, GUNI_LineBreakTailoring)` applies LB1
with the caller's choice, and the iterator takes the tailoring as an argument.
`regex` passes `STRICT` and its behaviour is byte-identical, which §12.3's
pairwise sweep proves.

`SA` - Thai, Lao, Khmer, Myanmar, and the other South-East Asian scripts that
write without spaces - resolves to `AL` under LB1 and therefore has **no
interior line-break opportunity at all**. That is what the Standard says to do
absent a dictionary, and it is useless for those scripts. The iterator
therefore takes an optional `GUNI_BreakProvider` (§9.1) which is handed each
maximal run of `SA` characters and returns the break opportunities within it.
Without one, the behaviour is the Standard's default, and the header says in
those words that a Thai paragraph will not wrap.

### 7.4 Segmentation state is the caller's

An iterator is a caller-owned struct initialised by `guni_break_iter_init()`,
holding the kind, the tailoring, the provider, and its position. No allocation;
no hidden state; two iterators over one buffer do not interact.

---

## 8. Case

Simple mappings (one codepoint to one) and full mappings (one to up to three,
per `SpecialCasing.txt`) for upper, lower, title and fold; `Changes_When_*`
properties; and the `SpecialCasing` **conditions**, which are the part every
`toupper` gets wrong (M13):

- `Final_Sigma` needs the surrounding text; the full-mapping functions take the
  whole buffer and a position, not a codepoint.
- `tr`/`az` dotted and dotless `i`, and `lt` with its combining dot above, are
  the three language-sensitive rules **in UCD's own data file**. They are a
  `GUNI_CaseTailoring { NONE = 0, TURKIC, LITHUANIAN }` argument. This is not
  CLDR: the rules are in `SpecialCasing.txt`; CLDR adds nothing to them but
  the decision of when to apply them, and that decision is the caller's.

Fold orbits - every codepoint that folds to the same value, which is what a
case-insensitive character class needs - are `guni_case_orbit()`, moved from
`regex` (the ECMAScript legacy rules stay behind in `regex` as a dialect
concern).

**How the case data is verified.** The UCD publishes no case test file, and the
simple and unconditional full mappings are a function of the codepoint - so
they are swept exhaustively against the independent parser instead, as eight
more pseudo-properties in `tests/data/sweep/<version>.sums`:
`Simple_Uppercase_Mapping`, `Simple_Lowercase_Mapping`,
`Simple_Titlecase_Mapping`, `Simple_Case_Folding`, `Case_Folding`,
`Uppercase_Mapping`, `Lowercase_Mapping`, `Titlecase_Mapping`. All 3,037 case
rows, over all 1,114,112 codepoints. That leaves exactly the sixteen
conditional lines of `SpecialCasing.txt`, whose answer depends on the text
around the character and which therefore get sixteen hand-written cases, each
transcribed from the file's own fields.

Two things that exercise found on the way, both in the oracle rather than the
library: a full mapping **defaults to the simple one** (`SpecialCasing.txt`
lists only the characters whose full mapping differs, so an oracle starting
from "no mapping" claims that "a" upper-cases to nothing), and an explicit
mapping equal to the codepoint is the identity - U+01C5's titlecase field names
itself, and the library cannot report that as anything but "no mapping".

Title-casing is the one case function that depends on another module:
`guni_to_title()` needs UAX #29's word boundaries, from `break.h`, because
UAX #21 defines it as the first cased character of each word. It is also the
one with a working-state bound, one byte per character, and it refuses text
longer than that rather than allocating behind the caller's back.

---

## 9. Provider seams

Where the Standard defers to data this library does not ship, the seam is a
vtable the caller fills, following `chron` §8.5 and `text`'s
`GTEXT_JSON_Regex_Provider`. The library defines the interface, ships nothing
behind it, and the dependency decision stays with the application. An
application that links ICU for its own reasons - `ctang` does - fills each
seam with ICU in a dozen lines.

### 9.1 `GUNI_BreakProvider`

Dictionary-based line breaking for the `SA` scripts (§7.3). One function, and
**it asks about one position** rather than filling an array: `sa_break_at(ctx,
text, start, end, position)`, where `start` and `end` bound the maximal run of
`SA` characters the position is inside. A provider that wants to segment the
whole run once - which a dictionary breaker does - caches that on its own
`ctx`, which is what the run's bounds are passed for.

The array form was the first design and the point form is better for one
reason: the run is a slice of the caller's own buffer, which is UTF-8 or
codepoints, so an array of positions would have had to be in one of those and
the provider would have had to know which. Instead the provider is handed the
text as an opaque `GUNI_BreakText` and reads it with
`guni_break_text_at()`, in the caller's own units. ICU's `brkitr`
dictionaries, `libthai`, or an application's own word list all fit behind it,
and the library ships nothing.

The provider is consulted **before** the rules and only strictly inside an
`SA` run, because LB1 has already turned `SA` into `AL` or `CM` by the time
the rules run - which is precisely why the rules cannot ask.

### 9.2 `GUNI_SentenceSuppressions`

UAX #29's sentence rules break after `Mr.`; CLDR's per-locale suppression lists
say not to. A provider that answers "is this a suppression" for a run ending in
a terminator. Low priority: layout rarely segments sentences.

### 9.3 What is *not* a seam

Collation, transliteration, charset conversion, number and date formatting,
plural rules and display names have no seam, because nothing in this suite has
asked and a seam nobody fills is an API promise nobody tests.

---

## 10. Names (tier 1)

UAX #44 character names, including the algorithmically derived ones (Hangul
syllables by the Jamo short names, CJK unified ideographs and Tangut by
codepoint), name aliases in all five kinds, named sequences, and the reverse
lookup with UAX #44-LM2 loose matching. In its own translation units behind
`name.h`, tier 1, and nothing in tier 0 includes it -
`make check-layering` reports the file counts of both tiers so that a run says
what it checked rather than only what it forbade.

**What it costs, measured.** 18,457 distinct words and 162,649 tokens encode
40,951 names that are 1,044,804 bytes as text: a token is a word number in
fifteen bits and the separator that precedes it in the sixteenth, which is
`regex`'s encoding and its round-trip check. 434 KB of table for a third of
what the strings would cost, and only `regex` links it.

**The loose matching took three attempts and the third one is the rule.** LM2
says to ignore case, whitespace, underscores and *medial* hyphens, with one
stated exception, and every word of that matters:

- the exception is U+1180 HANGUL JUNGSEONG O-E, because U+116C is HANGUL
  JUNGSEONG OE. It has to be checked on the **folded** form, not on the name
  with its spaces, because a caller may hand over a name that is already
  folded;
- "medial" has to be judged on the **original** name, where the spaces are
  still there. Nineteen names have a hyphen after a space - U+11C88 is MARCHEN
  LETTER -A - and judging medial-ness after the spaces are gone makes it
  U+11C8F, MARCHEN LETTER A. **The generator's collision check found that
  before any test did**, which is what that check is for: it refuses to emit a
  reverse index in which two names are one key;
- an underscore counts as whitespace, and a hyphen beside one is not medial
  either, which is what makes `TIBETAN_MARK_GTER_YIG_MGO_-UM_RNAM_BCAD_MA`
  resolve;
- and a *fragment* - an algorithmic name's prefix, or a jamo short name - has
  no medial hyphens to preserve, so it folds by a different function.
  "CJK UNIFIED IDEOGRAPH-" ends in a hyphen that is not medial in the fragment
  and is medial in the name the fragment starts, and one function for both
  meant the entire CJK reverse lookup never matched.

The gate on all of that is a round trip over every codepoint that has a name -
140,000 of them, most computed rather than stored - in three spellings each.

---

## 11. The consumers, and what each one needs

| Consumer | Uses | Modules | Migration note |
| --- | --- | --- | --- |
| `regex` | properties as range lists for `\p{...}`; the four segmentations for `\b{gcb}` etc.; fold orbits; names for `\N{...}`; script runs for `(*sr:...)` | `set`, `break` (with `STRICT`), `case`, `name`, `script` | Deletes `src/unicode/` except `vim_class.c`; its own tables go; `test_unicode.cpp` and `test_break.cpp` move here. Applies LB1 itself (§7.3) |
| `text` | NFC for IDNA and YAML; `Bidi_Class`, `Joining_Type`, `Hangul_Syllable_Type`, combining class for the IDNA validity rules | `norm`, `char` | Deletes `nfc.c`, `nfc_utf8.c`, `nfc_tables.c`; keeps `idna.c` and its two mapping tables (§15.3) |
| `ctang` | grapheme cluster boundaries for its string type | `break` | Replaces `UBRK_CHARACTER` and the two UTF-16 conversions in `src/unicodeString.c`; ICU leaves the suite |
| `font` | script itemisation; bidi levels and reordering; grapheme boundaries for the cluster map; line-break opportunities with all three tailorings and the `SA` seam; NFD for mark decomposition; joining, Indic, USE, emoji and vertical-orientation properties for the shapers | `script`, `bidi`, `break`, `norm`, `char` | Designed against it; `libs/font/documentation/design.md` |

`chron` needs nothing: its formatter's names come from a provider and are
already case-correct as provided. `image`, `compress`, `model` and `cjelly`
need nothing directly; `cjelly` reaches it through `font`.

---

## 12. Correctness: oracles and tests

The principle from `regex`'s `testing.md` and `chron` §12 applies: **the oracle
is the authority, and every vector is generated from one, never written from
memory.** For Unicode the Consortium publishes the oracle, which makes this the
best-instrumented domain in the suite - and the domain is finite, which makes
exhaustive checking the norm rather than the exception.

| Claim | Oracle | Driver | Exhaustive? | Image |
| --- | --- | --- | --- | --- |
| every property of every codepoint | the UCD files themselves, re-read by an independent parser in the test (not `gen_tables.py`), and **Python's `unicodedata` module** as a second opinion where it exposes the property | `tests/unit/test_sweep.cpp`; `tools/oracle/unicodedata_diff.py` | **yes** - 1,114,112 codepoints per property | `python`, stock, by digest - see §12.6 for the UCD-version caveat |
| the trie and the range list agree | each other | `tests/unit/test_sweep.cpp` | yes | none: in-process |
| grapheme, word, sentence, line boundaries | `GraphemeBreakTest.txt`, `WordBreakTest.txt`, `SentenceBreakTest.txt`, `LineBreakTest.txt` | `tests/conformance/test_break.cpp` | the files enumerate every rule interaction | none: the files are committed |
| the same, second opinion | **ICU** `ubrk_*` in the root locale, over generated strings | `tools/oracle/icu_break.cpp`, `make check-oracle-icu` | random, seeded | `icu`, built here: the driver links only ICU and is compiled inside its image, as `regex`'s `pcre2_match` is |
| normalisation | `NormalizationTest.txt` - every codepoint with a decomposition, in all four forms, plus the invariants of its Part 1 over *every other* codepoint | `tests/conformance/test_norm.cpp` | **yes**, by the file's own design | none |
| bidi | `BidiTest.txt` and `BidiCharacterTest.txt` | `tests/conformance/test_bidi.cpp` | the first is exhaustive over class sequences | none |
| case mapping | `UnicodeData.txt` and `SpecialCasing.txt` re-read; Python `str.upper()`/`lower()`/`casefold()` | `test_sweep.cpp`; `unicodedata_diff.py` | yes for simple mappings | `python` |
| names | `UnicodeData.txt` and `NameAliases.txt` re-read; Python `unicodedata.name()`/`lookup()` | as above | yes | `python` |
| UTF-8 decoding | the Standard's Table 3-7 well-formed byte ranges, enumerated; the maximal-subpart examples in §3.9 | `tests/unit/test_utf.cpp` | every 1-, 2-, 3- and 4-byte sequence pattern | none |

### 12.1 The exhaustive sweep is a committed artifact

For every property, the value at all 1,114,112 codepoints is canonicalised to
text - one line per maximal range, `%06X..%06X <value's long name>` - and
hashed; **the hash and the range count per property are committed** in
`tests/data/sweep/<version>.sums`, written by `tools/ucd/gen_sweep.py`. That
script is **a second implementation of reading the UCD**: its own field
splitting, its own `@missing` handling, its own alias resolution, range lists
and a linear merge where `gen_tables.py` fills an array. Two parsers that
shared code would share its bugs. It found two on its first run - one in each
direction - which is the outcome this arrangement cannot hide.

Values are compared as **text and never as numbers**, so a renumbered enum
fails here too. The file also carries the **partition**: the boundary set of
the record itself, which is where any property changes. The library's runs
table has to reproduce it exactly, and `test_sweep.cpp` derives it from the
trie rather than reading the runs table, so the two are compared against each
other and both against the oracle.

`make test` recomputes every number in the fixture from the compiled library,
over every codepoint, in under a second: the walk compares a cheap integer key
per codepoint and only spells a value out at a range start. A single flipped
stage-2 entry - one codepoint pointing at the wrong record - was planted and
failed three of the four tests, naming the two properties that differed and
reporting 8,036 partition boundaries where the oracle has 8,035.

The fixture is checksums and not the sweep in full text, which is the third
departure from this section's draft: a text sweep is 640 KB of a *third* copy
of the data, and the review artifact on an upgrade already exists - the diff
of the committed tables under `src/char/tables/`, plus `enums.h`'s new
members. When a checksum does fail, localising it is a diff of two streams
rather than a rebuild with printfs:

```
tools/ucd/gen_sweep.py --property Script > /tmp/oracle
GUNI_SWEEP_DUMP=Script build/linux/release/apps/testSweep > /tmp/ours
diff /tmp/oracle /tmp/ours
```

This is also the machinery that makes migrating `regex` and `text` safe (§16
phases D and E): before either migration, the sweep is run against *their*
implementations and the sums committed; after, the sums must match.

This is the machinery that makes migrating `regex` and `text` safe (§16
phases D and E): before either migration, the sweep is run against *their*
implementations and the sums committed; after, the sums must match.

### 12.2 The gates cannot skip

The conformance files are committed (§5.1), so `make test` on a fresh clone
runs every gate. The ICU and Python differentials are the ones that can be
absent, and a run without them says how many comparisons it could not make,
never nothing.

### 12.3 The gates are themselves tested

Every gate here has been *observed to fail* before it is trusted, per `chron`
§12.3 and this suite's history. Specifically, before the migration commits:

- flip one entry in one generated table → the sweep sum fails and the boundary
  fixture names the codepoint;
- resolve `CJ` to `ID` in the table → the **pairwise Line_Break class sweep**
  (every ordered pair of classes, several codepoints per class, old
  implementation against new) fails and names the pair. The conformance file
  alone does *not* catch this, because it tests the `strict` resolution;
- delete one rule from `break.c` → the conformance file fails on its line.

### 12.4 The oracles run in containers

`notes/suite/CONTAINERS.md` §2 and §4 record the pattern, prototyped and
measured on `regex`; **this library is the first to land it**, and adopts it
rather than inventing a second one: `tools/oracle/containers/IMAGES` pins every reference
(stock images by digest, built-here images by every input they read plus a
run-time version check); `tools/oracle/oracle_env.py` is the one place a
reference is spelled; `tools/oracle/oracle_run.py` resolves the reference,
prints `oracle(container): <version>` above the numbers, and refuses to run a
gate whose reference is not the one `IMAGES` names. `ORACLE_MODE` is
`container` or `host` with **no silent fallback**; the repository is mounted
read-only at its own host path with `--network none`; every driver speaks the
batch protocol, because a container costs 200 ms per gate and would be fatal
per case. The conformance gates in `tests/conformance/` need no container:
their files are committed (§5.1) and they run in `make test`.

Three places this library departs from the prototype, each because the
prototype's reason does not hold here:

- **`ORACLE_REQUIRED` defaults to 1**, where the prototype defaults to 0. There
  it had to, because its oracle gates run inside `make test` and a clone without
  an engine still has to build. Neither gate here is in `TEST_GATES`, so the
  only caller is someone who typed `check-oracle-*`, and the honest answer to
  "your engine is missing" is an error rather than a skip.
- **`check_pin()` asserts in container mode and reports in host mode.** The
  prototype runs it in both, for a reason that is about images *built here*: two
  builds of one Dockerfile are not two copies of one image, so the run-time
  version check is the only guarantee they have. Every image named here is a
  stock one pinned by digest, and asserting the pin in host mode would only make
  host mode unreachable - this machine's CPython is 3.13 and no pin worth having
  names 3.13. Host mode therefore prints `oracle(host, unpinned)`, which is the
  claim it is entitled to.
- **The provenance line names the pin that answered**, not the one the gate
  asked for. Under `GHOTI_ORACLE_ALIAS=python=python-next` the prototype's line
  reads `oracle(container): python Python 3.15.0rc2`: the version is right and
  the name is the one a reader would grep for. It now reads `python-next as
  python`.

Two things are specific to Unicode oracles:

- **An oracle's Unicode version is the pin that matters**, and it is rarely
  ours. CPython's `unicodedata` tracks the interpreter release (3.13 carries
  15.1, 3.14 carries 16.0); ICU tracks its own (ICU 78 carries 17.0). So an
  oracle's `IMAGES` entry names its UCD version **as the version field** - the
  interpreter's own release is incidental and the UCD is the claim - and
  `unicodedata_diff.py` **excludes the codepoints whose `DerivedAge.txt` age is
  newer than the oracle's UCD version**, counting them as *not comparable* in
  its output rather than as agreement, under the reason for the skip.

  So `IMAGES` carries **two CPythons**: `python`, a released interpreter on UCD
  16.0.0, and `python-next` on UCD **17.0.0**, an exact match for
  `tools/ucd/UCD_VERSION`. `check-oracle-unicodedata` is advisory against the
  first; `check-oracle-unicodedata-strict` is a gate that can fail against the
  second, and the difference between the two is a reading of what the Consortium
  changed. The rc is not the gating pin and becomes one when 3.15.0 releases.

  **The filter excludes only what is newer, not everything unassigned**, and
  getting that wrong is worth recording because it read as caution. The first
  version also excluded every codepoint with no age at all - 814,664 of them,
  73% of the codespace. Assignments are never withdrawn, so a codepoint
  unassigned in 17.0.0 is unassigned in every earlier version and *both sides
  have an answer for it*. Comparing them takes the strict run from 2.2 million
  comparisons to 7,947,413 and puts the end of every trie run under the oracle's
  eye, which is where a last range one codepoint too long would show. It also
  bought a real reading: CPython 3.15 answers a *default* `Bidi_Class` for
  unassigned codepoints where 3.14 answers nothing, so 814,730 default-range
  derivations from `DerivedBidiClass.txt` are now differentially checked, and
  they agree.

  **That filter is necessary and not sufficient, which only running it showed.**
  `DerivedAge` says when a codepoint was *added* and nothing about when its
  properties *changed*, and between 15.1 and 17.0 they changed for codepoints
  decades old: U+0295 `Ll` to `Lo`, U+1171E `Mn` to `Mc`, 188 symbols `N` to
  `W`, U+226D newly mirrored, U+5146's numeric value a million to a million
  million. A differential two releases behind cannot tell that from a defect,
  so `unicodedata_diff.py` is **advisory by default** and `--strict` is for an
  oracle on our own version. Measured against the two pins: UCD 16.0.0 leaves 2
  differences (U+0295 and U+5146, the two decisions that land in 17.0) and 6,153
  "ours only"; UCD 17.0.0 leaves **none of either**, across 7,947,413
  comparisons - which is what a matching pin buys, and it retires the whole
  triage rather than shortening it. It also reports four buckets rather than two -
  agreed, differed, ours only, theirs only - because "ours only" is usually the
  oracle's limitation (it does not compute the Tangut names) and "theirs only"
  is the bucket that would most likely be ours.

  The script's own two defects are worth recording as the shape of the risk: it
  first compared `Decomposition_Type` as short aliases against the UCD's tag
  text, reporting all 13,233 decomposable characters, and then reported the
  11,172 Hangul syllables because `unicodedata.decomposition()` does not return
  an arithmetic decomposition. **An oracle that is wrong looks exactly like an
  implementation that is wrong**, from the outside, which is why the triage
  lives in the script rather than in somebody's memory.
- **The ICU driver builds inside its image** against the image's ICU alone,
  as `regex`'s `pcre2_match` builds against pcre2 alone, so that the reference
  cannot reach the implementation it answers for.

### 12.5 Fuzzing

`tests/fuzz/fuzz_utf.cpp` (the decoder under every policy),
`fuzz_norm.cpp` (all four forms, the first byte selecting one; the property
that `NFx(NFx(s)) == NFx(s)` and that the output is stream-safe when the input
was), `fuzz_break.cpp` (every kind and tailoring; the property that
boundaries are monotone and that the point query agrees with the iterator),
`fuzz_bidi.cpp` (every direction; the property that levels are within range
and reordering is a permutation), `fuzz_case.cpp`. Under ASan and UBSan, with
the limits driven by the options byte per `CONVENTIONS.md` §7.

### 12.6 Properties

Beyond the vectors, invariants checked over random input:

1. `NFC(NFD(s)) == NFC(s)` and the three other compositions of forms UAX #15
   §7 guarantees.
2. Every boundary the point query reports, the iterator reports, and vice
   versa.
3. `fold(upper(s)) == fold(lower(s)) == fold(s)`.
4. Bidi levels are `0..125`, reordering is a permutation, and an all-LTR
   paragraph reorders to the identity.

---

## 13. Code layout

```
include/ghoti.io/unicode/
  macros.h  libver.h  libver_gen.h  namespace.h  allocator.h     (CONVENTIONS §4)
  enums.h       every enumerated property value  GENERATED, COMMITTED         [tier 0]
  core.h        GUNI_Result, GUNI_Limits, GUNI_Error, version                 [tier 0]
  utf.h         decode/encode, GUNI_Invalid                                    [tier 0]
  char.h        every per-codepoint property                                   [tier 0]
  set.h         properties as GUNI_Range lists; name/value lookup              [tier 0]
  script.h      script-run segmentation                                        [tier 0]
  case.h        mappings, conditions, tailorings, orbits                       [tier 0]
  norm.h        the four forms, quick check, stream-safe                       [tier 0]
  break.h       UAX #29 ×3, UAX #14, LB1, iterator, GUNI_BreakProvider         [tier 0]
  bidi.h        UAX #9                                                         [tier 0]
  name.h        UAX #44 names                                                  [tier 1]
  unicode.h     umbrella for tier 0
src/
  core/  utf/  char/  set/  script/  case/  norm/  break/  bidi/  name/
    each with <module>_internal.h and tables/ where generated
  char/tables/
    tables.h       GENERATED: the record struct, the trie, guni_record()
    props_data.c   GENERATED: 2,374 records, the two-stage trie, 8,035 runs
    misc_data.c    GENERATED: blocks, numeric values
    names_data.c   GENERATED: property and value names, loose-matched aliases
tools/ucd/
  fetch.sh  gen_tables.py  gen_sweep.py  test_gen.py  UCD_VERSION
tools/oracle/
  unicodedata_diff.py  icu_break.cpp
tests/
  unit/  conformance/  fuzz/  data/ucd/<version>/  data/sweep/
```

### 13.0 What the tables cost

Measured at UCD 17.0.0, `.rodata` in the release build:

| Table | Bytes | What it is |
| --- | ---: | --- |
| `props_data.o` | 247,416 | 2,374 records (36 bytes each), the trie's 17,408-entry stage 1 and 597 shared 64-codepoint blocks, the 8,035 runs, the Script_Extensions pool |
| `misc_data.o` | 43,144 | 347 block ranges, 2,734 numeric-value ranges with 64-bit numerators |
| `names_data.o` | 23,204 | every property's and every value's long name, and 2,527 loose-matched spellings |

316 KB for 97 properties in both call shapes, against the 54,736 lines of
generated C that `regex` carries for fewer of them. The reason is §4.2's
shared record: the alternative - a table per property - spends its space on
storing the same correlations once per property.

### 13.1 Allocation

Tier 0's property, case-simple, mirror and quick-check functions allocate
nothing and take no allocator. Normalisation, full case mapping and bidi
write to caller buffers (§4.5). The one place an allocator appears is the
`_with_allocator` variant of the bidi resolver for paragraphs longer than a
caller wants on the stack, and it uses `GUNI_Allocator`, a typedef of
`GCU_Allocator` per `CONVENTIONS.md` §5.

### 13.2 Limits

`GUNI_Limits` caps `max_text_bytes` (default 64 MiB), `max_bidi_depth` (fixed
at 125 by the Standard and not raisable), and `max_nonstarters` for the
stream-safe transform (30, per UAX #15). Every function that walks a buffer
takes one; `NULL` means default.

### 13.3 Threads

Every table is `const` and every function is reentrant. Iterators and bidi
state are caller-owned structs. There is no shared mutable state anywhere in
the library, and no function acquires a lock.

---

## 14. Non-goals for the first stable release

- **CLDR data**, in any form (M6). Collation, transliteration, formatting,
  plural rules, display names, locale-specific tailorings beyond the three in
  `SpecialCasing.txt`.
- **Charset conversion** other than UTF-8/UTF-16/UTF-32.
- **Dictionaries** for `SA` word breaking (§9.1 is the seam).
- **IDNA and UTS #46** (§15.3).
- **Regular-expression syntax** for properties (`\p{...}` parsing is `regex`'s;
  this library exposes the name lookup it needs).
- **Unicode Security Mechanisms** (UTS #39 confusables), beyond the script-run
  detection that already exists. A reasonable module for a second release.

---

## 15. Decisions

Listed so that they were decided on purpose. The first three were put to the
author and answered on 2026-09-24; the rest stand as recommended.

1. **The library exists, and is built before `font`, and `text` and `regex`
   migrate to it before `font` starts.** Decided. The staged alternative -
   build `font`'s tables inside `font` and converge later - was rejected
   because "converge when touched" never fires for generated data, and because
   an API designed against one consumer is the wrong API.
2. **`unicode` is the name.** Decided, with the ICU-adjacency caveat in the
   preamble. `ucd` would undersell a library that holds algorithms.
3. **Names move, as tier 1.** Decided. The alternative leaves 31,603 lines in
   `regex` for the next consumer to write again.
4. **IDNA and UTS #46 stay in `text`.** Recommended. A Unicode standard, but
   about host names, and one consumer is not a library. Revisit on a second.
5. **LB1 is applied by the caller.** §7.3. The alternative - two tables, one
   per tailoring - doubles the data to avoid one function call.
6. **Script runs move.** `regex`'s `script_run.c` implements UTS #39's
   notion, which with `Script_Extensions` and bracket pairing is also a
   shaper's itemiser; one implementation serves both, in `script.h`.
7. **Zero refuses** on the invalid-UTF-8 policy (§4.3), on the case tailoring
   (`NONE`), and on the line-break tailoring (`STRICT` is zero because it is
   the Standard's own worked example and `regex`'s existing behaviour, not
   because it is stricter).
8. **No UTF-16 API.** §4.1. `cutil` converts.
9. **The conformance files are committed.** §5.1, M4.
10. **Emoji data is pinned to the UCD version.** They have tracked each other
    since Emoji 11; if they ever diverge, `fetch.sh` gains a second pin.
11. **One record for every property, not a table per property.** §4.2.
    Decided when the measurement came in: 2,374 distinct records over
    1,114,112 codepoints.
12. **`set.h` materialises ranges from the runs table.** §4.2. Decided
    against committing a range array per property value, which would have
    been a second copy of the data.
13. **`enums.h` is generated and committed, and is the numbering's record.**
    §5.5. The alternative - a side file - would have let the header and the
    numbering disagree, and the header is what a consumer compiled against.
14. **`check-ucd-tables` is not in `TEST_GATES`; the sweep fixture covers the
    same ground from committed data.** §5.2.
15. **Normalisation works in the caller's buffer, and the composing forms
    need room for the intermediate.** §6.4. The alternative - an allocating
    variant, or a working buffer proportional to the input - buys an exact
    preflight length for the composing forms and nothing else.
16. **`guni_normalize_utf8()` has a bounded window and refuses a run with no
    normalisation boundary in it.** §6.4. The alternative is a buffer whose
    size the input chooses.
17. **The bidi resolver's levels are per character, and reordering is a
    separate function returning a permutation.** §7.2. A resolver that
    returned reordered text would be useless to the consumer it exists for.
18. **The point query is the primitive; the iterator walks it.** §4.2. The
    rules are stated as "is there a boundary between these two characters".
19. **`GUNI_BreakProvider` answers one position, and reads the caller's text
    through an accessor.** §9.1.
20. **A byte inside a UTF-8 character is not a boundary**, and is answered
    false rather than decoded as two ill-formed fragments - which would
    report a boundary in the middle of one character, and a caller mapping
    clusters to glyphs would believe it.

---

## 16. Plan

Phases, in dependency order, with the milestone each unlocks. Sizes follow
`regex`'s `plan.md` and `chron`: S up to a week, M two to four, L four to
eight, for one engineer who knows the suite. Phases A-C build the library;
D-F migrate its consumers; nothing in `font` that needs Unicode starts before
E. (`font`'s tiers 0 and 1 - file parsing, outlines, rasterisation - need no
Unicode at all and can proceed in parallel from phase B onward; see
`libs/font/documentation/design.md` §17.)

**Phases A, B and C are built**, in five commits, each of which builds and
passes `make test`. What the three of them came to:

| | A | B | C |
| --- | --- | --- | --- |
| Modules | `utf`, `char`, `set` | `norm`, `bidi` | `break`, `case`, `script`, `name` |
| Conformance files | - | 3 | 4 |
| Tests | 50 | 99 | 148 in 15 binaries |
| Fuzz harnesses | 1 | 3 | 6 |

And the whole of it: 11,571 lines of code, 65,689 lines of generated tables,
99.0% line coverage, clean under Valgrind and ASan from an empty build
directory, serially and under `-j8`.

**What is deliberately not done**, from the phase rows below: the comparison of
the sweep sums against `regex`'s own `property.c`, and the pairwise Line_Break
sweep against a pre-move `regex` build. Both are differentials against a
library that has not migrated yet, so both belong to phase E rather than ahead
of it; the pairwise sweep's own artifact is committed and waiting for them
(`tests/data/break/linebreak-pairs.txt`).

**The oracle containers are built** (§12.4): `tools/oracle/oracle_env.py`,
`oracle_run.py`, `containers/IMAGES` and `unicodedata_ask.py`, with
`make check-oracle-unicodedata` against a released CPython and
`make check-oracle-unicodedata-strict` against one carrying this library's own
UCD version, where 7,947,413 comparisons over all 1,114,112 codepoints leave no
difference at all. The ICU differential from C's gate column is still absent and
is now the only thing the image pattern was blocking: it wants a driver
compiled *inside* its image, which is the `pcre2` shape in
`notes/suite/CONTAINERS.md` §2.4 rather than the stock-image shape landed here.

The suite-level `check-ucd-pins.sh` is built, in the workspace rather than
here: three libraries pin a UCD version today and the point of the check is
that they agree.

| Phase | Work | Size | Gate | Unlocks |
| --- | --- | --- | --- | --- |
| **A** | Scaffold from `model` per `CONVENTIONS.md` §12; `core.h`, `utf.h`; `tools/ucd/` with `fetch.sh`, `gen_tables.py` (ported from `regex`), `UCD_VERSION`; the committed conformance files; `char.h` and `set.h` with their tables; the exhaustive sweep (§12.1) and the trie/range agreement test; `check-ucd-tables`, `check-layering`, `check-symbols`; the suite-level `check-ucd-pins.sh` | M | sweep sums match `regex`'s `property.c` for every property both have; every gate observed to fail once | **U1: a library exists that answers every property for every codepoint, provably identically to what `regex` answers today** |
| **B** | The modules nobody has: `norm.h` in all four forms with quick-check and the expansion constants; `bidi.h`; the shaping properties in `char.h` (joining, Indic, USE inputs, emoji, vertical orientation, mirroring) | L | `NormalizationTest`, `BidiTest`, `BidiCharacterTest`, all committed, all passing; the sweep extended to the new properties | **U2: `font`'s shaping tier has every Unicode input it needs** |
| **C** | Move `break.c`, `case.c`, `display.c`, `script_run.c` and `names.c` with their tables into `break.h`, `case.h`, `char.h`, `script.h`, `name.h`; **LB1 exposed** (§7.3); the iterator form; `GUNI_BreakProvider`; the pairwise Line_Break sweep against the pre-move `regex` build; `test_unicode.cpp` and `test_break.cpp` move here | M | the four UAX #29/#14 files; the pairwise sweep byte-identical under `STRICT`; the ICU differential | **U3: every algorithm `regex` had, with its tailoring axis opened** |
| **D** | Migrate `text`: `nfc.c`, `nfc_utf8.c`, `nfc_tables.c` deleted; IDNA's validity checks read `char.h`; `workspace.txt` gains `unicode` on `text`'s line | S | `text`'s 1,534 tests unchanged; its NFC oracle script unchanged; the sweep sums for the composition tables unchanged | **U4: first consumer migrated; the API has survived a second consumer** |
| **E** | Migrate `regex`: `src/unicode/` reduced to `vim_class.c` and the ECMAScript legacy rules; `regex` applies LB1 with `STRICT`; `\p{...}`, `\N{...}`, `\b{...}` and `(*sr:...)` over this library; `workspace.txt` updated | M | `regex`'s 484 tests and 33,829 vectors unchanged; the Perl differential unchanged; the pairwise sweep unchanged | **U5: the duplicate is gone; three libraries, one Unicode** |
| **F** | `ctang`: `src/unicodeString.c` calls `guni_break_iter_*` for graphemes; the UTF-16 conversions and the ICU dependency removed; `workspace.txt` and the Makefile's dependency block updated | S | `ctang`'s 174 test executions unchanged; `pkg-config icu-uc` no longer required by any library | **U6: ICU is not linked by anything in the suite** |

Each phase ends with `make test`, `test-valgrind`, `test-asan`, `fuzz` and
`check-symbols` clean from an empty build directory, serially and under `-j`,
per `CONVENTIONS.md` §12 item 10. D, E and F are each independently
deferrable: nothing in a later phase depends on an earlier migration having
landed, and old code is deleted only once the new path passes.

**What is deliberately absent at the end of F:** everything in §14. The
absent things are absent, not stubbed: there is no `guni_collate()` that
returns `ERR_UNSUPPORTED`, because a function that exists and refuses is a
promise the tests do not check.

---

## References

- The Unicode Standard, Version 17.0, Core Specification: §3.9 (UTF-8, maximal
  subparts), §3.11-3.13 (normalisation and case), Chapter 5.
- UAX #9, Unicode Bidirectional Algorithm. UAX #14, Line Breaking Properties.
  UAX #15, Unicode Normalization Forms. UAX #29, Unicode Text Segmentation.
  UAX #44, Unicode Character Database. UTS #39, Unicode Security Mechanisms
  (script runs). UTS #51, Unicode Emoji.
- Unicode License v3, for the data and conformance files.
- `CONVENTIONS.md` §4 (namespacing), §5 (the API contract), §7 (tests), §12
  (the checklist for a new library).
- `libs/chron/documentation/design.md` §3.7 (zero refuses), §8.5 (providers),
  §8.6 (output contract), §12.3 (gates observed to fail), §14 (no CLDR).
- `libs/regex/src/unicode/` and `tools/unicode/`, the code that moves.
- `notes/suite/UNICODE-LIBRARY.md`, the decision record.
