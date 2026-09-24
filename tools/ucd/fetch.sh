#!/bin/sh
#
# Fetch the Unicode Character Database that every table here is generated
# from, and the conformance files the tests run against.
#
# The UCD files themselves are not committed: they are 15 MB, they are
# reproducible from a version number and a URL, and committing them would
# make this repository the second-best copy of somebody else's data. Two
# things derived from them *are* committed, for two different reasons:
#
#   * the generated tables under src/*/tables/, so that a build needs
#     neither the network nor Python (documentation/design.md section 5.1);
#   * the conformance files under tests/data/ucd/<version>/, so that no
#     conformance gate can skip for want of data. That is the departure
#     from regex, whose test_break.cpp skips without third_party/ucd
#     (design.md section 2, M4). "make install-conformance" copies them.
#
# The version is read from tools/ucd/UCD_VERSION, the one place it is
# written down. Everything lands in third_party/ucd/<version>/, which
# .gitignore excludes.
#
# Usage:  tools/ucd/fetch.sh [version]
#
# Copyright 2026 by Corey Pennycuff

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
version=${1:-$(cat "$root/tools/ucd/UCD_VERSION")}
dest="$root/third_party/ucd/$version"
base="https://www.unicode.org/Public/$version"

# Paths are relative to $base and the directory structure is flattened on
# disk: the generator asks for "DerivedGeneralCategory.txt", not for the
# "extracted/" it happens to live under upstream.
#
# The emoji/ entries without a "ucd/" prefix are a different directory and
# not a typo: UTS #51's sequence data is published beside the UCD rather
# than inside it, and it is versioned with the UCD only from this path -
# "Public/emoji/latest" is already 18.0 while "Public/17.0.0/emoji" is the
# 17.0 that matches everything else here (design.md section 5.1).
# Not all four are in that directory, which is why each path is spelled in
# full: emoji-variation-sequences.txt is published under ucd/emoji/ and
# 404s from emoji/, and emoji-test.txt is the other way round.
files="
ucd/UnicodeData.txt
ucd/NameAliases.txt
ucd/NamedSequences.txt
ucd/Jamo.txt
ucd/PropList.txt
ucd/PropertyAliases.txt
ucd/PropertyValueAliases.txt
ucd/DerivedCoreProperties.txt
ucd/DerivedNormalizationProps.txt
ucd/DerivedAge.txt
ucd/CaseFolding.txt
ucd/SpecialCasing.txt
ucd/Scripts.txt
ucd/ScriptExtensions.txt
ucd/Blocks.txt
ucd/ArabicShaping.txt
ucd/BidiMirroring.txt
ucd/BidiBrackets.txt
ucd/EastAsianWidth.txt
ucd/LineBreak.txt
ucd/IndicSyllabicCategory.txt
ucd/IndicPositionalCategory.txt
ucd/VerticalOrientation.txt
ucd/HangulSyllableType.txt
ucd/CompositionExclusions.txt
ucd/NormalizationTest.txt
ucd/BidiTest.txt
ucd/BidiCharacterTest.txt
ucd/extracted/DerivedGeneralCategory.txt
ucd/extracted/DerivedCombiningClass.txt
ucd/extracted/DerivedNumericValues.txt
ucd/extracted/DerivedNumericType.txt
ucd/extracted/DerivedDecompositionType.txt
ucd/extracted/DerivedBinaryProperties.txt
ucd/extracted/DerivedBidiClass.txt
ucd/extracted/DerivedJoiningType.txt
ucd/extracted/DerivedJoiningGroup.txt
ucd/auxiliary/GraphemeBreakProperty.txt
ucd/auxiliary/WordBreakProperty.txt
ucd/auxiliary/SentenceBreakProperty.txt
ucd/auxiliary/GraphemeBreakTest.txt
ucd/auxiliary/WordBreakTest.txt
ucd/auxiliary/SentenceBreakTest.txt
ucd/auxiliary/LineBreakTest.txt
ucd/emoji/emoji-data.txt
ucd/emoji/emoji-variation-sequences.txt
emoji/emoji-sequences.txt
emoji/emoji-zwj-sequences.txt
emoji/emoji-test.txt
"

mkdir -p "$dest"

for path in $files; do
  name=$(basename "$path")
  if [ -s "$dest/$name" ]; then
    printf 'have    %s\n' "$name"
    continue
  fi
  printf 'fetch   %s\n' "$name"
  # --fail so that an HTML error page never lands on disk looking like data.
  curl --fail --silent --show-error --location \
      --output "$dest/$name.partial" "$base/$path"
  mv "$dest/$name.partial" "$dest/$name"
done

printf '\nUCD %s is in %s\n' "$version" "$dest"
