#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "T4Dict/T4Layout.h"
#include "T4Dict/T4UserLexicon.h"

using namespace t4;

namespace {

// English groups: btn1 "abcdef'", btn2 "ghijkl-", btn3 "mnopqrs", btn4 "tuvwxyz"
// "wifi" -> w(4) i(2) f(1) i(2); "tick" -> t(4) i(2) c(1) k(2) — same sequence.
const std::vector<uint8_t> kWifiSeq = {4, 2, 1, 2};

bool learn(T4UserLexicon& lex, const char* word, T4Language lang = T4Language::EN) {
  return lex.learnWord(lang, word, strlen(word));
}

uint8_t match(const T4UserLexicon& lex, const std::vector<uint8_t>& seq, uint16_t* out, uint8_t cap,
              T4Language lang = T4Language::EN) {
  return lex.findMatches(lang, seq.data(), static_cast<uint8_t>(seq.size()), out, cap);
}

}  // namespace

class T4UserLexiconTest : public ::testing::Test {
 protected:
  void SetUp() override { setActiveAdditionalLayout(0); }  // Russian

  T4UserLexicon lex;
  uint16_t found[T4UserLexicon::kMaxMatches] = {};
};

// ── Learning a single word ──────────────────────────────────────────────

TEST_F(T4UserLexiconTest, LearnsWordAndFindsItBySequence) {
  ASSERT_TRUE(learn(lex, "wifi"));
  EXPECT_EQ(lex.getEntryCount(), 1u);
  EXPECT_TRUE(lex.isDirty());

  ASSERT_EQ(match(lex, kWifiSeq, found, T4UserLexicon::kMaxMatches), 1u);
  EXPECT_STREQ(lex.getWord(found[0]), "wifi");
  EXPECT_EQ(lex.getScore(found[0]), 1u);
}

TEST_F(T4UserLexiconTest, MatchingIsExactLengthNotPrefix) {
  ASSERT_TRUE(learn(lex, "wifi"));
  const std::vector<uint8_t> prefix = {4, 2, 1};
  EXPECT_EQ(match(lex, prefix, found, T4UserLexicon::kMaxMatches), 0u);
}

TEST_F(T4UserLexiconTest, NormalizesCaseIntoOneEntry) {
  ASSERT_TRUE(learn(lex, "WiFi"));
  ASSERT_TRUE(learn(lex, "wifi"));
  EXPECT_EQ(lex.getEntryCount(), 1u);

  ASSERT_EQ(match(lex, kWifiSeq, found, T4UserLexicon::kMaxMatches), 1u);
  EXPECT_STREQ(lex.getWord(found[0]), "wifi");
  EXPECT_EQ(lex.getScore(found[0]), 2u);
}

TEST_F(T4UserLexiconTest, RejectsIneligibleWords) {
  EXPECT_FALSE(learn(lex, "a"));                  // shorter than kMinWordChars
  EXPECT_FALSE(learn(lex, "ab3"));                // digits are not letters
  EXPECT_FALSE(learn(lex, "hi!"));                // punctuation is not a letter
  EXPECT_FALSE(learn(lex, ""));                   // empty
  EXPECT_FALSE(learn(lex, "abcdefghijklmnopq"));  // 17 letters > kMaxWordChars
  EXPECT_EQ(lex.getEntryCount(), 0u);
}

TEST_F(T4UserLexiconTest, KeepsLanguagesApart) {
  // "жаргон": ж(1) а(1) р(3) г(1) о(2) н(2)
  ASSERT_TRUE(learn(lex, "жаргон", T4Language::ADDITIONAL));
  const std::vector<uint8_t> seq = {1, 1, 3, 1, 2, 2};

  EXPECT_EQ(match(lex, seq, found, T4UserLexicon::kMaxMatches, T4Language::ADDITIONAL), 1u);
  EXPECT_EQ(match(lex, seq, found, T4UserLexicon::kMaxMatches, T4Language::EN), 0u);
}

