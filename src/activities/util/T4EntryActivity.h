#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "KeyboardEntryActivity.h"
#include "T4Dictionary.h"
#include "T4InputEngine.h"
#include "T4Layout.h"
#include "T4UserLexicon.h"
#include "activities/Activity.h"

class MappedInputManager;

class T4EntryActivity : public Activity {
 public:
  explicit T4EntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                           std::string initialText, size_t maxLength, InputType inputType);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr unsigned long LONG_PRESS_MS = 500;
  static constexpr unsigned long BACKSPACE_INITIAL_DELAY_MS = 500;
  static constexpr unsigned long BACKSPACE_REPEAT_MS = 150;

  // Learned-word store, shared by every Text field (see docs/file-formats.md).
  static constexpr const char* USER_LEXICON_DIR = "/.crosspoint/dicts";
  static constexpr const char* USER_LEXICON_PATH = "/.crosspoint/dicts/user_words.bin";

  // Punctuation cycle
  static constexpr const char* PUNCT_CYCLE[] = {" ", ". ", ", ", "! ", "? ", ": ", "; ", "... "};
  static constexpr int PUNCT_COUNT = 8;
  // Same cycle without the trailing space, used for Password/URL input where
  // the text-sentence spacing convention ("period + space", …) doesn't apply —
  // the substitution is the bare punctuation glyph.
  static constexpr const char* PUNCT_CYCLE_BARE[] = {" ", ".", ",", "!", "?", ":", ";", "..."};

  // Pick the punctuation cycle active for the current input type.
  const char* const* punctCycle() const { return (_inputType == InputType::Text) ? PUNCT_CYCLE : PUNCT_CYCLE_BARE; }

  void onComplete();
  void onCancel();
  void handlePunctuation();
  static bool isAutoCapPunct(const char* punct, const t4::SentenceConfig& cfg);
  // Effective confirmed-text byte limit: _maxLength clamped to the input
  // engine's buffer, with 0 meaning "no field limit" (engine-bounded).
  size_t textLimitBytes() const;
  // Bytes of punctuation @p punct appendable to text of length @p textLen
  // within @p limit.  Drops a trailing space separator when it alone would
  // overflow (". " → "."); returns 0 when the glyph itself doesn't fit.
  static size_t punctBytesWithin(size_t textLen, const char* punct, size_t limit);
  bool isTextInputFull() const;

  // Mode transition helpers
  bool togglePredictMultiTap();

  // Apply _userMode to the input engine, falling back to MULTI_TAP when
  // the current language has no dictionary.  Call after _lang changes.
  void applyEffectiveMode();

  // Returns true if predictive input is available for @p lang, i.e. its
  // dictionary file exists on the SD card. DIGIT (and any language whose
  // .trie is missing) returns false, meaning that language is Multi-tap only.
  bool languageSupportsPredict(t4::T4Language lang) const;

  // ── User lexicon ──────────────────────────────────────────────────────

  // Allocate the lexicon and read it from SD, then attach it to the input
  // engine. Called from onEnter() for Text input only — password and URL
  // fields must never contribute to (or be predicted from) the lexicon.
  void loadUserLexicon();

  // Write the lexicon back to SD when it changed. Called from onExit(),
  // so the whole session costs a single SD write.
  void saveUserLexicon();

  // Learn the words of the confirmed text. Words the field started with
  // are skipped — they were not typed by the user.
  void learnIntoLexicon(const std::string& text);

  // Shift/uppercase helpers
  void cycleShift();
  // Apply the active Shift/Caps state to a candidate word for display and
  // commit (Caps → whole word, Shift/auto-cap → first letter only). Pure:
  // reads state but does not consume the one-shot Shift / auto-cap.
  std::string applyWordCase(const char* word) const;

  // ── Render helpers (called from render()) ─────────────────────────────

  // Draw header bar: title, language badge, mode, and shift indicator.
  void renderHeader();

  // Draw text input field: confirmed text + unconfirmed candidate with
  // word-wrap, cursor, optional password toggle, and vertical overflow
  // clamping (maxHeight = 0 disables the limit). Returns the Y
  // coordinate just below the field for chaining the next section.
  int renderTextField(int startY, int lineHeight, int maxHeight, bool& overflowOut);

  // Draw info line above text field: ^^^^^^ 45/140
  void renderInfoLine(int aboveTextY, bool overflow);

  // Compute the left/right margins of the text-field area.  On X3 the
  // left edge reserves one side capsule and the right edge two
  // side-by-side capsules (short- + long-press hints).
  void textFieldMargins(int pageWidth, int& leftMargin, int& rightMargin) const;

  // Draw candidate scroll row (Predict mode only). Returns the Y
  // coordinate after the row (same as startY when there are no candidates).
  int renderCandidateRow(int startY);

  // Draw letter blocks for Predict/Multi-tap modes plus long-press hints
  // and side-button hints.
  void renderButtonHints(int lineHeight);

  // Max letter-block height across all cycle languages (EN, ADDITIONAL if
  // active, DIGIT) so the block area and the text field above it stay put
  // when the language cycles. Called once in onEnter(), result cached in
  // _maxBlockH.
  int maxLetterBlockHeight(int lineHeight) const;

  // Draw the mode hint text (Up+Right combo sentence in Predict mode,
  // then the short/long-press legend) centered horizontally and anchored
  // just above the letter blocks' top edge (@p blocksBaseY). Returns the
  // vertical pixel height the hint block occupies.
  int renderModeHint(int blocksBaseY);

  // Draw the visual Up+Right combo connector: an arrowed line linking the
  // Up side-button hint (Bksp) to the 4th letter block (Right), labeled
  // with STR_T4_CANDIDATES. Predict mode only, when the candidate list is
  // non-empty and cycle mode is off. X3 uses an L-shaped connector with a
  // horizontal label run; X4 uses a vertical connector in the corridor
  // right of the text area, with the label breaking the vertical.
  void renderCandidateComboHint(int blocksBaseY);

  // ── Button dispatch (called from loop()) ──────────────────────────────

  // Phase 1: long-press detection. Returns true if loop must exit early
  // (Back→cancel, Confirm→finish).
  bool handleLongPresses();

  // Phase 2: record press-start for all 6 buttons.
  void trackButtonPresses();

  // Phase 3: simultaneous Up+Right → toggle candidate-cycle mode.
  void handleUpRightCombo();

  // Phase 4: short-press actions on button release for all 6 buttons.
  void handleShortPressReleases();

  // Phase 5: auto-repeat backspace while Up is held.
  void handleBackspaceHoldRepeat();

  // Phase 6: time-based updates (punctuation popup timeout + predictor poll).
  void pollTimeBasedState();

  // Shared: press a letter group (1-4), resetting punctuation/candidate state.
  void pressLetterGroup(uint8_t groupNum);

  std::string _title;
  std::string _initialText;
  size_t _maxLength;
  InputType _inputType;

  t4::T4InputEngine<> _inputEngine;
  t4::T4Language _lang;
  t4::T4Mode _mode;         // Tracks _inputEngine.getMode() for rendering
  t4::T4Mode _userMode;     // User's preferred mode (changed only on manual toggle)
  int _punctIndex;          // 0 = space (just-confirmed), 1-7 = punct
  bool _wordJustConfirmed;  // true after confirmWord, false after next input
  /// Byte length of the punctuation mark actually appended last.  At the
  /// field limit the trailing space separator is dropped (". " → "."), so the
  /// applied mark can be shorter than the minimal cycle entry.  The cycle
  /// uses this to erase exactly the applied suffix instead of clobbering the
  /// final character's trailing UTF-8 bytes.
  size_t _lastPunctLen = 0;
  /// When true, one-shot Shift level 1 was auto-generated (sentence-start)
  /// and should NOT be consumed on the first letter press.  Manual shift
  /// sets this in pressLetterGroup after consuming the engine level.
  bool _autoCap;
  bool _autoCapFromSentence = false;
  /// True when the current _autoCap + shift level 1 was triggered by
  /// sentence-ending punctuation detection (not manual Shift).  Used to
  /// selectively cancel auto-cap on backspace.

  // Cached sentence config for the current language.  Updated when _lang
  // changes (cycleLanguage, setLanguage); never null.
  const t4::SentenceConfig* _sentenceCfg;

  // Render buffer (heap-allocated, reused across render calls)
  std::unique_ptr<char[]> _displayBuf;

  // Learned words, merged into the predictions. Null for password and URL
  // input, which neither read from nor write to the lexicon.
  std::unique_ptr<t4::T4UserLexicon> _lexicon;

  // Max letter-block height across all cycle languages (EN, ADDITIONAL if
  // active, DIGIT). Computed once in onEnter() after the additional
  // layout is applied; constant for the activity's lifetime.
  int _maxBlockH = 0;

  // Long-press tracking (6 buttons)
  bool _backHeld, _backLongHandled;
  bool _confirmHeld, _confirmLongHandled;
  bool _leftHeld, _leftLongHandled;
  bool _rightHeld, _rightLongHandled;
  bool _upHeld, _upLongHandled;
  bool _upRightComboHandled = false;

  // Down long-press tracking (toggles Shift/Caps; short press = punctuation)
  bool _downHeld = false, _downLongHandled = false;

  // Toggle for left side button action: false=backspace, true=cycleCandidate
  bool _upSideCyclesCandidates = false;

  // Backspace long-press hold tracking (auto-repeat on Up button)
  unsigned long _backspaceLastActionMs;

  // Punctuation cycle timeout tracking
  unsigned long _lastConfirmMs = 0;

  // Candidate row horizontal scroll offset (pixels)
  int16_t _candidateScrollX;

  // Password visibility toggle
  bool _passwordVisible = false;
};
