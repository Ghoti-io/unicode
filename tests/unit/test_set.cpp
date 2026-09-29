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

/**
 * @file
 *
 * Properties as sets: the name lookup and the range enumeration.
 *
 * The exhaustive agreement between these ranges and char.h's point queries is
 * test_sweep.cpp's. What is here is the API's own contract - loose matching,
 * the preflight, the shape of the output - and the two cases a regex compiler
 * would hit first: a value name that means different things for different
 * properties, and a category group that is a mask rather than a value.
 */

#include <cstdint>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

#include "test_helpers.h"

namespace {

GUNI_Property property(const std::string & name) {
  GUNI_Property out = static_cast<GUNI_Property>(0);
  EXPECT_EQ(guni_property_by_name(name.data(), name.size(), &out), GUNI_OK)
      << "no property named " << name;
  return out;
}

uint32_t value(GUNI_Property id, const std::string & name) {
  uint32_t out = 0;
  EXPECT_EQ(guni_value_by_name(id, name.data(), name.size(), &out), GUNI_OK)
      << "no value named " << name << " for " << guni_property_name(id);
  return out;
}

std::vector<GUNI_Range> ranges(GUNI_Property id, uint32_t val) {
  size_t needed = 0;
  /* The preflight: cap 0 reports the requirement, which is the shape every
   * output in this library has (design.md section 4.5). */
  GUNI_Result first = guni_set_ranges(id, val, nullptr, 0, &needed);
  EXPECT_TRUE(first == GUNI_OK || first == GUNI_ERR_LIMIT);
  std::vector<GUNI_Range> out(needed);
  size_t written = 0;
  EXPECT_EQ(guni_set_ranges(id, val, out.data(), needed, &written), GUNI_OK);
  EXPECT_EQ(written, needed);
  return out;
}

TEST(Set, PropertyNamesMatchLoosely) {
  /* UAX #44-LM3. Every spelling a pattern might use has to work, or this
   * library rejects patterns every other engine accepts. */
  const GUNI_Property gc = property("General_Category");
  EXPECT_EQ(property("gc"), gc);
  EXPECT_EQ(property("generalcategory"), gc);
  EXPECT_EQ(property("GENERAL-CATEGORY"), gc);
  EXPECT_EQ(property("  general category  "), gc);
  EXPECT_EQ(property("General_category"), gc);

  /* The short name of a binary property, which is only in
   * PropertyAliases.txt keyed the other way round - the lookup that was
   * missing when this test was written. */
  EXPECT_EQ(property("Alpha"), property("Alphabetic"));
  EXPECT_EQ(property("AHex"), property("ASCII_Hex_Digit"));
  EXPECT_EQ(property("scx"), property("Script_Extensions"));
  EXPECT_EQ(property("NFC_QC"), property("NFC_Quick_Check"));
  EXPECT_EQ(property("ccc"), property("Canonical_Combining_Class"));
  EXPECT_EQ(property("blk"), property("Block"));
  EXPECT_EQ(property("InCB"), property("Indic_Conjunct_Break"));
}

TEST(Set, UnknownAndDegeneratePropertyNamesAreRefused) {
  GUNI_Property out = static_cast<GUNI_Property>(0);
  EXPECT_EQ(guni_property_by_name("not_a_property", 14, &out), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_property_by_name("", 0, &out), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_property_by_name("___", 3, &out), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_property_by_name(nullptr, 0, &out), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_property_by_name("gc", 2, nullptr), GUNI_ERR_INVALID);
  /* A name longer than any in the tables cannot match, and must not overrun
   * the buffer it is normalised into. */
  const std::string huge(4096, 'x');
  EXPECT_EQ(guni_property_by_name(huge.data(), huge.size(), &out),
      GUNI_ERR_INVALID);
  /* And a length shorter than the string is honoured: the API takes a length
   * because a pattern's property name is a slice of the pattern. */
  EXPECT_EQ(guni_property_by_name("gcx", 2, &out), GUNI_OK);
  EXPECT_EQ(out, property("General_Category"));
}

TEST(Set, ValueNamesMatchLooselyAndPerProperty) {
  const GUNI_Property gc = property("General_Category");
  EXPECT_EQ(value(gc, "Lu"), static_cast<uint32_t>(GUNI_GC_LU));
  EXPECT_EQ(value(gc, "Uppercase_Letter"), static_cast<uint32_t>(GUNI_GC_LU));
  EXPECT_EQ(value(gc, "uppercaseletter"), static_cast<uint32_t>(GUNI_GC_LU));
  EXPECT_EQ(value(gc, "cntrl"), static_cast<uint32_t>(GUNI_GC_CC));

  const GUNI_Property sc = property("Script");
  EXPECT_EQ(value(sc, "Grek"), static_cast<uint32_t>(GUNI_SCRIPT_GREEK));
  EXPECT_EQ(value(sc, "greek"), static_cast<uint32_t>(GUNI_SCRIPT_GREEK));

  /* "AL" is Above_Left for Canonical_Combining_Class and Alphabetic for
   * Line_Break - the file says so in a comment, and a lookup that was not
   * per-property would have to guess. */
  EXPECT_EQ(value(property("ccc"), "AL"), 228u);
  EXPECT_EQ(value(property("lb"), "AL"), static_cast<uint32_t>(GUNI_LB_AL));
  EXPECT_EQ(value(property("ccc"), "230"), 230u);
  EXPECT_EQ(value(property("ccc"), "Above"), 230u);

  /* Binary properties take the UCD's five spellings of each answer. */
  const GUNI_Property alpha = property("Alphabetic");
  EXPECT_EQ(value(alpha, "Y"), 1u);
  EXPECT_EQ(value(alpha, "Yes"), 1u);
  EXPECT_EQ(value(alpha, "T"), 1u);
  EXPECT_EQ(value(alpha, "True"), 1u);
  EXPECT_EQ(value(alpha, "N"), 0u);
  EXPECT_EQ(value(alpha, "False"), 0u);

  /* And the quick checks have three. */
  const GUNI_Property qc = property("NFC_Quick_Check");
  EXPECT_EQ(value(qc, "Maybe"), static_cast<uint32_t>(GUNI_QC_MAYBE));
  EXPECT_EQ(value(qc, "M"), static_cast<uint32_t>(GUNI_QC_MAYBE));
  EXPECT_EQ(value(qc, "No"), static_cast<uint32_t>(GUNI_QC_NO));

  uint32_t out = 0;
  EXPECT_EQ(guni_value_by_name(gc, "Greek", 5, &out), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_value_by_name(gc, "", 0, &out), GUNI_ERR_INVALID);
  EXPECT_EQ(guni_value_by_name(static_cast<GUNI_Property>(GUNI_PROPERTY_COUNT),
                "Lu", 2, &out),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_value_by_name(gc, "Lu", 2, nullptr), GUNI_ERR_INVALID);
}

TEST(Set, NamesRoundTrip) {
  /* Every property's every value: the long name it reports must be a name it
   * accepts back, or a pattern that echoes a property value cannot be
   * reparsed. */
  size_t checked = 0;
  for (uint32_t id = 0; id < GUNI_PROPERTY_COUNT; ++id) {
    GUNI_Property prop = static_cast<GUNI_Property>(id);
    const char * name = guni_property_name(prop);
    ASSERT_NE(name, nullptr);
    GUNI_Property back = static_cast<GUNI_Property>(0);
    ASSERT_EQ(guni_property_by_name(name, std::strlen(name), &back), GUNI_OK)
        << name;
    EXPECT_EQ(back, prop) << name;
    for (uint32_t val = 0; val < guni_property_value_count(prop); ++val) {
      const char * value_name = guni_value_name(prop, val);
      ASSERT_NE(value_name, nullptr);
      if (value_name[0] == '\0') {
        continue; /* a combining class with no name of its own */
      }
      uint32_t round = 0;
      ASSERT_EQ(guni_value_by_name(prop, value_name, std::strlen(value_name),
                    &round),
          GUNI_OK)
          << name << " = " << value_name;
      EXPECT_EQ(round, val) << name << " = " << value_name;
      ++checked;
    }
  }
  EXPECT_GT(checked, static_cast<size_t>(700)) << "only " << checked
                                              << " values round-tripped";
  EXPECT_EQ(guni_property_name(static_cast<GUNI_Property>(GUNI_PROPERTY_COUNT)),
      nullptr);
  EXPECT_EQ(guni_value_name(static_cast<GUNI_Property>(GUNI_PROPERTY_COUNT), 0),
      nullptr);
  EXPECT_EQ(guni_value_name(GUNI_PROPERTY_SCRIPT, GUNI_SCRIPT_COUNT), nullptr);
}

TEST(Set, RangesAreSortedMergedAndComplete) {
  const GUNI_Property gc = property("gc");
  std::vector<GUNI_Range> upper = ranges(gc, GUNI_GC_LU);
  ASSERT_FALSE(upper.empty());
  EXPECT_EQ(upper[0].first, static_cast<uint32_t>('A'));
  EXPECT_EQ(upper[0].last, static_cast<uint32_t>('Z'));
  uint32_t total = 0;
  for (size_t index = 0; index < upper.size(); ++index) {
    EXPECT_LE(upper[index].first, upper[index].last);
    if (index != 0) {
      EXPECT_GT(upper[index].first, upper[index - 1].last + 1)
          << "ranges " << index - 1 << " and " << index << " touch";
    }
    total += upper[index].last - upper[index].first + 1;
  }
  /* Counted against the point query over the whole space, so that a range
   * list missing a codepoint cannot pass. */
  uint32_t counted = 0;
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    if (guni_general_category(cp) == GUNI_GC_LU) {
      ++counted;
    }
  }
  EXPECT_EQ(total, counted);
}

