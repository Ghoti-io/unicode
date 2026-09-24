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
 * The exhaustive sweep: every property of every one of the 1,114,112
 * codepoints, against an oracle that parsed the UCD with different code.
 *
 * documentation/design.md section 12.1. This is the gate that makes an edited
 * table fail on a fresh clone with no network and no Python: the fixture
 * tests/data/sweep/<version>.sums was written by tools/ucd/gen_sweep.py, a
 * second implementation of reading the UCD, and every number in it is
 * recomputed here from the compiled library.
 *
 * Values are compared as **text**, never as enum numbers, so that a
 * renumbered enum - which would change the meaning of a value a consumer
 * stored - fails here too (design.md section 2, M12).
 *
 * The sweep is over every codepoint and not over the table's runs, and the
 * distinction matters: walking the runs and trusting them would leave a wrong
 * value in the middle of a run invisible, which is exactly the bug this
 * exists to catch. What makes an exhaustive walk affordable is that the loop
 * compares a cheap integer key per codepoint and only spells a value out when
 * the key changes - 1,114,112 integer comparisons per property rather than as
 * many string constructions.
 *
 * Set GUNI_SWEEP_DUMP=<property long name> to print this side's ranges
 * instead of running the tests, for diffing against
 * "tools/ucd/gen_sweep.py --property <name>".
 */

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

/* The generated tables, by their internal header. The tests link the static
 * archive with --whole-archive, so the hidden symbols are reachable; reaching
 * them is how the trie and the runs table get compared to each other rather
 * than each to itself. */
#include "../../src/char/tables/tables.h"

#include "test_helpers.h"

namespace {

constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001B3ULL;

/** FNV-1a-64, the same arithmetic gen_sweep.py does. */
class Fnv {
public:
  void add(const char * bytes, size_t length) {
    for (size_t index = 0; index < length; ++index) {
      hash_ ^= static_cast<unsigned char>(bytes[index]);
      hash_ *= kFnvPrime;
    }
  }
  uint64_t value() const { return hash_; }

private:
  uint64_t hash_ = kFnvOffset;
};

struct Expectation {
  uint64_t runs = 0;
  uint64_t hash = 0;
};

/** Is this property one the record holds, and so one the partition covers? */
bool in_record(GUNI_Property id) {
  return id != GUNI_PROPERTY_BLOCK;
}

/**
 * A cheap integer that changes exactly when the property's value changes.
 *
 * Not the value itself for two of them: Script_Extensions is a set, so the
 * key is a hash of it, and Numeric_Value is a rational. A hash could in
 * principle collide across a boundary and hide it - which would then show up
 * as a wrong run count, because the oracle counted the boundary and this
 * would not.
 */
uint64_t property_key(const std::string & name, GUNI_Property id, uint32_t cp) {
  if (name == "Numeric_Value") {
    int64_t numerator = 0;
    uint32_t denominator = 0;
    if (!guni_numeric_value(cp, &numerator, &denominator)) {
      return ~UINT64_C(0);
    }
    return (static_cast<uint64_t>(numerator) << 12) ^ denominator;
  }
  if (guni_property_kind(id) == GUNI_PROP_KIND_SCX) {
    GUNI_Script scripts[64];
    size_t count = 0;
    if (guni_script_extensions(cp, scripts, 64, &count) != GUNI_OK) {
      return ~UINT64_C(1);
    }
    uint64_t key = count;
    for (size_t index = 0; index < count; ++index) {
      key = key * 1000003u + static_cast<uint64_t>(scripts[index]);
    }
    return key;
  }
  return guni_property_value(cp, id);
}

/** The property's value at @p cp, spelled the way the oracle spells it. */
std::string value_text(const std::string & name, GUNI_Property id, uint32_t cp) {
  if (name == "Numeric_Value") {
    int64_t numerator = 0;
    uint32_t denominator = 0;
    if (!guni_numeric_value(cp, &numerator, &denominator)) {
      return "None";
    }
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%" PRId64 "/%" PRIu32, numerator,
        denominator);
    return buffer;
  }
  if (name == "Canonical_Combining_Class") {
    return std::to_string(static_cast<unsigned>(guni_combining_class(cp)));
  }
  if (guni_property_kind(id) == GUNI_PROP_KIND_SCX) {
    GUNI_Script scripts[64];
    size_t count = 0;
    if (guni_script_extensions(cp, scripts, 64, &count) != GUNI_OK) {
      return "<overflow>";
    }
    std::vector<std::string> names;
    names.reserve(count);
    for (size_t index = 0; index < count; ++index) {
      const char * text = guni_value_name(GUNI_PROPERTY_SCRIPT,
          static_cast<uint32_t>(scripts[index]));
      names.push_back(text != nullptr ? text : "<unnamed>");
    }
    /* Sorted by name, not by enum value, so that the text does not depend on
     * this library's numbering. */
    std::sort(names.begin(), names.end());
    std::string out;
    for (size_t index = 0; index < names.size(); ++index) {
      if (index != 0) {
        out += ',';
      }
      out += names[index];
    }
    return out;
  }
  const char * text = guni_value_name(id, guni_property_value(cp, id));
  return text != nullptr ? text : "<unnamed>";
}

