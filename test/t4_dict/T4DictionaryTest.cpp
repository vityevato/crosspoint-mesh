#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "T4Dict/T4TrieFormat.h"

using namespace t4;

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

namespace {

// Helper: build path relative to TEST_DATA_DIR
std::string testPath(const char* filename) { return std::string(TEST_DATA_DIR) + "/" + filename; }

}  // namespace

TEST(T4TrieFormat, ConstantsAreCorrect) {
  EXPECT_EQ(T4_TRIE_MAGIC, 0x54347269u);
  EXPECT_EQ(T4_TRIE_VERSION, 1u);
  EXPECT_EQ(T4_TRIE_HEADER_SIZE, 16u);
  EXPECT_EQ(T4_TRIE_NODE_SIZE, 22u);
}

TEST(T4TrieFormat, HeaderStructSizeAndLayout) {
  EXPECT_EQ(sizeof(T4TrieHeader), 16u);
  EXPECT_EQ(offsetof(T4TrieHeader, magic), 0u);
  EXPECT_EQ(offsetof(T4TrieHeader, version), 4u);
  EXPECT_EQ(offsetof(T4TrieHeader, lang_code), 6u);
  EXPECT_EQ(offsetof(T4TrieHeader, word_count), 8u);
  EXPECT_EQ(offsetof(T4TrieHeader, node_count), 12u);
}

TEST(T4TrieFormat, NodeStructSizeAndLayout) {
  EXPECT_EQ(sizeof(T4TrieNode), 22u);
  EXPECT_EQ(offsetof(T4TrieNode, child_offset), 0u);
  EXPECT_EQ(offsetof(T4TrieNode, word_count), 16u);
  EXPECT_EQ(offsetof(T4TrieNode, str_offset), 18u);
}

namespace {

// Helper: read entire file into a vector
std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return {};
  auto size = f.tellg();
  f.seekg(0);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  f.read(reinterpret_cast<char*>(data.data()), size);
  return data;
}

}  // namespace

TEST(T4TrieFormat, ValidateRealEnTrie) {
  auto data = readFile(testPath("en_1000.trie"));
  ASSERT_FALSE(data.empty()) << "en_1000.trie not found — run build_t4_dict.py first";

  EXPECT_TRUE(validateTrieHeader(data.data(), data.size()));

  T4TrieHeader hdr;
  memcpy(&hdr, data.data(), sizeof(hdr));
  EXPECT_EQ(hdr.lang_code, ('e' << 8) | 'n');

  // Try reading root node
  const uint8_t* nodePool = data.data() + T4_TRIE_HEADER_SIZE;
  T4TrieNode root;
  ASSERT_TRUE(readTrieNode(nodePool, hdr.node_count, 0, root));

  // Navigate to "hello": h→1, e→0, l→1, l→1, o→2
  // en groups: abcdef=0, ghijkl=1, mnopqrs=2, tuvwxyz=3
  // hello = h(1) e(0) l(1) l(1) o(2)
  uint32_t seq[] = {1, 0, 1, 1, 2};
  T4TrieNode node = root;
  for (uint32_t btn : seq) {
    uint32_t off = node.child_offset[btn];
    ASSERT_NE(off, T4_TRIE_NULL_OFFSET) << "No child for button " << btn;
    ASSERT_TRUE(readTrieNode(nodePool, hdr.node_count, (off - T4_TRIE_HEADER_SIZE) / T4_TRIE_NODE_SIZE, node));
  }

  EXPECT_GT(node.word_count, 0u) << "Expected words at 'hello' node";

  // Extract candidates
  const uint8_t* stringPool = data.data() + T4_TRIE_HEADER_SIZE + hdr.node_count * T4_TRIE_NODE_SIZE;
  size_t poolLen = data.size() - (stringPool - data.data());
  uint32_t poolRelative = node.str_offset - T4_TRIE_HEADER_SIZE - hdr.node_count * T4_TRIE_NODE_SIZE;

  char buf[4096];
  size_t count = extractCandidates(stringPool, poolLen, poolRelative, node.word_count, buf, sizeof(buf));
  EXPECT_GT(count, 0u);

  // "hello" should be the first candidate (highest frequency)
  EXPECT_STREQ(buf, "hello");
}

TEST(T4TrieFormat, DeadEndSequenceReturnsNoChild) {
  auto data = readFile(testPath("en_1000.trie"));
  ASSERT_FALSE(data.empty());

  T4TrieHeader hdr;
  memcpy(&hdr, data.data(), sizeof(hdr));

  const uint8_t* nodePool = data.data() + T4_TRIE_HEADER_SIZE;
  T4TrieNode root;
  ASSERT_TRUE(readTrieNode(nodePool, hdr.node_count, 0, root));

  // "xqxqz" — unlikely sequence, should hit dead end
  // x=3, q=2, x=3, q=2, z=3
  uint32_t seq[] = {3, 2, 3, 2, 3};
  T4TrieNode node = root;
  bool deadEnd = false;
  for (uint32_t btn : seq) {
    uint32_t off = node.child_offset[btn];
    if (off == T4_TRIE_NULL_OFFSET) {
      deadEnd = true;
      break;
    }
    ASSERT_TRUE(readTrieNode(nodePool, hdr.node_count, (off - T4_TRIE_HEADER_SIZE) / T4_TRIE_NODE_SIZE, node));
  }
  EXPECT_TRUE(deadEnd) << "xqxqz should not be a valid sequence";
}

TEST(T4TrieFormat, ValidateRealRuTrie) {
  auto data = readFile(testPath("ru_1000.trie"));
  ASSERT_FALSE(data.empty()) << "ru_1000.trie not found — run build_t4_dict.py first";

  EXPECT_TRUE(validateTrieHeader(data.data(), data.size()));

  T4TrieHeader hdr;
  memcpy(&hdr, data.data(), sizeof(hdr));
  EXPECT_EQ(hdr.lang_code, ('r' << 8) | 'u');
}

// ── T4Dictionary tests (link only when HalStorage is available) ─────────

#ifndef CMake_TEST_T4TRIEFORMAT_ONLY

#include "T4Dict/T4Dictionary.h"

TEST(T4Dictionary, InitiallyNotLoaded) {
  T4Dictionary dict;
  EXPECT_FALSE(dict.isLoaded());
  EXPECT_EQ(dict.getCandidateCount(), 0u);
}

#endif  // CMake_TEST_T4TRIEFORMAT_ONLY
