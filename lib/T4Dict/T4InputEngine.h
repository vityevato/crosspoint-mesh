#pragma once

#include <Logging.h>

#include <cstdint>
#include <cstring>
#include <memory>

#include "T4Layout.h"

// Forward declaration — T4Dictionary.h pulls in HalStorage which may not
// be available in all test environments. Concrete instantiation happens in
// the .cpp file which is compiled only when HalStorage is available.
class T4Dictionary;

namespace t4 {

/// Input engine state machine for T4 text input.
///
/// Template parameter @p Dict allows test injection of MockDictionary.
/// The default is T4Dictionary (SD-card-backed trie). Tests can substitute
/// a lightweight in-memory implementation without virtual dispatch.
///
/// Usage:
///   T4InputEngine<> engine;                  // production
///   T4InputEngine<MockDictionary> engine;    // test
template <typename Dict = T4Dictionary>
class T4InputEngine {
 public:
  T4InputEngine();
  ~T4InputEngine();

  // Non-copyable
  T4InputEngine(const T4InputEngine&) = delete;
  T4InputEngine& operator=(const T4InputEngine&) = delete;

  // ── Language ──────────────────────────────────────────────────────

  /// Switch to a new language. Unloads current dictionary, loads the
  /// dictionary for @p lang from SD. Resets sequence but preserves
  /// confirmed text and mode. Stores error in _lastError on failure.
  void setLanguage(T4Language lang);

  /// Current language.
  T4Language getLanguage() const;

  /// Last error message, or "" if no error.
  const char* getLastError() const;

  // ── Mode ──────────────────────────────────────────────────────────

  /// Switch input mode. Preserves language and confirmed text.
  void setMode(T4Mode mode);

  /// Current mode.
  T4Mode getMode() const;

  // ── Input ─────────────────────────────────────────────────────────

  /// Handle a button press (1–4) according to current mode.
  /// In PREDICT: appends to sequence, navigates trie, loads candidates.
  /// In MULTI_TAP: cycles letter within group or fixes + starts new.
  /// @return true if the press produced a valid result.
  bool pressButton(uint8_t btn);

  // ── Candidate navigation (Predict mode) ───────────────────────────

  /// Cycle to the next candidate. Wraps around after last.
  void cycleCandidate();

  /// Cycle to the previous candidate. Wraps around after first.
  void cycleCandidateBackward();

  /// Current candidate word, or nullptr if none.
  const char* getCurrentCandidate();

  /// Index of current candidate (0 = best).
  uint16_t getCandidateIndex() const;

  /// Total number of candidates for current sequence.
  uint16_t getCandidateCount() const;

  /// Retrieve a specific candidate by index.
  /// @return candidate word, or nullptr if index is out of range.
  const char* getCandidate(uint16_t index);

  // ── Sequence (for UI display) ─────────────────────────────────────

  /// Button press sequence (1–4 values). Length = getSequenceLength().
  const uint8_t* getSequence() const;

  /// Number of button presses in current sequence.
  uint8_t getSequenceLength() const;

  // ── Multi-tap state (for UI display) ──────────────────────────────

  /// Currently active letter in Multi-tap (pointer into group string).
  /// outByteLen receives UTF-8 byte length (1–4). Returns nullptr if idle.
  const char* getCurrentTapLetter(uint8_t& outByteLen) const;

  /// Current button being multi-tapped (1–4), or 0 if idle.
  uint8_t getActiveButton() const;

  /// Fix the currently cycling multi-tap letter into confirmed text.
  /// Does NOT append a space — unlike confirmWord(), this is the
  /// building block for MULTI_TAP where each letter is individually
  /// confirmed by timeout or next button press.
  void fixMultiTapLetter();

  // ── Shift / uppercase ─────────────────────────────────────────────

  /// Uppercase level applied to newly typed letters:
  /// 0 = off, 1 = one-shot (next letter only), 2 = locked (Caps Lock).
  /// In MULTI_TAP the level is applied when a letter is fixed; a
  /// one-shot level resets to 0 after the first fixed letter. In
  /// PREDICT the level is read by the UI layer at word-commit time.
  void setShiftLevel(uint8_t level) { _shiftLevel = level; }

