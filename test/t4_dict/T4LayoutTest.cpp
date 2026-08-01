#include <gtest/gtest.h>

#include <cstring>
#include <set>

#include "T4Dict/T4Layout.h"

using namespace t4;

// ── Button range validation ─────────────────────────────────────────────

TEST(T4Layout, GetGroupInvalidButton) {
  EXPECT_EQ(getGroup(T4Language::EN, 0), nullptr);
  EXPECT_EQ(getGroup(T4Language::EN, 5), nullptr);
  EXPECT_EQ(getGroup(T4Language::RU, 0), nullptr);
  EXPECT_EQ(getGroup(T4Language::DIGIT, 5), nullptr);
}

TEST(T4Layout, GetGroupLengthInvalidButton) {
  EXPECT_EQ(getGroupLength(T4Language::EN, 0), 0);
  EXPECT_EQ(getGroupLength(T4Language::EN, 5), 0);
}

TEST(T4Layout, GetGroupLetterInvalidInput) {
  uint8_t blen;
  EXPECT_EQ(getGroupLetter(T4Language::EN, 0, 0, blen), nullptr);
  EXPECT_EQ(getGroupLetter(T4Language::EN, 1, 99, blen), nullptr);
}

// ── English alphabet coverage ───────────────────────────────────────────

TEST(T4Layout, EnglishAllLettersCovered) {
  std::set<char> found;
  for (uint8_t btn = 1; btn <= 4; ++btn) {
    const char* group = getGroup(T4Language::EN, btn);
    ASSERT_NE(group, nullptr);
    for (const char* p = group; *p; ++p) {
      if (*p >= 'a' && *p <= 'z') found.insert(*p);
    }
  }
  EXPECT_EQ(found.size(), 26u) << "All 26 English letters must be covered";
  for (char c = 'a'; c <= 'z'; ++c) {
    EXPECT_TRUE(found.count(c)) << "Missing letter: " << c;
  }
}

TEST(T4Layout, EnglishNoOverlap) {
  for (uint8_t b1 = 1; b1 <= 4; ++b1) {
    for (uint8_t b2 = b1 + 1; b2 <= 4; ++b2) {
      const char* g1 = getGroup(T4Language::EN, b1);
      const char* g2 = getGroup(T4Language::EN, b2);
      for (const char* p = g1; *p; ++p) {
        EXPECT_EQ(strchr(g2, *p), nullptr)
            << "Letter '" << *p << "' appears in both groups " << (int)b1 << " and " << (int)b2;
      }
    }
  }
}

TEST(T4Layout, EnglishGroupSizes) {
  EXPECT_EQ(getGroupLength(T4Language::EN, 1), 7u);  // abcdef'
  EXPECT_EQ(getGroupLength(T4Language::EN, 2), 7u);  // ghijkl-
  EXPECT_EQ(getGroupLength(T4Language::EN, 3), 7u);  // mnopqrs
  EXPECT_EQ(getGroupLength(T4Language::EN, 4), 7u);  // tuvwxyz
}

TEST(T4Layout, EnglishGetGroupLetter) {
  uint8_t blen;
  const char* ptr;
  // btn1: abcdef'
  ptr = getGroupLetter(T4Language::EN, 1, 0, blen);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(blen, 1u);
  EXPECT_EQ(*ptr, 'a');
  ptr = getGroupLetter(T4Language::EN, 1, 1, blen);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 'b');
  ptr = getGroupLetter(T4Language::EN, 1, 5, blen);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 'f');
  ptr = getGroupLetter(T4Language::EN, 1, 6, blen);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(blen, 1u);
  EXPECT_EQ(*ptr, '\'');
  // btn4: tuvwxyz
  ptr = getGroupLetter(T4Language::EN, 4, 0, blen);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 't');
  ptr = getGroupLetter(T4Language::EN, 4, 6, blen);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 'z');
}

// ── Russian alphabet coverage ───────────────────────────────────────────

/// Count UTF-8 code points (not bytes) in a null-terminated string.
static size_t utf8CodePointCount(const char* s) {
  size_t count = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
    // Continuation bytes (10xxxxxx) do not start a new code point.
    if ((*p & 0xC0) != 0x80) ++count;
  }
  return count;
}

/// Extract the nth UTF-8 code point (0-indexed) as a 5-byte buffer.
/// Returns empty string if out of range.
static std::string utf8CodePointAt(const char* s, size_t n) {
  size_t current = 0;
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    if ((*p & 0xC0) != 0x80) {  // start of code point
      if (current == n) {
        const unsigned char* start = p;
        ++p;
        while (*p && (*p & 0xC0) == 0x80) ++p;  // skip continuation bytes
        return std::string((const char*)start, (const char*)p);
      }
      ++current;
    }
    ++p;
  }
  return {};
}

