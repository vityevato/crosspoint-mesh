#pragma once

#include <cstdint>
#include <memory>

#include <HalStorage.h>
#include "T4TrieFormat.h"

class T4Dictionary {
public:
  T4Dictionary();
  ~T4Dictionary();

  // Non-copyable, movable
  T4Dictionary(const T4Dictionary&) = delete;
  T4Dictionary& operator=(const T4Dictionary&) = delete;
  T4Dictionary(T4Dictionary&&) noexcept;
  T4Dictionary& operator=(T4Dictionary&&) noexcept;

  /// Load a .trie dictionary from SD card.
  /// @param path  Full path, e.g. "/.crosspoint/dicts/en.trie"
  /// @return true on success, false on any error (missing file, bad format).
  bool loadFromSD(const char* path);

  /// Navigate to the child node for button 1-4.
  /// @param btn  Button index (1-based: 1, 2, 3, or 4).
  /// @return true if a child exists for this button, false if dead end.
  bool pressButton(uint8_t btn);

  /// Number of candidate words at the current node.
  uint16_t getCandidateCount() const;

  /// Load candidate words from the String Pool into the internal buffer.
  /// Must be called after navigation before getCandidate().
  /// @return true on success, false on I/O error.
  bool loadCandidates();

  /// Get a pointer to a candidate word by index (0 = most frequent).
  /// Caller must call loadCandidates() first.
  /// @return null-terminated word, or nullptr if index out of range.
  const char* getCandidate(uint16_t index) const;

  /// Reset navigation to the root node. Clears the candidate buffer.
  void reset();

  /// Close the dictionary file and free all buffers.
  void close();

  /// Whether a dictionary is currently loaded and ready.
  bool isLoaded() const;

  /// The language code of the loaded dictionary (e.g. "en", "ru").
  /// Returns empty string if not loaded.
  const char* getLangCode() const;

private:
  static constexpr size_t DEFAULT_CANDIDATE_BUF_SIZE = 4096;

  HalFile _file;
  t4::T4TrieNode _currentNode{};
  std::unique_ptr<char[]> _candidateBuf;
  size_t _candidateBufSize = 0;
  uint16_t _candidateCount = 0;
  bool _loaded = false;
  char _langCode[3] = {};  // 2-char ISO code + null

  bool readNodeAtOffset(uint32_t offset, t4::T4TrieNode& out);
  void freeCandidates();
};
