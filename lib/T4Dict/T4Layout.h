#pragma once

#include <cstdint>

namespace t4 {

/// Input language for T4 predictive text. The keyboard always cycles through
/// exactly three slots: English → Additional → Digits. The ADDITIONAL slot is
/// user-configurable (System settings ▸ Additional Keyboard Layout) and backed
/// by one of the registered additional layouts (default: Russian). English and
/// Digits are fixed.
enum class T4Language : uint8_t {
  EN = 0,          ///< English (fixed)
  ADDITIONAL = 1,  ///< Configurable additional layout (default: Russian)
  DIGIT = 2,       ///< Digits + symbols (fixed)
};

/// Input mode for T4 keyboard.
enum class T4Mode : uint8_t {
  PREDICT = 0,    ///< Predictive word input (T4)
  MULTI_TAP = 1,  ///< Multi-tap letter-by-letter
};

/// Return the letter group string for a given language and 1-based button
/// index (1–4). Returns nullptr if button is out of range.
const char* getGroup(T4Language lang, uint8_t button);

/// Return the number of letters in a group. Returns 0 for invalid input.
uint8_t getGroupLength(T4Language lang, uint8_t button);

/// Return pointer to the nth letter (0-indexed character count) within
/// a group string. outByteLen receives the UTF-8 byte length (1–4).
/// Returns nullptr if index is out of range.
const char* getGroupLetter(T4Language lang, uint8_t button, uint8_t index, uint8_t& outByteLen);

/// Return the 1-based button (1–4) whose letter group contains @p letter,
/// matching case-insensitively (an uppercased letter matches its lowercase
/// group entry). @p byteLen is the letter's UTF-8 byte length (1–4). Returns
/// 0 if the letter is not part of any group for @p lang. Used to recover the
/// button sequence of an already-typed word (which may be capitalized).
uint8_t buttonForLetter(T4Language lang, const char* letter, uint8_t byteLen);

/// Uppercase a single UTF-8 letter (1–4 bytes). Case mapping is data-driven
/// (see kCaseRanges / kCaseExceptions in T4Layout.cpp); currently English
/// (a–z) and Russian (а–я, ё) are mapped. Any code point without a mapping
/// (symbols, digits, already-upper letters, unsupported scripts) is copied
/// unchanged. Writes up to 4 bytes into @p out (which must have room) and
/// returns the written byte length. @p in points to the letter, @p inLen is
/// its UTF-8 byte length (1–4). To add a language, extend the tables in the
/// .cpp — no change to this API is needed.
uint8_t upperLetterUtf8(const char* in, uint8_t inLen, char* out);

/// Lowercase a single UTF-8 letter (1–4 bytes) — the inverse of
/// upperLetterUtf8(), driven by the same tables. Any code point without a
/// mapping is copied unchanged. Used to store user-lexicon words in a
/// canonical form so "Hello" and "hello" share one entry.
uint8_t lowerLetterUtf8(const char* in, uint8_t inLen, char* out);

/// Return a 2-character ISO language code for the given language.
const char* getLanguageCode(T4Language lang);

/// Return a human-readable language name (e.g., "EN", "RU", "12").
const char* getLanguageName(T4Language lang);

/// Cycle to the next language: EN → ADDITIONAL → DIGIT → EN.
T4Language cycleLanguage(T4Language current);

/// Return the SD-card path to the predictive dictionary for @p lang, or
/// nullptr if the language has no dictionary (e.g. DIGIT). The presence of
/// this file on the SD card determines whether predictive input is available
/// for the language.
const char* getDictionaryPath(T4Language lang);

// ── Additional (configurable) keyboard layouts ──────────────────────────
//
// The ADDITIONAL cycle slot resolves to whichever layout is currently active.
// These accessors let the settings UI enumerate the choices and select one;
// getGroup/getGroupLength/getLanguageCode/getLanguageName/getDictionaryPath
// for T4Language::ADDITIONAL all follow the active selection. When no layout
// is active (kNoAdditionalLayout), the ADDITIONAL slot is skipped and the
// cycle is English -> Digits only.

/// Sentinel value for setActiveAdditionalLayout / getActiveAdditionalLayout /
/// CrossPointSettings::t4AdditionalLayout meaning "no additional layout
/// selected" — the ADDITIONAL cycle slot is skipped.
constexpr uint8_t kNoAdditionalLayout = 0xFF;

/// Number of additional layouts the firmware ships letter tables for.
uint8_t getAdditionalLayoutCount();

/// T4 language code (2-char lowercase, e.g. "ru") for the additional layout at
/// @p index — also the dictionary filename stem. Returns "" if out of range.
const char* getAdditionalLayoutCode(uint8_t index);

/// i18n ISO code (e.g. "RU") for the additional layout at @p index, used to
/// look up a localized display name. Returns "" if out of range.
const char* getAdditionalLayoutI18nCode(uint8_t index);

/// Select the active additional layout (the middle cycle slot), or
/// kNoAdditionalLayout to disable it. Other out-of-range values are ignored.
void setActiveAdditionalLayout(uint8_t index);

/// Index of the currently active additional layout, or kNoAdditionalLayout
/// when none is selected.
uint8_t getActiveAdditionalLayout();

/// True when an additional layout is active (the ADDITIONAL slot participates
/// in the cycle). False when kNoAdditionalLayout is selected.
bool hasActiveAdditionalLayout();

/// Find an additional layout index by its T4 code (e.g. "ru"). Returns
/// kNoAdditionalLayout when @p code is null, empty, or not a known layout.
uint8_t additionalLayoutIndexForCode(const char* code);

/// Return the number of supported languages.
constexpr uint8_t kLanguageCount = 3;

// ── Sentence-level metadata ───────────────────────────────────────────
//
// Per-language sentence-handling rules.  Used by the T4 keyboard to
// auto-capitalise the next word after a sentence-ending punctuation
// character (for languages that have letter case), and to skip
// auto-capitalisation for case-less scripts (Hebrew, Arabic) and
// non-linguistic input (DIGIT).

/// Per-language sentence-handling configuration.
struct SentenceConfig {
  /// True when sentences conventionally start with an uppercase letter.
  /// False for case-less scripts (Hebrew, Arabic) and non-linguistic
  /// layouts (DIGIT).
  bool autoCapitalize = true;

