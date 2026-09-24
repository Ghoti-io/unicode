# Development

## Layout

```
include/ghoti.io/unicode/   Public headers, one per module (design.md section 13)
src/core/                   Result strings, limits, the allocator
src/utf/                    UTF-8 decode and encode, the GUNI_Invalid policy
src/char/                   Every per-codepoint property
src/char/tables/            GENERATED: the record, the trie, the runs, the names
src/set/                    Properties as sets, and the name lookup
src/unicode.c               The version
tests/unit/                 Unit tests (gtest)
tests/conformance/          The Unicode conformance files and their runners (from phase A)
tests/data/                 Fixtures, reached through GUNI_TEST_DATA
tests/fuzz/                 libFuzzer harnesses and seed corpus (from phase A)
tools/ucd/                  fetch.sh, gen_tables.py, gen_sweep.py, UCD_VERSION
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

## Fuzzing

The library is rebuilt with `-fsanitize=fuzzer-no-link` rather than linking the
ordinary shared library, so libFuzzer sees the parser's branches. The first
byte of each input selects the limits, so the capped paths are reachable
rather than only the wide-open defaults.

```bash
make fuzz FUZZ_TIME=3600
```

## Memory

Every allocation goes through the `GUNI_Allocator` the caller supplied, which
is cutil's `GCU_Allocator`. `tests/unit/test_allocator.cpp` proves the default
is the suite's; as modules arrive, the counting allocator there proves each
returns everything, including on the error paths.
