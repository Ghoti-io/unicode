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
 * UAX #15 against NormalizationTest.txt, the Consortium's own file.
 *
 * The file is committed under tests/data/ucd/<version>/, so this gate cannot
 * skip (design.md section 2, M4). It states its own conformance requirements
 * and both are checked here: the twelve equalities per line, and - the one
 * implementations forget - that **every codepoint the file does not list is
 * unchanged by all four forms**. That second requirement is what catches a
 * decomposition table with an entry it should not have, which no amount of
 * per-line checking can see.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

#include "test_helpers.h"

namespace {

using Codepoints = std::vector<uint32_t>;

struct Row {
  int part = 0;
  int line = 0;
  Codepoints column[5]; ///< source, NFC, NFD, NFKC, NFKD
};

Codepoints parse_codepoints(const std::string & field) {
  Codepoints out;
  size_t index = 0;
  while (index < field.size()) {
    while (index < field.size() && std::isspace((unsigned char)field[index])) {
      ++index;
    }
    size_t start = index;
    while (index < field.size() && std::isxdigit((unsigned char)field[index])) {
      ++index;
    }
    if (index == start) {
      break;
    }
    out.push_back(static_cast<uint32_t>(
        std::strtoul(field.substr(start, index - start).c_str(), nullptr, 16)));
  }
  return out;
}

/** The file, parsed. Loaded once; it is 2.8 MB and 19,000 lines. */
class Suite {
public:
  Suite() {
    path_ = gunitest::data(std::string("ucd/") + guni_ucd_version()
        + "/NormalizationTest.txt");
    FILE * handle = std::fopen(path_.c_str(), "rb");
    if (handle == nullptr) {
      return;
    }
    char buffer[4096];
    int part = 0;
    int number = 0;
    while (std::fgets(buffer, sizeof(buffer), handle) != nullptr) {
      ++number;
      std::string line(buffer);
      if (line.compare(0, 6, "@Part0") == 0) {
        part = 0;
      }
      else if (line.compare(0, 6, "@Part1") == 0) {
        part = 1;
      }
      else if (line.compare(0, 6, "@Part2") == 0) {
        part = 2;
      }
      else if (line.compare(0, 6, "@Part3") == 0) {
        part = 3;
      }
      size_t hash = line.find('#');
      if (hash != std::string::npos) {
        line.erase(hash);
      }
      if (line.empty() || line[0] == '@') {
        continue;
      }
      Row row;
      row.part = part;
      row.line = number;
      size_t start = 0;
      int column = 0;
      while (column < 5) {
        size_t semicolon = line.find(';', start);
        if (semicolon == std::string::npos) {
          break;
        }
        row.column[column] = parse_codepoints(line.substr(start, semicolon - start));
        start = semicolon + 1;
        ++column;
      }
      if (column != 5 || row.column[0].empty()) {
        continue;
      }
      rows_.push_back(row);
      if (part == 1) {
        listed_.insert(row.column[0][0]);
      }
    }
    std::fclose(handle);
  }

