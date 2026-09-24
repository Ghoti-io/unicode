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
src/name/                   TIER 1: the character names
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

design.md section 3.1 names the modules and the tier each sits in. A module
arrives with:

1. Its public header under `include/ghoti.io/unicode/`, listed in
   `TIER0_FILES` (or a tier-1 list) in the Makefile, so that `check-layering`
   places it. Every header includes `macros.h` first.
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
make check-oracles                     # both
```

**The reference is pinned, and that is the whole point.** This differential used
to `import unicodedata` in its own process, which meant "the reference" was
whichever CPython ran it - and on this machine that is 3.13, two Unicode releases
behind these tables. Every disagreement it reported was that skew. The reference
is now `unicodedata_ask.py` run in an image pinned by digest in
`tools/oracle/containers/IMAGES`, through the pattern `notes/suite/CONTAINERS.md`
describes, and against a pin carrying UCD 17.0.0 there are **no differences at
all** over 7,947,413 comparisons.

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