TEST(T4Layout, RussianAllLettersCovered) {
  // RU group 1: 9 code points (абвгдеёж-), groups 2-3: 8, group 4: 9
  EXPECT_EQ(utf8CodePointCount(getGroup(T4Language::RU, 1)), 9u);
  EXPECT_EQ(utf8CodePointCount(getGroup(T4Language::RU, 2)), 8u);
  EXPECT_EQ(utf8CodePointCount(getGroup(T4Language::RU, 3)), 8u);
  EXPECT_EQ(utf8CodePointCount(getGroup(T4Language::RU, 4)), 9u);

  // Collect all code points across all groups.
  std::set<std::string> all;
  for (uint8_t btn = 1; btn <= 4; ++btn) {
    const char* group = getGroup(T4Language::RU, btn);
    ASSERT_NE(group, nullptr);
    size_t count = utf8CodePointCount(group);
    for (size_t i = 0; i < count; ++i) {
      all.insert(utf8CodePointAt(group, i));
    }
  }
  // 34 code points: 33 Russian letters + '-' (shared with EN layout)
  EXPECT_EQ(all.size(), 34u);
}

TEST(T4Layout, RussianNoOverlap) {
  // Check no code point appears in more than one group.
  std::set<std::string> seen;
  for (uint8_t btn = 1; btn <= 4; ++btn) {
    const char* group = getGroup(T4Language::RU, btn);
    ASSERT_NE(group, nullptr);
    size_t count = utf8CodePointCount(group);
    for (size_t i = 0; i < count; ++i) {
      auto cp = utf8CodePointAt(group, i);
      EXPECT_EQ(seen.count(cp), 0u) << "Code point appears in multiple RU groups: " << cp;
      seen.insert(cp);
    }
  }
}

TEST(T4Layout, RussianGroupSizes) {
  EXPECT_EQ(getGroupLength(T4Language::RU, 1), 9u);  // абвгдеёж-
  EXPECT_EQ(getGroupLength(T4Language::RU, 2), 8u);
  EXPECT_EQ(getGroupLength(T4Language::RU, 3), 8u);
  EXPECT_EQ(getGroupLength(T4Language::RU, 4), 9u);
}

// ── Digit groups ────────────────────────────────────────────────────────

TEST(T4Layout, DigitGroupsCorrect) {
  EXPECT_STREQ(getGroup(T4Language::DIGIT, 1), "123'@_()\"[]");
  EXPECT_STREQ(getGroup(T4Language::DIGIT, 2), "456-#/*+&$%");
  EXPECT_STREQ(getGroup(T4Language::DIGIT, 3), "789=<>\\^`{}");
  // Group 4 ends with \x01 sentinel for newline (displayed as ↵)
  EXPECT_STREQ(getGroup(T4Language::DIGIT, 4), "0.,!?:;|~\x01");
}

TEST(T4Layout, DigitGroupSizes) {
  EXPECT_EQ(getGroupLength(T4Language::DIGIT, 1), 11u);
  EXPECT_EQ(getGroupLength(T4Language::DIGIT, 2), 10u);
  EXPECT_EQ(getGroupLength(T4Language::DIGIT, 3), 10u);
  EXPECT_EQ(getGroupLength(T4Language::DIGIT, 4), 10u);  // incl. \x01 sentinel
}

TEST(T4Layout, DigitNoOverlap) {
  for (uint8_t b1 = 1; b1 <= 4; ++b1) {
    for (uint8_t b2 = b1 + 1; b2 <= 4; ++b2) {
      const char* g1 = getGroup(T4Language::DIGIT, b1);
      const char* g2 = getGroup(T4Language::DIGIT, b2);
      for (const char* p = g1; *p; ++p) {
        EXPECT_EQ(strchr(g2, *p), nullptr)
            << "Char '" << *p << "' appears in both DIGIT groups " << (int)b1 << " and " << (int)b2;
      }
    }
  }
}

// ── Language metadata ───────────────────────────────────────────────────

TEST(T4Layout, LanguageCodes) {
  EXPECT_STREQ(getLanguageCode(T4Language::EN), "en");
  EXPECT_STREQ(getLanguageCode(T4Language::RU), "ru");
  EXPECT_STREQ(getLanguageCode(T4Language::DIGIT), "12");
}

TEST(T4Layout, LanguageNames) {
  EXPECT_STREQ(getLanguageName(T4Language::EN), "EN");
  EXPECT_STREQ(getLanguageName(T4Language::RU), "RU");
  EXPECT_STREQ(getLanguageName(T4Language::DIGIT), "12");
}

TEST(T4Layout, CycleLanguage) {
  EXPECT_EQ(cycleLanguage(T4Language::EN), T4Language::RU);
  EXPECT_EQ(cycleLanguage(T4Language::RU), T4Language::DIGIT);
  EXPECT_EQ(cycleLanguage(T4Language::DIGIT), T4Language::EN);
}

// ── getGroup returns same pointer for same args ─────────────────────────

TEST(T4Layout, IdempotentGroupPointer) {
  EXPECT_EQ(getGroup(T4Language::EN, 1), getGroup(T4Language::EN, 1));
  EXPECT_EQ(getGroup(T4Language::RU, 3), getGroup(T4Language::RU, 3));
}

// ── upperLetterUtf8 ─────────────────────────────────────────────────────