TEST_F(T4UserLexiconTest, ClassifiesWordByScriptWhenPreferredLanguageDoesNotFit) {
  // Preferred language is English, but the word only fits the additional
  // layout — it must still be learned, under that layout.
  ASSERT_TRUE(learn(lex, "жаргон", T4Language::EN));
  const std::vector<uint8_t> seq = {1, 1, 3, 1, 2, 2};
  EXPECT_EQ(match(lex, seq, found, T4UserLexicon::kMaxMatches, T4Language::ADDITIONAL), 1u);
}

// ── Ranking ─────────────────────────────────────────────────────────────

TEST_F(T4UserLexiconTest, RanksMatchesByScoreDescending) {
  ASSERT_TRUE(learn(lex, "tick"));
  ASSERT_TRUE(learn(lex, "wifi"));
  ASSERT_TRUE(learn(lex, "wifi"));  // score 2

  ASSERT_EQ(match(lex, kWifiSeq, found, T4UserLexicon::kMaxMatches), 2u);
  EXPECT_STREQ(lex.getWord(found[0]), "wifi");
  EXPECT_STREQ(lex.getWord(found[1]), "tick");
}

TEST_F(T4UserLexiconTest, ReportsAtMostCapMatchesKeepingStrongest) {
  ASSERT_TRUE(learn(lex, "tick"));
  ASSERT_TRUE(learn(lex, "wifi"));
  ASSERT_TRUE(learn(lex, "wifi"));

  uint16_t single[1] = {};
  ASSERT_EQ(match(lex, kWifiSeq, single, 1), 1u);
  EXPECT_STREQ(lex.getWord(single[0]), "wifi");
}

TEST_F(T4UserLexiconTest, AgesScoresAtCeiling) {
  for (int i = 0; i < 250; i++) ASSERT_TRUE(learn(lex, "wifi"));

  ASSERT_EQ(match(lex, kWifiSeq, found, T4UserLexicon::kMaxMatches), 1u);
  const uint8_t score = lex.getScore(found[0]);
  EXPECT_GE(score, 1u);
  EXPECT_LT(score, T4UserLexicon::kScoreMax);
}

// ── Text learning ───────────────────────────────────────────────────────

TEST_F(T4UserLexiconTest, LearnsWordsFromTextAndTrimsPunctuation) {
  EXPECT_EQ(lex.learnText(T4Language::EN, "hello, world! ok", nullptr), 3u);
  EXPECT_EQ(lex.getEntryCount(), 3u);

  const std::vector<uint8_t> helloSeq = {2, 1, 2, 2, 3};  // h e l l o
  ASSERT_EQ(match(lex, helloSeq, found, T4UserLexicon::kMaxMatches), 1u);
  EXPECT_STREQ(lex.getWord(found[0]), "hello");
}

TEST_F(T4UserLexiconTest, SkipsWordsTheFieldStartedWith) {
  EXPECT_EQ(lex.learnText(T4Language::EN, "old new", "old"), 1u);
  EXPECT_EQ(lex.getEntryCount(), 1u);

  const std::vector<uint8_t> newSeq = {3, 1, 4};  // n e w
  EXPECT_EQ(match(lex, newSeq, found, T4UserLexicon::kMaxMatches), 1u);
}

TEST_F(T4UserLexiconTest, LearnsRepeatedWordOnlyOncePerOccurrence) {
  EXPECT_EQ(lex.learnText(T4Language::EN, "wifi wifi", nullptr), 2u);
  EXPECT_EQ(lex.getEntryCount(), 1u);

  ASSERT_EQ(match(lex, kWifiSeq, found, T4UserLexicon::kMaxMatches), 1u);
  EXPECT_EQ(lex.getScore(found[0]), 2u);
}

TEST_F(T4UserLexiconTest, IgnoresNullText) { EXPECT_EQ(lex.learnText(T4Language::EN, nullptr, nullptr), 0u); }

// ── Capacity ────────────────────────────────────────────────────────────

