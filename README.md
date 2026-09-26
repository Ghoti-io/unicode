# Ghoti.io Unicode

The Unicode Character Database as generated tables, and the algorithms the
Standard Annexes define over them. `text` and `regex` link this library for
properties, normalisation, case mapping, segmentation and character names.

## What is implemented

This is what the library implements.

- UTF-8 decode, encode and validation.
- Character properties, sets, and case mapping.
- NFC, NFD, NFKC and NFKD.
- Grapheme, word and sentence boundaries, and line breaking.
- The bidirectional algorithm, script runs, and character names.

## Before you call it

- UTF-8 first. There is no locale data, and nothing that touches the operating system. Collation and charset conversion are absent; [documentation/design.md](documentation/design.md) is where that is settled.
- `guni_ucd_version()` is the Unicode version the tables were generated from. `guni_version_string()` is this library's own version.
- One function allocates: the bidi resolver, once a paragraph is longer than its stack buffer. Everything else is a pure function over the caller's memory.
- `NULL` for an allocator is cutil's default, and only the bidi resolver asks for one.

| Standard | What it means here |
| --- | --- |
| UTF-8 | `utf.h` decodes, encodes and validates. |
| UAX #15 | NFC, NFD, NFKC and NFKD, over code points or UTF-8. |
| UAX #29 | Grapheme, word and sentence boundaries. |
| UAX #14 | Line breaking, including the CSS `line-break` tailorings. |
| UAX #9 | The bidirectional algorithm. The one allocating call. |
| UTS #39 | Script runs and itemisation. |
| Character names | `guni_name()`. The name tables are their own translation units; a program that never calls them does not pull them in by including the other headers. |

## Examples

```c
#include <ghoti.io/unicode/name.h>
#include <stdio.h>

int main(void) {
  char name[GUNI_NAME_MAX_LENGTH + 1];
  size_t length = 0;

  if (guni_name(0x00E9, name, sizeof(name), &length) != GUNI_OK) {
    return 1;
  }
  printf("U+00E9 %s\n", name);
  return 0;
}
```

```
U+00E9 LATIN SMALL LETTER E WITH ACUTE
```

## Compile and link

Once the library is installed, pkg-config carries the include path, the
library, and its dependencies:

```bash
cc -o show show.c $(pkg-config --cflags --libs ghoti.io-unicode-0)
```

The module name ends in the major version, `-0` for this release, so two
majors can be installed side by side. A build made with `make BRANCH=-dev`
installs `ghoti.io-unicode-dev` instead.

## Building the library

[cutil](https://github.com/Ghoti-io/cutil) must already be installed where
pkg-config can see it. A dependency it cannot find is a hard error naming
the fix.

```bash
make
make test
sudo make install
```

From the workspace, which installs cutil first:

```bash
./bootstrap.sh
export PKG_CONFIG_PATH="$PWD/.local/share/pkgconfig"
make -C libs/unicode test PREFIX="$PWD/.local"
```

`make test` is the suite. `make help` lists the rest, including
`make test-asan`, `make test-valgrind` and `make coverage`. The ones that
reach outside this repository:

| Target | What it does |
| --- | --- |
| `make fuzz` | Build and run the fuzzers (`FUZZ_TIME=` for a longer run) |
| `make docs` | The Doxygen manual, into `./docs` |

## The API

Everything is prefixed `guni_` / `GUNI_`, under `<ghoti.io/unicode/...>`.
`<ghoti.io/unicode/unicode.h>` is the umbrella.

- **`core.h`** — `GUNI_Result`, `guni_result_string()`, `GUNI_Limits`, and `guni_version_string()`.
- **`utf.h`**, **`char.h`**, **`set.h`**, **`case.h`**, **`norm.h`**, **`break.h`**, **`bidi.h`**, **`script.h`**, **`name.h`** — the modules in the table above. `guni_ucd_version()` is declared in `char.h`.
- **`allocator.h`** — `GUNI_Allocator`, which is cutil's `GCU_Allocator`.

[What is implemented](#what-is-implemented) is the inventory.
[Before you call it](#before-you-call-it) is what that changes about a call.

## Dependencies

Found through pkg-config, and the installed `.pc` file names it, so a
program that links `ghoti.io-unicode-0` links this too.

- [ghoti.io-cutil](https://github.com/Ghoti-io/cutil) — the allocator the bidi resolver uses.

## Documentation

[documentation/design.md](documentation/design.md) is the design: what each
module holds, and what the library refuses. `make docs` builds the manual.

## Status

The modules above are built. The tables are generated from one pinned Unicode
version; `guni_ucd_version()` is that pin as the library reports it.

## License

LGPL-3.0-only. See [COPYING.LESSER](COPYING.LESSER) for the license, and
[COPYING](COPYING) for the GPL text it is written as additional permissions
on top of.

Contributions are not being accepted at this time; see
[CONTRIBUTING.md](CONTRIBUTING.md) for what is useful instead.
