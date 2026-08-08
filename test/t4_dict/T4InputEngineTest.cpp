#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Include T4InputEngine without pulling in T4Dictionary/HalStorage
// We supply our own MockDictionary.
#include "T4Dict/T4InputEngine.h"
#include "T4Dict/T4Layout.h"

using namespace t4;

// ── MockDictionary ──────────────────────────────────────────────────────
//
// In-memory trie for unit testing T4InputEngine without HalStorage.
// Stores a small hand-crafted trie: nodes and string pool in vectors.

struct MockNode {
  uint32_t child_offset[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
  uint16_t word_count = 0;
  std::vector<std::string> words;  // in frequency order
};

class MockDictionary {
 public:
  MockDictionary() = default;

  bool loadFromSD(const char* /*path*/) {
    // Build a minimal trie with CORRECT button mappings based on
    // T4Layout English groups:
    //   btn1 (idx 0): abcdef    btn2 (idx 1): ghijkl
    //   btn3 (idx 2): mnopqrs   btn4 (idx 3): tuvwxyz
    //
    // "hello" = h(2)→e(1)→l(2)→l(2)→o(3)
    // "home"  = h(2)→o(3)→m(3)→e(1)
    // "hope"  = h(2)→o(3)→p(3)→e(1)

    _nodes.resize(9);  // pre-allocate

    // Node 0: root
    // btn2 → Node 1 (for h/g/i/j/k/l)
    _nodes[0].child_offset[1] = 1;

    // Node 1: after "h/g/i/j/k/l"
    // btn1 → Node 2 (for e/a/b/c/d/f)  → "he" prefix
    // btn3 → Node 5 (for o/m/n/p/q/r/s) → "ho" prefix
    _nodes[1].child_offset[0] = 2;
    _nodes[1].child_offset[2] = 5;

    // Node 2: after "he"
    // btn2 → Node 3 (for l/g/i/j/k) → "hel" prefix
    _nodes[2].child_offset[1] = 3;

    // Node 3: after "hel"
    // btn2 → Node 4 (for l/g/i/j/k) → "hell" prefix
    _nodes[3].child_offset[1] = 4;

    // Node 4: after "hell"
    // btn3 → Node 8 (for o/m/n/p/q/r/s) → "hello"
    _nodes[4].child_offset[2] = 8;

    // Node 5: after "ho"
    // btn3 → Node 6 (for m/n/p/q/r/s) → "hom"/"hop" prefix
    _nodes[5].child_offset[2] = 6;

    // Node 6: after "hom"/"hop"
    // btn1 → Node 7 (for e/a/b/c/d/f) → "home"/"hope"
    _nodes[6].child_offset[0] = 7;

    // Node 7: words "home", "hope"
    _nodes[7].word_count = 2;
    _nodes[7].words = {"home", "hope"};

    // Node 8: word "hello"
    _nodes[8].word_count = 1;
    _nodes[8].words = {"hello"};

    _currentNode = 0;
    _loaded = true;
    _langCode = "en";
    return true;
  }

  bool pressButton(uint8_t btn) {
    if (!_loaded || btn < 1 || btn > 4) return false;
    _candidatesLoaded = false;
    uint8_t idx = btn - 1;
    uint32_t child = _nodes[_currentNode].child_offset[idx];
    if (child == 0xFFFFFFFF) return false;
    _currentNode = child;
    return true;
  }

  uint16_t getCandidateCount() const { return _nodes[_currentNode].word_count; }

  bool loadCandidates() {
    _candidatesLoaded = true;
    return true;
  }

  const char* getCandidate(uint16_t index) const {
    if (index >= _nodes[_currentNode].words.size()) return nullptr;
    return _nodes[_currentNode].words[index].c_str();
  }

  void reset() {
    _currentNode = 0;
    _candidatesLoaded = false;
  }

  void close() {
    _loaded = false;
    _nodes.clear();
  }

  bool isLoaded() const { return _loaded; }
  const char* getLangCode() const { return _langCode; }

