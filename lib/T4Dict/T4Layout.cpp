#include "T4Layout.h"

#include <Logging.h>

namespace t4 {

namespace {

// ── English letter groups (T9 merged to 4 buttons) ─────────────────────
// btn1: abc + def = abcdef
// btn2: ghi + jkl = ghijkl
// btn3: mno + pqrs = mnopqrs
// btn4: tuv + wxyz = tuvwxyz
static constexpr const char* kEnGroups[4] = {
    "abcdef'",
    "ghijkl-",
    "mnopqrs",
    "tuvwxyz",
};

static constexpr uint8_t kEnGroupLens[4] = {7, 7, 7, 7};

// ── Russian letter groups ───────────────────────────────────────────────
static constexpr const char* kRuGroups[4] = {
    "абвгдеёж-",
    "зийклмно",
    "прстуфхц",
    "чшщъыьэюя",
};

static constexpr uint8_t kRuGroupLens[4] = {9, 8, 8, 9};

// ── Digit groups ────────────────────────────────────────────────────────
static constexpr const char* kDigitGroups[4] = {
    "123'@_()\"[]",
    "456-#/*+&$%",
    "789=<>\\^`{}",
    "0.,!?:;|~",
};

static constexpr uint8_t kDigitGroupLens[4] = {11, 10, 10, 9};

// ── Language metadata ───────────────────────────────────────────────────
static constexpr const char* kLangCodes[3] = {"en", "ru", "12"};
static constexpr const char* kLangNames[3] = {"EN", "RU", "12"};

}  // namespace

const char* getGroup(T4Language lang, uint8_t button) {
  LOG_DBG("T4", "getGroup: lang=%d btn=%u", static_cast<int>(lang), button);
  if (button < 1 || button > 4) return nullptr;
  uint8_t idx = button - 1;
  switch (lang) {
    case T4Language::EN:    return kEnGroups[idx];
    case T4Language::RU:    return kRuGroups[idx];
    case T4Language::DIGIT: return kDigitGroups[idx];
  }
  return nullptr;
}

uint8_t getGroupLength(T4Language lang, uint8_t button) {
  LOG_DBG("T4", "getGroupLength: lang=%d btn=%u", static_cast<int>(lang), button);
  if (button < 1 || button > 4) return 0;
  uint8_t idx = button - 1;
  switch (lang) {
    case T4Language::EN:    return kEnGroupLens[idx];
    case T4Language::RU:    return kRuGroupLens[idx];
    case T4Language::DIGIT: return kDigitGroupLens[idx];
  }
  return 0;
}

const char* getGroupLetter(T4Language lang, uint8_t button, uint8_t index, uint8_t& outByteLen) {
  const char* group = getGroup(lang, button);
  if (!group) { outByteLen = 0; return nullptr; }
  uint8_t charCount = getGroupLength(lang, button);
  if (index >= charCount) { outByteLen = 0; return nullptr; }

  // Walk through UTF-8 characters to find the index-th one
  const char* p = group;
  for (uint8_t i = 0; i < index; i++) {
    unsigned char c0 = static_cast<unsigned char>(*p);
    if ((c0 & 0xE0) == 0xC0)
      p += 2;
    else if ((c0 & 0xF0) == 0xE0)
      p += 3;
    else if ((c0 & 0xF8) == 0xF0)
      p += 4;
    else
      p += 1;
  }

  unsigned char c0 = static_cast<unsigned char>(*p);
  if ((c0 & 0xE0) == 0xC0)
    outByteLen = 2;
  else if ((c0 & 0xF0) == 0xE0)
    outByteLen = 3;
  else if ((c0 & 0xF8) == 0xF0)
    outByteLen = 4;
  else
    outByteLen = 1;

  LOG_DBG("T4", "getGroupLetter: lang=%d btn=%u idx=%u blen=%u",
          static_cast<int>(lang), button, index, outByteLen);
  return p;
}

const char* getLanguageCode(T4Language lang) {
  auto i = static_cast<uint8_t>(lang);
  return (i < 3) ? kLangCodes[i] : "";
}

const char* getLanguageName(T4Language lang) {
  auto i = static_cast<uint8_t>(lang);
  return (i < 3) ? kLangNames[i] : "";
}

T4Language cycleLanguage(T4Language current) {
  auto i = static_cast<uint8_t>(current);
  return static_cast<T4Language>((i + 1) % 3);
}

}  // namespace t4
