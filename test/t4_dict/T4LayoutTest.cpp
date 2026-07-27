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
      found.insert(*p);
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
            << "Letter '" << *p << "' appears in both groups "
            << (int)b1 << " and " << (int)b2;
      }
    }
  }
}

TEST(T4Layout, EnglishGroupSizes) {
  EXPECT_EQ(getGroupLength(T4Language::EN, 1), 6u);  // abcdef
  EXPECT_EQ(getGroupLength(T4Language::EN, 2), 6u);  // ghijkl
  EXPECT_EQ(getGroupLength(T4Language::EN, 3), 7u);  // mnopqrs
  EXPECT_EQ(getGroupLength(T4Language::EN, 4), 7u);  // tuvwxyz
}

TEST(T4Layout, EnglishGetGroupLetter) {
  uint8_t blen;
  const char* ptr;
  // btn1: abcdef
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
  // Each RU group has 8 letters (UTF-8: 2 bytes each, 16 bytes per group).
  EXPECT_EQ(utf8CodePointCount(getGroup(T4Language::RU, 1)), 8u);
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
  EXPECT_EQ(all.size(), 33u) << "All 33 Russian letters must be covered";
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
      EXPECT_EQ(seen.count(cp), 0u)
          << "Code point appears in multiple RU groups: "
          << cp;
      seen.insert(cp);
    }
  }
}

TEST(T4Layout, RussianGroupSizes) {
  EXPECT_EQ(getGroupLength(T4Language::RU, 1), 8u);
  EXPECT_EQ(getGroupLength(T4Language::RU, 2), 8u);
  EXPECT_EQ(getGroupLength(T4Language::RU, 3), 8u);
  EXPECT_EQ(getGroupLength(T4Language::RU, 4), 9u);
}

// ── Digit groups ────────────────────────────────────────────────────────

TEST(T4Layout, DigitGroupsCorrect) {
  EXPECT_STREQ(getGroup(T4Language::DIGIT, 1), "123");
  EXPECT_STREQ(getGroup(T4Language::DIGIT, 2), "456");
  EXPECT_STREQ(getGroup(T4Language::DIGIT, 3), "789");
  EXPECT_STREQ(getGroup(T4Language::DIGIT, 4), "0.,!?:;");
}

TEST(T4Layout, DigitGroupSizes) {
  EXPECT_EQ(getGroupLength(T4Language::DIGIT, 1), 3u);
  EXPECT_EQ(getGroupLength(T4Language::DIGIT, 2), 3u);
  EXPECT_EQ(getGroupLength(T4Language::DIGIT, 3), 3u);
  EXPECT_EQ(getGroupLength(T4Language::DIGIT, 4), 7u);
}

TEST(T4Layout, DigitNoOverlap) {
  for (uint8_t b1 = 1; b1 <= 4; ++b1) {
    for (uint8_t b2 = b1 + 1; b2 <= 4; ++b2) {
      const char* g1 = getGroup(T4Language::DIGIT, b1);
      const char* g2 = getGroup(T4Language::DIGIT, b2);
      for (const char* p = g1; *p; ++p) {
        EXPECT_EQ(strchr(g2, *p), nullptr)
            << "Char '" << *p << "' appears in both DIGIT groups "
            << (int)b1 << " and " << (int)b2;
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
