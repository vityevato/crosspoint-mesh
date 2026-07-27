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
  /// When entering COMMAND, stores current mode in _prevMode for return.
  void setMode(T4Mode mode);

  /// Current mode.
  T4Mode getMode() const;

  // ── Input ─────────────────────────────────────────────────────────

  /// Handle a button press (1–4) according to current mode.
  /// In PREDICT: appends to sequence, navigates trie, loads candidates.
  /// In MULTI_TAP: cycles letter within group or fixes + starts new.
  /// In COMMAND: ignored (command mode has its own button handlers).
  /// @return true if the press produced a valid result.
  bool pressButton(uint8_t btn);

  // ── Candidate navigation (Predict mode) ───────────────────────────

  /// Cycle to the next candidate. Wraps around after last.
  void cycleCandidate();

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
  /// sequence). In Multi-tap: deletes last fixed letter. In COMMAND:
  /// delegates to the mode stored in _prevMode.
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
  static constexpr uint16_t kMaxTextLen = 50;

 private:
  static constexpr uint8_t kMaxSeqLen = 31;

  std::unique_ptr<Dict> _dict;
  T4Language _lang = T4Language::EN;
  T4Mode _mode = T4Mode::PREDICT;
  T4Mode _prevMode = T4Mode::PREDICT;

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

  // Confirmed text
  char _confirmedText[kMaxTextLen + 1] = {};
  uint16_t _textLen = 0;

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
    return;
  }
  auto len = strlen(text);
  if (len > kMaxTextLen) len = kMaxTextLen;
  memcpy(_confirmedText, text, len);
  _confirmedText[len] = '\0';
  _textLen = static_cast<uint16_t>(len);
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
  switch (lang) {
    case T4Language::EN:
      return "/.crosspoint/dicts/en.trie";
    case T4Language::RU:
      return "/.crosspoint/dicts/ru.trie";
    case T4Language::DIGIT:
      return nullptr;  // no dictionary for digits
  }
  return nullptr;
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
  if (_activeButton == 0) { outByteLen = 0; return nullptr; }
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

  // Entering COMMAND — remember where we came from
  if (mode == T4Mode::COMMAND && _mode != T4Mode::COMMAND) {
    _prevMode = _mode;
  }

  // Exiting COMMAND — restore previous mode
  if (_mode == T4Mode::COMMAND && mode != T4Mode::COMMAND) {
    _mode = mode;  // caller explicitly set mode, honor it
    return;
  }

  _mode = mode;

  // Discard PREDICT navigation state when leaving PREDICT mode —
  // avoids stale candidates leaking into other modes' rendering.
  if (_mode != T4Mode::PREDICT) {
    clearSequence();
    if (_dict) _dict->reset();
  }

  // Fix any in-progress multi-tap letter on mode switch away from
  // MULTI_TAP, but not when entering COMMAND (preserve state for return).
  if (_activeButton != 0 && mode != T4Mode::MULTI_TAP && mode != T4Mode::COMMAND) {
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

    // Save current candidate before navigating further (dead-end fallback)
    if (_candidateCount > 0 && _candidatesLoaded && _dict) {
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

  // COMMAND mode — button presses not handled here (UI layer)
  return false;
}

// ── cycleCandidate ──────────────────────────────────────────────────────

template <typename Dict>
void T4InputEngine<Dict>::cycleCandidate() {
  if (_candidateCount == 0) return;
  _candidateIndex = (_candidateIndex + 1) % _candidateCount;
  LOG_DBG("T4", "cycleCandidate: %u/%u", _candidateIndex, _candidateCount);
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
    }
    return;
  }
}

// ── backspace ───────────────────────────────────────────────────────────

