#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "KeyboardEntryActivity.h"
#include "T4Dictionary.h"
#include "T4InputEngine.h"
#include "T4Layout.h"
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

  // Punctuation cycle
  static constexpr const char* PUNCT_CYCLE[] = {" ", ". ", ", ", "! ", "? ", ": ", "; ", "... "};
  static constexpr int PUNCT_COUNT = 8;

  void onComplete();
  void onCancel();
  void handlePunctuation();
  static bool isAutoCapPunct(const char* punct);
  bool isTextInputFull() const;

  // Mode transition helpers
  bool togglePredictMultiTap();

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
  int renderTextField(int startY, int lineHeight, int maxHeight);

  // Draw candidate scroll row (Predict mode only). Returns the Y
  // coordinate after the row (same as startY when there are no candidates).
  int renderCandidateRow(int startY);

  // Draw letter blocks for Predict/Multi-tap modes plus long-press hints
  // and side-button hints.
  void renderButtonHints(int lineHeight);

  std::string _title;
  std::string _initialText;
  size_t _maxLength;
  InputType _inputType;

  t4::T4InputEngine<> _inputEngine;
  t4::T4Language _lang;
  t4::T4Mode _mode;         // Tracks _inputEngine.getMode() for rendering
  int _punctIndex;          // 0 = space (just-confirmed), 1-7 = punct
  bool _wordJustConfirmed;  // true after confirmWord, false after next input
  bool _autoCap;            // next confirmWord should capitalize

  // Render buffer (heap-allocated, reused across render calls)
  std::unique_ptr<char[]> _displayBuf;

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