TEST(Set, BlockRangesIncludeTheGaps) {
  const GUNI_Property blk = property("Block");
  std::vector<GUNI_Range> basic = ranges(blk, value(blk, "Basic_Latin"));
  ASSERT_EQ(basic.size(), static_cast<size_t>(1));
  EXPECT_EQ(basic[0].first, 0u);
  EXPECT_EQ(basic[0].last, 0x7Fu);

  /* No_Block is every codepoint Blocks.txt does not mention, which is a value
   * with no lines of its own. */
  std::vector<GUNI_Range> none = ranges(blk, GUNI_BLOCK_NO_BLOCK);
  ASSERT_FALSE(none.empty());
  bool covers_gap = false;
  for (const GUNI_Range & range : none) {
    if (range.first <= 0x1FC00 && 0x1FC00 <= range.last) {
      covers_gap = true;
    }
    EXPECT_EQ(guni_block(range.first), GUNI_BLOCK_NO_BLOCK);
    EXPECT_EQ(guni_block(range.last), GUNI_BLOCK_NO_BLOCK);
  }
  EXPECT_TRUE(covers_gap);
}

TEST(Set, ScriptExtensionsEnumeratesMembership) {
  /* \p{scx=Greek} is membership of a set, not equality of a value, and is a
   * strictly larger set than \p{sc=Greek}. */
  const GUNI_Property scx = property("scx");
  const GUNI_Property sc = property("sc");
  std::vector<GUNI_Range> extended = ranges(scx, GUNI_SCRIPT_GREEK);
  std::vector<GUNI_Range> plain = ranges(sc, GUNI_SCRIPT_GREEK);
  uint32_t extended_count = 0;
  uint32_t plain_count = 0;
  for (const GUNI_Range & range : extended) {
    extended_count += range.last - range.first + 1;
  }
  for (const GUNI_Range & range : plain) {
    plain_count += range.last - range.first + 1;
  }
  EXPECT_GT(extended_count, plain_count);
  for (const GUNI_Range & range : plain) {
    EXPECT_TRUE(guni_set_contains(scx, GUNI_SCRIPT_GREEK, range.first));
  }
}

