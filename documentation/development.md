# Development

## Layout

```
include/ghoti.io/unicode/   Public headers, one per module (design.md section 13)
src/core/                   Result strings, limits, the allocator
src/unicode.c               The version
tests/unit/                 Unit tests (gtest)
tests/conformance/          The Unicode conformance files and their runners (from phase A)
tests/data/                 Fixtures, reached through GUNI_TEST_DATA
tests/fuzz/                 libFuzzer harnesses and seed corpus (from phase A)
tools/ucd/                  fetch.sh, gen_tables.py, UCD_VERSION (from phase A)
tools/check-stamps.py       The flag-stamp gate
```

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
