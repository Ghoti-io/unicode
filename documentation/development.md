# Development

## Layout

```
include/ghoti.io/unicode/   Public headers, one per module (design.md section 13)
src/core/                   Result strings, limits, the allocator
src/utf/                    UTF-8 decode and encode, the GUNI_Invalid policy
src/char/                   Every per-codepoint property
src/char/tables/            GENERATED: the record, the trie, the runs, the blocks
src/set/                    Properties as sets, and the property-name lookup
src/norm/                   UAX #15: the four forms, quick check, stream-safe
src/break/                  UAX #29 and UAX #14, with LB1 exposed
src/bidi/                   UAX #9: levels, reordering, mirroring
src/case/                   Case mapping, the conditions, the fold orbits
src/script/                 UTS #39 script runs, and the shaper's itemiser
src/name/                   the character names (not included by the umbrella)
src/unicode.c               The version

Each module with generated tables keeps them under its own tables/ directory,
so that the file holding the case mappings is the one a reader looking for the
case mappings would open.
tests/unit/                 Unit tests (gtest)
tests/conformance/          The Unicode conformance files and their runners (from phase A)
tests/data/                 Fixtures, reached through GUNI_TEST_DATA
tests/fuzz/                 libFuzzer harnesses and seed corpus (from phase A)
tools/ucd/                  fetch.sh, gen_tables.py, gen_sweep.py, UCD_VERSION
tools/oracle/               The differentials, and how a reference is reached
tools/oracle/containers/    IMAGES: every oracle pinned by digest
tools/check-stamps.py       The flag-stamp gate
```

## The data, and the two scripts that read it

The UCD is **fetched, not committed** - 29 MB of somebody else's data - and
everything derived from it is committed, so that a clone builds and tests with
no network and no Python:

```bash
tools/ucd/fetch.sh            # into third_party/ucd/<version>/, gitignored
make gen-ucd-tables           # regenerate the tables and the sweep fixture
make check-ucd-tables         # fail if a committed file is not what they emit
make install-conformance      # copy the conformance files in, to be committed
```

Two scripts read the UCD and **they are deliberately not one script**:

- `tools/ucd/gen_tables.py` emits what the library ships: `enums.h`, the
  property record, the trie, the runs, the blocks, the numeric values and the
  name tables. Adding a property is a row in its registry plus an accessor in
  `char.h` - the trie, the runs, the name tables and the sweep all read that
  registry (design.md section 5.3). If adding one needs more than that, the
  generator has grown a special case and the special case is the bug.
- `tools/ucd/gen_sweep.py` emits the oracle: `tests/data/sweep/<version>.sums`,
  a checksum per property over its value at all 1,114,112 codepoints. It parses
  the same files with its own code - range lists and a linear merge where the
  other fills an array - because two parsers that share code share its bugs.
  On its first run the two disagreed twice, once in each direction.

`make test` checks the tables against that fixture and needs neither script.
When it fails, localise before debugging:

```bash
tools/ucd/gen_sweep.py --property Script > /tmp/oracle
GUNI_SWEEP_DUMP=Script build/linux/release/apps/testSweep > /tmp/ours
diff /tmp/oracle /tmp/ours
```

**The enum numbering lives in the committed `enums.h`.** The generator reads it
before writing it and adopts every value it finds, so a new Unicode version
appends and nothing moves; a value that appears twice, or one too wide for its
record field, is refused (design.md section 5.5). To renumber deliberately -
which breaks every consumer that stored a value - delete the enum from the
header first.

## Adding a module

design.md section 3.1 names the modules. A module arrives with:

1. Its public header under `include/ghoti.io/unicode/`, listed in the
   Makefile so that `check-layering` places it. `name.h` stays off the
   umbrella. Every header includes `macros.h` first.
2. Its generated tables under `src/<module>/tables/`, emitted by
   `tools/ucd/gen_tables.py` and committed; `make check-ucd-tables` regenerates
   and fails on a byte difference.
3. Every public name in `namespace.h`; `check-symbols` fails otherwise.
4. Its conformance file under `tests/data/ucd/<version>/`, committed, and a
   runner under `tests/conformance/` that cannot skip.
5. The exhaustive sweep extended to its properties (design.md section 12.1).
6. **A fuzz harness**, registered with
   `$(eval $(call fuzz-rule,fuzz_<name>,<name>))`, with its options byte
   driving `GUNI_Limits`.

Point 6 is not optional. An algorithm over untrusted text without a fuzzer is
one nobody has actually tested.

## The gates

