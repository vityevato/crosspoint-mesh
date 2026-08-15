#include <gtest/gtest.h>

#include <cstdint>

#include "MockChannelInject.h"

// Verifies the cyclic slot selection for mock channel injection
// (MeshCore hotkey 3): first free slot after JSON-defined channels,
// cycling within [base, maxChannels).

TEST(MockChannelInject, StartsAtFirstFreeSlot) {
  uint8_t seq = 0;
  uint8_t idx = 0xFF;
  EXPECT_TRUE(nextMockChannelSlot(2, 8, seq, idx));
  EXPECT_EQ(idx, 2);
  EXPECT_EQ(seq, 1);
}

TEST(MockChannelInject, FillsAllFreeSlotsInOrder) {
  uint8_t seq = 0;
  uint8_t idx = 0;
  for (uint8_t expected = 2; expected < 8; ++expected) {
    ASSERT_TRUE(nextMockChannelSlot(2, 8, seq, idx));
    EXPECT_EQ(idx, expected);
  }
  EXPECT_EQ(seq, 6);
}

TEST(MockChannelInject, WrapsToFirstFreeSlotAfterLast) {
  uint8_t seq = 0;
  uint8_t idx = 0;
  for (int i = 0; i < 6; ++i) {
    ASSERT_TRUE(nextMockChannelSlot(2, 8, seq, idx));
  }
  EXPECT_EQ(idx, 7);  // Last free slot

  ASSERT_TRUE(nextMockChannelSlot(2, 8, seq, idx));
  EXPECT_EQ(idx, 2);  // Wraps to first free slot

  ASSERT_TRUE(nextMockChannelSlot(2, 8, seq, idx));
  EXPECT_EQ(idx, 3);
}

TEST(MockChannelInject, BaseZeroCyclesFullRange) {
  uint8_t seq = 0;
  uint8_t idx = 0;
  for (int round = 0; round < 2; ++round) {
    for (uint8_t expected = 0; expected < 8; ++expected) {
      ASSERT_TRUE(nextMockChannelSlot(0, 8, seq, idx));
      EXPECT_EQ(idx, expected);
    }
  }
}

TEST(MockChannelInject, NoFreeSlotsReturnsFalseAndLeavesState) {
  uint8_t seq = 7;
  uint8_t idx = 0xAB;
  EXPECT_FALSE(nextMockChannelSlot(8, 8, seq, idx));
  EXPECT_EQ(seq, 7);
  EXPECT_EQ(idx, 0xAB);

  EXPECT_FALSE(nextMockChannelSlot(9, 8, seq, idx));
  EXPECT_EQ(seq, 7);
}

TEST(MockChannelInject, SingleSlotCyclesToItself) {
  uint8_t seq = 0;
  uint8_t idx = 0;
  ASSERT_TRUE(nextMockChannelSlot(0, 1, seq, idx));
  EXPECT_EQ(idx, 0);
  ASSERT_TRUE(nextMockChannelSlot(0, 1, seq, idx));
  EXPECT_EQ(idx, 0);
}

TEST(MockChannelInject, SeqOverflowAtUint8Boundary) {
  uint8_t seq = 255;
  uint8_t idx = 0;
  ASSERT_TRUE(nextMockChannelSlot(2, 8, seq, idx));
  // 255 % 6 == 3 → base 2 + 3 = 5; seq wraps to 0.
  EXPECT_EQ(idx, 5);
  EXPECT_EQ(seq, 0);

  ASSERT_TRUE(nextMockChannelSlot(2, 8, seq, idx));
  EXPECT_EQ(idx, 2);
}
