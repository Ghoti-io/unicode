/**
 * @file
 *
 * The core module: the result vocabulary, the limits, and the version.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <cstring>
#include <set>
#include <string>

#include <ghoti.io/unicode/libver.h>

// CONVENTIONS.md section 5: GUNI_RESULT_COUNT closes the enum so that a test
// can check the string table is complete. A new result that arrives without a
// string falls into the default arm and is caught here.
TEST(Result, EveryCodeHasItsOwnString) {
  std::set<std::string> seen;
  for (int i = 0; i < GUNI_RESULT_COUNT; i++) {
    const char * s = guni_result_string(static_cast<GUNI_Result>(i));
    ASSERT_NE(s, nullptr) << "result " << i;
    EXPECT_STRNE(s, "Unknown error") << "result " << i << " has no string";
    EXPECT_TRUE(seen.insert(s).second) << "result " << i << " shares a string";
  }
}

TEST(Result, OutOfRangeIsUnknownNotUndefined) {
  EXPECT_STREQ(guni_result_string(GUNI_RESULT_COUNT), "Unknown error");
  EXPECT_STREQ(guni_result_string(static_cast<GUNI_Result>(-1)), "Unknown error");
}

TEST(Result, ZeroIsSuccess) {
  EXPECT_EQ(GUNI_OK, 0);
  EXPECT_STREQ(guni_result_string(GUNI_OK), "No error");
}

TEST(Limits, DefaultsAreTheDocumentedOnes) {
  GUNI_Limits limits;
  std::memset(&limits, 0xA5, sizeof limits);
  guni_limits_default(&limits);
  // documentation/design.md section 13.2.
  EXPECT_EQ(limits.max_text_bytes, (size_t)64 * 1024 * 1024);
  EXPECT_EQ(limits.max_bidi_depth, 125u) << "UAX #9's max_depth";
  EXPECT_EQ(limits.max_nonstarters, 30u) << "UAX #15 section 13";
}

TEST(Limits, NullIsIgnored) {
  guni_limits_default(nullptr);
}

// The linked library's version is the one that matters when it differs from
// the header's; here they are the same build, so they must agree.
TEST(Version, LibraryAgreesWithHeader) {
  EXPECT_STREQ(guni_version_string(), GUNI_VERSION_STRING);
  EXPECT_EQ(guni_version_number(), GUNI_VERSION_NUMBER);
  EXPECT_EQ(guni_version_number(),
      GUNI_MAKE_VERSION(GUNI_VERSION_MAJOR, GUNI_VERSION_MINOR, GUNI_VERSION_PATCH));
}

TEST(Version, PackingIsOneBytePerComponent) {
  // libcurl's LIBCURL_VERSION_NUM layout: 1.2.3 reads as 0x010203, so a
  // plain < compares two versions correctly. CONVENTIONS.md section 4.
  EXPECT_EQ(GUNI_MAKE_VERSION(1, 2, 3), 0x010203u);
  EXPECT_LT(GUNI_MAKE_VERSION(1, 9, 9), GUNI_MAKE_VERSION(2, 0, 0));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