`make test` runs `TEST_GATES` - `check-symbols`, `check-layering`,
`check-aliasing`, `check-stamps` - before the tests. Each has been observed
to fail on a planted defect before it was trusted, and a gate that has never
failed is one whose sensitivity is unmeasured. Add a gate the same way: plant
the defect, watch it fail, then commit it green.

## The differentials

The conformance files are the authority and they run in `make test`. The
differentials are second opinions and they need two things `make test` does not -
a container engine and the fetched UCD - so they are targets of their own:

```bash
make check-oracle-unicodedata          # a released CPython, UCD 16.0: advisory
make check-oracle-unicodedata-strict   # one on UCD 17.0: this one can fail
make check-oracle-icu                  # ICU 78.3, UCD 17.0: segmentation
make check-oracles                     # all three
make oracle-images                     # build the images that are built here
```

`oracle-images` is separate because building ICU from source takes minutes and a
gate should not. The CPython pins are stock images pulled on demand.

**The reference is pinned, and that is the whole point.** This differential used
to `import unicodedata` in its own process, which meant "the reference" was
whichever CPython ran it - and on this machine that is 3.13, two Unicode releases
behind these tables. Every disagreement it reported was that skew. The reference
is now `unicodedata_ask.py` run in an image pinned by digest in
`tools/oracle/containers/IMAGES`. Against a pin carrying UCD 17.0.0 there are **no differences at
all** over 7,958,585 comparisons, and nothing reported as not comparable.

Three rules the pattern is built on, and each has been observed to hold:

- **No silent fallback.** `ORACLE_MODE` is `container` or `host`, never "try the
  container and fall back". A gate whose reference is not the one it names is
  worse than one that did not run, because it prints the same green line.
- **Fail closed.** `ORACLE_REQUIRED=1` is the default here; an unreachable
  reference is an error naming what is missing. A `command -v python3` guard
  would ask whether something called python3 exists, which is not the question -
  it always did exist here, carrying the wrong UCD.
- **Say which instrument answered.** Every run prints
  `oracle(container): python-next Python 3.15.0rc2, unicodedata 17.0.0` above its
  numbers, naming the pin that actually answered rather than the one the gate
  asked for.

To ask the gating pin's questions of the other pin:

```bash
GHOTI_ORACLE_ALIAS=python=python-next make check-oracle-unicodedata
```

## The ICU differential, and why segmentation needed one

`unicodedata` exposes no boundary function, so until this existed UAX #29 and
UAX #14 had **nothing but the Consortium's conformance files** answering for
them - and those are tables of *pairs*. They say nothing about a boundary four
characters into a string of nine, which is where GB9c's prepend context, WB4's
ignore rule, LB25's number sequences and the regional-indicator pair count in
GB12/GB13 actually live. An implementation can pass every pair and still get
those wrong.

`tools/oracle/icu_break_diff.py` runs strings over a pool **stratified by every
value of every break property**, read out of this library's own sweep dumps: a
uniform sample of the codespace is 73% unassigned and would spend its budget on
`XX`. Two modes, and they see different things:

```bash
make check-oracle-icu                             # the gate: random + pairwise
make check-oracle-icu-exhaustive                  # every codepoint; tens of minutes
make check-oracle-icu ICU_CASES=20000 ICU_SEED=$RANDOM   # a hunt
tools/oracle/icu_break_diff.py --self-test-only   # the explainer alone, no container
```

Three modes, and the differences between them are the point:

- **Random strings** over a pool stratified by every value of every break
  property. Samples characters, and reaches the long-context rules a pair table
  cannot. Found the six Japanese iteration marks.
- **`--pairwise`**, every ordered pair of every break class. Exhaustive over
  pairs, but one representative per class, so a character its own class treats
  specially is invisible to it. Found `IN × IN`, which random text almost never
  produces adjacent. The gate runs this and the random mode, because neither
  found both divergences.
- **`--exhaustive`**, every codepoint in three contexts. **This is what entitles
  the divergence list to say "complete"**, and it is the mode to re-run when the
  ICU pin moves - a new ICU is new tailorings, and the explanation table is
  written against the ones measured under the current pin. Tens of minutes, so it
  is not in `check-oracles`; `ICU_UPTO` stops it short for a bisect and a partial
  run says in its own output that it does not support the claim.

Why the last one is not optional: the explainer's guard suite is a **floor**.
Its cases are ones somebody thought of, and it caught the defect it caught
because that defect happened to share a shape with a case already written. A
divergence nobody imagined walks straight through it. `--exhaustive` is the only
mode that can finish the question, and it is affordable once the per-case
process cost is gone: the oracle answers about 20,000 requests a second.

