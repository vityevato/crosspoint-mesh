#pragma once

#include <cstdint>
#include <cstring>

namespace t4 {

constexpr uint32_t T4_TRIE_MAGIC = 0x54347269;
constexpr uint16_t T4_TRIE_VERSION = 1;
constexpr uint32_t T4_TRIE_NULL_OFFSET = 0xFFFFFFFF;
constexpr size_t T4_TRIE_HEADER_SIZE = 16;
constexpr size_t T4_TRIE_NODE_SIZE = 22;

struct __attribute__((packed)) T4TrieHeader {
  uint32_t magic;       // offset 0
  uint16_t version;     // offset 4
  uint16_t lang_code;   // offset 6 — ISO 639-1 packed: 'e'<<8|'n'
  uint32_t word_count;  // offset 8
  uint32_t node_count;  // offset 12
};

struct __attribute__((packed)) T4TrieNode {
  uint32_t child_offset[4];  // offsets 0-15 — one per button 1-4
  uint16_t word_count;       // offset 16
  uint32_t str_offset;       // offset 18 — into String Pool
};

// ── Pure in-memory helpers (no I/O) ────────────────────────────────────

/// Validate a T4TrieHeader from raw bytes. Returns true if magic, version,
/// and basic sanity checks pass. Does NOT validate against file size —
/// that's the caller's responsibility.
inline bool validateTrieHeader(const uint8_t* data, size_t len) {
  if (len < T4_TRIE_HEADER_SIZE) return false;
  T4TrieHeader hdr;
  memcpy(&hdr, data, sizeof(hdr));
  if (hdr.magic != T4_TRIE_MAGIC) return false;
  if (hdr.version != T4_TRIE_VERSION) return false;
  if (hdr.node_count == 0) return false;
  if (hdr.word_count == 0) return false;
  return true;
}

/// Read a T4TrieNode from the node pool at the given index.
/// nodePool points to the start of the Node Pool section.
/// Returns false if index is out of range.
inline bool readTrieNode(const uint8_t* nodePool, uint32_t nodeCount, uint32_t nodeIndex, T4TrieNode& out) {
  if (nodeIndex >= nodeCount) return false;
  memcpy(&out, nodePool + static_cast<size_t>(nodeIndex) * T4_TRIE_NODE_SIZE, sizeof(T4TrieNode));
  return true;
}

/// Measure the String Pool span needed for up to @p wordCount
/// null-terminated words, capped at @p maxBytes. Only complete words are
/// counted: a word whose terminator falls beyond the cap is excluded, so the
/// returned span never ends mid-word.
/// @param readByte      callable returning the next byte (0..255) or -1 on
///                      error/EOF.
/// @param wordsThatFit  set to the number of complete words within the span.
/// @return bytes occupied by complete words (0 when none fits).
template <typename ReadByte>
inline size_t measureCandidateSpan(ReadByte readByte, uint16_t wordCount, size_t maxBytes, uint16_t& wordsThatFit) {
  size_t consumed = 0;
  size_t completeBytes = 0;
  wordsThatFit = 0;
  while (wordsThatFit < wordCount && consumed < maxBytes) {
    const int value = readByte();
    if (value < 0) break;
    ++consumed;
    if (static_cast<char>(value) == '\0') {
      completeBytes = consumed;
      ++wordsThatFit;
    }
  }
  return completeBytes;
}

/// Read up to @p wordCount null-terminated words into @p buf of @p bufSize
/// bytes. Stops at the word count, at buffer capacity (a partial trailing
/// word is dropped from the count), or on a read error.
/// @return number of complete words written.
template <typename ReadByte>
inline uint16_t readCandidateSpan(ReadByte readByte, uint16_t wordCount, char* buf, size_t bufSize) {
  size_t bytesRead = 0;
  uint16_t wordsRead = 0;
  while (wordsRead < wordCount && bytesRead < bufSize) {
    const int value = readByte();
    if (value < 0) break;
    const char ch = static_cast<char>(value);
    buf[bytesRead++] = ch;
    if (ch == '\0') ++wordsRead;
  }
  return wordsRead;
}

/// Extract null-terminated candidate words from the String Pool.
/// stringPool points to the start of the String Pool section (absolute
/// str_offset is relative to file start; caller must subtract pool start).
/// Writes up to bufSize-1 bytes to buf. Returns number of words extracted.
inline size_t extractCandidates(const uint8_t* stringPool, size_t poolLen, uint32_t poolRelativeOffset,
                                uint16_t wordCount, char* buf, size_t bufSize) {
  if (poolRelativeOffset >= poolLen || wordCount == 0 || bufSize == 0) {
    return 0;
  }
  size_t wordsFound = 0;
  size_t writePos = 0;
  size_t readPos = poolRelativeOffset;
  for (uint16_t i = 0; i < wordCount && readPos < poolLen; ++i) {
    // Find null terminator
    size_t end = readPos;
    while (end < poolLen && stringPool[end] != '\0') ++end;
    if (end >= poolLen) break;  // unterminated string
    size_t wordLen = end - readPos;
    if (writePos + wordLen + 1 > bufSize) break;  // buffer full
    memcpy(buf + writePos, stringPool + readPos, wordLen);
    buf[writePos + wordLen] = '\0';
    writePos += wordLen + 1;
    ++wordsFound;
    readPos = end + 1;
  }
  return wordsFound;
}

}  // namespace t4
