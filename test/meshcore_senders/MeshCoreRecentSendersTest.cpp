#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MeshCoreRecentSenders.h"

namespace {

using List = char[MESHCORE_MAX_RECENT_SENDERS][64];

void fill(List names, const char* const* src, uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    snprintf(names[i], 64, "%s", src[i]);
  }
}

int indexOf(const List names, uint8_t count, const char* name) {
  for (uint8_t i = 0; i < count; ++i) {
    if (strcmp(names[i], name) == 0) return i;
  }
  return -1;
}

}  // namespace

TEST(MeshCoreRecentSenders, NoteAddsNewFront) {
  List names = {};
  uint8_t count = 0;

  count = meshcore::recentSendersNote(names, count, MESHCORE_MAX_RECENT_SENDERS, "Alice");
  ASSERT_EQ(count, 1u);
  EXPECT_STREQ(names[0], "Alice");

  count = meshcore::recentSendersNote(names, count, MESHCORE_MAX_RECENT_SENDERS, "Bob");
  ASSERT_EQ(count, 2u);
  EXPECT_STREQ(names[0], "Bob");
  EXPECT_STREQ(names[1], "Alice");
}

TEST(MeshCoreRecentSenders, NoteMovesExistingToFront) {
  List names = {};
  const char* src[] = {"A", "B", "C"};
  uint8_t count = 3;
  fill(names, src, count);

  count = meshcore::recentSendersNote(names, count, MESHCORE_MAX_RECENT_SENDERS, "B");
  ASSERT_EQ(count, 3u);
  EXPECT_STREQ(names[0], "B");
  EXPECT_STREQ(names[1], "A");
  EXPECT_STREQ(names[2], "C");
  EXPECT_EQ(indexOf(names, count, "B"), 0);
}

TEST(MeshCoreRecentSenders, NoteMovesTailToFront) {
  List names = {};
  const char* src[] = {"A", "B", "C"};
  uint8_t count = 3;
  fill(names, src, count);

  count = meshcore::recentSendersNote(names, count, MESHCORE_MAX_RECENT_SENDERS, "C");
  ASSERT_EQ(count, 3u);
  EXPECT_STREQ(names[0], "C");
  EXPECT_STREQ(names[1], "A");
  EXPECT_STREQ(names[2], "B");
}

TEST(MeshCoreRecentSenders, NoteAlreadyFrontIsNoOp) {
  List names = {};
  const char* src[] = {"A", "B"};
  uint8_t count = 2;
  fill(names, src, count);

  count = meshcore::recentSendersNote(names, count, MESHCORE_MAX_RECENT_SENDERS, "A");
  ASSERT_EQ(count, 2u);
  EXPECT_STREQ(names[0], "A");
  EXPECT_STREQ(names[1], "B");
}

TEST(MeshCoreRecentSenders, NoteCapsAtMax) {
  List names = {};
  const char* src[] = {"S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7"};
  uint8_t count = 8;
  fill(names, src, count);

  count = meshcore::recentSendersNote(names, count, MESHCORE_MAX_RECENT_SENDERS, "S8");
  ASSERT_EQ(count, MESHCORE_MAX_RECENT_SENDERS);
  EXPECT_STREQ(names[0], "S8");
  EXPECT_EQ(indexOf(names, count, "S8"), 0);
  // Only the capped tail falls off; the rest shift right by one.
  EXPECT_STREQ(names[1], "S0");
  EXPECT_EQ(indexOf(names, count, "S7"), -1);
}

TEST(MeshCoreRecentSenders, NoteDuplicateWhenFullKeepsCount) {
  List names = {};
  const char* src[] = {"S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7"};
  uint8_t count = 8;
  fill(names, src, count);

  count = meshcore::recentSendersNote(names, count, MESHCORE_MAX_RECENT_SENDERS, "S5");
  ASSERT_EQ(count, MESHCORE_MAX_RECENT_SENDERS);
  EXPECT_STREQ(names[0], "S5");
  EXPECT_EQ(indexOf(names, count, "S5"), 0);
  // The previous occurrence was removed, so nothing falls off the tail.
  EXPECT_STREQ(names[7], "S7");
}

TEST(MeshCoreRecentSenders, NoteIgnoresEmptyName) {
  List names = {};
  const char* src[] = {"A"};
  uint8_t count = 1;
  fill(names, src, count);

  count = meshcore::recentSendersNote(names, count, MESHCORE_MAX_RECENT_SENDERS, "");
  ASSERT_EQ(count, 1u);
  EXPECT_STREQ(names[0], "A");
}

TEST(MeshCoreRecentSenders, NoteHonorsSmallMax) {
  List names = {};
  uint8_t count = 0;

  count = meshcore::recentSendersNote(names, count, 1, "A");
  ASSERT_EQ(count, 1u);
  count = meshcore::recentSendersNote(names, count, 1, "B");
  ASSERT_EQ(count, 1u);
  EXPECT_STREQ(names[0], "B");
}

TEST(MeshCoreRecentSenders, AppendDedupes) {
  List names = {};
  uint8_t count = 0;

  count = meshcore::recentSendersAppend(names, count, MESHCORE_MAX_RECENT_SENDERS, "A");
  count = meshcore::recentSendersAppend(names, count, MESHCORE_MAX_RECENT_SENDERS, "B");
  count = meshcore::recentSendersAppend(names, count, MESHCORE_MAX_RECENT_SENDERS, "A");
  ASSERT_EQ(count, 2u);
  EXPECT_STREQ(names[0], "A");
  EXPECT_STREQ(names[1], "B");
}

TEST(MeshCoreRecentSenders, AppendCapsAtMax) {
  List names = {};
  uint8_t count = 0;

  for (int i = 0; i < MESHCORE_MAX_RECENT_SENDERS + 2; ++i) {
    char name[8];
    snprintf(name, sizeof(name), "S%d", i);
    count = meshcore::recentSendersAppend(names, count, MESHCORE_MAX_RECENT_SENDERS, name);
  }
  ASSERT_EQ(count, MESHCORE_MAX_RECENT_SENDERS);
  EXPECT_STREQ(names[0], "S0");
  EXPECT_STREQ(names[MESHCORE_MAX_RECENT_SENDERS - 1], "S7");
}

TEST(MeshCoreRecentSenders, AppendIgnoresEmptyName) {
  List names = {};
  uint8_t count = meshcore::recentSendersAppend(names, 0, MESHCORE_MAX_RECENT_SENDERS, "");
  ASSERT_EQ(count, 0u);
}