 private:
  std::vector<MockNode> _nodes;
  uint32_t _currentNode = 0;
  bool _loaded = false;
  bool _candidatesLoaded = false;
  const char* _langCode = "";
};

// ── Test fixture ────────────────────────────────────────────────────────

// Convenience: current multi-tap letter as a single char ('\0' when idle).
// Wraps the byte-length API for the ASCII cases used in these tests.
static char tapChar(const T4InputEngine<MockDictionary>& e) {
  uint8_t blen = 0;
  const char* p = e.getCurrentTapLetter(blen);
  return (p && blen == 1) ? *p : '\0';
}

class T4InputEngineTest : public ::testing::Test {
 protected:
  void SetUp() override { predictor.getDictionary()->loadFromSD("/mock/en.trie"); }

  T4InputEngine<MockDictionary> predictor;
};

// ── Predict mode: basic candidate lookup ────────────────────────────────

TEST_F(T4InputEngineTest, PredictHelloSequence) {
  // "hello": h(2)→e(1)→l(2)→l(2)→o(3)
  ASSERT_TRUE(predictor.pressButton(2));
  ASSERT_TRUE(predictor.pressButton(1));
  ASSERT_TRUE(predictor.pressButton(2));
  ASSERT_TRUE(predictor.pressButton(2));
  ASSERT_TRUE(predictor.pressButton(3));

  EXPECT_GE(predictor.getCandidateCount(), 1u);
  const char* cand = predictor.getCurrentCandidate();
  ASSERT_NE(cand, nullptr);
  EXPECT_STREQ(cand, "hello");
  EXPECT_EQ(predictor.getCandidateIndex(), 0u);
}

TEST_F(T4InputEngineTest, PredictCycleCandidates) {
  // Navigate to node with "home" and "hope"
  // "home": h(2)→o(3)→m(3)→e(1)
  ASSERT_TRUE(predictor.pressButton(2));  // h
  ASSERT_TRUE(predictor.pressButton(3));  // o
  ASSERT_TRUE(predictor.pressButton(3));  // m
  ASSERT_TRUE(predictor.pressButton(1));  // e

  EXPECT_GE(predictor.getCandidateCount(), 2u);
  EXPECT_STREQ(predictor.getCurrentCandidate(), "home");
  EXPECT_EQ(predictor.getCandidateIndex(), 0u);

  predictor.cycleCandidate();
  EXPECT_STREQ(predictor.getCurrentCandidate(), "hope");
  EXPECT_EQ(predictor.getCandidateIndex(), 1u);

  predictor.cycleCandidate();
  EXPECT_STREQ(predictor.getCurrentCandidate(), "home");
  EXPECT_EQ(predictor.getCandidateIndex(), 0u);
}

TEST_F(T4InputEngineTest, PredictDeadEndSequence) {
  // "h" then "z" (button 4, but no "hz" prefix in mock)
  ASSERT_TRUE(predictor.pressButton(2));  // h
  bool ok = predictor.pressButton(4);     // z/w/x/y group — no child
  EXPECT_FALSE(ok);
  EXPECT_EQ(predictor.getCandidateCount(), 0u);
  // Dead-end fallback: the engine keeps the sequence and returns a raw
  // first-letter preview string rather than nullptr.
  EXPECT_NE(predictor.getCurrentCandidate(), nullptr);
}

// ── Predict: confirm word ───────────────────────────────────────────────

TEST_F(T4InputEngineTest, PredictConfirmWord) {
  ASSERT_TRUE(predictor.pressButton(2));  // h
  ASSERT_TRUE(predictor.pressButton(1));  // e
  ASSERT_TRUE(predictor.pressButton(2));  // l
  ASSERT_TRUE(predictor.pressButton(2));  // l
  ASSERT_TRUE(predictor.pressButton(3));  // o

  predictor.confirmWord();
  EXPECT_STREQ(predictor.getConfirmedText(), "hello ");
  EXPECT_EQ(predictor.getConfirmedTextLength(), 6u);
  EXPECT_EQ(predictor.getSequenceLength(), 0u);
}

TEST_F(T4InputEngineTest, PredictConfirmMultipleWords) {
  // Type "hello"
  predictor.pressButton(2);
  predictor.pressButton(1);
  predictor.pressButton(2);
  predictor.pressButton(2);
  predictor.pressButton(3);
  predictor.confirmWord();  // "hello "

  // Type "home" — trie is at root after confirmWord reset
  predictor.pressButton(2);  // h
  predictor.pressButton(3);  // o
  predictor.pressButton(3);  // m
  predictor.pressButton(1);  // e
  predictor.confirmWord();   // "home "

  EXPECT_STREQ(predictor.getConfirmedText(), "hello home ");
}

// ── Multi-tap: letter cycling ───────────────────────────────────────────

TEST_F(T4InputEngineTest, MultiTapSameButtonCycles) {
  predictor.setMode(T4Mode::MULTI_TAP);

  // btn1 EN group = abcdef' (length 7, apostrophe is the 7th tap target)
  predictor.pressButton(1);
  EXPECT_EQ(tapChar(predictor), 'a');
  EXPECT_EQ(predictor.getActiveButton(), 1u);

  predictor.pressButton(1);
  EXPECT_EQ(tapChar(predictor), 'b');

  predictor.pressButton(1);
  EXPECT_EQ(tapChar(predictor), 'c');

  // 8 presses should wrap back to 'a' (group length 7)
  predictor.pressButton(1);  // d
  predictor.pressButton(1);  // e
  predictor.pressButton(1);  // f
  predictor.pressButton(1);  // ' (apostrophe)
  EXPECT_EQ(tapChar(predictor), '\'');
  predictor.pressButton(1);  // a (wrap)
  EXPECT_EQ(tapChar(predictor), 'a');
}

TEST_F(T4InputEngineTest, MultiTapDifferentButtonFixesAndStarts) {
  predictor.setMode(T4Mode::MULTI_TAP);

  predictor.pressButton(1);  // 'a' from abcdef
  predictor.pressButton(1);  // 'b'
  predictor.pressButton(2);  // different btn — fix 'b', start 'g'

  // Letter 'b' should be fixed in confirmed text
  EXPECT_STREQ(predictor.getConfirmedText(), "b");
  EXPECT_EQ(predictor.getActiveButton(), 2u);
  EXPECT_EQ(tapChar(predictor), 'g');
}

// ── Multi-tap: timeout ──────────────────────────────────────────────────

TEST_F(T4InputEngineTest, MultiTapTimeoutFixesLetter) {
  predictor.setMode(T4Mode::MULTI_TAP);

  predictor.pressButton(1);  // 'a', lastTapTime = 0
  predictor.poll(0);         // set _lastTapTime to 0
  predictor.pressButton(1);  // 'b', reset _lastTapTime
  predictor.poll(100);
  predictor.pressButton(1);  // 'c', reset _lastTapTime
  predictor.poll(200);

  // Advance past timeout
  predictor.poll(1100);  // 1100 - 200 = 900ms → timeout triggers

  EXPECT_EQ(predictor.getActiveButton(), 0u);
  EXPECT_STREQ(predictor.getConfirmedText(), "c");
  EXPECT_EQ(tapChar(predictor), '\0');
}

TEST_F(T4InputEngineTest, MultiTapNoTimeoutWithinWindow) {
  predictor.setMode(T4Mode::MULTI_TAP);

  predictor.pressButton(1);  // 'a'
  predictor.poll(0);
  predictor.poll(500);  // only 500ms — no timeout

  EXPECT_EQ(predictor.getActiveButton(), 1u);         // still active
  EXPECT_EQ(predictor.getConfirmedTextLength(), 0u);  // not fixed yet
}

// ── Multi-tap: confirm word adds space ──────────────────────────────────

TEST_F(T4InputEngineTest, MultiTapConfirmWordAddsSpace) {
  predictor.setMode(T4Mode::MULTI_TAP);

  predictor.pressButton(2);
  predictor.pressButton(2);  // 'h'
  predictor.pressButton(1);  // different btn → fix 'h', start 'a'
  predictor.pressButton(1);  // 'b'
  predictor.confirmWord();   // fix 'b' + space

  EXPECT_STREQ(predictor.getConfirmedText(), "hb ");
}

// ── Multi-tap: shift / uppercase ────────────────────────────────────────

TEST_F(T4InputEngineTest, MultiTapShiftDefaultsOff) { EXPECT_EQ(predictor.getShiftLevel(), 0u); }

TEST_F(T4InputEngineTest, MultiTapOneShotUppercasesFirstLetterOnly) {
  predictor.setMode(T4Mode::MULTI_TAP);
  predictor.setShiftLevel(1);  // one-shot

  predictor.pressButton(1);  // 'a'
  predictor.pressButton(2);  // different btn → fix 'a' → 'A', start 'g'

  EXPECT_STREQ(predictor.getConfirmedText(), "A");
  EXPECT_EQ(predictor.getShiftLevel(), 0u);  // reset after first fixed letter

  predictor.pressButton(3);  // fix 'g' lowercase → 'g', start 'm'
  EXPECT_STREQ(predictor.getConfirmedText(), "Ag");
}

TEST_F(T4InputEngineTest, MultiTapCapsLockUppercasesEveryLetter) {
  predictor.setMode(T4Mode::MULTI_TAP);
  predictor.setShiftLevel(2);  // locked

  predictor.pressButton(1);  // 'a'
  predictor.pressButton(2);  // fix 'a' → 'A', start 'g'
  predictor.pressButton(3);  // fix 'g' → 'G', start 'm'

  EXPECT_STREQ(predictor.getConfirmedText(), "AG");
  EXPECT_EQ(predictor.getShiftLevel(), 2u);  // stays locked
}

TEST_F(T4InputEngineTest, MultiTapShiftClearedOnReset) {
  predictor.setShiftLevel(2);
  predictor.reset();
  EXPECT_EQ(predictor.getShiftLevel(), 0u);
}

// ── Mode switching preserves text ───────────────────────────────────────

TEST_F(T4InputEngineTest, ModeSwitchPreservesConfirmedText) {
  // Type in Predict
  predictor.pressButton(2);
  predictor.pressButton(1);
  predictor.pressButton(2);
  predictor.pressButton(2);
  predictor.pressButton(3);  // "hello"
  predictor.confirmWord();   // "hello "

  // Switch to Multi-tap
  predictor.setMode(T4Mode::MULTI_TAP);
  EXPECT_STREQ(predictor.getConfirmedText(), "hello ");
  EXPECT_EQ(predictor.getMode(), T4Mode::MULTI_TAP);

  // Add letter in Multi-tap
  predictor.pressButton(4);  // 't'
  predictor.pressButton(2);  // fix 't', start 'g'
  EXPECT_STREQ(predictor.getConfirmedText(), "hello t");

  // Switch back to Predict — fixes in-progress 'g'
  predictor.setMode(T4Mode::PREDICT);
  EXPECT_STREQ(predictor.getConfirmedText(), "hello tg");
}

TEST_F(T4InputEngineTest, BackspacePullsBackCapitalizedWord) {
  // A word committed in Shift/Caps is stored uppercase. Backspace must still
  // recognize the letters (case-insensitively) and pull the word back into
  // the prediction sequence for re-editing — not delete it char-by-char.
  predictor.setConfirmedText("MAN ");

  // First backspace removes the trailing space.
  predictor.backspace();
  EXPECT_STREQ(predictor.getConfirmedText(), "MAN");
  EXPECT_EQ(predictor.getSequenceLength(), 0u);

  // Second backspace recovers the whole capitalized word's button sequence.
  predictor.backspace();
  EXPECT_EQ(predictor.getConfirmedTextLength(), 0u);
  ASSERT_EQ(predictor.getSequenceLength(), 3u);  // M, A, N
  const uint8_t* seq = predictor.getSequence();
  ASSERT_NE(seq, nullptr);
  EXPECT_EQ(seq[0], 3u);  // 'm' → btn3 (mnopqrs)
  EXPECT_EQ(seq[1], 1u);  // 'a' → btn1 (abcdef')
  EXPECT_EQ(seq[2], 3u);  // 'n' → btn3 (mnopqrs)
}

TEST_F(T4InputEngineTest, BackspacePullsBackCrossLanguageWord) {
  // A Russian word is present in confirmed text while English is active.
  // Backspace must pull the word back using the Russian layout and
  // auto-switch the input language to RU.
  predictor.setConfirmedText("привет ");  // setConfirmedText rebuilds _wordLang
  EXPECT_EQ(predictor.getLanguage(), T4Language::EN);

  // First backspace removes the trailing space.
  predictor.backspace();
  EXPECT_STREQ(predictor.getConfirmedText(), "привет");

  // Second backspace pulls back the Russian word and switches language.
  predictor.backspace();
  EXPECT_EQ(predictor.getConfirmedTextLength(), 0u);
  // Language must have auto-switched to ADDITIONAL (Russian) so the letter blocks match.
  EXPECT_EQ(predictor.getLanguage(), T4Language::ADDITIONAL);
  // Button sequence must be extracted using the Russian layout.
  ASSERT_EQ(predictor.getSequenceLength(), 6u);  // п р и в е т
  const uint8_t* seq = predictor.getSequence();
  ASSERT_NE(seq, nullptr);
  EXPECT_EQ(seq[0], 3u);  // п → btn3 (RU: прстуфхц)
  EXPECT_EQ(seq[1], 3u);  // р → btn3
  EXPECT_EQ(seq[2], 2u);  // и → btn2 (RU: зийклмно)
  EXPECT_EQ(seq[3], 1u);  // в → btn1 (RU: абвгдеёж-)
  EXPECT_EQ(seq[4], 1u);  // е → btn1
  EXPECT_EQ(seq[5], 3u);  // т → btn3
}

// ── Sequence display ────────────────────────────────────────────────────

TEST_F(T4InputEngineTest, SequenceDisplay) {
  predictor.pressButton(2);  // h
  predictor.pressButton(1);  // e

  EXPECT_EQ(predictor.getSequenceLength(), 2u);
  EXPECT_EQ(predictor.getSequence()[0], 2u);
  EXPECT_EQ(predictor.getSequence()[1], 1u);
}

// ── Reset ───────────────────────────────────────────────────────────────

TEST_F(T4InputEngineTest, FullReset) {
  predictor.pressButton(2);
  predictor.pressButton(1);
  predictor.pressButton(2);
  predictor.pressButton(2);
  predictor.pressButton(3);
  predictor.confirmWord();

  EXPECT_EQ(predictor.getConfirmedTextLength(), 6u);

  predictor.reset();
  EXPECT_EQ(predictor.getConfirmedTextLength(), 0u);
  EXPECT_EQ(predictor.getSequenceLength(), 0u);
  EXPECT_EQ(predictor.getCandidateCount(), 0u);
}

// ── setLanguage ─────────────────────────────────────────────────────────

TEST_F(T4InputEngineTest, SetLanguagePreservesTextResetsSequence) {
  // Type "hello" in Predict
  predictor.pressButton(2);
  predictor.pressButton(1);
  predictor.pressButton(2);
  predictor.pressButton(2);
  predictor.pressButton(3);
  predictor.confirmWord();
  EXPECT_STREQ(predictor.getConfirmedText(), "hello ");

  // Switch to Russian
  predictor.setLanguage(T4Language::ADDITIONAL);
  EXPECT_EQ(predictor.getLanguage(), T4Language::ADDITIONAL);
  EXPECT_STREQ(predictor.getConfirmedText(), "hello ");  // preserved
  EXPECT_EQ(predictor.getSequenceLength(), 0u);          // reset

  // Switch to DIGIT
  predictor.setLanguage(T4Language::DIGIT);
  EXPECT_EQ(predictor.getLanguage(), T4Language::DIGIT);
  EXPECT_STREQ(predictor.getConfirmedText(), "hello ");  // still preserved
}

TEST_F(T4InputEngineTest, SetLanguageSameLanguageNoOp) {
  predictor.pressButton(2);
  EXPECT_GT(predictor.getSequenceLength(), 0u);

  predictor.setLanguage(T4Language::EN);  // same language
  // Sequence should NOT be reset when language is unchanged
  EXPECT_GT(predictor.getSequenceLength(), 0u);
}

// ── Invalid button ──────────────────────────────────────────────────────

TEST_F(T4InputEngineTest, InvalidButtonReturnsFalse) {
  EXPECT_FALSE(predictor.pressButton(0));
  EXPECT_FALSE(predictor.pressButton(5));
}

// ── Poll in non-Multi-tap mode is no-op ─────────────────────────────────

TEST_F(T4InputEngineTest, PollNoOpInPredict) {
  predictor.pressButton(2);  // start sequence
  predictor.poll(99999);     // huge time jump
  // Should not affect Predict mode
  EXPECT_EQ(predictor.getMode(), T4Mode::PREDICT);
  EXPECT_GT(predictor.getSequenceLength(), 0u);
}
