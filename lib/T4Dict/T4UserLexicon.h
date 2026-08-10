#pragma once

#include <cstddef>
#include <cstdint>

#include "T4Layout.h"

namespace t4 {

/// Persistent per-user word store for predictive input.
///
/// Solves two problems with one structure:
///   1. Words the shipped .trie does not contain (names, jargon) become
///      predictable after being typed once in Multi-tap.
///   2. Words the user types often are ranked ahead of the static
///      frequency order baked into the .trie at build time.
///
/// Words are keyed by (language, button sequence). A sequence is packed
/// two bits per letter into a uint32, so matching a typed sequence is an
/// integer compare instead of a string walk. Because a trie node holds
/// only words of exactly that length, matching is exact-length, not
/// prefix-based.
///
/// The class is pure: no HAL, no file I/O, no heap. The owner reads the
/// file into a buffer and calls loadFromBuffer() / serialize(). Memory is
/// a fixed ~4.6 KB, so instances belong on the heap (makeUniqueNoThrow)
/// for the lifetime of the keyboard activity only.
///
/// Ranking contract used by T4InputEngine:
///   [entries with score >= kStrongScore, by score desc]
///   [best dictionary candidate]
///   [remaining entries (score 1), by score desc]
///   [remaining dictionary candidates]
/// A newly learned word therefore never displaces the most frequent
/// dictionary word, but is one Right press away; from the second use on
/// it takes the top slot.
class T4UserLexicon {
 public:
  /// Maximum stored words. 128 * sizeof(Entry) drives the RAM footprint.
  static constexpr uint16_t kMaxEntries = 128;
  /// Word storage capacity in bytes, including the null terminator.
  static constexpr uint8_t kMaxWordBytes = 28;
  /// Longest storable word in bytes (UTF-8).
  static constexpr uint8_t kMaxWordLen = kMaxWordBytes - 1;
  /// Maximum stored word length in letters (2 bits each in a uint32).
  static constexpr uint8_t kMaxWordChars = 16;
  /// Shorter tokens are noise ("a", "I") and are never learned.
  static constexpr uint8_t kMinWordChars = 2;
  /// Maximum matches reported for one sequence.
  static constexpr uint8_t kMaxMatches = 8;
  /// Score at which an entry outranks the best dictionary candidate.
  static constexpr uint8_t kStrongScore = 2;
  /// Score ceiling; reaching it halves every score (LFU aging).
  static constexpr uint8_t kScoreMax = 200;
  /// Maximum tokens learned from one text, matching the engine's word cap.
  static constexpr uint8_t kMaxLearnPerText = 32;

  // ── File format (see docs/file-formats.md) ──────────────────────────
  static constexpr uint32_t kMagic = 0x54345557;  // 'T4UW'
  static constexpr uint16_t kVersion = 1;
  static constexpr size_t kHeaderSize = 12;
  static constexpr size_t kEntryHeaderSize = 3;
  static constexpr size_t kMaxSerializedSize = kHeaderSize + kMaxEntries * (kEntryHeaderSize + kMaxWordLen);

  /// Drop every entry and mark the store clean.
  void clear();

  /// Learn every eligible word of @p text.
  ///
  /// @param preferredLang  Language tried first when classifying a word;
  ///                       English and the active additional layout are
  ///                       tried as fallbacks.
  /// @param text           Text the user confirmed.
  /// @param skipText       Text the field started with (may be null). Its
  ///                       words are not learned — they were not typed.
  /// @return number of words stored or bumped.
  uint16_t learnText(T4Language preferredLang, const char* text, const char* skipText);

  /// Learn a single word. Rejects anything that is not a run of letters of
  /// one language, is shorter than kMinWordChars, or longer than the
  /// stored limits.
  /// @return true when the word was stored or its score bumped.
  bool learnWord(T4Language preferredLang, const char* word, size_t len);

  /// Collect entries whose language and button sequence match exactly.
  /// Results are written to @p outEntries ordered by score, highest first.
  /// @return number of entries written (at most @p cap).
  uint8_t findMatches(T4Language lang, const uint8_t* seq, uint8_t seqLen, uint16_t* outEntries, uint8_t cap) const;

  /// Word text of an entry, or nullptr when @p entry is out of range.
  const char* getWord(uint16_t entry) const;

  /// Usage score of an entry, or 0 when @p entry is out of range.
  uint8_t getScore(uint16_t entry) const;

  uint16_t getEntryCount() const { return _count; }

  /// True when the store changed since the last load/serialize and needs
  /// to be written back.
  bool isDirty() const { return _dirty; }

  /// Replace the store from a serialized buffer. Entries whose letters no
  /// longer map to their language (layout changed) are dropped.
  /// @return false when the header is missing or invalid.
  bool loadFromBuffer(const uint8_t* data, size_t len);

  /// Write the store to @p out. Clears the dirty flag on success.
  /// @return number of bytes written, or 0 when @p cap is too small.
  size_t serialize(uint8_t* out, size_t cap);

  /// Pack @p word into a button sequence for @p lang.
  /// @return false when any character is not a letter of that language or
  ///         the letter count is outside [kMinWordChars, kMaxWordChars].
  static bool packWord(T4Language lang, const char* word, size_t len, uint32_t& outSeq, uint8_t& outSeqLen);

 private:
  struct Entry {
    uint32_t seq;              // 2 bits per letter, first letter in the high bits
    uint8_t seqLen;            // letters, 1..kMaxWordChars
    uint8_t score;             // 1..kScoreMax
    uint8_t lang;              // T4Language value
    uint8_t wordLen;           // bytes used in word, excluding the terminator
    char word[kMaxWordBytes];  // UTF-8, lowercase, null-terminated
  };

  Entry _entries[kMaxEntries] = {};
  uint16_t _count = 0;
  bool _dirty = false;

  int findEntry(uint8_t lang, const char* word, uint8_t wordLen) const;
  void bump(uint16_t index);
  void ageAll();
  uint16_t victimIndex() const;
  bool insertEntry(T4Language lang, uint32_t seq, uint8_t seqLen, const char* word, uint8_t wordLen);

  /// Lowercase @p word into @p out (kMaxWordBytes). Fails when the result
  /// would not fit.
  static bool normalizeWord(const char* word, size_t len, char* out, uint8_t& outLen);

  /// Pick the language whose letter groups cover @p word, preferring
  /// @p preferredLang, then English, then the active additional layout.
  static bool resolveLanguage(T4Language preferredLang, const char* word, uint8_t len, T4Language& outLang,
                              uint32_t& outSeq, uint8_t& outSeqLen);
};

}  // namespace t4