TEST(Set, CategoryGroupsAreMasks) {
  size_t needed = 0;
  ASSERT_EQ(guni_gc_mask_ranges(GUNI_GC_MASK_L, nullptr, 0, &needed),
      GUNI_ERR_LIMIT);
  ASSERT_GT(needed, static_cast<size_t>(100));
  std::vector<GUNI_Range> letters(needed);
  size_t written = 0;
  ASSERT_EQ(guni_gc_mask_ranges(GUNI_GC_MASK_L, letters.data(), needed,
                &written),
      GUNI_OK);
  EXPECT_EQ(written, needed);
  EXPECT_TRUE(guni_gc_mask_contains(GUNI_GC_MASK_L, 'a'));
  EXPECT_FALSE(guni_gc_mask_contains(GUNI_GC_MASK_L, '1'));
  EXPECT_TRUE(guni_gc_mask_contains(GUNI_GC_MASK_N, '1'));

  /* The mask's ranges are the union of its members' ranges, and the count is
   * checked against the point query rather than against itself. */
  uint32_t total = 0;
  for (const GUNI_Range & range : letters) {
    total += range.last - range.first + 1;
  }
  uint32_t counted = 0;
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    if (guni_general_category_mask(cp) & GUNI_GC_MASK_L) {
      ++counted;
    }
  }
  EXPECT_EQ(total, counted);

  /* An empty mask is an empty set, not an error. */
  size_t empty = 1;
  EXPECT_EQ(guni_gc_mask_ranges(0, nullptr, 0, &empty), GUNI_OK);
  EXPECT_EQ(empty, static_cast<size_t>(0));
  EXPECT_EQ(guni_gc_mask_ranges(GUNI_GC_MASK_L, nullptr, 0, nullptr),
      GUNI_ERR_INVALID);
}

