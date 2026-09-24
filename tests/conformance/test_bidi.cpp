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
 * along with this message.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * UAX #9 against BidiTest.txt and BidiCharacterTest.txt.
 *
 * The two files test different things and both are needed. BidiTest.txt
 * enumerates **every sequence of bidirectional classes** up to four long -
 * about half a million cases with the three paragraph directions - so it is
 * exhaustive over the rules and says nothing about real characters.
 * BidiCharacterTest.txt is the other way round: real codepoints, which is
 * what exercises the paired-bracket rule N0, because a bracket is a
 * particular character and not a class.
 *
 * Both are committed under tests/data/ucd/<version>/, so neither can skip.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/unicode/unicode.h>

#include "test_helpers.h"

namespace {

/**
 * A codepoint for each bidirectional class, found in the data rather than
 * written down.
 *
 * Paired brackets are skipped deliberately: BidiTest.txt's cases are about
 * classes, and picking U+0028 for ON would drag rule N0 into every one of
 * them and disagree with the file for a reason that is not a defect.
 */
class Representatives {
public:
  Representatives() {
    for (uint32_t cp = 1; cp < 0x3000 && missing() != 0; ++cp) {
      GUNI_BidiClass class_ = guni_bidi_class(cp);
      if (guni_bidi_paired_bracket(cp, nullptr) != 0) {
        continue;
      }
      if (by_class_[class_] == 0) {
        by_class_[class_] = cp;
      }
    }
  }

  uint32_t of(GUNI_BidiClass class_) const { return by_class_[class_]; }

  size_t missing() const {
    size_t count = 0;
    for (uint32_t cp : by_class_) {
      if (cp == 0) {
        ++count;
      }
    }
    return count;
  }

private:
  uint32_t by_class_[GUNI_BIDI_COUNT] = {0};
};

const Representatives & representatives() {
  static const Representatives instance;
  return instance;
}

std::vector<std::string> split(const std::string & text, char separator) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    size_t at = text.find(separator, start);
    out.push_back(text.substr(start, at == std::string::npos ? at : at - start));
    if (at == std::string::npos) {
      break;
    }
    start = at + 1;
  }
  return out;
}

std::vector<std::string> tokens(const std::string & text) {
  std::vector<std::string> out;
  size_t index = 0;
  while (index < text.size()) {
    while (index < text.size() && std::isspace((unsigned char)text[index])) {
      ++index;
    }
    size_t start = index;
    while (index < text.size() && !std::isspace((unsigned char)text[index])) {
      ++index;
    }
    if (index > start) {
      out.push_back(text.substr(start, index - start));
    }
  }
  return out;
}

/** Resolve, with the allocator variant for anything long. */
GUNI_Result levels_of(const std::vector<uint32_t> & text,
    GUNI_BidiDirection direction, std::vector<uint8_t> * levels,
    uint8_t * paragraph_level) {
  levels->assign(text.size(), 0);
  if (text.size() <= GUNI_BIDI_MAX_STACK_LENGTH) {
    return guni_bidi_levels(text.data(), text.size(), direction, nullptr,
        levels->data(), levels->size(), paragraph_level);
  }
  return guni_bidi_levels_with_allocator(text.data(), text.size(), direction,
      nullptr, levels->data(), levels->size(), paragraph_level, nullptr);
}

/** The visual order, with the positions the file does not check removed. */
std::vector<size_t> visual_order(const std::vector<uint8_t> & levels,
    const std::vector<bool> & checked) {
  std::vector<size_t> order(levels.size());
  size_t written = 0;
  if (guni_bidi_reorder(levels.data(), levels.size(), order.data(),
          order.size())
      != GUNI_OK) {
    return {};
  }
  (void)written;
  std::vector<size_t> out;
  for (size_t index : order) {
    if (index < checked.size() && checked[index]) {
      out.push_back(index);
    }
  }
  return out;
}

std::string describe(const std::vector<uint8_t> & levels) {
  std::string out;
  for (uint8_t level : levels) {
    out += std::to_string((unsigned)level) + " ";
  }
  return out;
}

TEST(Bidi, EveryClassHasARepresentativeCodepoint) {
  /* If a class had none, every case using it would silently test something
   * else. The denominator, before the tests that depend on it. */
  ASSERT_EQ(representatives().missing(), static_cast<size_t>(0));
}

