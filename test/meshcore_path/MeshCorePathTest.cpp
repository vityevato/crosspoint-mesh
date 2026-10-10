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