TEST(Set, AValueWithNoCodepointsIsAnEmptySetNotAnError) {
  /* GUNI_BLOCK_NO_BLOCK aside, a property can have a value nothing uses -
   * and on a Unicode upgrade a retired value keeps its number and empties. A
   * caller must be able to tell that from a failure. */
  const GUNI_Property hst = property("hst");
  size_t needed = 1;
  ASSERT_EQ(guni_set_ranges(hst, 0, nullptr, 0, &needed), GUNI_ERR_LIMIT);
  /* Not_Applicable is most of the space, so use an out-of-range value for the
   * empty case: it is a value no codepoint has. */
  size_t empty = 1;
  EXPECT_EQ(guni_set_ranges(hst, 9999, nullptr, 0, &empty), GUNI_OK);
  EXPECT_EQ(empty, static_cast<size_t>(0));
}

TEST(Set, ErrorPathsAreRefusedRatherThanGuessed) {
  GUNI_Range buffer[4];
  size_t written = 0;
  EXPECT_EQ(guni_set_ranges(static_cast<GUNI_Property>(GUNI_PROPERTY_COUNT), 0,
                buffer, 4, &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_set_ranges(GUNI_PROPERTY_SCRIPT, 0, nullptr, 4, &written),
      GUNI_ERR_INVALID);
  EXPECT_EQ(guni_set_ranges(GUNI_PROPERTY_SCRIPT, 0, buffer, 4, nullptr),
      GUNI_ERR_INVALID);
  EXPECT_FALSE(guni_set_contains(static_cast<GUNI_Property>(GUNI_PROPERTY_COUNT),
      0, 'A'));
  EXPECT_EQ(guni_property_value_count(
                static_cast<GUNI_Property>(GUNI_PROPERTY_COUNT)),
      0u);
  EXPECT_EQ(guni_property_kind(static_cast<GUNI_Property>(GUNI_PROPERTY_COUNT)),
      GUNI_PROP_KIND_ENUM);
}

TEST(Set, PartialBufferReportsTheFullRequirement) {
  const GUNI_Property gc = property("gc");
  size_t needed = 0;
  ASSERT_EQ(guni_set_ranges(gc, GUNI_GC_LU, nullptr, 0, &needed),
      GUNI_ERR_LIMIT);
  ASSERT_GT(needed, static_cast<size_t>(2));
  GUNI_Range two[2];
  size_t written = 0;
  EXPECT_EQ(guni_set_ranges(gc, GUNI_GC_LU, two, 2, &written), GUNI_ERR_LIMIT);
  EXPECT_EQ(written, needed);
  /* What fitted is still the first two ranges, so a caller that grows its
   * buffer and asks again gets a superset and not a different answer. */
  EXPECT_EQ(two[0].first, static_cast<uint32_t>('A'));
}


TEST(Set, EveryRangeOfEveryValueAgreesWithThePointQuery) {
  /* For every property and every one of its values: the enumerated ranges
   * contain their endpoints and not the codepoints either side of them, and
   * the ranges are sorted and never adjacent. That is set.h and char.h
   * answering the same question two ways, over 97 properties and every value
   * between them - which is the claim design.md section 4.2 makes and the
   * one thing a shared table cannot be trusted about without checking. */
  size_t values_checked = 0;
  size_t ranges_checked = 0;
  for (uint32_t id = 0; id < GUNI_PROPERTY_COUNT; ++id) {
    GUNI_Property prop = static_cast<GUNI_Property>(id);
    for (uint32_t val = 0; val < guni_property_value_count(prop); ++val) {
      std::vector<GUNI_Range> found = ranges(prop, val);
      uint32_t previous_end = 0;
      for (size_t index = 0; index < found.size(); ++index) {
        const GUNI_Range & range = found[index];
        ASSERT_LE(range.first, range.last);
        if (index != 0) {
          ASSERT_GT(range.first, previous_end + 1)
              << guni_property_name(prop) << " ranges " << index - 1 << " and "
              << index << " touch or overlap";
        }
        previous_end = range.last;
        ASSERT_TRUE(guni_set_contains(prop, val, range.first))
            << guni_property_name(prop) << " = " << val;
        ASSERT_TRUE(guni_set_contains(prop, val, range.last));
        if (range.first > 0) {
          ASSERT_FALSE(guni_set_contains(prop, val, range.first - 1));
        }
        if (range.last < GUNI_MAX_CODEPOINT) {
          ASSERT_FALSE(guni_set_contains(prop, val, range.last + 1));
        }
        ++ranges_checked;
      }
      ++values_checked;
    }
  }
  /* The denominators, so that a loop that never ran cannot read as a pass. */
  EXPECT_GT(values_checked, static_cast<size_t>(1000));
  EXPECT_GT(ranges_checked, static_cast<size_t>(10000));
}

TEST(Set, EverySpellingTheUcdDefinesResolves) {
  /* The gate that was missing when `blk` lost all 347 of its short value
   * aliases - `Greek`, `Greek_Ext`, `ASCII` for `Basic_Latin`, 143 of them
   * differing from the long name - while `gc=Lu`, `sc=Grek` and `lb=AL` all
   * resolved and nothing pointed at the one property that did not.
   *
   * **The denominator is the UCD's, and that is the whole design.** A sweep
   * over the spellings this library holds would have walked every name in
   * its own tables, resolved all of them, and printed green: a spelling
   * that was never recorded is not in the set such a sweep enumerates. So
   * the list comes from PropertyValueAliases.txt, parsed by
   * tools/ucd/gen_sweep.py - the second reader, not the one that generates
   * the tables - and committed as a fixture so this runs on a clone with no
   * network and no UCD.
   *
   * What it asserts is agreement rather than mere resolution: every
   * spelling must give the same value the canonical long name gives, so a
   * table that resolved a name to the wrong value fails here too. */
  const std::string path
      = gunitest::data(std::string("sweep/") + guni_ucd_version()
          + ".spellings");
  std::ifstream in(path);
  ASSERT_TRUE(in.is_open()) << "fixture not found: " << path;

  std::string line;
  size_t declared = 0;
  size_t checked = 0;
  size_t skipped = 0;
  size_t properties = 0;
  std::set<std::string> skipped_keys;
  std::string last_key;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line.rfind("version ", 0) == 0) {
      EXPECT_EQ(line.substr(8), std::string(guni_ucd_version()))
          << "fixture is for a different UCD than the library";
      continue;
    }
    if (line.rfind("count ", 0) == 0) {
      declared = static_cast<size_t>(std::stoul(line.substr(6)));
      continue;
    }
    const size_t first = line.find('\t');
    const size_t second = line.find('\t', first + 1);
    ASSERT_NE(first, std::string::npos) << line;
    ASSERT_NE(second, std::string::npos) << line;
    const std::string key = line.substr(0, first);
    const std::string spelling = line.substr(first + 1, second - first - 1);
    const std::string canonical = line.substr(second + 1);

    GUNI_Property id = static_cast<GUNI_Property>(0);
    if (guni_property_by_name(key.data(), key.size(), &id) != GUNI_OK) {
      /* A property this library does not carry at all is a different
       * question from a spelling it lost. Six are absent and each is
       * out of scope for a property-set API rather than missing: `age`
       * is a version, `JSN` a string, `CE` a normalisation input, `bpt`
       * bracket pairing, and `kEH_NoMirror`/`kEH_NoRotate` are Unihan.
       *
       * **Counted, not merely skipped.** The skip path is where a gate
       * goes blind: a change that made some property unreachable by name
       * would turn every one of its spellings into a silent `continue`
       * and this test would still pass. So the skipped rows are added to
       * the checked ones and the total must be the fixture's own count,
       * and the number of distinct absent keys is held down as well. */
      if (skipped_keys.insert(key).second) {
        /* first time this key was seen */
      }
      skipped++;
      continue;
    }
    if (key != last_key) {
      properties++;
      last_key = key;
    }

    uint32_t got = 0;
    ASSERT_EQ(guni_value_by_name(id, spelling.data(), spelling.size(), &got),
        GUNI_OK)
        << key << "=" << spelling << " does not resolve";
    uint32_t want = 0;
    ASSERT_EQ(
        guni_value_by_name(id, canonical.data(), canonical.size(), &want),
        GUNI_OK)
        << key << "=" << canonical << " (the canonical name) does not resolve";
    EXPECT_EQ(got, want)
        << key << "=" << spelling << " resolves to " << got << ", but "
        << canonical << " is " << want;
    checked++;
  }

  /* Denominators, so that a fixture that failed to load or a loop that
   * never ran cannot read as a pass. Every row is accounted for: checked
   * or explicitly skipped, and the skips are bounded by name count so
   * that a property falling out of the lookup shows up here. */
  EXPECT_EQ(checked + skipped, declared)
      << "fixture says " << declared << " rows; checked " << checked
      << " and skipped " << skipped;
  EXPECT_GT(checked, static_cast<size_t>(2000));
  EXPECT_GT(properties, static_cast<size_t>(20));
  EXPECT_LE(skipped_keys.size(), static_cast<size_t>(6))
      << "a property stopped resolving by name";
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