template <typename Dict>
void T4InputEngine<Dict>::backspace() {
  T4Mode effectiveMode = _mode;
  if (_mode == T4Mode::COMMAND) {
    effectiveMode = _prevMode;
  }
  LOG_DBG("T4", "backspace: mode=%d effective=%d textLen=%u", static_cast<int>(_mode), static_cast<int>(effectiveMode),
          _textLen);

  if (effectiveMode == T4Mode::PREDICT) {
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

    // Check if last char is a letter from any group — if not, it's punct/space
    const char* lastCharPtr = _confirmedText + lastStart;
    bool isLetter = false;
    for (uint8_t btn = 1; btn <= 4; btn++) {
      uint8_t groupLen = getGroupLength(_lang, btn);
      for (uint8_t idx = 0; idx < groupLen; idx++) {
        uint8_t charBlen;
        const char* groupChar = getGroupLetter(_lang, btn, idx, charBlen);
        if (groupChar && charBlen == blen && memcmp(groupChar, lastCharPtr, blen) == 0) {
          isLetter = true;
          break;
        }
      }
      if (isLetter) break;
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
      // Find start byte of the character before wordStart
      uint8_t prevBlen = 1;
      uint16_t prevStart = wordStart - 1;
      while (prevStart > 0 && (_confirmedText[prevStart] & 0xC0) == 0x80) {
        prevStart--;
        prevBlen++;
      }
      const char* prevChar = _confirmedText + prevStart;

      // Is it a letter?
      bool prevIsLetter = false;
      for (uint8_t btn = 1; btn <= 4; btn++) {
        uint8_t groupLen = getGroupLength(_lang, btn);
        for (uint8_t idx = 0; idx < groupLen; idx++) {
          uint8_t charBlen;
          const char* gc = getGroupLetter(_lang, btn, idx, charBlen);
          if (gc && charBlen == prevBlen && memcmp(gc, prevChar, prevBlen) == 0) {
            prevIsLetter = true;
            break;
          }
        }
        if (prevIsLetter) break;
      }
      if (!prevIsLetter) break;
      wordStart -= prevBlen;
    }

    // Extract word and its button sequence
    uint8_t seqIdx = 0;
    uint16_t pos = wordStart;
    while (pos < _textLen && seqIdx < kMaxSeqLen) {
      // Determine UTF-8 byte length from the start byte
      uint8_t charBlen = 1;
      unsigned char firstByte = static_cast<unsigned char>(_confirmedText[pos]);
      if ((firstByte & 0xE0) == 0xC0)
        charBlen = 2;
      else if ((firstByte & 0xF0) == 0xE0)
        charBlen = 3;
      else if ((firstByte & 0xF8) == 0xF0)
        charBlen = 4;

      // Find the button that produces this character
      bool found = false;
      for (uint8_t btn = 1; btn <= 4; btn++) {
        uint8_t groupLen = getGroupLength(_lang, btn);
        for (uint8_t idx = 0; idx < groupLen; idx++) {
          uint8_t gcBlen;
          const char* gc = getGroupLetter(_lang, btn, idx, gcBlen);
          if (gc && gcBlen == charBlen && memcmp(gc, _confirmedText + pos, charBlen) == 0) {
            _sequence[seqIdx++] = btn;
            found = true;
            break;
          }
        }
        if (found) break;
      }
      if (!found) break;  // shouldn't happen for keyboard-typed text
      pos += charBlen;
    }
    _seqLen = seqIdx;

    // Remove the word from confirmed text
    _textLen = wordStart;
    _confirmedText[_textLen] = '\0';

    // Navigate dictionary with the recovered sequence
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
  _activeButton = 0;
  _tapIndex = 0;
  if (_dict) _dict->reset();
}

// ── fixMultiTapLetter (private) ─────────────────────────────────────────

template <typename Dict>
void T4InputEngine<Dict>::fixMultiTapLetter() {
  if (_activeButton == 0) return;
  uint8_t blen;
  const char* letter = getGroupLetter(_lang, _activeButton, _tapIndex, blen);
  if (letter && blen > 0 && _textLen + blen <= kMaxTextLen) {
    memcpy(_confirmedText + _textLen, letter, blen);
    _textLen += blen;
    _confirmedText[_textLen] = '\0';
    LOG_DBG("T4", "fixMultiTapLetter: blen=%u -> text[%u]", blen, _textLen - blen);
  }
  _activeButton = 0;
  _tapIndex = 0;
}

}  // namespace t4