  /// Null-terminated **UTF-8** string of sentence-ending characters
  /// (e.g. ".!?").  Each character may be 1–4 bytes.  The keyboard
  /// checks this set when punctuation is committed; if the committed
  /// character is in this set the next word is auto-capitalised
  /// (provided autoCapitalize is true).  The trailing space that the
  /// keyboard appends after punctuation is handled by the caller.
  const char* endChars = ".!?";
};

/// Check whether a single UTF-8 character (at @p ch, @p byteLen bytes)
/// is one of the sentence-ending characters in @p endChars.
/// endChars is walked by complete UTF-8 code points, not bytes.
inline bool isSentenceEndChar(const char* endChars, const char* ch, uint8_t byteLen) {
  if (!endChars || !ch || byteLen == 0) return false;
  const char* p = endChars;
  while (*p) {
    unsigned char c0 = static_cast<unsigned char>(*p);
    uint8_t len = 1;
    if ((c0 & 0xE0) == 0xC0)
      len = 2;
    else if ((c0 & 0xF0) == 0xE0)
      len = 3;
    else if ((c0 & 0xF8) == 0xF0)
      len = 4;
    if (len == byteLen && __builtin_memcmp(p, ch, byteLen) == 0) return true;
    p += len;
  }
  return false;
}

/// Return the sentence configuration for @p lang. Never null.
const SentenceConfig* getSentenceConfig(T4Language lang);

}  // namespace t4
