#include <gtest/gtest.h>

#include "T4Prefs.h"

namespace {

TEST(T4PrefsCodec, RoundTrip) {
  const t4::T4Prefs in{1, 2};
  uint8_t buf[t4::T4_PREFS_FILE_SIZE];
  ASSERT_EQ(t4::encodeT4Prefs(in, buf, sizeof(buf)), t4::T4_PREFS_FILE_SIZE);
  EXPECT_EQ(buf[0], t4::T4_PREFS_VERSION);

  t4::T4Prefs out;
  ASSERT_TRUE(t4::decodeT4Prefs(buf, sizeof(buf), out));
  EXPECT_EQ(out.userMode, 1);
  EXPECT_EQ(out.lastLanguage, 2);
}

TEST(T4PrefsCodec, RejectsMissingOrShortBuffer) {
  t4::T4Prefs out{7, 7};
  EXPECT_FALSE(t4::decodeT4Prefs(nullptr, 0, out));

  const uint8_t shortBuf[t4::T4_PREFS_FILE_SIZE - 1] = {t4::T4_PREFS_VERSION, 1};
  EXPECT_FALSE(t4::decodeT4Prefs(shortBuf, sizeof(shortBuf), out));
  EXPECT_EQ(out.userMode, 7);  // untouched on failure
  EXPECT_EQ(out.lastLanguage, 7);
}

TEST(T4PrefsCodec, RejectsUnknownVersion) {
  const uint8_t buf[t4::T4_PREFS_FILE_SIZE] = {static_cast<uint8_t>(t4::T4_PREFS_VERSION + 1), 1, 1};
  t4::T4Prefs out;
  EXPECT_FALSE(t4::decodeT4Prefs(buf, sizeof(buf), out));
}

TEST(T4PrefsCodec, ClampsOutOfRangeValues) {
  const uint8_t buf[t4::T4_PREFS_FILE_SIZE] = {t4::T4_PREFS_VERSION, 9, 9};
  t4::T4Prefs out;
  ASSERT_TRUE(t4::decodeT4Prefs(buf, sizeof(buf), out));
  EXPECT_EQ(out.userMode, 0);
  EXPECT_EQ(out.lastLanguage, 0);
}

TEST(T4PrefsCodec, EncodeRejectsSmallBuffer) {
  uint8_t buf[t4::T4_PREFS_FILE_SIZE];
  EXPECT_EQ(t4::encodeT4Prefs({0, 0}, buf, t4::T4_PREFS_FILE_SIZE - 1), 0u);
}

}  // namespace