TEST(Bidi, BidiTestFileIsExhaustiveOverClassSequences) {
  const std::string path = gunitest::data(std::string("ucd/") + guni_ucd_version()
      + "/BidiTest.txt");
  FILE * handle = std::fopen(path.c_str(), "rb");
  ASSERT_NE(handle, nullptr) << path << " is committed and cannot be absent";

  std::vector<std::string> expected_levels;
  std::vector<std::string> expected_order;
  size_t cases = 0;
  size_t failures = 0;
  char buffer[4096];
  int line_number = 0;
  while (std::fgets(buffer, sizeof(buffer), handle) != nullptr) {
    ++line_number;
    std::string line(buffer);
    size_t hash = line.find('#');
    if (hash != std::string::npos) {
      line.erase(hash);
    }
    while (!line.empty() && std::isspace((unsigned char)line.back())) {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    if (line.compare(0, 8, "@Levels:") == 0) {
      expected_levels = tokens(line.substr(8));
      continue;
    }
    if (line.compare(0, 9, "@Reorder:") == 0) {
      expected_order = tokens(line.substr(9));
      continue;
    }
    std::vector<std::string> fields = split(line, ';');
    if (fields.size() < 2) {
      continue;
    }
    std::vector<std::string> classes = tokens(fields[0]);
    unsigned long bitset = std::strtoul(fields[1].c_str(), nullptr, 10);
    if (classes.size() != expected_levels.size()) {
      continue;
    }

    std::vector<uint32_t> text;
    text.reserve(classes.size());
    bool usable = true;
    for (const std::string & name : classes) {
      GUNI_Property property = GUNI_PROPERTY_BIDI_CLASS;
      uint32_t value = 0;
      if (guni_value_by_name(property, name.data(), name.size(), &value)
          != GUNI_OK) {
        usable = false;
        break;
      }
      uint32_t cp = representatives().of(static_cast<GUNI_BidiClass>(value));
      if (cp == 0) {
        usable = false;
        break;
      }
      text.push_back(cp);
    }
    ASSERT_TRUE(usable) << "line " << line_number << ": unknown class in "
                        << fields[0];

    /* Bit 1 is auto, bit 2 is LTR, bit 4 is RTL, per the file's header. */
    const struct {
      unsigned long bit;
      GUNI_BidiDirection direction;
    } modes[] = {
      {1, GUNI_BIDI_AUTO}, {2, GUNI_BIDI_LTR}, {4, GUNI_BIDI_RTL}};
    for (const auto & mode : modes) {
      if ((bitset & mode.bit) == 0) {
        continue;
      }
      std::vector<uint8_t> levels;
      uint8_t paragraph_level = 0;
      ASSERT_EQ(levels_of(text, mode.direction, &levels, &paragraph_level),
          GUNI_OK);
      std::vector<bool> checked(levels.size(), true);
      bool ok = true;
      for (size_t index = 0; index < expected_levels.size(); ++index) {
        if (expected_levels[index] == "x") {
          checked[index] = false;
          continue;
        }
        unsigned long want = std::strtoul(expected_levels[index].c_str(),
            nullptr, 10);
        if (levels[index] != want) {
          ok = false;
        }
      }
      if (ok) {
        std::vector<size_t> order = visual_order(levels, checked);
        if (order.size() != expected_order.size()) {
          ok = false;
        }
        else {
          for (size_t index = 0; index < order.size(); ++index) {
            if (order[index]
                != std::strtoul(expected_order[index].c_str(), nullptr, 10)) {
              ok = false;
            }
          }
        }
      }
      if (!ok) {
        if (failures < 10) {
          std::string want;
          for (const std::string & level : expected_levels) {
            want += level + " ";
          }
          std::string want_order;
          for (const std::string & index : expected_order) {
            want_order += index + " ";
          }
          std::string got_order;
          for (size_t index : visual_order(levels, checked)) {
            got_order += std::to_string(index) + " ";
          }
          ADD_FAILURE() << "BidiTest.txt line " << line_number << " ("
                        << fields[0] << ") paragraph direction "
                        << (mode.bit == 1 ? "auto" : (mode.bit == 2 ? "LTR" : "RTL"))
                        << "\n  levels   got " << describe(levels)
                        << "\n  levels  want " << want
                        << "\n  reorder  got " << got_order
                        << "\n  reorder want " << want_order;
        }
        ++failures;
      }
      ++cases;
    }
  }
  std::fclose(handle);
  EXPECT_EQ(failures, static_cast<size_t>(0)) << failures << " of " << cases
                                             << " cases failed";
  /* The denominator: the file has about half a million cases, and a parser
   * that read none of them would otherwise pass. */
  EXPECT_GT(cases, static_cast<size_t>(400000)) << "only " << cases
                                               << " cases were run";
}

TEST(Bidi, BidiCharacterTestCoversRealCodepointsAndBrackets) {
  const std::string path = gunitest::data(std::string("ucd/") + guni_ucd_version()
      + "/BidiCharacterTest.txt");
  FILE * handle = std::fopen(path.c_str(), "rb");
  ASSERT_NE(handle, nullptr) << path << " is committed and cannot be absent";

  size_t cases = 0;
  size_t failures = 0;
  size_t with_brackets = 0;
  char buffer[65536];
  int line_number = 0;
  while (std::fgets(buffer, sizeof(buffer), handle) != nullptr) {
    ++line_number;
    std::string line(buffer);
    size_t hash = line.find('#');
    if (hash != std::string::npos) {
      line.erase(hash);
    }
    while (!line.empty() && std::isspace((unsigned char)line.back())) {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    std::vector<std::string> fields = split(line, ';');
    if (fields.size() < 5) {
      continue;
    }
    std::vector<uint32_t> text;
    for (const std::string & token : tokens(fields[0])) {
      text.push_back(
          static_cast<uint32_t>(std::strtoul(token.c_str(), nullptr, 16)));
    }
    unsigned long mode = std::strtoul(fields[1].c_str(), nullptr, 10);
    unsigned long want_paragraph = std::strtoul(fields[2].c_str(), nullptr, 10);
    std::vector<std::string> want_levels = tokens(fields[3]);
    std::vector<std::string> want_order = tokens(fields[4]);
    if (text.empty() || want_levels.size() != text.size()) {
      continue;
    }
    for (uint32_t cp : text) {
      if (guni_bidi_paired_bracket(cp, nullptr) != 0) {
        ++with_brackets;
        break;
      }
    }

    GUNI_BidiDirection direction = (mode == 0)
        ? GUNI_BIDI_LTR
        : ((mode == 1) ? GUNI_BIDI_RTL : GUNI_BIDI_AUTO);
    std::vector<uint8_t> levels;
    uint8_t paragraph_level = 0;
    ASSERT_EQ(levels_of(text, direction, &levels, &paragraph_level), GUNI_OK);

    bool ok = paragraph_level == want_paragraph;
    std::vector<bool> checked(levels.size(), true);
    for (size_t index = 0; index < want_levels.size(); ++index) {
      if (want_levels[index] == "x") {
        checked[index] = false;
        continue;
      }
      if (levels[index]
          != std::strtoul(want_levels[index].c_str(), nullptr, 10)) {
        ok = false;
      }
    }
    if (ok) {
      std::vector<size_t> order = visual_order(levels, checked);
      if (order.size() != want_order.size()) {
        ok = false;
      }
      else {
        for (size_t index = 0; index < order.size(); ++index) {
          if (order[index]
              != std::strtoul(want_order[index].c_str(), nullptr, 10)) {
            ok = false;
          }
        }
      }
    }
    if (!ok && failures < 10) {
      ADD_FAILURE() << "BidiCharacterTest.txt line " << line_number
                    << "\n  paragraph level got " << (unsigned)paragraph_level
                    << " expected " << want_paragraph << "\n  levels got      "
                    << describe(levels) << "\n  levels expected " << fields[3];
    }
    if (!ok) {
      ++failures;
    }
    ++cases;
  }
  std::fclose(handle);
  EXPECT_EQ(failures, static_cast<size_t>(0)) << failures << " of " << cases
                                             << " cases failed";
  EXPECT_GT(cases, static_cast<size_t>(90000)) << "only " << cases << " cases";
  /* The bracket cases are the reason this file exists beside the other one. */
  EXPECT_GT(with_brackets, static_cast<size_t>(1000))
      << "only " << with_brackets << " cases contained a paired bracket";
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
