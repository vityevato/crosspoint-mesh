#pragma once

#include <cstdint>

namespace t4 {

/// Input language for T4 predictive text.
enum class T4Language : uint8_t {
  EN = 0,    ///< English
  RU = 1,    ///< Russian
  DIGIT = 2, ///< Digits + symbols
};

/// Input mode for T4 keyboard.
enum class T4Mode : uint8_t {
  PREDICT = 0,    ///< Predictive word input (T4)
  MULTI_TAP = 1,  ///< Multi-tap letter-by-letter
  COMMAND = 2,    ///< Command mode (backspace, undo, actions)
};

/// Return the letter group string for a given language and 1-based button
/// index (1–4). Returns nullptr if button is out of range.
const char* getGroup(T4Language lang, uint8_t button);

/// Return the number of letters in a group. Returns 0 for invalid input.
uint8_t getGroupLength(T4Language lang, uint8_t button);

/// Return the nth letter (0-indexed) from a group. Returns '\0' if invalid.
char getGroupLetter(T4Language lang, uint8_t button, uint8_t index);

/// Return a 2-character ISO language code for the given language.
const char* getLanguageCode(T4Language lang);

/// Return a human-readable language name (e.g., "EN", "RU", "12").
const char* getLanguageName(T4Language lang);

/// Cycle to the next language: EN → RU → DIGIT → EN.
T4Language cycleLanguage(T4Language current);

/// Return the number of supported languages.
constexpr uint8_t kLanguageCount = 3;

}  // namespace t4