Both sides of the comparison speak one protocol - one request per line, one
framed answer per line, **the answer echoing the request** - so `testSegment`
under `GUNI_BREAK_DUMP=stdin` and `icu_break.cpp` are interchangeable and a
stray line on stdout cannot shift the answers and be absorbed. That is not
hypothetical: the first run of this reported "asked 1050 and 34 answers arrived"
because `GUNI_BREAK_DUMP` was missing from the environment and `testSegment` had
run its gtest suite instead.

**`icu_break.cpp` links only ICU**, and is compiled inside its image on each
run. An oracle that could reach the implementation it answers for is not an
oracle; `regex`'s `pcre2_match` is the same shape.

### The six divergences, and what they are not

Differences are **explained, never excluded** - and offset by offset, not case by
case, so a case carrying one known divergence and one defect is not filed under
the divergence. Nothing is dropped from the pool, so a difference of any *other*
shape at those same characters still fails the gate.

1. **ICU implements CSS `normal` as it stood before CSSWG issue 10363**, which
   allowed a break before class `CJ`. The current specification forbids it for
   `normal` and `strict` alike and says so in its own change log. This library
   follows the current text, which is why `normal` and `strict` are the same rule
   set in neutral writing systems.
2. **ICU applies the hyphen rule to all eleven codepoints of class `HH`** where
   CSS names `U+2010` and `U+2013`, and gates it on language where CSS gates it
   on the preceding character's class. Visible on both sides of the character: it
   breaks before one and then keeps it attached to what follows.
3. **ICU's prefix and suffix tailorings are broader than CSS's.** CSS asks for a
   break before a wide `PO` and after a wide `PR`, and only beside a number or
   ideograph; ICU also breaks the other way round and beside letters and symbols.
   Measured as 41 left-hand classes against a wide `PR`.
4. **ICU segments some scripts with a dictionary**, in *word* breaking only.
   Inside a run of Han, Hiragana, Katakana or one of the eleven
   `Complex_Context` scripts, it joins two Han characters into one word where
   WB999 breaks them and splits supplementary-plane Katakana where WB13 joins it.
   This library implements the rules and offers the dictionary as a provider seam
   (design.md section 9).
5. **ICU's zh/ja locales break beside `U+201C` and `U+201D`** in every
   tailoring, where UAX #14's LB19 forbids it. Neither UAX #14 nor CSS asks for
   it - it is a language convention, and design.md section 9 routes those through
   `GUNI_BreakProvider`. Exactly two of class `QU`'s 39 codepoints, measured one
   by one.
6. The four writing-system-conditional tailorings **agree with ICU exactly**
   under a `ja` locale, which is where it can answer them. That is not a
   divergence; it is here because asking root instead - and reporting that ICU
   lacked them - is the mistake the locale field exists to prevent.

Everything else agrees exactly, across all four algorithms, three CSS values and
three writing systems. `anywhere` is not compared: ICU has no `@lb=anywhere`, so
it is gated by the unit tests instead.

It reads the library's answers out of the test binaries' dump modes -
`GUNI_SWEEP_DUMP=<property>` on `testSweep`, `GUNI_NAME_DUMP=1` on `testName` -
rather than through a driver of its own, because a differential wants the
library's answers as text and a test binary already links the library. Those
same dump modes are how a failing sweep is localised, so they pay for
themselves twice.

**An oracle behind the library's UCD version disagrees for reasons that are not
defects**, and the script's header records the triage so that a reader does not
re-derive it. Two of those differences were once the script's own, which is the
shape of the risk: an oracle that is wrong looks exactly like an implementation
that is wrong. That is also why the driver's frame carries a property name and
an exact line count - a parent that loses sync with its oracle would otherwise
score the engine's own banner as an answer.

## Fuzzing

The library is rebuilt with `-fsanitize=fuzzer-no-link` rather than linking the
ordinary shared library, so libFuzzer sees the parser's branches. The first
byte of each input selects the limits, so the capped paths are reachable
rather than only the wide-open defaults.

```bash
make fuzz FUZZ_TIME=3600          # utf, norm, bidi, break, case, name
make fuzz-run-bidi FUZZ_TIME=600  # one of them
```

The bidi harness earned its keep in its first minute: four characters -
U+202B U+2066 U+000A U+2069 - made the isolating-run-sequence chain loop for
ever, on input the conformance files cannot contain because rule P1 makes a
paragraph separator the last character of its paragraph. A library must not
hang on input it was not promised.

## Memory

Every allocation goes through the `GUNI_Allocator` the caller supplied, which
is cutil's `GCU_Allocator`. `tests/unit/test_allocator.cpp` proves the default
is the suite's; as modules arrive, the counting allocator there proves each
returns everything, including on the error paths.