/**
 * Sweep one property over every codepoint.
 *
 * @param boundaries If not null, every codepoint at which this property's
 *        value changes is marked. The union over the record's properties is
 *        the partition, so the partition is a by-product of the sweep rather
 *        than a second pass over the whole space.
 */
Expectation sweep_property(const std::string & name, GUNI_Property id,
    std::string * dump, std::vector<uint8_t> * boundaries) {
  Expectation out;
  Fnv hash;
  uint64_t previous_key = property_key(name, id, 0);
  std::string previous_text = value_text(name, id, 0);
  uint32_t start = 0;
  for (uint32_t cp = 1; cp <= GUNI_MAX_CODEPOINT + 1; ++cp) {
    bool changed = true;
    uint64_t key = 0;
    if (cp <= GUNI_MAX_CODEPOINT) {
      key = property_key(name, id, cp);
      changed = key != previous_key;
    }
    if (!changed) {
      continue;
    }
    if (cp <= GUNI_MAX_CODEPOINT && boundaries != nullptr) {
      (*boundaries)[cp] = 1;
    }
    /* Built as a string rather than into a fixed buffer: the longest value
     * here is a Script_Extensions set of 21 script names, 250 characters, and
     * a 192-byte buffer truncated it - which merged two runs and, worse, made
     * the dump append snprintf's would-be length and read past the buffer. */
    char range[24];
    std::snprintf(range, sizeof(range), "%06X..%06X ", start, cp - 1);
    std::string line = std::string(range) + previous_text + "\n";
    hash.add(line.data(), line.size());
    if (dump != nullptr) {
      dump->append(line);
    }
    ++out.runs;
    start = cp;
    previous_key = key;
    if (cp <= GUNI_MAX_CODEPOINT) {
      previous_text = value_text(name, id, cp);
    }
  }
  out.hash = hash.value();
  return out;
}

/** The fixture, parsed once. */
class Fixture {
public:
  Fixture() {
    path_ = gunitest::data(std::string("sweep/") + guni_ucd_version() + ".sums");
    FILE * handle = std::fopen(path_.c_str(), "rb");
    if (handle == nullptr) {
      /* Not a skip: the fixture is committed precisely so that it cannot be
       * absent, and a check that passes when it could not run is worse than
       * one that fails (design.md section 2, M4). */
      return;
    }
    char line[512];
    while (std::fgets(line, sizeof(line), handle) != nullptr) {
      if (line[0] == '#' || line[0] == '\n') {
        continue;
      }
      char name[256];
      unsigned long long runs = 0;
      unsigned long long hash = 0;
      if (std::sscanf(line, "version %255s", name) == 1) {
        version_ = name;
      }
      else if (std::sscanf(line, "%255s %llu %llx", name, &runs, &hash) == 3) {
        Expectation expectation;
        expectation.runs = runs;
        expectation.hash = hash;
        entries_[name] = expectation;
      }
    }
    std::fclose(handle);
  }

  const std::string & version() const { return version_; }
  const std::string & path() const { return path_; }
  const std::map<std::string, Expectation> & entries() const { return entries_; }

private:
  std::string version_;
  std::string path_;
  std::map<std::string, Expectation> entries_;
};

const Fixture & fixture() {
  static const Fixture instance;
  return instance;
}

/**
 * The whole sweep, in one pass, because every part of it wants the same walk:
 * the per-property checksums, the partition the checksums' boundaries imply,
 * and the count of how many properties were actually compared.
 */
class Sweep : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    boundaries_ = new std::vector<uint8_t>(GUNI_CODEPOINT_COUNT, 0);
    (*boundaries_)[0] = 1;
    results_ = new std::map<std::string, Expectation>();
    for (const auto & entry : fixture().entries()) {
      if (entry.first == "partition") {
        continue;
      }
      GUNI_Property id = static_cast<GUNI_Property>(0);
      bool known = guni_property_by_name(entry.first.c_str(),
                       entry.first.size(), &id) == GUNI_OK;
      if (!known && entry.first != "Numeric_Value") {
        unknown_->push_back(entry.first);
        continue;
      }
      (*results_)[entry.first] = sweep_property(entry.first, id, nullptr,
          (known && in_record(id)) ? boundaries_ : nullptr);
    }
  }

  static void TearDownTestSuite() {
    delete boundaries_;
    delete results_;
    boundaries_ = nullptr;
    results_ = nullptr;
  }

  static std::vector<uint8_t> * boundaries_;
  static std::map<std::string, Expectation> * results_;
  static std::vector<std::string> * unknown_;
};