  const std::vector<Row> & rows() const { return rows_; }
  const std::set<uint32_t> & listed() const { return listed_; }
  const std::string & path() const { return path_; }

private:
  std::vector<Row> rows_;
  std::set<uint32_t> listed_;
  std::string path_;
};

const Suite & suite() {
  static const Suite instance;
  return instance;
}

/** Normalise, sizing the buffer from the generated expansion bound. */
Codepoints normalize(GUNI_NormForm form, const Codepoints & input) {
  std::vector<uint32_t> out(input.size() * GUNI_NORM_MAX_EXPANSION_NFKD + 1);
  size_t written = 0;
  GUNI_Result result = guni_normalize(form, input.data(), input.size(), nullptr,
      out.data(), out.size(), &written);
  EXPECT_EQ(result, GUNI_OK);
  out.resize(written);
  return out;
}

std::string to_utf8(const Codepoints & input) {
  std::string out(input.size() * GUNI_UTF8_MAX_LENGTH + 1, '\0');
  size_t written = 0;
  EXPECT_EQ(guni_utf8_from_codepoints(input.data(), input.size(),
                GUNI_INVALID_REFUSE, out.data(), out.size(), &written),
      GUNI_OK);
  out.resize(written);
  return out;
}

std::string normalize_utf8(GUNI_NormForm form, const std::string & input) {
  std::string out(input.size() * GUNI_NORM_MAX_EXPANSION_NFKD + 4, '\0');
  size_t written = 0;
  GUNI_Result result = guni_normalize_utf8(form, input.data(), input.size(),
      GUNI_INVALID_REFUSE, nullptr, out.data(), out.size(), &written);
  EXPECT_EQ(result, GUNI_OK);
  out.resize(written);
  return out;
}

std::string describe(const Codepoints & input) {
  std::string out;
  char buffer[16];
  for (uint32_t cp : input) {
    std::snprintf(buffer, sizeof(buffer), "%04X ", cp);
    out += buffer;
  }
  return out;
}

TEST(Normalization, TheFileIsPresent) {
  ASSERT_FALSE(suite().rows().empty())
      << "NormalizationTest.txt is committed under tests/data and cannot be "
         "absent: " << suite().path();
  /* The denominator, so that a truncated file cannot read as a pass. */
  EXPECT_GT(suite().rows().size(), static_cast<size_t>(18000));
  EXPECT_GT(suite().listed().size(), static_cast<size_t>(2000));
}

TEST(Normalization, EveryLineSatisfiesTheFilesInvariants) {
  /* The file's own CONFORMANCE section 1: twelve equalities per line, which
   * between them pin all four forms on both the composed and the decomposed
   * spelling of the same text. */
  size_t checked = 0;
  for (const Row & row : suite().rows()) {
    const Codepoints & source = row.column[0];
    const Codepoints & nfc = row.column[1];
    const Codepoints & nfd = row.column[2];
    const Codepoints & nfkc = row.column[3];
    const Codepoints & nfkd = row.column[4];

    for (int column = 0; column < 3; ++column) {
      EXPECT_EQ(normalize(GUNI_NFC, row.column[column]), nfc)
          << "line " << row.line << " column " << column + 1 << ": NFC of "
          << describe(row.column[column]);
      EXPECT_EQ(normalize(GUNI_NFD, row.column[column]), nfd)
          << "line " << row.line << " column " << column + 1 << ": NFD of "
          << describe(row.column[column]);
    }
    for (int column = 3; column < 5; ++column) {
      EXPECT_EQ(normalize(GUNI_NFC, row.column[column]), nfkc)
          << "line " << row.line << " column " << column + 1;
      EXPECT_EQ(normalize(GUNI_NFD, row.column[column]), nfkd)
          << "line " << row.line << " column " << column + 1;
    }
    for (int column = 0; column < 5; ++column) {
      EXPECT_EQ(normalize(GUNI_NFKC, row.column[column]), nfkc)
          << "line " << row.line << " column " << column + 1 << ": NFKC of "
          << describe(row.column[column]);
      EXPECT_EQ(normalize(GUNI_NFKD, row.column[column]), nfkd)
          << "line " << row.line << " column " << column + 1 << ": NFKD of "
          << describe(row.column[column]);
    }
    (void)source;
    ++checked;
  }
  EXPECT_EQ(checked, suite().rows().size());
}

TEST(Normalization, TheUtf8EntryPointAgreesWithTheCodepointOne) {
  /* Two entry points over one algorithm, so a difference between them is a
   * defect in the chunking that only the UTF-8 path has. */
  size_t checked = 0;
  for (const Row & row : suite().rows()) {
    for (int column = 0; column < 5; ++column) {
      const std::string source = to_utf8(row.column[column]);
      for (int form = 0; form < GUNI_NORM_FORM_COUNT; ++form) {
        GUNI_NormForm which = static_cast<GUNI_NormForm>(form);
        EXPECT_EQ(normalize_utf8(which, source),
            to_utf8(normalize(which, row.column[column])))
            << "line " << row.line << " column " << column + 1 << " form "
            << form;
      }
      ++checked;
    }
  }
  EXPECT_GT(checked, static_cast<size_t>(90000));
}

TEST(Normalization, EveryCodepointTheFileDoesNotListIsUnchanged) {
  /* The file's CONFORMANCE section 2, and the half that implementations skip.
   * Part 1 lists every codepoint with a decomposition; for every other
   * assigned codepoint all four forms are the identity. A table with an entry
   * it should not have fails here and nowhere else. */
  size_t checked = 0;
  size_t skipped = 0;
  for (uint32_t cp = 0; cp < GUNI_CODEPOINT_COUNT; ++cp) {
    if (suite().listed().count(cp) != 0) {
      ++skipped;
      continue;
    }
    if (guni_is_surrogate(cp)) {
      /* A surrogate is not text; the file's requirement is about characters,
       * and no UTF-8 encodes one. It still has to be the identity, which is
       * checked through the codepoint entry point. */
    }
    uint32_t single = cp;
    for (int form = 0; form < GUNI_NORM_FORM_COUNT; ++form) {
      uint32_t out[GUNI_NORM_MAX_EXPANSION_NFKD];
      size_t written = 0;
      ASSERT_EQ(guni_normalize(static_cast<GUNI_NormForm>(form), &single, 1,
                    nullptr, out, sizeof(out) / sizeof(*out), &written),
          GUNI_OK);
      ASSERT_EQ(written, static_cast<size_t>(1))
          << "U+" << std::hex << cp << " form " << std::dec << form
          << " is not listed in Part 1 and is not unchanged";
      ASSERT_EQ(out[0], cp) << "U+" << std::hex << cp << " form " << std::dec
                            << form;
    }
    ++checked;
  }
  EXPECT_GT(checked, static_cast<size_t>(1000000));
  EXPECT_EQ(checked + skipped, GUNI_CODEPOINT_COUNT);
}

TEST(Normalization, IdempotenceAndTheCompositionsOfForms) {
  /* UAX #15 section 7's guarantees, over the file's own text: normalising
   * twice is normalising once, and NFC(NFD(s)) is NFC(s). These hold for
   * reasons independent of the tables, so they catch an algorithm that is
   * right on the vectors and wrong on the composition of two of them. */
  size_t checked = 0;
  for (const Row & row : suite().rows()) {
    const Codepoints & source = row.column[0];
    for (int form = 0; form < GUNI_NORM_FORM_COUNT; ++form) {
      GUNI_NormForm which = static_cast<GUNI_NormForm>(form);
      Codepoints once = normalize(which, source);
      EXPECT_EQ(normalize(which, once), once) << "line " << row.line;
      bool normalised = false;
      ASSERT_EQ(guni_is_normalized(which, once.data(), once.size(), nullptr,
                    &normalised),
          GUNI_OK);
      EXPECT_TRUE(normalised)
          << "line " << row.line << " form " << form
          << ": its own output is not reported as normalised";
    }
    EXPECT_EQ(normalize(GUNI_NFC, normalize(GUNI_NFD, source)),
        normalize(GUNI_NFC, source))
        << "line " << row.line;
    EXPECT_EQ(normalize(GUNI_NFKC, normalize(GUNI_NFD, source)),
        normalize(GUNI_NFKC, source))
        << "line " << row.line;
    EXPECT_EQ(normalize(GUNI_NFKD, normalize(GUNI_NFKC, source)),
        normalize(GUNI_NFKD, source))
        << "line " << row.line;
    ++checked;
  }
  EXPECT_EQ(checked, suite().rows().size());
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