namespace {
// Uppercase helper returning the result as a std::string for easy comparison.
std::string upper(const char* in) {
  char out[5] = {};
  uint8_t inLen = 1;
  unsigned char c0 = static_cast<unsigned char>(in[0]);
  if ((c0 & 0xE0) == 0xC0)
    inLen = 2;
  else if ((c0 & 0xF0) == 0xE0)
    inLen = 3;
  else if ((c0 & 0xF8) == 0xF0)
    inLen = 4;
  uint8_t outLen = upperLetterUtf8(in, inLen, out);
  return std::string(out, outLen);
}
}  // namespace

TEST(T4Layout, UpperEnglishLetters) {
  EXPECT_EQ(upper("a"), "A");
  EXPECT_EQ(upper("z"), "Z");
  EXPECT_EQ(upper("m"), "M");
}

TEST(T4Layout, UpperEnglishNonLettersUnchanged) {
  EXPECT_EQ(upper("A"), "A");  // already upper
  EXPECT_EQ(upper("'"), "'");
  EXPECT_EQ(upper("-"), "-");
  EXPECT_EQ(upper("5"), "5");
  EXPECT_EQ(upper("@"), "@");
}

TEST(T4Layout, UpperRussianLetters) {
  EXPECT_EQ(upper("а"), "А");  // U+0430 → U+0410
  EXPECT_EQ(upper("я"), "Я");  // U+044F → U+042F
  EXPECT_EQ(upper("п"), "П");  // spans the 0xD0/0xD1 lead-byte boundary
  EXPECT_EQ(upper("р"), "Р");
  EXPECT_EQ(upper("ё"), "Ё");  // U+0451 → U+0401
}

TEST(T4Layout, UpperRussianAlreadyUpperUnchanged) {
  EXPECT_EQ(upper("А"), "А");
  EXPECT_EQ(upper("Ё"), "Ё");
}

TEST(T4Layout, UpperEmptyInput) {
  char out[5] = {};
  EXPECT_EQ(upperLetterUtf8(nullptr, 0, out), 0u);
  EXPECT_EQ(upperLetterUtf8("a", 0, out), 0u);
}

TEST(T4Layout, UpperPreservesByteLength) {
  char out[5] = {};
  EXPECT_EQ(upperLetterUtf8("a", 1, out), 1u);
  EXPECT_EQ(upperLetterUtf8("я", 2, out), 2u);  // Russian stays 2 bytes
}

TEST(T4Layout, UpperUnmappedMultiByteUnchanged) {
  // Unsupported scripts / symbols round-trip through the generic UTF-8
  // decode/encode path without modification (byte length preserved).
  EXPECT_EQ(upper("à"), "à");    // Latin-1 Supplement (2 bytes) — not yet mapped
  EXPECT_EQ(upper("€"), "€");    // U+20AC euro sign (3 bytes)
  EXPECT_EQ(upper("😀"), "😀");  // U+1F600 (4 bytes)

  char out[5] = {};
  EXPECT_EQ(upperLetterUtf8("€", 3, out), 3u);
  EXPECT_EQ(upperLetterUtf8("😀", 4, out), 4u);
}

// ── buttonForLetter ─────────────────────────────────────────────────────

TEST(T4Layout, ButtonForLetterLowercase) {
  // EN: btn1=abcdef', btn2=ghijkl-, btn3=mnopqrs, btn4=tuvwxyz
  EXPECT_EQ(buttonForLetter(T4Language::EN, "a", 1), 1u);
  EXPECT_EQ(buttonForLetter(T4Language::EN, "g", 1), 2u);
  EXPECT_EQ(buttonForLetter(T4Language::EN, "m", 1), 3u);
  EXPECT_EQ(buttonForLetter(T4Language::EN, "z", 1), 4u);
  EXPECT_EQ(buttonForLetter(T4Language::RU, "а", 2), 1u);
}

TEST(T4Layout, ButtonForLetterCaseInsensitive) {
  // A capitalized letter must map to the same button as its lowercase form,
  // so a committed uppercase word can be pulled back for re-editing.
  EXPECT_EQ(buttonForLetter(T4Language::EN, "A", 1), 1u);
  EXPECT_EQ(buttonForLetter(T4Language::EN, "M", 1), 3u);
  EXPECT_EQ(buttonForLetter(T4Language::EN, "Z", 1), 4u);
  EXPECT_EQ(buttonForLetter(T4Language::RU, "А", 2), 1u);  // uppercase Cyrillic
  EXPECT_EQ(buttonForLetter(T4Language::RU, "Ё", 2), 1u);  // Ё (ё is in btn1)
}

TEST(T4Layout, ButtonForLetterNonLetter) {
  EXPECT_EQ(buttonForLetter(T4Language::EN, "5", 1), 0u);
  EXPECT_EQ(buttonForLetter(T4Language::EN, "@", 1), 0u);
  EXPECT_EQ(buttonForLetter(T4Language::EN, nullptr, 1), 0u);
  EXPECT_EQ(buttonForLetter(T4Language::EN, "a", 0), 0u);
}