std::vector<uint8_t> * Sweep::boundaries_ = nullptr;
std::map<std::string, Expectation> * Sweep::results_ = nullptr;
std::vector<std::string> * Sweep::unknown_ = new std::vector<std::string>();

TEST_F(Sweep, FixtureIsPresentAndForThisUcdVersion) {
  ASSERT_FALSE(fixture().entries().empty())
      << "the sweep fixture " << fixture().path()
      << " is missing or empty. It is committed and cannot be absent: it is "
         "what make test checks the tables against.";
  EXPECT_EQ(fixture().version(), std::string(guni_ucd_version()));
}

TEST_F(Sweep, EveryPropertyOfEveryCodepointMatchesTheOracle) {
  for (const auto & name : *unknown_) {
    ADD_FAILURE() << "the oracle swept " << name
                  << " and this library does not know that property. A "
                     "property added to the UCD surfaces here rather than as "
                     "silence.";
  }
  size_t checked = 0;
  for (const auto & entry : fixture().entries()) {
    if (entry.first == "partition") {
      continue;
    }
    auto found = results_->find(entry.first);
    if (found == results_->end()) {
      continue;
    }
    EXPECT_EQ(found->second.runs, entry.second.runs)
        << entry.first << ": number of ranges of constant value";
    EXPECT_EQ(found->second.hash, entry.second.hash)
        << entry.first
        << ": the value at some codepoint differs from the oracle's. To "
           "localise:\n  tools/ucd/gen_sweep.py --property "
        << entry.first << " > /tmp/oracle\n  GUNI_SWEEP_DUMP=" << entry.first
        << " ./testSweep > /tmp/ours\n  diff /tmp/oracle /tmp/ours";
    ++checked;
  }
  /* The denominator, reported rather than assumed: a fixture that had lost
   * its lines would otherwise pass by comparing nothing. */
  EXPECT_GE(checked, static_cast<size_t>(95))
      << "only " << checked << " properties were compared";
}

/**
 * The partition is where the record changes, and it is a by-product of the
 * property sweeps: the union of every record-held property's boundaries. The
 * oracle computed the same union from the UCD.
 */
TEST_F(Sweep, ThePartitionMatchesTheOracle) {
  auto found = fixture().entries().find("partition");
  ASSERT_NE(found, fixture().entries().end());
  Fnv hash;
  size_t count = 0;
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    if ((*boundaries_)[cp] == 0) {
      continue;
    }
    char line[16];
    int length = std::snprintf(line, sizeof(line), "%06X\n", cp);
    hash.add(line, static_cast<size_t>(length));
    ++count;
  }
  EXPECT_EQ(count, found->second.runs);
  EXPECT_EQ(hash.value(), found->second.hash);
}

/**
 * The trie and the runs table, against each other, at every codepoint.
 *
 * design.md section 4.2 claims the point query and the set enumeration cannot
 * disagree because they are one source. This is the claim checked rather than
 * asserted: the runs table is what set.h walks and the trie is what char.h
 * reads, and here every codepoint's record is looked up both ways.
 */
TEST_F(Sweep, TheRunsTableDescribesTheTrieExactly) {
  ASSERT_EQ(static_cast<size_t>(GUNI_PROP_RUN_COUNT),
      static_cast<size_t>(guni_prop_run_first[GUNI_PROP_RUN_COUNT - 1] > 0
              ? GUNI_PROP_RUN_COUNT
              : GUNI_PROP_RUN_COUNT));
  size_t run = 0;
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    if (run + 1 < GUNI_PROP_RUN_COUNT && cp >= guni_prop_run_first[run + 1]) {
      ++run;
    }
    ASSERT_EQ(guni_record(cp), &guni_prop_records[guni_prop_run_record[run]])
        << "codepoint U+" << std::hex << cp << " is in run " << std::dec << run;
    /* And the runs are the *maximal* ones: a boundary with no change either
     * side is a table the generator did not compress, which would make the
     * range output depend on the table rather than on the data. */
    if (cp != 0 && cp == guni_prop_run_first[run]) {
      ASSERT_NE(guni_prop_run_record[run], guni_prop_run_record[run - 1])
          << "run " << run << " starts at U+" << std::hex << cp
          << " with the record the previous run had";
    }
  }
  EXPECT_EQ(run + 1, static_cast<size_t>(GUNI_PROP_RUN_COUNT));
}

} // namespace

int main(int argc, char ** argv) {
  const char * dump = std::getenv("GUNI_SWEEP_DUMP");
  if (dump != nullptr) {
    GUNI_Property id = static_cast<GUNI_Property>(0);
    bool known = guni_property_by_name(dump, std::strlen(dump), &id) == GUNI_OK;
    if (!known && std::strcmp(dump, "Numeric_Value") != 0) {
      std::fprintf(stderr, "no property named %s\n", dump);
      return 2;
    }
    std::string text;
    sweep_property(dump, id, &text, nullptr);
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
  }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
