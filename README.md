# Ghoti.io Unicode

The Unicode Character Database as generated tables, and the algorithms the
Standard Annexes define over them: normalisation (UAX #15), segmentation
(UAX #29), line breaking (UAX #14), the bidirectional algorithm (UAX #9), case
mapping, and the properties a text shaper needs. UTF-8 first; no locale data;
nothing that touches the operating system.

It exists so that the libraries in this suite use one Unicode rather than
approximating it each: `regex` and `text` each generated their own tables,
`ctang` linked ICU to get one grapheme iterator, and `font` would have been the
fourth. The design, the tiers, and the migration plan are in
[documentation/design.md](documentation/design.md).

```c
#include <ghoti.io/unicode/unicode.h>

printf("%s\n", guni_version_string());
```

## Building

Requires [cutil](https://github.com/Ghoti-io/cutil), found through pkg-config.
That is the only way it is looked for: a dependency pkg-config cannot find is
a hard error naming the fix, rather than a fallback to a checkout next door
that only an in-tree build would ever exercise.

```bash
make            # shared and static libraries
make test       # unit tests and the gates
sudo make install
```

| Target | What it does |
| --- | --- |
| `make test` | Run the unit tests and every gate in `TEST_GATES` |
| `make test-quiet` | One line per suite |
| `make test-valgrind-quiet` | Same, under Valgrind |
| `make test-asan` | Rebuild with ASan+UBSan and run the suite |
| `make check-layering` | Fail if a lower tier includes a higher tier's header |
| `make coverage` | Line coverage, per file |
| `make fuzz` | Build and run every fuzzer (`FUZZ_TIME=3600` for a real campaign) |
| `make docs` | Doxygen, into `./docs` |

## The API

Everything is prefixed `guni_` / `GUNI_`, under `<ghoti.io/unicode/...>`.

- **`core.h`** - `GUNI_Result`, `guni_result_string()`, `GUNI_Limits`, and
  the version.
- **`allocator.h`** - `GUNI_Allocator`, which is cutil's `GCU_Allocator`, so an
  allocator written for any library in the suite works with all of them.
- **`unicode.h`** - the umbrella for tier 0.

The modules design.md section 3 names - `char.h`, `set.h`, `case.h`, `norm.h`,
`break.h`, `bidi.h`, `script.h`, `utf.h` and tier 1's `name.h` - do not exist
yet. Absent, not stubbed: there is no function here that returns
`GUNI_ERR_UNSUPPORTED` in place of an algorithm.

## Status

Scaffold. The library builds, installs, and passes its gates - `check-symbols`,
`check-layering`, `check-aliasing`, `check-stamps` - with the core module and
nothing else. Phase A of [documentation/design.md](documentation/design.md)
section 16 is the next thing.

## License

LGPL-3.0-only. See [COPYING.LESSER](COPYING.LESSER) for the license, and
[COPYING](COPYING) for the GPL text it is written as additional permissions
on top of.

Contributions are not being accepted at this time; see
[CONTRIBUTING.md](CONTRIBUTING.md) for what is useful instead.
