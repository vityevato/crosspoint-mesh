#include <gtest/gtest.h>

#include <cstdint>

#include "MeshCorePath.h"

// Decoding of the MeshCore path byte: bits 0-5 = hop count, bits 6-7 = hash
// size - 1. Regression coverage for the "65 hops" display bug: 0x41 (65) is
// 2-byte hashes + 1 hop, not 65 hops.

TEST(MeshPath, HopCountExtractsLowSixBits) {
  EXPECT_EQ(MeshPath::hopCount(0x00), 0);
  EXPECT_EQ(MeshPath::hopCount(0x01), 1);
  EXPECT_EQ(MeshPath::hopCount(0x3F), 63);
  EXPECT_EQ(MeshPath::hopCount(0x40), 0);
  EXPECT_EQ(MeshPath::hopCount(0x41), 1);
  EXPECT_EQ(MeshPath::hopCount(0x7F), 63);
  EXPECT_EQ(MeshPath::hopCount(0x80), 0);
  EXPECT_EQ(MeshPath::hopCount(0x81), 1);
  EXPECT_EQ(MeshPath::hopCount(0xC1), 1);
}

TEST(MeshPath, HashSizeExtractsTopTwoBitsPlusOne) {
  EXPECT_EQ(MeshPath::hashSize(0x00), 1);
  EXPECT_EQ(MeshPath::hashSize(0x3F), 1);
  EXPECT_EQ(MeshPath::hashSize(0x40), 2);
  EXPECT_EQ(MeshPath::hashSize(0x41), 2);
  EXPECT_EQ(MeshPath::hashSize(0x80), 3);
  EXPECT_EQ(MeshPath::hashSize(0x81), 3);
  EXPECT_EQ(MeshPath::hashSize(0xC0), 4);
  EXPECT_EQ(MeshPath::hashSize(0xFF), 4);
}

TEST(MeshPath, SixtyFiveIsOneHopWithTwoByteHashes) {
  // The exact value from the field report: 0x41 = 2-byte hash mode, 1 hop.
  EXPECT_EQ(MeshPath::hopCount(65), 1);
  EXPECT_EQ(MeshPath::hashSize(65), 2);
  EXPECT_FALSE(MeshPath::isUnknown(65));
}

TEST(MeshPath, UnknownSentinel) {
  EXPECT_TRUE(MeshPath::isUnknown(0xFF));
  EXPECT_FALSE(MeshPath::isUnknown(0x3F));
  EXPECT_FALSE(MeshPath::isUnknown(0x00));
  EXPECT_FALSE(MeshPath::isUnknown(0xC1));
}

TEST(MeshPathHashSet, DeduplicatesByFullHashWidth) {
  MeshPath::HashSet set;
  const uint8_t first[2] = {0xAA, 0x11};
  const uint8_t sameFirstByte[2] = {0xAA, 0x22};

  EXPECT_TRUE(set.add(first, 2));
  EXPECT_FALSE(set.add(first, 2));         // exact duplicate
  EXPECT_TRUE(set.add(sameFirstByte, 2));  // shared first byte, distinct hash
  EXPECT_EQ(set.count, 2);
  EXPECT_EQ(set.hashSize, 2);
}

TEST(MeshPathHashSet, FirstByteOnlyHashesStillDeduplicate) {
  MeshPath::HashSet set;
  const uint8_t a[1] = {0xAA};
  const uint8_t b[1] = {0xAA};
  EXPECT_TRUE(set.add(a, 1));
  EXPECT_FALSE(set.add(b, 1));
  EXPECT_EQ(set.count, 1);
  EXPECT_EQ(set.hashSize, 1);
}

TEST(MeshPathHashSet, CapsAtCapacity) {
  MeshPath::HashSet set;
  uint8_t hash[1] = {0};
  for (uint8_t i = 0; i < MeshPath::HashSet::CAPACITY; ++i) {
    hash[0] = i;
    EXPECT_TRUE(set.add(hash, 1));
  }
  hash[0] = 0xFE;
  EXPECT_FALSE(set.add(hash, 1));
  EXPECT_EQ(set.count, MeshPath::HashSet::CAPACITY);
}

TEST(MeshPathHashSet, RejectsInvalidHashSizes) {
  MeshPath::HashSet set;
  const uint8_t hash[4] = {1, 2, 3, 4};
  EXPECT_FALSE(set.add(hash, 0));
  EXPECT_FALSE(set.add(hash, MeshPath::MAX_HASH_SIZE + 1));
  EXPECT_EQ(set.count, 0);
  EXPECT_EQ(set.hashSize, 0);
}