  /// Current uppercase level (0 = off, 1 = one-shot, 2 = locked).
  uint8_t getShiftLevel() const { return _shiftLevel; }

  // ── Text output ───────────────────────────────────────────────────

  /// Confirm current word (Predict) or space (Multi-tap).
  /// In Predict: appends current candidate + space to confirmed text,
  ///   resets sequence.
  /// In Multi-tap: fixes current letter if any, appends space.
  void confirmWord();

  /// Full confirmed text so far (null-terminated).
  const char* getConfirmedText() const;

  /// Length of confirmed text.
  uint16_t getConfirmedTextLength() const;

  /// Replace confirmed text buffer with @p text.
  /// Used to sync confirmed text between activity and engine on mode switch.
  /// Text is truncated to kMaxTextLen if needed.
  void setConfirmedText(const char* text);

  // ── Editing ───────────────────────────────────────────────────────

  /// Backspace. In Predict: deletes entire candidate word (resets
  /// sequence). In Multi-tap: deletes last fixed letter.
  void backspace();

  // ── Time-based updates ────────────────────────────────────────────

  /// Call periodically with current clock (ms). Checks Multi-tap
  /// timeout (800ms), fixes letter if expired.
  void poll(uint32_t nowMs);

  // ── Full reset ────────────────────────────────────────────────────

  /// Reset all state: sequence, confirmed text, candidates.
  /// Does NOT unload the dictionary or change language/mode.
  void reset();

  // ── Dictionary access ─────────────────────────────────────────────

  /// Direct access to the owned dictionary (for loading, etc.).
  Dict* getDictionary();

  /// Multi-tap timeout: if no button is pressed within this window,
  /// the current cycling letter is fixed and the next press starts a new letter.
  static constexpr uint32_t kMultiTapTimeoutMs = 800;

  /// Maximum confirmed text length in bytes (UTF-8).
  static constexpr uint16_t kMaxTextLen = 300;

 private:
  static constexpr uint8_t kMaxSeqLen = 31;

  std::unique_ptr<Dict> _dict;
  T4Language _lang = T4Language::EN;
  T4Mode _mode = T4Mode::PREDICT;

  // Predict state
  uint8_t _sequence[kMaxSeqLen + 1] = {};
  uint8_t _seqLen = 0;
  uint16_t _candidateIndex = 0;
  uint16_t _candidateCount = 0;
  bool _candidatesLoaded = false;

  // Multi-tap state
  uint8_t _activeButton = 0;
  uint8_t _tapIndex = 0;
  uint32_t _lastTapTime = 0;

  // Uppercase level: 0 = off, 1 = one-shot (shift), 2 = locked (caps).
  uint8_t _shiftLevel = 0;

  // Confirmed text
  char _confirmedText[kMaxTextLen + 1] = {};
  uint16_t _textLen = 0;

  // Per-word language metadata (indexed by word order in confirmed text).
  // Used by backspace to recover the correct language when pulling a word
  // back for re-editing, and to auto-switch the input language to match.
  static constexpr uint8_t kMaxWords = 32;
  T4Language _wordLang[kMaxWords] = {};
  uint8_t _wordCount = 0;

  // Scratch / error
  mutable char _candidateBuf[64] = {};
  char _lastError[64] = {};

  // Dead-end fallback: last valid candidate + raw first-letters from
  // the dead-end tail of the sequence.
  char _savedCandidate[64] = {};
  uint8_t _savedSeqLen = 0;

  // ── Internal helpers ──────────────────────────────────────────────

  void setError(const char* msg);
  void clearSequence();
  void loadDictionaryForLanguage(T4Language lang);
  const char* dictPathForLanguage(T4Language lang);