TEST_F(T4UserLexiconTest, EvictsWeakestEntryWhenFull) {
  ASSERT_TRUE(learn(lex, "wifi"));
  ASSERT_TRUE(learn(lex, "wifi"));
  ASSERT_TRUE(learn(lex, "wifi"));  // score 3 — must survive eviction

  // Fill the remaining slots with distinct two-letter words (score 1).
  for (uint16_t i = 0; i + 1 < T4UserLexicon::kMaxEntries; i++) {
    char word[3] = {static_cast<char>('a' + i / 26), static_cast<char>('a' + i % 26), '\0'};
    ASSERT_TRUE(learn(lex, word));
  }
  EXPECT_EQ(lex.getEntryCount(), T4UserLexicon::kMaxEntries);

  ASSERT_TRUE(learn(lex, "zulu"));
  EXPECT_EQ(lex.getEntryCount(), T4UserLexicon::kMaxEntries);

  ASSERT_EQ(match(lex, kWifiSeq, found, T4UserLexicon::kMaxMatches), 1u);
  EXPECT_STREQ(lex.getWord(found[0]), "wifi");
}

// ── Serialization ───────────────────────────────────────────────────────

TEST_F(T4UserLexiconTest, SerializeRoundTrip) {
  ASSERT_TRUE(learn(lex, "wifi"));
  ASSERT_TRUE(learn(lex, "wifi"));
  ASSERT_TRUE(learn(lex, "жаргон", T4Language::ADDITIONAL));

  std::vector<uint8_t> buffer(T4UserLexicon::kMaxSerializedSize);
  const size_t written = lex.serialize(buffer.data(), buffer.size());
  ASSERT_GT(written, T4UserLexicon::kHeaderSize);
  EXPECT_FALSE(lex.isDirty());

  T4UserLexicon restored;
  ASSERT_TRUE(restored.loadFromBuffer(buffer.data(), written));
  EXPECT_EQ(restored.getEntryCount(), 2u);
  EXPECT_FALSE(restored.isDirty());

  ASSERT_EQ(match(restored, kWifiSeq, found, T4UserLexicon::kMaxMatches), 1u);
  EXPECT_STREQ(restored.getWord(found[0]), "wifi");
  EXPECT_EQ(restored.getScore(found[0]), 2u);

  const std::vector<uint8_t> ruSeq = {1, 1, 3, 1, 2, 2};
  EXPECT_EQ(match(restored, ruSeq, found, T4UserLexicon::kMaxMatches, T4Language::ADDITIONAL), 1u);
}

TEST_F(T4UserLexiconTest, SerializeFailsOnUndersizedBuffer) {
  ASSERT_TRUE(learn(lex, "wifi"));
  uint8_t tiny[4] = {};
  EXPECT_EQ(lex.serialize(tiny, sizeof(tiny)), 0u);
  EXPECT_TRUE(lex.isDirty());
}

TEST_F(T4UserLexiconTest, LoadRejectsInvalidHeader) {
  std::vector<uint8_t> buffer(T4UserLexicon::kHeaderSize, 0);
  EXPECT_FALSE(lex.loadFromBuffer(buffer.data(), buffer.size()));
  EXPECT_FALSE(lex.loadFromBuffer(nullptr, 0));
  EXPECT_EQ(lex.getEntryCount(), 0u);
}

TEST_F(T4UserLexiconTest, LoadStopsAtTruncatedEntry) {
  ASSERT_TRUE(learn(lex, "wifi"));
  ASSERT_TRUE(learn(lex, "tick"));

  std::vector<uint8_t> buffer(T4UserLexicon::kMaxSerializedSize);
  const size_t written = lex.serialize(buffer.data(), buffer.size());
  ASSERT_GT(written, 0u);

  T4UserLexicon restored;
  ASSERT_TRUE(restored.loadFromBuffer(buffer.data(), written - 3));
  EXPECT_EQ(restored.getEntryCount(), 1u);
}

TEST_F(T4UserLexiconTest, ClearDropsEverything) {
  ASSERT_TRUE(learn(lex, "wifi"));
  lex.clear();
  EXPECT_EQ(lex.getEntryCount(), 0u);
  EXPECT_FALSE(lex.isDirty());
  EXPECT_EQ(match(lex, kWifiSeq, found, T4UserLexicon::kMaxMatches), 0u);
}
