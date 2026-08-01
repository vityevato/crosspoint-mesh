#include "T4Layout.h"

#include <cstring>

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
// Group 4 ends with \x01 sentinel: it represents a newline action.
// At insertion time (fixMultiTapLetter) it is translated to '\n'.
// In the letter-block UI it is rendered as "\n" (font-safe ASCII fallback —
// the ↵ (U+21B5) glyph is not present in the builtin Ubuntu/Noto fonts).
static constexpr const char* kDigitGroups[4] = {
    "123'@_()\"[]",
    "456-#/*+&$%",
    "789=<>\\^`{}",
    "0.,!?:;|~\x01",
};

static constexpr uint8_t kDigitGroupLens[4] = {11, 10, 10, 10};

// ── Language metadata ───────────────────────────────────────────────────
static constexpr const char* kLangCodes[3] = {"en", "ru", "12"};
static constexpr const char* kLangNames[3] = {"EN", "RU", "12"};

}  // namespace

const char* getGroup(T4Language lang, uint8_t button) {
  if (button < 1 || button > 4) return nullptr;
  uint8_t idx = button - 1;
  switch (lang) {
    case T4Language::EN:
      return kEnGroups[idx];
    case T4Language::RU:
      return kRuGroups[idx];
    case T4Language::DIGIT:
      return kDigitGroups[idx];
  }
  return nullptr;
}

uint8_t getGroupLength(T4Language lang, uint8_t button) {
  if (button < 1 || button > 4) return 0;
  uint8_t idx = button - 1;
  switch (lang) {
    case T4Language::EN:
      return kEnGroupLens[idx];
    case T4Language::RU:
      return kRuGroupLens[idx];
    case T4Language::DIGIT:
      return kDigitGroupLens[idx];
  }
  return 0;
}

const char* getGroupLetter(T4Language lang, uint8_t button, uint8_t index, uint8_t& outByteLen) {
  const char* group = getGroup(lang, button);
  if (!group) {
    outByteLen = 0;
    return nullptr;
  }
  uint8_t charCount = getGroupLength(lang, button);
  if (index >= charCount) {
    outByteLen = 0;
    return nullptr;
  }

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

  return p;
}

uint8_t buttonForLetter(T4Language lang, const char* letter, uint8_t byteLen) {
  if (!letter || byteLen == 0) return 0;
  for (uint8_t btn = 1; btn <= 4; btn++) {
    uint8_t groupLen = getGroupLength(lang, btn);
    for (uint8_t idx = 0; idx < groupLen; idx++) {
      uint8_t gcLen;
      const char* gc = getGroupLetter(lang, btn, idx, gcLen);
      if (!gc) continue;
      // Direct (lowercase) match.
      if (gcLen == byteLen && memcmp(gc, letter, byteLen) == 0) return btn;
      // Case-insensitive: compare the uppercased group letter.
      char up[4];
      uint8_t upLen = upperLetterUtf8(gc, gcLen, up);
      if (upLen == byteLen && memcmp(up, letter, byteLen) == 0) return btn;
    }
  }
  return 0;
}

namespace {

// ── UTF-8 case mapping (extensible) ─────────────────────────────────────
//
// To support uppercasing for a new language, add its lowercase code-point
// range(s) to kCaseRanges and any irregular letters to kCaseExceptions.
// The only letters that ever reach upperLetterUtf8() are those present in
// the group tables above, so only the scripts used by those groups need
// entries here.
//
// NOTE: locale-specific casing (e.g. Turkish i↔İ / ı↔I) cannot be expressed
// as a single global mapping — revisit this if such a language becomes a
// T4 input language.

// A contiguous block of lowercase code points that uppercase by a fixed
// subtraction (upper = cp - offset).
struct CaseRange {
  uint32_t lo;
  uint32_t hi;
  uint32_t offset;
};

static constexpr CaseRange kCaseRanges[] = {
    {0x0061, 0x007A, 0x20},  // ASCII a–z
    {0x0430, 0x044F, 0x20},  // Cyrillic а–я
};

// Irregular single-letter mappings that don't fit a range offset.
struct CasePair {
  uint32_t lower;
  uint32_t upper;
};

static constexpr CasePair kCaseExceptions[] = {
    {0x0451, 0x0401},  // Cyrillic ё → Ё
};

// Map one Unicode code point to uppercase, or return it unchanged.
uint32_t toUpperCodePoint(uint32_t cp) {
  for (const CasePair& e : kCaseExceptions) {
    if (cp == e.lower) return e.upper;
  }
  for (const CaseRange& r : kCaseRanges) {
    if (cp >= r.lo && cp <= r.hi) return cp - r.offset;
  }
  return cp;
}

// Decode one UTF-8 character of known byte length (1–4) into a code point.
uint32_t decodeUtf8(const char* in, uint8_t inLen) {
  const unsigned char* u = reinterpret_cast<const unsigned char*>(in);
  switch (inLen) {
    case 1:
      return u[0];
    case 2:
      return (static_cast<uint32_t>(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
    case 3:
      return (static_cast<uint32_t>(u[0] & 0x0F) << 12) | (static_cast<uint32_t>(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
    case 4:
      return (static_cast<uint32_t>(u[0] & 0x07) << 18) | (static_cast<uint32_t>(u[1] & 0x3F) << 12) |
             (static_cast<uint32_t>(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
    default:
      return 0;
  }
}

// Encode a code point into UTF-8. Returns the byte length written (1–4).
uint8_t encodeUtf8(uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

}  // namespace

uint8_t upperLetterUtf8(const char* in, uint8_t inLen, char* out) {
  if (!in || inLen == 0) return 0;
  if (inLen > 4) inLen = 4;  // UTF-8 is at most 4 bytes

  uint32_t cp = decodeUtf8(in, inLen);
  uint32_t up = toUpperCodePoint(cp);
  if (up == cp) {
    memcpy(out, in, inLen);  // no case mapping — copy unchanged
    return inLen;
  }
  return encodeUtf8(up, out);
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