  /// Detect the dominant language of a UTF-8 word by inspecting lead bytes.
  /// Cyrillic (0xD0–0xD1) → RU; otherwise EN. Returns EN for empty input.
  /// Used when rebuilding _wordLang from raw text in setConfirmedText().
  static T4Language detectWordLanguage(const char* word, uint16_t wordLen);
};

// ── Template implementation (inline for Dict-dependent parts) ───────────

template <typename Dict>
T4InputEngine<Dict>::T4InputEngine() : _dict(std::make_unique<Dict>()) {}

template <typename Dict>
T4InputEngine<Dict>::~T4InputEngine() = default;

template <typename Dict>
T4Language T4InputEngine<Dict>::getLanguage() const {
  return _lang;
}

template <typename Dict>
const char* T4InputEngine<Dict>::getLastError() const {
  return _lastError;
}

template <typename Dict>
T4Mode T4InputEngine<Dict>::getMode() const {
  return _mode;
}

template <typename Dict>
uint16_t T4InputEngine<Dict>::getCandidateIndex() const {
  return _candidateIndex;
}

template <typename Dict>
uint16_t T4InputEngine<Dict>::getCandidateCount() const {
  return _candidateCount;
}

template <typename Dict>
const char* T4InputEngine<Dict>::getCandidate(uint16_t index) {
  if (!_dict || !_candidatesLoaded || index >= _candidateCount) return nullptr;
  return _dict->getCandidate(index);
}

template <typename Dict>
const uint8_t* T4InputEngine<Dict>::getSequence() const {
  return _sequence;
}

template <typename Dict>
uint8_t T4InputEngine<Dict>::getSequenceLength() const {
  return _seqLen;
}

template <typename Dict>
const char* T4InputEngine<Dict>::getConfirmedText() const {
  return _confirmedText;
}

template <typename Dict>
uint16_t T4InputEngine<Dict>::getConfirmedTextLength() const {
  return _textLen;
}

template <typename Dict>
void T4InputEngine<Dict>::setConfirmedText(const char* text) {
  if (!text) {
    _confirmedText[0] = '\0';
    _textLen = 0;
    _wordCount = 0;
    return;
  }
  auto len = strlen(text);
  if (len > kMaxTextLen) len = kMaxTextLen;
  memcpy(_confirmedText, text, len);
  _confirmedText[len] = '\0';
  _textLen = static_cast<uint16_t>(len);

  // Rebuild per-word language metadata by scanning the text.
  _wordCount = 0;
  uint16_t pos = 0;
  while (pos < _textLen && _wordCount < kMaxWords) {
    // Skip leading spaces/punctuation
    while (pos < _textLen && _confirmedText[pos] == ' ') pos++;
    if (pos >= _textLen) break;

    // Find end of this word (until space or end)
    uint16_t wordStart = pos;
    while (pos < _textLen && _confirmedText[pos] != ' ') pos++;
    uint16_t wordLen = pos - wordStart;
    if (wordLen > 0) {
      _wordLang[_wordCount++] = detectWordLanguage(_confirmedText + wordStart, wordLen);
    }
    // Skip trailing spaces
    while (pos < _textLen && _confirmedText[pos] == ' ') pos++;
  }
}

template <typename Dict>
uint8_t T4InputEngine<Dict>::getActiveButton() const {
  return _activeButton;
}

template <typename Dict>
Dict* T4InputEngine<Dict>::getDictionary() {
  return _dict.get();
}

template <typename Dict>
void T4InputEngine<Dict>::setError(const char* msg) {
  auto len = strlen(msg);
  if (len > sizeof(_lastError) - 1) len = sizeof(_lastError) - 1;
  memcpy(_lastError, msg, len);
  _lastError[len] = '\0';
}

template <typename Dict>
T4Language T4InputEngine<Dict>::detectWordLanguage(const char* word, uint16_t wordLen) {
  if (!word || wordLen == 0) return T4Language::EN;
  // Scan the word: any 2-byte UTF-8 Cyrillic lead byte → RU.
  for (uint16_t i = 0; i < wordLen;) {
    unsigned char c0 = static_cast<unsigned char>(word[i]);
    if (c0 >= 0xD0 && c0 <= 0xD1) return T4Language::RU;
    // Advance by UTF-8 byte length
    if ((c0 & 0xE0) == 0xC0)
      i += 2;
    else if ((c0 & 0xF0) == 0xE0)
      i += 3;
    else if ((c0 & 0xF8) == 0xF0)
      i += 4;
    else
      i += 1;
  }
  return T4Language::EN;
}

template <typename Dict>
void T4InputEngine<Dict>::clearSequence() {
  _seqLen = 0;
  _candidateIndex = 0;
  _candidateCount = 0;
  _candidatesLoaded = false;
  _candidateBuf[0] = '\0';
  _savedCandidate[0] = '\0';
  _savedSeqLen = 0;
}

template <typename Dict>
const char* T4InputEngine<Dict>::dictPathForLanguage(T4Language lang) {
  return getDictionaryPath(lang);
}

template <typename Dict>
void T4InputEngine<Dict>::loadDictionaryForLanguage(T4Language lang) {
  const char* path = dictPathForLanguage(lang);
  if (!path) return;  // DIGIT mode has no dictionary
  _dict = std::make_unique<Dict>();
  if (!_dict->loadFromSD(path)) {
    setError("Failed to load dictionary");
    _dict.reset();
  }
}

template <typename Dict>
const char* T4InputEngine<Dict>::getCurrentTapLetter(uint8_t& outByteLen) const {
  if (_activeButton == 0) {
    outByteLen = 0;
    return nullptr;
  }
  return getGroupLetter(_lang, _activeButton, _tapIndex, outByteLen);
}

// ── setLanguage ─────────────────────────────────────────────────────────

template <typename Dict>
void T4InputEngine<Dict>::setLanguage(T4Language lang) {
  LOG_DBG("T4", "setLanguage: %d (current=%d)", static_cast<int>(lang), static_cast<int>(_lang));
  if (lang == _lang && _dict && _dict->isLoaded()) return;
  _lang = lang;
  clearSequence();

  if (_dict) {
    _dict->close();
    _dict.reset();
  }

  if (lang != T4Language::DIGIT) {
    loadDictionaryForLanguage(lang);
  }

  _lastError[0] = '\0';
}

// ── setMode ─────────────────────────────────────────────────────────────

template <typename Dict>
void T4InputEngine<Dict>::setMode(T4Mode mode) {
  LOG_DBG("T4", "setMode: %d (current=%d)", static_cast<int>(mode), static_cast<int>(_mode));
  if (mode == _mode) return;

  _mode = mode;

  // Discard PREDICT navigation state when leaving PREDICT mode —
  // avoids stale candidates leaking into other modes' rendering.
  if (_mode != T4Mode::PREDICT) {
    clearSequence();
    if (_dict) _dict->reset();
  }

  // Fix any in-progress multi-tap letter on mode switch away from MULTI_TAP.
  if (_activeButton != 0 && mode != T4Mode::MULTI_TAP) {
    fixMultiTapLetter();
  }
}

// ── pressButton ─────────────────────────────────────────────────────────

template <typename Dict>
bool T4InputEngine<Dict>::pressButton(uint8_t btn) {
  LOG_DBG("T4", "pressButton: btn=%u mode=%d lang=%d", btn, static_cast<int>(_mode), static_cast<int>(_lang));
  if (btn < 1 || btn > 4) return false;

  if (_mode == T4Mode::PREDICT) {
    // ── Predict mode ────────────────────────────────────────────────
    if (!_dict) return false;  // no dictionary (e.g. DIGIT)

    // DIGIT language in Predict: just type the first digit in group
    if (_lang == T4Language::DIGIT) {
      uint8_t blen;
      const char* digit = getGroupLetter(T4Language::DIGIT, btn, 0, blen);
      if (digit && blen > 0 && _textLen + blen <= kMaxTextLen) {
        memcpy(_confirmedText + _textLen, digit, blen);
        _textLen += blen;
        _confirmedText[_textLen] = '\0';
        LOG_DBG("T4", "pressButton: digit confirmed");
      }
      return true;
    }

    if (_seqLen >= kMaxSeqLen) return false;

    // Save current candidate before navigating further (dead-end fallback).
    // _dict is non-null here (checked at the top of the PREDICT block).
    if (_candidateCount > 0 && _candidatesLoaded) {
      const char* cur = _dict->getCandidate(_candidateIndex);
      if (cur && cur[0]) {
        auto clen = strlen(cur);
        if (clen < sizeof(_savedCandidate)) {
          memcpy(_savedCandidate, cur, clen);
          _savedCandidate[clen] = '\0';
        }
      }
      _savedSeqLen = _seqLen;
    }

    _sequence[_seqLen++] = btn;

    bool ok = _dict->pressButton(btn);
    if (!ok) {
      LOG_DBG("T4", "pressButton: predict dead end at seqLen=%u", _seqLen);
      // Dead end — keep sequence but no candidates
      _candidateCount = 0;
      _candidatesLoaded = false;
      _candidateIndex = 0;
      _candidateBuf[0] = '\0';
      return false;
    }

    // Load candidates FIRST — T4Dictionary::pressButton() frees previous
    // candidates, so getCandidateCount() returns 0 until loadCandidates().
    _candidatesLoaded = _dict->loadCandidates();
    _candidateCount = _dict->getCandidateCount();
    _candidateIndex = 0;

    return true;
  }

  if (_mode == T4Mode::MULTI_TAP) {
    // ── Multi-tap mode ──────────────────────────────────────────────
    if (_activeButton == 0) {
      // Start new letter
      _activeButton = btn;
      _tapIndex = 0;
      _lastTapTime = 0;  // will be set by poll()
      return true;
    }

    if (_activeButton == btn) {
      // Same button — cycle to next letter
      uint8_t groupLen = getGroupLength(_lang, btn);
      if (groupLen == 0) return false;
      _tapIndex = (_tapIndex + 1) % groupLen;
      _lastTapTime = 0;
      LOG_DBG("T4", "pressButton: multi-tap cycle btn=%u tapIdx=%u", btn, _tapIndex);
      return true;
    }

    // Different button — fix current letter, start new
    fixMultiTapLetter();
    _activeButton = btn;
    _tapIndex = 0;
    _lastTapTime = 0;
    LOG_DBG("T4", "pressButton: multi-tap new btn=%u", btn);
    return true;
  }

  return false;
}

// ── cycleCandidate ──────────────────────────────────────────────────────

template <typename Dict>
void T4InputEngine<Dict>::cycleCandidate() {
  if (_candidateCount == 0) return;
  _candidateIndex = (_candidateIndex + 1) % _candidateCount;
  LOG_DBG("T4", "cycleCandidate: %u/%u", _candidateIndex, _candidateCount);
}

template <typename Dict>
void T4InputEngine<Dict>::cycleCandidateBackward() {
  if (_candidateCount == 0) return;
  _candidateIndex = (_candidateIndex == 0) ? _candidateCount - 1 : _candidateIndex - 1;
  LOG_DBG("T4", "cycleCandidateBackward: %u/%u", _candidateIndex, _candidateCount);
}

// ── getCurrentCandidate ─────────────────────────────────────────────────

template <typename Dict>
const char* T4InputEngine<Dict>::getCurrentCandidate() {
  // Fallback: no dictionary candidates — show last valid candidate + raw
  // first-letter of each dead-end button press after it.
  if (_candidateCount == 0) {
    if (_seqLen == 0) return nullptr;
    int pos = 0;

    // Copy saved (last valid) candidate if available
    if (_savedCandidate[0] && _savedSeqLen > 0 && _savedSeqLen < _seqLen) {
      auto slen = strlen(_savedCandidate);
      if (slen < sizeof(_candidateBuf)) {
        memcpy(_candidateBuf, _savedCandidate, slen);
        pos = slen;
      }
    }

    // Append raw first-letter from dead-end tail of sequence
    for (uint8_t i = (pos > 0) ? _savedSeqLen : 0; i < _seqLen; i++) {
      const char* group = getGroup(_lang, _sequence[i]);
      if (!group || !group[0]) continue;
      unsigned char c0 = static_cast<unsigned char>(group[0]);
      uint8_t blen = 1;
      if ((c0 & 0xE0) == 0xC0)
        blen = 2;
      else if ((c0 & 0xF0) == 0xE0)
        blen = 3;
      else if ((c0 & 0xF8) == 0xF0)
        blen = 4;
      if (pos + blen >= (int)sizeof(_candidateBuf)) break;
      memcpy(_candidateBuf + pos, group, blen);
      pos += blen;
    }
    _candidateBuf[pos] = '\0';
    return _candidateBuf;
  }

  if (!_candidatesLoaded || !_dict) return nullptr;

  const char* word = _dict->getCandidate(_candidateIndex);
  if (!word) return nullptr;

  // Copy to scratch buffer
  auto len = strlen(word);
  if (len >= sizeof(_candidateBuf)) len = sizeof(_candidateBuf) - 1;
  memcpy(_candidateBuf, word, len);
  _candidateBuf[len] = '\0';
  return _candidateBuf;
}

// ── confirmWord ─────────────────────────────────────────────────────────

template <typename Dict>
void T4InputEngine<Dict>::confirmWord() {
  LOG_DBG("T4", "confirmWord: mode=%d textLen=%u", static_cast<int>(_mode), _textLen);
  if (_mode == T4Mode::PREDICT) {
    // Fix current candidate and append space
    const char* cand = getCurrentCandidate();
    if (cand && cand[0] != '\0') {
      auto len = strlen(cand);
      if (_textLen + len + 1 <= kMaxTextLen) {
        memcpy(_confirmedText + _textLen, cand, len);
        _textLen += len;
        _confirmedText[_textLen++] = ' ';
        _confirmedText[_textLen] = '\0';
        // Record word language (for the Multi-tap path; in Predict the
        // activity's subsequent setConfirmedText() call will rebuild this).
        if (_wordCount < kMaxWords) _wordLang[_wordCount++] = _lang;
      }
    }

    // Reset sequence, reset dictionary to root
    if (_dict) _dict->reset();
    clearSequence();
    return;
  }

  if (_mode == T4Mode::MULTI_TAP) {
    // Fix any in-progress letter, then append space
    fixMultiTapLetter();
    if (_textLen < kMaxTextLen) {
      _confirmedText[_textLen++] = ' ';
      _confirmedText[_textLen] = '\0';
      if (_wordCount < kMaxWords) _wordLang[_wordCount++] = _lang;
    }
    return;
  }
}

// ── backspace ───────────────────────────────────────────────────────────

template <typename Dict>
void T4InputEngine<Dict>::backspace() {
  LOG_DBG("T4", "backspace: mode=%d textLen=%u", static_cast<int>(_mode), _textLen);

  if (_mode == T4Mode::PREDICT) {
    // Go back one step in the prediction: remove last button press
    // and rebuild the dictionary state from the shortened sequence.
    if (_seqLen > 0) {
      _seqLen--;
      if (_dict) {
        _dict->reset();
        for (uint8_t i = 0; i < _seqLen; i++) {
          _dict->pressButton(_sequence[i]);
        }
        _candidatesLoaded = _dict->loadCandidates();
        _candidateCount = _dict->getCandidateCount();
      }
      _candidateIndex = 0;
      _candidateBuf[0] = '\0';
      _savedCandidate[0] = '\0';
      _savedSeqLen = 0;
      return;
    }

    // Sequence is empty: delete trailing punctuation, or pull the last
    // word back into the sequence for re-editing.
    if (_textLen == 0) return;

    // Find start byte of last UTF-8 character (scan backwards from end)
    uint8_t blen = 1;
    uint16_t lastStart = _textLen - 1;
    while (lastStart > 0 && (_confirmedText[lastStart] & 0xC0) == 0x80) {
      lastStart--;
      blen++;
    }

    // Check if last char is a letter (current language first, then fallback
    // to other languages for cross-language words).
    const char* lastCharPtr = _confirmedText + lastStart;
    bool isLetter = buttonForLetter(_lang, lastCharPtr, blen) != 0;
    if (!isLetter) {
      for (uint8_t li = 0; li < kLanguageCount; li++) {
        T4Language tl = static_cast<T4Language>(li);
        if (tl == _lang || tl == T4Language::DIGIT) continue;
        if (buttonForLetter(tl, lastCharPtr, blen) != 0) {
          isLetter = true;
          break;
        }
      }
    }

    if (!isLetter) {
      // Punctuation or space: just delete one character
      _textLen -= blen;
      _confirmedText[_textLen] = '\0';
      return;
    }

    // It's a letter: extract the entire word from confirmed text,
    // move it into the prediction sequence for re-editing.
    uint16_t wordStart = _textLen;
    while (wordStart > 0) {
      uint8_t prevBlen = 1;
      uint16_t prevStart = wordStart - 1;
      while (prevStart > 0 && (_confirmedText[prevStart] & 0xC0) == 0x80) {
        prevStart--;
        prevBlen++;
      }
      const char* prevChar = _confirmedText + prevStart;

      bool prevIsLetter = buttonForLetter(_lang, prevChar, prevBlen) != 0;
      if (!prevIsLetter) {
        for (uint8_t li = 0; li < kLanguageCount; li++) {
          T4Language tl = static_cast<T4Language>(li);
          if (tl == _lang || tl == T4Language::DIGIT) continue;
          if (buttonForLetter(tl, prevChar, prevBlen) != 0) {
            prevIsLetter = true;
            break;
          }
        }
      }
      if (!prevIsLetter) break;
      wordStart -= prevBlen;
    }

    // Determine the language of this word from stored metadata.
    // Every word in confirmed text arrived via confirmWord() which records
    // _lang at commit time, so the metadata is always available here.
    T4Language wordLang = (_wordCount > 0) ? _wordLang[_wordCount - 1] : _lang;

    // Extract word and its button sequence using the word's language.
    uint8_t seqIdx = 0;
    uint16_t pos = wordStart;
    while (pos < _textLen && seqIdx < kMaxSeqLen) {
      uint8_t charBlen = 1;
      unsigned char firstByte = static_cast<unsigned char>(_confirmedText[pos]);
      if ((firstByte & 0xE0) == 0xC0)
        charBlen = 2;
      else if ((firstByte & 0xF0) == 0xE0)
        charBlen = 3;
      else if ((firstByte & 0xF8) == 0xF0)
        charBlen = 4;

      uint8_t btn = buttonForLetter(wordLang, _confirmedText + pos, charBlen);
      if (btn == 0) break;
      _sequence[seqIdx++] = btn;
      pos += charBlen;
    }
    _seqLen = seqIdx;

    // Save the word before removing it from confirmed text so we can
    // restore the correct candidate index after reloading candidates.
    uint16_t wordLen = _textLen - wordStart;
    char savedWord[64] = {};
    if (wordLen < sizeof(savedWord)) {
      memcpy(savedWord, _confirmedText + wordStart, wordLen);
      savedWord[wordLen] = '\0';
    }

    // Remove the word from confirmed text
    _textLen = wordStart;
    _confirmedText[_textLen] = '\0';
    if (_wordCount > 0) _wordCount--;

    // Auto-switch input language to match the word being re-edited.
    // Save the recovered sequence before clearing old-language state.
    if (wordLang != _lang) {
      uint8_t savedSeq[kMaxSeqLen];
      uint8_t savedSeqLen = seqIdx;
      memcpy(savedSeq, _sequence, seqIdx);

      _lang = wordLang;
      clearSequence();  // clear old-language state (candidates, buf)
      if (_dict) {
        _dict->close();
        _dict.reset();
      }
      if (wordLang != T4Language::DIGIT) {
        loadDictionaryForLanguage(wordLang);
      }
      // Restore the recovered button sequence (cleared by clearSequence above).
      _seqLen = savedSeqLen;
      memcpy(_sequence, savedSeq, savedSeqLen);
    }

    // Navigate the (possibly new) dictionary with the recovered sequence.
    if (_dict) {
      _dict->reset();
      for (uint8_t i = 0; i < _seqLen; i++) {
        _dict->pressButton(_sequence[i]);
      }
      _candidatesLoaded = _dict->loadCandidates();
      _candidateCount = _dict->getCandidateCount();
    }

    // Restore the previously selected candidate index by matching the
    // saved word against the reloaded candidate list.
    _candidateIndex = 0;
    if (savedWord[0] != '\0') {
      for (uint16_t i = 0; i < _candidateCount; i++) {
        const char* cand = _dict->getCandidate(i);
        if (cand && strcmp(cand, savedWord) == 0) {
          _candidateIndex = i;
          break;
        }
      }
    }
    _candidateBuf[0] = '\0';
    _savedCandidate[0] = '\0';
    _savedSeqLen = 0;
    return;
  }

  // Delete one UTF-8 character (may be 1–4 bytes)
  if (_textLen == 0) return;
  // Find start byte of last UTF-8 character
  uint8_t blen = 1;
  uint16_t startPos = _textLen - 1;
  while (startPos > 0 && (_confirmedText[startPos] & 0xC0) == 0x80) {
    startPos--;
    blen++;
  }
  _textLen = startPos;
  _confirmedText[startPos] = '\0';
  return;
}

// ── poll ────────────────────────────────────────────────────────────────

template <typename Dict>
void T4InputEngine<Dict>::poll(uint32_t nowMs) {
  if (_mode != T4Mode::MULTI_TAP) return;
  if (_activeButton == 0) return;

  if (_lastTapTime == 0) {
    _lastTapTime = nowMs;
    return;
  }

  if (nowMs - _lastTapTime >= kMultiTapTimeoutMs) {
    fixMultiTapLetter();
  }
}

// ── reset ───────────────────────────────────────────────────────────────

template <typename Dict>
void T4InputEngine<Dict>::reset() {
  LOG_DBG("T4", "reset: textLen=%u seqLen=%u", _textLen, _seqLen);
  clearSequence();
  _confirmedText[0] = '\0';
  _textLen = 0;
  _wordCount = 0;
  _activeButton = 0;
  _tapIndex = 0;
  _shiftLevel = 0;
  if (_dict) _dict->reset();
}

// ── fixMultiTapLetter (private) ─────────────────────────────────────────

template <typename Dict>
void T4InputEngine<Dict>::fixMultiTapLetter() {
  if (_activeButton == 0) return;
  uint8_t blen;
  const char* letter = getGroupLetter(_lang, _activeButton, _tapIndex, blen);
  if (letter && blen > 0 && _textLen + blen <= kMaxTextLen) {
    // SOH sentinel (\x01) = newline action
    if (blen == 1 && *letter == '\x01') {
      _confirmedText[_textLen++] = '\n';
      _confirmedText[_textLen] = '\0';
      LOG_DBG("T4", "fixMultiTapLetter: newline");
    } else {
      // Apply uppercase when Shift/Caps is active (no-op for symbols).
      char upper[4];
      uint8_t writeLen = blen;
      const char* src = letter;
      if (_shiftLevel != 0) {
        writeLen = upperLetterUtf8(letter, blen, upper);
        src = upper;
      }
      if (_textLen + writeLen <= kMaxTextLen) {
        memcpy(_confirmedText + _textLen, src, writeLen);
        _textLen += writeLen;
        _confirmedText[_textLen] = '\0';
        LOG_DBG("T4", "fixMultiTapLetter: blen=%u shift=%u -> text[%u]", writeLen, _shiftLevel, _textLen - writeLen);
      }
      // One-shot Shift resets after a single fixed letter.
      if (_shiftLevel == 1) _shiftLevel = 0;
    }
  }
  _activeButton = 0;
  _tapIndex = 0;
}

}  // namespace t4
