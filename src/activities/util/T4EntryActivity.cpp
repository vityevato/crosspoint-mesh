#include "T4EntryActivity.h"

#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "KeyboardEntryActivity.h"
#include "Logging.h"
#include "MappedInputManager.h"
#include "Memory.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Count the UTF-8 code points (visual characters) in @p s.  The password mask
// renders one '*' per code point so multi-byte text (Cyrillic, CJK, …) shows
// as many dots as visible characters, not bytes.
static size_t utf8CharCount(const char* s) {
  size_t n = 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  while (*p != '\0') {
    utf8NextCodepoint(&p);
    n++;
  }
  return n;
}

// ── Constructor ──────────────────────────────────────────────────────────

T4EntryActivity::T4EntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                 std::string initialText, size_t maxLength, InputType inputType)
    : Activity("T4Entry", renderer, mappedInput),
      _title(std::move(title)),
      _initialText(std::move(initialText)),
      _maxLength(maxLength),
      _inputType(inputType),
      _lang(t4::T4Language::EN),
      _sentenceCfg(t4::getSentenceConfig(t4::T4Language::EN)),
      _mode(t4::T4Mode::PREDICT),
      _userMode(t4::T4Mode::PREDICT),
      _punctIndex(0),
      _wordJustConfirmed(false),
      _autoCap(false),
      _backHeld(false),
      _backLongHandled(false),
      _confirmHeld(false),
      _confirmLongHandled(false),
      _leftHeld(false),
      _leftLongHandled(false),
      _rightHeld(false),
      _rightLongHandled(false),
      _upHeld(false),
      _upLongHandled(false),
      _backspaceLastActionMs(0),
      _candidateScrollX(0) {
  LOG_DBG("T4", "Constructor: title='%s' maxLen=%u inputType=%d", _title.c_str(), _maxLength,
          static_cast<int>(_inputType));
}

// ── Lifecycle ────────────────────────────────────────────────────────────

void T4EntryActivity::onEnter() {
  LOG_DBG("T4", "onEnter: lang=%d mode=%d text='%s'", static_cast<int>(_lang), static_cast<int>(_mode),
          _initialText.c_str());
  LOG_DBG("MEMM", "T4 onEnter:start free=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  Activity::onEnter();

  // Allocate reusable render buffer on heap (512 bytes — too large for stack)
  _displayBuf = makeUniqueNoThrow<char[]>(512);
  if (!_displayBuf) {
    LOG_ERR("T4", "OOM: display buffer");
    setResult(KeyboardResult{});
    finish();
    return;
  }

  // Apply the user-selected additional keyboard layout (System settings ▸
  // Additional Keyboard Layout) to the middle EN/ADDITIONAL/DIGIT cycle slot
  // before loading its dictionary.
  t4::setActiveAdditionalLayout(SETTINGS.t4AdditionalLayout);

  // Cache the max letter-block height once — it depends only on the fixed
  // language set and the active additional layout, so it cannot change
  // while this activity is alive.
  _maxBlockH = maxLetterBlockHeight(renderer.getLineHeight(UI_12_FONT_ID));

  // Restore the user's globally-persisted input mode preference.
  _userMode = static_cast<t4::T4Mode>(SETTINGS.t4UserMode);

  // Restore the last-used language for Text input.  Password and URL
  // always start with EN — there's no point restoring e.g. Russian for a
  // password field.  Digit input always opens on the numeric keyboard.
  if (_inputType == InputType::Text) {
    _lang = static_cast<t4::T4Language>(SETTINGS.t4LastLanguage);
    if (_lang == t4::T4Language::ADDITIONAL && !t4::hasActiveAdditionalLayout()) {
      _lang = t4::T4Language::EN;
    }
    loadUserLexicon();
  } else if (_inputType == InputType::Digit) {
    _lang = t4::T4Language::DIGIT;
  }

  _inputEngine.setLanguage(_lang);
  _sentenceCfg = t4::getSentenceConfig(_lang);

  // Apply the user's preferred mode for this language.  When the language
  // has no dictionary (DIGIT, or a missing .trie), the effective mode is
  // forced to Multi-tap; _userMode is left unchanged so the user's
  // preference is restored when switching back to a language with a dict.
  applyEffectiveMode();

  // Enforce this field's byte limit inside the engine too, so engine-side
  // appends — most importantly the in-flight multi-tap letter committed by
  // poll()/fixMultiTapLetter() — can never push confirmed text past the cap.
  _inputEngine.setMaxTextLen(static_cast<uint16_t>(textLimitBytes()));

  // Priming the engine with initial text (e.g., file rename)
  if (!_initialText.empty()) {
    _inputEngine.setConfirmedText(_initialText.c_str());
  }

  // ── Auto-cap on entry ─────────────────────────────────────────────
  // When the field opens empty, the first word is the start of a sentence.
  // When initial text ends with sentence-ending punctuation (.!? followed
  // by optional spaces), the next typed word is also the start of a sentence.
  // Password and URL fields are excluded — capitalization is meaningless there.
  if (_inputType == InputType::Text && _sentenceCfg->autoCapitalize) {
    if (_initialText.empty()) {
      if (_inputEngine.getShiftLevel() == 0) {
        _inputEngine.setShiftLevel(1);
        _autoCap = true;
        _autoCapFromSentence = true;
      }
    } else {
      // Scan backwards past trailing spaces, then find the last non-space
      // UTF-8 character and check it against the sentence-ending set.
      const char* t = _initialText.c_str();
      int pos = static_cast<int>(_initialText.size()) - 1;
      while (pos >= 0 && t[pos] == ' ') pos--;
      if (pos >= 0) {
        // Walk back past UTF-8 continuation bytes (0x80–0xBF).
        int charStart = pos;
        while (charStart > 0 && (static_cast<unsigned char>(t[charStart]) & 0xC0) == 0x80) {
          charStart--;
        }
        unsigned char c0 = static_cast<unsigned char>(t[charStart]);
        uint8_t blen = 1;
        if ((c0 & 0xE0) == 0xC0)
          blen = 2;
        else if ((c0 & 0xF0) == 0xE0)
          blen = 3;
        else if ((c0 & 0xF8) == 0xF0)
          blen = 4;
        if (t4::isSentenceEndChar(_sentenceCfg->endChars, t + charStart, blen)) {
          if (_inputEngine.getShiftLevel() == 0) {
            _inputEngine.setShiftLevel(1);
            _autoCap = true;
            _autoCapFromSentence = true;
          }
        }
      }
    }
  }

  _punctIndex = 0;
  _wordJustConfirmed = false;
  _candidateScrollX = 0;
  requestUpdate();
}

void T4EntryActivity::onExit() {
  LOG_DBG("T4", "onExit");
  LOG_DBG("MEMM", "T4 onExit:start free=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  saveUserLexicon();
  _inputEngine.setUserLexicon(nullptr);
  _lexicon.reset();
  _inputEngine.reset();
  _displayBuf.reset();
  Activity::onExit();
}

// ── Button dispatch helpers ──────────────────────────────────────────────

void T4EntryActivity::loop() {
  // NOTE: Do NOT call mappedInput.update() here — gpio.update() is already
  // called in the main loop (main.cpp) before activityManager.loop() runs.
  // A second update() on real hardware would clear the press/release events
  // already latched by the first call, making buttons unresponsive.

  // Phase 1: long-press detection (may return early on cancel/complete)
  if (handleLongPresses()) return;

  // Phase 2: record button press state
  trackButtonPresses();

  // Phase 3: simultaneous Up+Right combo
  handleUpRightCombo();

  // Phase 4: short-press actions on release
  handleShortPressReleases();

  // Phase 5: backspace hold auto-repeat
  handleBackspaceHoldRepeat();

  // Phase 6: time-based state updates
  pollTimeBasedState();
}

// ── Completion / Cancellation ────────────────────────────────────────────

void T4EntryActivity::render(RenderLock&& lock) {
  (void)lock;

  renderer.clearScreen();

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const auto& metrics = UITheme::getInstance().getMetrics();

  renderHeader();

  const int inputStartY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing +
                          metrics.verticalSpacing * 4 + metrics.keyboardVerticalOffset;

  // Compute the vertical limit for the text field so it cannot overflow
  // into the letter-block area at the bottom of the screen.  The
  // calculation mirrors renderButtonHints' blocksBaseY derivation.
  static constexpr int kBlockGapAbove = 12;

  const int maxBlockH = _maxBlockH;
  const int hintTopY = renderer.getScreenHeight() - 40;
  const int blocksBaseY = hintTopY - kBlockGapAbove - maxBlockH - lineHeight;

  const int modeHintH = renderModeHint(blocksBaseY);

  // Reserve lineHeight for the candidate row + counter row + gap.
  const int kCounterRowH = lineHeight * 2;
  const int maxTextFieldBottom = blocksBaseY - modeHintH - lineHeight - kCounterRowH - metrics.verticalSpacing;
  const int maxTextFieldHeight = maxTextFieldBottom - inputStartY;

  bool textOverflow = false;
  int y = renderTextField(inputStartY, lineHeight, maxTextFieldHeight, textOverflow);

  renderInfoLine(inputStartY, textOverflow);

  y = y + lineHeight + metrics.verticalSpacing;
  renderCandidateRow(y);
  renderButtonHints(lineHeight);

  renderer.displayBuffer();
}

// ══════════════════════════════════════════════════════════════════
//  PRIVATE METHODS
// ══════════════════════════════════════════════════════════════════

bool T4EntryActivity::handleLongPresses() {
  // Back long-press → cancel
  if (_backHeld && !_backLongHandled && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    _backLongHandled = true;
    LOG_DBG("T4", "loop: long-press Back → cancel");
    onCancel();
    return true;
  }

  // Confirm long-press → finish with result
  if (_confirmHeld && !_confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    _confirmLongHandled = true;
    LOG_DBG("T4", "loop: long-press Confirm → finish, mode=%d text='%s'", static_cast<int>(_mode),
            _inputEngine.getConfirmedText());
    onComplete();
    return true;
  }

  // Left long-press → cycle language (disabled in cycle mode; the numeric
  // keyboard in Digit input never changes layout)
  if (_leftHeld && !_leftLongHandled && mappedInput.isPressed(MappedInputManager::Button::Left) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS && _inputType != InputType::Digit &&
      !(_mode == t4::T4Mode::PREDICT && _upSideCyclesCandidates)) {
    _leftLongHandled = true;
    _lang = t4::cycleLanguage(_lang);
    LOG_DBG("T4", "loop: long-press Left → cycle language to %s", t4::getLanguageName(_lang));

    // Persist language changes only for Text input — Password, URL, and
    // Digit language choices are transient and shouldn't affect the next
    // session.
    if (_inputType == InputType::Text) {
      SETTINGS.t4LastLanguage = static_cast<uint8_t>(_lang);
      SETTINGS.saveToFile();
    }

    // Save text before reset destroys it
    std::string savedText(_inputEngine.getConfirmedText());
    if (_mode == t4::T4Mode::PREDICT) {
      const char* cand = _inputEngine.getCurrentCandidate();
      if (cand && cand[0] != '\0') savedText += cand;
    }
    LOG_DBG("T4", "loop: lang cycle saved text='%s'", savedText.c_str());

    // reset() also clears the engine's Shift level, so save the current
    // Shift/Caps and sentence-start auto-cap state here and restore it
    // after the layout switch below.
    const uint8_t savedShiftLevel = _inputEngine.getShiftLevel();
    const bool savedAutoCap = _autoCap;
    const bool savedAutoCapFromSentence = _autoCapFromSentence;

    _inputEngine.reset();
    _inputEngine.setLanguage(_lang);
    _sentenceCfg = t4::getSentenceConfig(_lang);

    // Restore text
    if (!savedText.empty()) {
      _inputEngine.setConfirmedText(savedText.c_str());
    }

    // Restore Shift/Caps and sentence-start auto-cap state across the cycle.
    // reset() clears the engine's Shift level; this state describes the
    // user's intent for the next typed word (sentence-start) and must
    // survive a layout switch — including a trip through the case-less
    // DIGIT layout on the way to another language.
    _inputEngine.setShiftLevel(savedShiftLevel);
    _autoCap = savedAutoCap;
    _autoCapFromSentence = savedAutoCapFromSentence;

    // Apply the user's preferred mode.  When the language has no
    // dictionary the effective mode is forced to Multi-tap; _userMode
    // stays unchanged so the user's preference is preserved when
    // switching back to a language with a dict.
    applyEffectiveMode();
    _punctIndex = 0;
    _wordJustConfirmed = false;
    _candidateScrollX = 0;
    _upSideCyclesCandidates = false;
    requestUpdate();
  }

  // Right long-press → toggle Predict ↔ Multi-tap (disabled in cycle mode)
  if (_rightHeld && !_rightLongHandled && mappedInput.isPressed(MappedInputManager::Button::Right) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS && !(_mode == t4::T4Mode::PREDICT && _upSideCyclesCandidates)) {
    _rightLongHandled = true;
    // Only allow Predict/Multi-tap toggle for Text input on languages
    // that have a dictionary.  Password and URL are always Multi-tap.
    if (_inputType == InputType::Text && languageSupportsPredict(_lang)) {
      LOG_DBG("T4", "loop: Right long-press → toggle Predict/Multi-tap (mode=%d)", static_cast<int>(_mode));
      togglePredictMultiTap();
      _userMode = _mode;  // Remember user's explicit choice
      SETTINGS.t4UserMode = static_cast<uint8_t>(_userMode);
      SETTINGS.saveToFile();
      requestUpdate();
    } else if (_inputType == InputType::Password) {
      // Right long-press → toggle password visibility.  The Right button is
      // otherwise unused for a long-press in password mode (no mode toggle),
      // and the visibility state is shown on its long-press hint.
      _passwordVisible = !_passwordVisible;
      LOG_DBG("T4", "loop: Right long-press → toggle password visibility=%d", static_cast<int>(_passwordVisible));
      requestUpdate();
    } else {
      LOG_DBG("T4", "loop: Right long-press ignored — inputType=%d lang=%d", static_cast<int>(_inputType),
              static_cast<int>(_lang));
    }
  }

  // Down long-press → toggle Shift/Caps
  if (_downHeld && !_downLongHandled && mappedInput.isPressed(MappedInputManager::Button::Down) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    _downLongHandled = true;
    cycleShift();
    requestUpdate();
  }

  return false;
}

void T4EntryActivity::trackButtonPresses() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    _backHeld = true;
    _backLongHandled = false;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    _confirmHeld = true;
    _confirmLongHandled = false;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    _leftHeld = true;
    _leftLongHandled = false;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    _rightHeld = true;
    _rightLongHandled = false;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    _upHeld = true;
    _upLongHandled = false;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    _downHeld = true;
    _downLongHandled = false;
  }
}

void T4EntryActivity::handleUpRightCombo() {
  if (!_upRightComboHandled && mappedInput.isPressed(MappedInputManager::Button::Up) &&
      mappedInput.isPressed(MappedInputManager::Button::Right)) {
    _upRightComboHandled = true;
    _upLongHandled = true;
    _rightLongHandled = true;
    if (_mode == t4::T4Mode::PREDICT) {
      bool wasCycle = _upSideCyclesCandidates;
      _upSideCyclesCandidates = !_upSideCyclesCandidates;
      if (_upSideCyclesCandidates && !wasCycle) {
        // Entering cycle mode: advance to next candidate
        _inputEngine.cycleCandidate();
        _candidateScrollX = 0;
      }
      LOG_DBG("T4", "loop: Up+Right combo → toggle upSideAction=%d", _upSideCyclesCandidates);
      requestUpdate();
    }
  }
}

void T4EntryActivity::pressLetterGroup(uint8_t groupNum) {
  LOG_DBG("T4", "loop: press group %u (mode=%d)", groupNum, static_cast<int>(_mode));
  _inputEngine.pressButton(groupNum);
  _punctIndex = 0;
  _wordJustConfirmed = false;
  _candidateScrollX = 0;
  // One-shot Shift: consume engine level after first letter so the
  // key caps revert to lowercase.  _autoCap preserves the intent for
  // applyWordCase — both manual Shift and auto-cap (sentence-start)
  // use the same path.
  if (_mode == t4::T4Mode::PREDICT && _inputEngine.getShiftLevel() == 1) {
    _inputEngine.setShiftLevel(0);
    _autoCap = true;
  }
  requestUpdate();
}

void T4EntryActivity::handleShortPressReleases() {
  // Button::Back — letter group 1 (disabled in cycle mode)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (_backHeld && !_backLongHandled) {
      if (!(_mode == t4::T4Mode::PREDICT && _upSideCyclesCandidates)) {
        if (_mode != t4::T4Mode::MULTI_TAP || !isTextInputFull()) {
          pressLetterGroup(1);
        }
      }
    }
    _backHeld = false;
    _backLongHandled = false;
  }

  // Button::Confirm — letter group 2 (disabled in cycle mode)
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (_confirmHeld && !_confirmLongHandled) {
      if (!(_mode == t4::T4Mode::PREDICT && _upSideCyclesCandidates)) {
        if (_mode != t4::T4Mode::MULTI_TAP || !isTextInputFull()) {
          pressLetterGroup(2);
        }
      }
    }
    _confirmHeld = false;
    _confirmLongHandled = false;
  }

  // Button::Left — in cycle mode: previous candidate; otherwise: letter group 3
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (_leftHeld && !_leftLongHandled) {
      if (_mode == t4::T4Mode::PREDICT && _upSideCyclesCandidates) {
        LOG_DBG("T4", "loop: Left → cycleCandidateBackward (idx=%u/%u)", _inputEngine.getCandidateIndex(),
                _inputEngine.getCandidateCount());
        _inputEngine.cycleCandidateBackward();
        _candidateScrollX = 0;
        requestUpdate();
      } else if (_mode != t4::T4Mode::MULTI_TAP || !isTextInputFull()) {
        pressLetterGroup(3);
      }
    }
    _leftHeld = false;
    _leftLongHandled = false;
    // Reset combo tracking when both buttons released
    if (_upRightComboHandled && !mappedInput.isPressed(MappedInputManager::Button::Up)) {
      _upRightComboHandled = false;
    }
  }

  // Button::Right — in cycle mode: next candidate; otherwise: letter group 4
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (_rightHeld && !_rightLongHandled) {
      if (_mode == t4::T4Mode::PREDICT && _upSideCyclesCandidates) {
        LOG_DBG("T4", "loop: Right → cycleCandidate (idx=%u/%u)", _inputEngine.getCandidateIndex() + 1,
                _inputEngine.getCandidateCount());
        _inputEngine.cycleCandidate();
        _candidateScrollX = 0;
        requestUpdate();
      } else if (_mode != t4::T4Mode::MULTI_TAP || !isTextInputFull()) {
        pressLetterGroup(4);
      }
    }
    _rightHeld = false;
    _rightLongHandled = false;
  }

  // Button::Up release (side left) — backspace
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    bool wasLongPress = _upLongHandled;
    _upHeld = false;
    _upLongHandled = false;
    _backspaceLastActionMs = 0;

    // Reset combo tracking when both buttons released
    if (_upRightComboHandled && !mappedInput.isPressed(MappedInputManager::Button::Right)) {
      _upRightComboHandled = false;
    }

    if (!wasLongPress) {
      if (_mode == t4::T4Mode::PREDICT || _mode == t4::T4Mode::MULTI_TAP) {
        LOG_DBG("T4", "loop: Up → backspace (mode=%d, textLen=%u)", static_cast<int>(_mode),
                _inputEngine.getConfirmedTextLength());
        _inputEngine.backspace();
        _lang = _inputEngine.getLanguage();
        _sentenceCfg = t4::getSentenceConfig(_lang);
        // Backspace may auto-switch the language to the word being deleted
        // (e.g. back to English without a dictionary). Re-derive the effective
        // mode so a dict-less language falls back to Multi-tap; otherwise a
        // stale PREDICT mode with no dictionary would block all input.
        applyEffectiveMode();
        _punctIndex = 0;
        _wordJustConfirmed = false;
        _candidateScrollX = 0;
        // When the sentence-ending punctuation that triggered auto-cap
        // is backspaced away, cancel the auto-cap.
        if (_autoCapFromSentence) {
          const char* t = _inputEngine.getConfirmedText();
          int len = static_cast<int>(strlen(t));
          int pos = len - 1;
          while (pos >= 0 && t[pos] == ' ') pos--;
          bool stillEnds = false;
          if (pos >= 0) {
            int cs = pos;
            while (cs > 0 && (static_cast<unsigned char>(t[cs]) & 0xC0) == 0x80) cs--;
            unsigned char c0 = static_cast<unsigned char>(t[cs]);
            uint8_t blen = 1;
            if ((c0 & 0xE0) == 0xC0)
              blen = 2;
            else if ((c0 & 0xF0) == 0xE0)
              blen = 3;
            else if ((c0 & 0xF8) == 0xF0)
              blen = 4;
            stillEnds = t4::isSentenceEndChar(_sentenceCfg->endChars, t + cs, blen);
          }
          if (!stillEnds) {
            _autoCap = false;
            _autoCapFromSentence = false;
            if (_inputEngine.getShiftLevel() == 1) _inputEngine.setShiftLevel(0);
          }
        }
        requestUpdate();
      }
    } else {
      LOG_DBG("T4", "loop: Up release after long-press, skip short-press action");
    }
  }

  // Button::Down release (side right) — punctuation
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    bool wasLongPress = _downLongHandled;
    _downHeld = false;
    _downLongHandled = false;
    if (!wasLongPress) {
      LOG_DBG("T4", "loop: Down → punctuation (mode=%d)", static_cast<int>(_mode));
      handlePunctuation();
      _upSideCyclesCandidates = false;
      requestUpdate();
    }
  }
}

void T4EntryActivity::handleBackspaceHoldRepeat() {
  // _upLongHandled is set by both long-press and combo, so this
  // naturally skips when either is active.
  if (_upLongHandled) return;
  if (!mappedInput.isPressed(MappedInputManager::Button::Up)) return;

  unsigned long held = mappedInput.getHeldTime();
  if (held < BACKSPACE_INITIAL_DELAY_MS) return;

  if (_backspaceLastActionMs == 0 || millis() - _backspaceLastActionMs >= BACKSPACE_REPEAT_MS) {
    if (_backspaceLastActionMs == 0) {
      LOG_DBG("T4", "loop: CMD backspace hold start (held=%lums)", held);
    }
    _inputEngine.backspace();
    _lang = _inputEngine.getLanguage();
    _sentenceCfg = t4::getSentenceConfig(_lang);
    // Backspace may auto-switch the language to the word being deleted
    // (e.g. back to English without a dictionary). Re-derive the effective
    // mode so a dict-less language falls back to Multi-tap; otherwise a
    // stale PREDICT mode with no dictionary would block all input.
    applyEffectiveMode();
    _backspaceLastActionMs = millis();

    // When the sentence-ending punctuation that triggered auto-cap
    // is backspaced away, cancel the auto-cap.
    if (_autoCapFromSentence) {
      const char* t = _inputEngine.getConfirmedText();
      int len = static_cast<int>(strlen(t));
      int pos = len - 1;
      while (pos >= 0 && t[pos] == ' ') pos--;
      bool stillEnds = false;
      if (pos >= 0) {
        int cs = pos;
        while (cs > 0 && (static_cast<unsigned char>(t[cs]) & 0xC0) == 0x80) cs--;
        unsigned char c0 = static_cast<unsigned char>(t[cs]);
        uint8_t blen = 1;
        if ((c0 & 0xE0) == 0xC0)
          blen = 2;
        else if ((c0 & 0xF0) == 0xE0)
          blen = 3;
        else if ((c0 & 0xF8) == 0xF0)
          blen = 4;
        stillEnds = t4::isSentenceEndChar(_sentenceCfg->endChars, t + cs, blen);
      }
      if (!stillEnds) {
        _autoCap = false;
        _autoCapFromSentence = false;
        if (_inputEngine.getShiftLevel() == 1) _inputEngine.setShiftLevel(0);
      }
    }
    requestUpdate();
  }
}

void T4EntryActivity::pollTimeBasedState() {
  // Auto-hide punctuation popup when the cycle timeout expires.
  if (_wordJustConfirmed && (millis() - _lastConfirmMs > t4::T4InputEngine<>::kMultiTapTimeoutMs)) {
    _wordJustConfirmed = false;
    requestUpdate();
  }

  // Poll predictor for any time-based state changes.
  const bool hadActiveTap = (_mode == t4::T4Mode::MULTI_TAP && _inputEngine.getActiveButton() != 0);
  _inputEngine.poll(millis());
  if (hadActiveTap && _inputEngine.getActiveButton() == 0) {
    requestUpdate();
  }
}

// ── Button Dispatch ──────────────────────────────────────────────────────

void T4EntryActivity::onComplete() {
  // MULTI_TAP: commit any still-cycling letter into confirmed text so it
  // isn't silently dropped when the user long-presses Confirm before the
  // 800 ms multi-tap timeout expires.  In PREDICT mode _activeButton is
  // always 0, so this is a harmless no-op.
  _inputEngine.fixMultiTapLetter();

  std::string result(_inputEngine.getConfirmedText());
  // In PREDICT: append the still-unconfirmed candidate to the engine text
  if (_mode == t4::T4Mode::PREDICT) {
    const char* candidate = _inputEngine.getCurrentCandidate();
    if (candidate && candidate[0] != '\0' && result.length() + strlen(candidate) <= textLimitBytes()) {
      result += candidate;
    }
  }
  // T4 inserts a trailing space only as a word/punctuation separator — there
  // is no bare-space key — so any trailing space in a Text result is a
  // separator artifact, never semantically meaningful. Strip all of them on
  // exit. (Do not gate this on _wordJustConfirmed: that flag is cleared 800 ms
  // after the last punctuation action, so a Confirm pressed later — the normal
  // case — would keep the separator in the message.) Password and URL fields
  // are excluded: a trailing space there could be intentional.
  if (_inputType == InputType::Text) {
    while (!result.empty() && result.back() == ' ') result.pop_back();
  }
  LOG_DBG("T4", "onComplete: mode=%d, result='%s'", static_cast<int>(_mode), result.c_str());
  learnIntoLexicon(result);
  setResult(KeyboardResult{std::move(result)});
  finish();
}

void T4EntryActivity::onCancel() {
  LOG_DBG("T4", "onCancel");
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

// ── Text limit ───────────────────────────────────────────────────────────

bool T4EntryActivity::isTextInputFull() const { return _inputEngine.getConfirmedTextLength() >= textLimitBytes(); }

size_t T4EntryActivity::textLimitBytes() const {
  constexpr auto kMax = decltype(_inputEngine)::kMaxTextLen;
  if (_maxLength == 0) return kMax;
  return (_maxLength < kMax) ? _maxLength : kMax;
}

size_t T4EntryActivity::punctBytesWithin(size_t textLen, const char* punct, size_t limit) {
  const size_t len = strlen(punct);
  const bool hasSpace = (len > 1 && punct[len - 1] == ' ');
  const size_t noSpace = hasSpace ? len - 1 : len;
  if (textLen + noSpace > limit) return 0;
  return (textLen + len <= limit) ? len : noSpace;
}

// ── Punctuation Handling ─────────────────────────────────────────────────

constexpr const char* T4EntryActivity::PUNCT_CYCLE[];
constexpr const char* T4EntryActivity::PUNCT_CYCLE_BARE[];
// Note: constexpr static arrays need out-of-class definition in C++17

bool T4EntryActivity::isAutoCapPunct(const char* punct, const t4::SentenceConfig& cfg) {
  if (!punct || punct[0] == '\0') return false;
  if (!cfg.autoCapitalize) return false;
  // Check that the punctuation is followed by a space (the keyboard
  // always appends " " after punctuation).
  if (punct[1] != ' ') return false;
  // Determine the UTF-8 byte length of the first character in punct.
  unsigned char c0 = static_cast<unsigned char>(punct[0]);
  uint8_t blen = 1;
  if ((c0 & 0xE0) == 0xC0)
    blen = 2;
  else if ((c0 & 0xF0) == 0xE0)
    blen = 3;
  else if ((c0 & 0xF8) == 0xF0)
    blen = 4;
  return t4::isSentenceEndChar(cfg.endChars, punct, blen);
}

void T4EntryActivity::handlePunctuation() {
  const char* candidate = _inputEngine.getCurrentCandidate();
  bool hasCandidate = candidate && candidate[0] != '\0';

  // Work on a local mutable copy of engine text; sync back at the end.
  std::string text(_inputEngine.getConfirmedText());
  LOG_DBG("T4", "handlePunct: mode=%d wjc=%d pIdx=%d hasCand=%d cand='%s' text='%s'", static_cast<int>(_mode),
          _wordJustConfirmed, _punctIndex, hasCandidate, hasCandidate ? candidate : "(none)", text.c_str());

  // MULTI_TAP: fix any cycling letter (no space — letters are already
  // individually confirmed by timeout or next press). Then apply punctuation.
  if (_mode == t4::T4Mode::MULTI_TAP) {
    _inputEngine.fixMultiTapLetter();
    text = _inputEngine.getConfirmedText();

    if (_wordJustConfirmed && millis() - _lastConfirmMs <= decltype(_inputEngine)::kMultiTapTimeoutMs) {
      // Within timeout: cycle punctuation
      // Only cycle when the replacement fits within the field limit — the
      // marks differ in width (" " vs "... "), and a trailing space
      // separator is dropped when it alone would overflow (". " → ".").
      // Erase exactly the mark actually applied (_lastPunctLen): a mark
      // truncated to its glyph is shorter than the cycle entry, and erasing
      // the full entry would clip the preceding character's UTF-8 tail.
      uint8_t nextIdx = (_punctIndex + 1) % PUNCT_COUNT;
      const char* nextPunct = punctCycle()[nextIdx];
      const size_t strippedLen = (text.length() >= _lastPunctLen) ? text.length() - _lastPunctLen : text.length();
      const size_t usable = punctBytesWithin(strippedLen, nextPunct, textLimitBytes());
      if (usable > 0) {
        if (text.length() >= _lastPunctLen) {
          text.erase(text.length() - _lastPunctLen);
        }
        _punctIndex = nextIdx;
        text.append(nextPunct, usable);
        _lastPunctLen = usable;
        // Sentence-ending punctuation → auto-cap next word (Text only).
        // Landing on a non-sentence-ending mark (e.g. ", " after cycling
        // through ". ") must cancel a previously armed auto-cap: otherwise
        // the next word would be capitalised after a comma.
        if (_inputType == InputType::Text) {
          if (isAutoCapPunct(nextPunct, *_sentenceCfg)) {
            if (_inputEngine.getShiftLevel() == 0) {
              _inputEngine.setShiftLevel(1);
              _autoCap = true;
              _autoCapFromSentence = true;
            }
          } else if (_autoCapFromSentence) {
            _autoCap = false;
            _autoCapFromSentence = false;
            if (_inputEngine.getShiftLevel() == 1) _inputEngine.setShiftLevel(0);
          }
        }
      }
      _lastConfirmMs = millis();
      LOG_DBG("T4", "handlePunct: MULTI_TAP cycle punct[%d]='%s' → text='%s'", _punctIndex, punctCycle()[_punctIndex],
              text.c_str());
    } else {
      // First confirm or timeout expired: add trailing space.  Refuse when
      // the field is at its limit so punctuation cannot exceed it.
      if (text.length() < textLimitBytes()) {
        text += ' ';
        _punctIndex = 0;
        _lastPunctLen = 1;
        _wordJustConfirmed = true;
        _candidateScrollX = 0;
        _lastConfirmMs = millis();
        LOG_DBG("T4", "handlePunct: MULTI_TAP first confirm → text='%s'", text.c_str());
      }
    }
    _inputEngine.setConfirmedText(text.c_str());
    requestUpdate();
    return;
  }

  // PREDICT mode:
  if (_wordJustConfirmed) {
    // Timeout: if user waited too long, start fresh with new space
    if (millis() - _lastConfirmMs > decltype(_inputEngine)::kMultiTapTimeoutMs) {
      LOG_DBG("T4", "handlePunct: PREDICT punct timeout expired, resetting _wordJustConfirmed");
      _wordJustConfirmed = false;
      // Fall through to first-press logic below
    } else {
      // Remove previous punctuation suffix, then append next in cycle —
      // but only when the replacement fits within the field limit: the
      // marks differ in width (" " vs "... "), and a trailing space
      // separator is dropped when it alone would overflow (". " → ".").
      // Erase exactly the mark actually applied (_lastPunctLen) so a mark
      // truncated to its glyph cannot clip the preceding character.
      uint8_t nextIdx = (_punctIndex + 1) % PUNCT_COUNT;
      const char* nextPunct = punctCycle()[nextIdx];
      const size_t strippedLen = (text.length() >= _lastPunctLen) ? text.length() - _lastPunctLen : text.length();
      // Only apply if it won't overflow the field limit
      const size_t usable = punctBytesWithin(strippedLen, nextPunct, textLimitBytes());
      if (usable > 0) {
        if (text.length() >= _lastPunctLen) {
          text.erase(text.length() - _lastPunctLen);
        }
        _punctIndex = nextIdx;
        text.append(nextPunct, usable);
        _lastPunctLen = usable;
        // Sentence-ending punctuation → auto-cap next word (Text only).
        // Cycling onto a non-sentence-ending mark must cancel any pending
        // auto-cap (see comment above; matches the MULTI_TAP cycle).
        if (_inputType == InputType::Text) {
          if (isAutoCapPunct(nextPunct, *_sentenceCfg)) {
            if (_inputEngine.getShiftLevel() == 0) {
              _inputEngine.setShiftLevel(1);
              _autoCap = true;
              _autoCapFromSentence = true;
            }
          } else if (_autoCapFromSentence) {
            _autoCap = false;
            _autoCapFromSentence = false;
            if (_inputEngine.getShiftLevel() == 1) _inputEngine.setShiftLevel(0);
          }
        }
      }
      _lastConfirmMs = millis();
      LOG_DBG("T4", "handlePunct: PREDICT cycle punct[%d]='%s' → text='%s'", _punctIndex, punctCycle()[_punctIndex],
              text.c_str());
      _inputEngine.setConfirmedText(text.c_str());
      requestUpdate();
      return;
    }
  }

  // First press: confirm current candidate into confirmed text
  if (hasCandidate) {
    if (isTextInputFull()) {
      // Buffer full — can't confirm candidate. Do nothing.
      return;
    }
    const char* word = _inputEngine.getCurrentCandidate();
    // Apply the active Shift/Caps state (Caps → whole word, Shift/auto-cap →
    // first letter). Returns the raw candidate when neither is active.
    std::string cased = applyWordCase(word);
    if (!cased.empty()) word = cased.c_str();

    // Confirm only if the word plus its separator fits within the field
    // limit; otherwise leave the candidate unconfirmed (field at its cap).
    std::string nextText = text;
    if (!nextText.empty() && nextText.back() != ' ') nextText += ' ';
    nextText += word;
    if (nextText.length() > textLimitBytes()) {
      return;
    }

    // Consume the one-shot state: auto-cap and one-shot Shift last for a
    // single word; Caps Lock stays on until toggled off.
    _autoCap = false;
    _autoCapFromSentence = false;
    if (_inputEngine.getShiftLevel() == 1) _inputEngine.setShiftLevel(0);

    text = std::move(nextText);

    // Reset predictor sequence for next word
    _inputEngine.confirmWord();
    if (text.length() < textLimitBytes()) {
      text += ' ';  // trailing space after confirmed word
      _lastPunctLen = 1;
    } else {
      _lastPunctLen = 0;
    }
    _punctIndex = 0;
    _wordJustConfirmed = true;
    _candidateScrollX = 0;
    _lastConfirmMs = millis();
    LOG_DBG("T4", "handlePunct: PREDICT confirm '%s' → text='%s' autoCap=%d", word, text.c_str(), _autoCap);
  } else if (!isTextInputFull()) {
    // No candidate: just append space
    text += ' ';
    _punctIndex = 0;
    _lastPunctLen = 1;
    _wordJustConfirmed = true;
    _lastConfirmMs = millis();
    LOG_DBG("T4", "handlePunct: PREDICT no-candidate → text='%s'", text.c_str());
  }
  _inputEngine.setConfirmedText(text.c_str());
}

// ── Mode Transitions ─────────────────────────────────────────────────────

void T4EntryActivity::applyEffectiveMode() {
  // Password and URL input types are always Multi-tap, regardless of
  // language or dictionary availability.
  if (_inputType != InputType::Text || !languageSupportsPredict(_lang)) {
    if (_mode != t4::T4Mode::MULTI_TAP) {
      LOG_INF("T4", "applyEffectiveMode: forcing Multi-tap (inputType=%d lang=%d)", static_cast<int>(_inputType),
              static_cast<int>(_lang));
      _inputEngine.setMode(t4::T4Mode::MULTI_TAP);
      _mode = _inputEngine.getMode();
    }
  } else {
    // Text input with dictionary — use the user's preferred mode
    if (_mode != _userMode) {
      LOG_DBG("T4", "applyEffectiveMode: restoring user mode=%d for lang=%d", static_cast<int>(_userMode),
              static_cast<int>(_lang));
      _inputEngine.setMode(_userMode);
      _mode = _inputEngine.getMode();
    }
  }
}

bool T4EntryActivity::togglePredictMultiTap() {
  t4::T4Mode newMode = (_mode == t4::T4Mode::MULTI_TAP) ? t4::T4Mode::PREDICT : t4::T4Mode::MULTI_TAP;
  LOG_DBG("T4", "togglePredictMultiTap: %d → %d, text='%s'", static_cast<int>(_mode), static_cast<int>(newMode),
          _inputEngine.getConfirmedText());
  if (newMode == _mode) return false;

  // Commit unconfirmed candidate when leaving PREDICT
  if (_mode == t4::T4Mode::PREDICT && newMode == t4::T4Mode::MULTI_TAP) {
    const char* cand = _inputEngine.getCurrentCandidate();
    LOG_DBG("T4", "togglePredictMultiTap: PREDICT→MULTI_TAP, cand='%s' seqLen=%u", cand ? cand : "(null)",
            _inputEngine.getSequenceLength());
    if (cand && cand[0] != '\0') {
      std::string t(_inputEngine.getConfirmedText());
      if (t.length() + strlen(cand) <= textLimitBytes()) {
        t += cand;
      }
      _inputEngine.setConfirmedText(t.c_str());
    }
  }

  _inputEngine.setMode(newMode);
  _mode = _inputEngine.getMode();
  _wordJustConfirmed = false;
  _punctIndex = 0;
  _candidateScrollX = 0;
  _upSideCyclesCandidates = false;
  LOG_DBG("T4", "togglePredictMultiTap: done, _mode=%d text='%s'", static_cast<int>(_mode),
          _inputEngine.getConfirmedText());
  return true;
}

bool T4EntryActivity::languageSupportsPredict(t4::T4Language lang) const {
  const char* path = t4::getDictionaryPath(lang);
  if (!path) return false;  // DIGIT — no dictionary
  return Storage.exists(path);
}

// ── User lexicon ─────────────────────────────────────────────────────────

void T4EntryActivity::loadUserLexicon() {
  _lexicon = makeUniqueNoThrow<t4::T4UserLexicon>();
  if (!_lexicon) {
    LOG_ERR("T4", "OOM: user lexicon");
    return;
  }
  _inputEngine.setUserLexicon(_lexicon.get());

  if (!Storage.exists(USER_LEXICON_PATH)) {
    LOG_DBG("T4", "No user lexicon yet: %s", USER_LEXICON_PATH);
    return;
  }

  HalFile file;
  if (!Storage.openFileForRead("T4", USER_LEXICON_PATH, file)) {
    LOG_ERR("T4", "Failed to open user lexicon: %s", USER_LEXICON_PATH);
    return;
  }

  const size_t size = file.size();
  if (size < t4::T4UserLexicon::kHeaderSize || size > t4::T4UserLexicon::kMaxSerializedSize) {
    LOG_ERR("T4", "User lexicon size out of range: %u", static_cast<unsigned>(size));
    return;
  }

  auto buffer = makeUniqueNoThrow<uint8_t[]>(size);
  if (!buffer) {
    LOG_ERR("T4", "OOM: user lexicon buffer %u bytes", static_cast<unsigned>(size));
    return;
  }
  if (file.read(buffer.get(), size) != static_cast<int>(size)) {
    LOG_ERR("T4", "Short read on user lexicon");
    return;
  }
  if (!_lexicon->loadFromBuffer(buffer.get(), size)) {
    LOG_ERR("T4", "Invalid user lexicon, starting empty");
    _lexicon->clear();
    return;
  }
  LOG_INF("T4", "Loaded user lexicon: %u words", _lexicon->getEntryCount());
}

void T4EntryActivity::saveUserLexicon() {
  if (!_lexicon || !_lexicon->isDirty()) return;

  auto buffer = makeUniqueNoThrow<uint8_t[]>(t4::T4UserLexicon::kMaxSerializedSize);
  if (!buffer) {
    LOG_ERR("T4", "OOM: user lexicon save buffer");
    return;
  }
  const size_t written = _lexicon->serialize(buffer.get(), t4::T4UserLexicon::kMaxSerializedSize);
  if (written == 0) {
    LOG_ERR("T4", "Failed to serialize user lexicon");
    return;
  }

  Storage.mkdir(USER_LEXICON_DIR);
  HalFile file;
  if (!Storage.openFileForWrite("T4", USER_LEXICON_PATH, file)) {
    LOG_ERR("T4", "Failed to open user lexicon for writing");
    return;
  }
  if (file.write(buffer.get(), written) != written) {
    LOG_ERR("T4", "Short write on user lexicon");
    return;
  }
  LOG_INF("T4", "Saved user lexicon: %u words, %u bytes", _lexicon->getEntryCount(), static_cast<unsigned>(written));
}

void T4EntryActivity::learnIntoLexicon(const std::string& text) {
  // Password and URL fields never allocate a lexicon, so this is also the
  // guard that keeps secrets out of the store.
  if (!_lexicon) return;
  const uint16_t learned = _lexicon->learnText(_lang, text.c_str(), _initialText.c_str());
  LOG_DBG("T4", "learnIntoLexicon: learned %u words", learned);
}

// ── Shift / Uppercase ────────────────────────────────────────────────────

void T4EntryActivity::cycleShift() {
  // All modes cycle Off → Shift → Caps → Off.  When _autoCap is set
  // it means level 1 was auto-generated — clear it so the user's manual
  // action takes over, then cycle from the engine's current level.
  _autoCap = false;
  _autoCapFromSentence = false;
  uint8_t level = (_inputEngine.getShiftLevel() + 1) % 3;
  _inputEngine.setShiftLevel(level);
  LOG_DBG("T4", "cycleShift: mode=%d → level=%u", static_cast<int>(_mode), level);
}

std::string T4EntryActivity::applyWordCase(const char* word) const {
  if (!word || word[0] == '\0') return std::string();

  const uint8_t level = _inputEngine.getShiftLevel();
  const bool capsLock = (level == 2);
  // One-shot Shift and auto-cap are only meaningful for languages
  // that have letter case (English, Russian, etc.).  For DIGIT and
  // case-less scripts (Hebrew) they produce no change.
  const bool firstOnly = ((level == 1) || _autoCap) && _sentenceCfg->autoCapitalize;
  if (!capsLock && !firstOnly) return std::string(word);

  // Caps: uppercase every letter. Shift/auto-cap: only the first letter.
  std::string out;
  const char* p = word;
  bool firstDone = false;
  while (*p) {
    unsigned char c0 = static_cast<unsigned char>(*p);
    uint8_t blen = 1;
    if ((c0 & 0xE0) == 0xC0)
      blen = 2;
    else if ((c0 & 0xF0) == 0xE0)
      blen = 3;
    else if ((c0 & 0xF8) == 0xF0)
      blen = 4;

    if (capsLock || !firstDone) {
      char up[4];
      uint8_t upLen = t4::upperLetterUtf8(p, blen, up);
      out.append(up, upLen);
    } else {
      out.append(p, blen);
    }
    firstDone = true;
    p += blen;
  }
  return out;
}

// ── Rendering ────────────────────────────────────────────────────────────

void T4EntryActivity::textFieldMargins(int pageWidth, int& leftMargin, int& rightMargin) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int reserve = 2 * metrics.sideButtonHintsWidth + metrics.sideButtonHintsGap + metrics.sideButtonHintsMargin;
  const int available = pageWidth - 2 * reserve;
  const int extra = (available - available * metrics.keyboardTextFieldWidthPercent / 100) / 2;
  leftMargin = reserve + extra;
  rightMargin = reserve + extra;
}

void T4EntryActivity::renderInfoLine(int aboveTextY, bool overflow) {
  int leftMargin = 0;
  int rightMargin = 0;
  textFieldMargins(renderer.getScreenWidth(), leftMargin, rightMargin);
  const int rightX = renderer.getScreenWidth() - rightMargin;
  const int infoY = aboveTextY - renderer.getLineHeight(UI_12_FONT_ID);
  char buf[32];

  // Measure total width for right-alignment
  int totalW = 0;
  const char* overflowStr = "^^^^^^  ";
  int overflowW = 0;
  if (overflow) {
    overflowW = renderer.getTextWidth(UI_12_FONT_ID, overflowStr);
    totalW += overflowW;
  }
  snprintf(buf, sizeof(buf), "%u/%u", _inputEngine.getConfirmedTextLength(), (unsigned)_maxLength);
  totalW += renderer.getTextWidth(UI_12_FONT_ID, buf);

  int infoX = rightX - totalW;
  if (overflow) {
    renderer.drawText(UI_12_FONT_ID, infoX, infoY, overflowStr, true);
    infoX += overflowW;
  }
  snprintf(buf, sizeof(buf), "%u/%u", _inputEngine.getConfirmedTextLength(), (unsigned)_maxLength);
  renderer.drawText(UI_12_FONT_ID, infoX, infoY, buf, true);
}

// ── renderHeader ─────────────────────────────────────────────────────────

void T4EntryActivity::renderHeader() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  char subtitle[32];
  const char* modeLabel;
  switch (_mode) {
    case t4::T4Mode::MULTI_TAP:
      modeLabel = tr(STR_T4_MODE_TAP);
      break;
    default:
      modeLabel = tr(STR_T4_MODE_PREDICT);
  }
  // Shift indicator: "Abc" = one-shot Shift, "ABC" = Caps Lock.
  const char* shiftLabel = "";
  switch (_inputEngine.getShiftLevel()) {
    case 1:
      shiftLabel = " | Abc";
      break;
    case 2:
      shiftLabel = " | ABC";
      break;
    default:
      shiftLabel = "";
  }
  snprintf(subtitle, sizeof(subtitle), "%s | %s%s", t4::getLanguageName(_lang), modeLabel, shiftLabel);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, _title.c_str(), subtitle);
}

// ── renderTextField ──────────────────────────────────────────────────────

int T4EntryActivity::renderTextField(int startY, int lineHeight, int maxHeight, bool& overflowOut) {
  const int pageWidth = renderer.getScreenWidth();

  const char* candidate = _inputEngine.getCurrentCandidate();
  bool hasCandidate = (candidate != nullptr) && (candidate[0] != '\0');

  // Show the candidate in its committed case form so Shift/Caps is WYSIWYG.
  std::string candCased;
  if (hasCandidate && _inputType != InputType::Password) {
    candCased = applyWordCase(candidate);
    if (!candCased.empty()) candidate = candCased.c_str();
  }

  const char* confirmedText = _inputEngine.getConfirmedText();

  // Build full text (real content, not masked)
  char fullText[512];
  if (_inputType == InputType::Password && !_passwordVisible) {
    const size_t maskCount = utf8CharCount(confirmedText);
    size_t i;
    for (i = 0; i < maskCount && i < 511; i++) fullText[i] = '*';
    if (hasCandidate) {
      // Multi-tap: the cycling letter may itself be multi-byte — mask it as a
      // single '*' per code point too.
      const size_t candMask = utf8CharCount(candidate);
      const size_t candFit = (i + candMask < 511) ? candMask : (511 - i);
      for (size_t j = 0; j < candFit; j++) fullText[i + j] = '*';
      fullText[i + candFit] = '\0';
    } else {
      fullText[i] = '\0';
    }
  } else if (hasCandidate) {
    snprintf(fullText, sizeof(fullText), "%s%s", confirmedText, candidate);
  } else {
    snprintf(fullText, sizeof(fullText), "%s", confirmedText);
  }

  // Display text (masked for password)
  std::string displayText;
  if (_inputType == InputType::Password && !_passwordVisible) {
    displayText = std::string(strlen(fullText), '*');
  } else {
    displayText = fullText;
  }

  const bool isPassword = (_inputType == InputType::Password);
  int leftMargin = 0;
  int rightMargin = 0;
  textFieldMargins(pageWidth, leftMargin, rightMargin);
  const int maxLineWidth = pageWidth - leftMargin - rightMargin;

  // ── Pass 1: collect line boundaries (no rendering) ────────────────────
  struct LineInfo {
    int startIdx;  // byte offset in displayText
    int endIdx;    // byte offset in displayText (exclusive)
    bool hardBreak;
  };
  static constexpr int kMaxLines = 30;
  LineInfo lines[kMaxLines];
  int lineCount = 0;

  int lineStartIdx = 0;
  int lineEndIdx = static_cast<int>(displayText.length());

  while (lineCount < kMaxLines) {
    // Hard newline: force line break at \n before pixel-width check
    bool hardBreak = false;
    {
      size_t nlPos = displayText.find('\n', static_cast<size_t>(lineStartIdx));
      if (nlPos != std::string::npos && static_cast<int>(nlPos) < lineEndIdx) {
        lineEndIdx = static_cast<int>(nlPos);
        hardBreak = true;
      }
    }

    std::string lineText =
        displayText.substr(static_cast<size_t>(lineStartIdx), static_cast<size_t>(lineEndIdx - lineStartIdx));
    int textWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
    if (textWidth <= maxLineWidth) {
      lines[lineCount].startIdx = lineStartIdx;
      lines[lineCount].endIdx = lineEndIdx;
      lines[lineCount].hardBreak = hardBreak;
      lineCount++;

      // Last segment of the text?
      if (lineEndIdx == static_cast<int>(displayText.length())) break;

      lineStartIdx = hardBreak ? (lineEndIdx + 1) : lineEndIdx;
      lineEndIdx = static_cast<int>(displayText.length());
    } else {
      // Walk back one full UTF-8 character — not just one byte.
      lineEndIdx -= 1;
      while (lineEndIdx > lineStartIdx) {
        unsigned char c = static_cast<unsigned char>(displayText[static_cast<size_t>(lineEndIdx)]);
        if ((c & 0xC0) != 0x80) break;  // not a continuation byte
        lineEndIdx -= 1;
      }
    }
  }

  // ── Determine visible range ───────────────────────────────────────────
  // drawTextField adds one lineHeight + verticalSpacing below the given
  // rect for the underline, so visibleHeight must exclude the last text
  // line (matching the original inputHeight = (N-1) * lineHeight convention).
  const int totalHeight = lineCount * lineHeight;
  int visibleHeight;
  int firstVisible;
  bool overflow = false;

  if (maxHeight > 0 && totalHeight > maxHeight) {
    const int maxVisibleLines = maxHeight / lineHeight;
    if (maxVisibleLines < 1) {
      // Not enough room for even one line — clamp to 1.
      firstVisible = lineCount - 1;
      visibleHeight = 0;
    } else {
      firstVisible = lineCount - maxVisibleLines;
      visibleHeight = (maxVisibleLines - 1) * lineHeight;
    }
    overflow = true;
  } else {
    firstVisible = 0;
    // Height of all lines except the last (drawTextField adds the rest).
    visibleHeight = (lineCount > 0) ? ((lineCount - 1) * lineHeight) : 0;
  }

  // ── Pass 2: render only the visible lines ─────────────────────────────
  int cursorPixelX = leftMargin;
  int cursorLineY = startY;

  for (int li = firstVisible; li < lineCount; li++) {
    const auto& line = lines[li];
    const int drawY = startY + (li - firstVisible) * lineHeight;
    const int lineStartX = leftMargin;
    std::string lineText =
        displayText.substr(static_cast<size_t>(line.startIdx), static_cast<size_t>(line.endIdx - line.startIdx));
    const bool isLastLine = (li == lineCount - 1);

    if (isLastLine) {
      // Last visible line: handle candidate/tap-letter highlighting + cursor
      size_t confLen = strlen(confirmedText);
      int confLenInLine = static_cast<int>(confLen) - line.startIdx;

      if (!isPassword && hasCandidate && _mode == t4::T4Mode::PREDICT && confLenInLine > 0) {
        // Draw confirmed portion normally, candidate inverted
        std::string confPart = lineText.substr(0, static_cast<size_t>(confLenInLine));
        renderer.drawText(UI_12_FONT_ID, lineStartX, drawY, confPart.c_str(), true);
        int confW = renderer.getTextAdvanceX(UI_12_FONT_ID, confPart.c_str(), EpdFontFamily::REGULAR);
        std::string candPart = lineText.substr(static_cast<size_t>(confLenInLine));
        int candW = renderer.getTextAdvanceX(UI_12_FONT_ID, candPart.c_str(), EpdFontFamily::REGULAR);
        if (candW > 0) {
          int candX = lineStartX + confW;
          renderer.fillRect(candX, drawY, candW + 4, lineHeight, true);
          renderer.drawText(UI_12_FONT_ID, candX + 2, drawY, candPart.c_str(), false);
        }
        int beforeWidth = confW + (candW > 0 ? candW + 4 : 0);
        cursorPixelX = lineStartX + beforeWidth;
      } else if (!isPassword && hasCandidate && _mode == t4::T4Mode::PREDICT && confLenInLine <= 0) {
        // Confirmed text ended on previous line; entire last line is candidate
        int candW = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
        renderer.fillRect(lineStartX, drawY, candW + 4, lineHeight, true);
        renderer.drawText(UI_12_FONT_ID, lineStartX + 2, drawY, lineText.c_str(), false);
        cursorPixelX = lineStartX + candW + 4;
      } else if (!isPassword && _mode == t4::T4Mode::MULTI_TAP) {
        // Multi-tap: confirmed text + cycling letter highlighted
        renderer.drawText(UI_12_FONT_ID, lineStartX, drawY, lineText.c_str(), true);
        uint8_t tapLen = 0;
        const char* tapPtr = _inputEngine.getCurrentTapLetter(tapLen);
        int confW = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
        if (tapPtr && tapLen > 0) {
          char tapStr[5];
          uint8_t dispLen = tapLen;
          if (_inputEngine.getShiftLevel() != 0) {
            dispLen = t4::upperLetterUtf8(tapPtr, tapLen, tapStr);
          } else {
            memcpy(tapStr, tapPtr, tapLen);
          }
          tapStr[dispLen] = '\0';
          int tapW = renderer.getTextWidth(UI_12_FONT_ID, tapStr);
          int tapX = lineStartX + confW;
          renderer.fillRect(tapX, drawY, tapW + 4, lineHeight, true);
          renderer.drawText(UI_12_FONT_ID, tapX + 2, drawY, tapStr, false);
          cursorPixelX = tapX + tapW + 4;
        } else {
          cursorPixelX = lineStartX + confW;
        }
      } else {
        // Password or no candidate: draw all text normally
        renderer.drawText(UI_12_FONT_ID, lineStartX, drawY, lineText.c_str(), true);
        int beforeWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
        cursorPixelX = lineStartX + beforeWidth;
      }
      cursorLineY = drawY;
    } else {
      // Non-last line: draw normally
      renderer.drawText(UI_12_FONT_ID, lineStartX, drawY, lineText.c_str(), true);
    }
  }

  // ── Draw text field border ────────────────────────────────────────────
  // Single-line: use actual text width.  Multi-line: use maxLineWidth.
  int fieldWidth = maxLineWidth;
  if (lineCount <= 1) {
    std::string lastLineText = (lineCount > 0)
                                   ? displayText.substr(static_cast<size_t>(lines[0].startIdx),
                                                        static_cast<size_t>(lines[0].endIdx - lines[0].startIdx))
                                   : std::string();
    fieldWidth = lastLineText.empty()
                     ? 0
                     : renderer.getTextAdvanceX(UI_12_FONT_ID, lastLineText.c_str(), EpdFontFamily::REGULAR);
  }
  GUI.drawTextField(renderer, Rect{0, startY, pageWidth, visibleHeight}, fieldWidth, false, leftMargin,
                    pageWidth - leftMargin - rightMargin);

  overflowOut = overflow;

  // ── Serif cursor at end of text ───────────────────────────────────────
  {
    static constexpr int serifW = 3;
    const int cX = cursorPixelX;
    const int cY = cursorLineY;
    const int cBottom = cursorLineY + lineHeight - 1;
    renderer.fillRect(cX, cY, 2, lineHeight, true);
    renderer.drawLine(cX - serifW, cY, cX - 1, cY, 2, true);
    renderer.drawLine(cX + 1, cY, cX + serifW, cY, 2, true);
    renderer.drawLine(cX - serifW, cBottom, cX - 1, cBottom, 2, true);
    renderer.drawLine(cX + 1, cBottom, cX + serifW, cBottom, 2, true);
  }

  return startY + visibleHeight;
}

// ── renderCandidateRow ───────────────────────────────────────────────────

int T4EntryActivity::renderCandidateRow(int startY) {
  if (_mode != t4::T4Mode::PREDICT) return startY;

  uint16_t candCount = _inputEngine.getCandidateCount();
  if (candCount == 0) return startY;

  const int pageWidth = renderer.getScreenWidth();
  int effectiveMargin = 0;
  int rightMargin = 0;
  textFieldMargins(pageWidth, effectiveMargin, rightMargin);
  const int textAreaWidth = pageWidth - effectiveMargin - rightMargin;
  const int rightEdge = effectiveMargin + textAreaWidth;

  uint16_t activeIdx = _inputEngine.getCandidateIndex();
  const int sepW = renderer.getTextWidth(UI_12_FONT_ID, "  ");
  const int ellipsisW = renderer.getTextWidth(UI_12_FONT_ID, "\u2026");
  const int leftEllipsisW = renderer.getTextWidth(UI_12_FONT_ID, "\u2026 ");

  // ── Active candidate: measure, center on screen ─────────────────────
  const char* activeName = _inputEngine.getCandidate(activeIdx);
  int activeW = renderer.getTextWidth(UI_12_FONT_ID, activeName);
  int activeX = effectiveMargin + (textAreaWidth - activeW) / 2;

  // ── Right side: collect from activeIdx+1 outward ────────────────────
  struct Item {
    int idx;
    int w;
  };
  Item rightItems[30];
  int rightCount = 0;
  bool hasRightEllipsis = false;
  {
    int rightX = activeX + activeW;
    for (int off = 1; off < (int)candCount; off++) {
      int idx = (activeIdx + off) % candCount;
      if (idx == (int)activeIdx) break;
      int w = renderer.getTextWidth(UI_12_FONT_ID, _inputEngine.getCandidate(idx));
      if (rightX + sepW + w + ellipsisW > rightEdge) {
        hasRightEllipsis = true;
        break;
      }
      rightItems[rightCount++] = {idx, w};
      rightX += sepW + w;
    }
  }

  // ── Left side: collect from activeIdx-1 outward ─────────────────────
  Item leftItems[30];
  int leftCount = 0;
  bool hasLeftEllipsis = false;
  {
    int leftX = activeX;
    for (int off = 1; off < (int)candCount; off++) {
      int idx = (activeIdx - off + candCount) % candCount;
      if (idx == (int)activeIdx) break;
      int w = renderer.getTextWidth(UI_12_FONT_ID, _inputEngine.getCandidate(idx));
      int need = (leftCount > 0 ? sepW : 0) + w + ellipsisW;
      if (leftX - need < effectiveMargin) {
        hasLeftEllipsis = true;
        break;
      }
      leftItems[leftCount++] = {idx, w};
      leftX -= sepW + w;
    }
  }

  // ── Compute total width of visible candidates (incl. separators) ────
  int totalVisibleW = activeW;
  for (int i = 0; i < leftCount; i++) totalVisibleW += sepW + leftItems[i].w;
  for (int i = 0; i < rightCount; i++) totalVisibleW += sepW + rightItems[i].w;

  // ── Center the candidate block in the full text area ─────────────────
  int drawX = effectiveMargin + (textAreaWidth - totalVisibleW) / 2;
  if (drawX < effectiveMargin) drawX = effectiveMargin;

  // ── Draw: left ellipsis → left candidates → active → right → right ellipsis ──
  if (hasLeftEllipsis) {
    int lx = drawX - leftEllipsisW;
    if (lx < effectiveMargin) lx = effectiveMargin;
    renderer.drawText(UI_12_FONT_ID, lx, startY, "\u2026 ", true);
  }

  // Left candidates (collected closest-to-active first; draw in reverse)
  for (int i = leftCount - 1; i >= 0; i--) {
    const char* name = _inputEngine.getCandidate(leftItems[i].idx);
    renderer.drawText(UI_12_FONT_ID, drawX, startY, name, true);
    drawX += leftItems[i].w + sepW;
  }

  // Active candidate
  if (_upSideCyclesCandidates) {
    // Inverted box: white text on black background
    const int boxH = renderer.getLineHeight(UI_12_FONT_ID);
    renderer.fillRect(drawX, startY, activeW, boxH, true);
    renderer.drawText(UI_12_FONT_ID, drawX, startY, activeName, false);
  } else {
    // Underlined
    renderer.drawText(UI_12_FONT_ID, drawX, startY, activeName, true);
    int ulY = startY + renderer.getTextHeight(UI_12_FONT_ID) + 2;
    renderer.fillRect(drawX, ulY, activeW, 2, true);
  }
  drawX += activeW + sepW;

  // Right candidates
  for (int i = 0; i < rightCount; i++) {
    const char* name = _inputEngine.getCandidate(rightItems[i].idx);
    renderer.drawText(UI_12_FONT_ID, drawX, startY, name, true);
    drawX += rightItems[i].w + sepW;
  }

  // Right ellipsis — attached right after last candidate
  if (hasRightEllipsis) {
    renderer.drawText(UI_12_FONT_ID, drawX, startY, "\u2026", true);
  }

  // ── Counter: "15 / 228" centered below ──────────────────────────────
  char counter[16];
  snprintf(counter, sizeof(counter), "%u / %u", static_cast<unsigned>(activeIdx + 1), static_cast<unsigned>(candCount));
  int counterW = renderer.getTextWidth(UI_12_FONT_ID, counter);
  int counterX = effectiveMargin + (textAreaWidth - counterW) / 2;
  int counterY = startY + renderer.getLineHeight(UI_12_FONT_ID) + 4;
  renderer.drawText(UI_12_FONT_ID, counterX, counterY, counter, true);

  return startY;
}

// ── maxLetterBlockHeight ─────────────────────────────────────────────────

int T4EntryActivity::maxLetterBlockHeight(int lineHeight) const {
  static constexpr int kBlockPadY = 6;
  static constexpr int kBlockRowGap = 6;
  static constexpr int kCharsPerRow = 3;

  int maxBlockH = 0;
  const t4::T4Language langs[] = {t4::T4Language::EN, t4::T4Language::ADDITIONAL, t4::T4Language::DIGIT};
  for (t4::T4Language lang : langs) {
    // Skip the ADDITIONAL slot when no additional layout is configured —
    // it is unreachable in the language cycle in that case.
    if (lang == t4::T4Language::ADDITIONAL && !t4::hasActiveAdditionalLayout()) continue;
    for (int i = 0; i < 4; i++) {
      int len = t4::getGroupLength(lang, i + 1);
      if (len == 0) continue;
      int rows = (len + kCharsPerRow - 1) / kCharsPerRow;
      int h = rows * lineHeight + (rows - 1) * kBlockRowGap + 2 * kBlockPadY;
      if (h > maxBlockH) maxBlockH = h;
    }
  }
  return maxBlockH;
}

// ── renderModeHint ────────────────────────────────────────────────────────

int T4EntryActivity::renderModeHint(int blocksBaseY) {
  // Compose the hint text. Predict mode prepends the Up+Right candidate
  // combo sentence; Multi-tap shows only the short/long-press legend.
  const char* hints = tr(STR_T4_HINTS_LONG_PRESS);
  char text[250];
  if (_mode == t4::T4Mode::PREDICT) {
    snprintf(text, sizeof(text), "%s %s", tr(STR_T4_HINTS_COMBO), hints);
  } else {
    snprintf(text, sizeof(text), "%s", hints);
  }

  // Constrain to the text-field area (between side-button hint capsules)
  // and anchor the block's bottom edge just above the letter panels.
  const int pageWidth = renderer.getScreenWidth();
  int leftMargin = 0;
  int rightMargin = 0;
  textFieldMargins(pageWidth, leftMargin, rightMargin);
  const int maxWidth = pageWidth - leftMargin - rightMargin;

  // Standard hint typography (SMALL_FONT_ID), same as drawHelpText.
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  static constexpr int kHintGapAboveBlocks = 100;
  auto lines = renderer.wrappedText(SMALL_FONT_ID, text, maxWidth, /*maxLines=*/6);
  const int bottomY = blocksBaseY - kHintGapAboveBlocks;
  const int topY = bottomY - static_cast<int>(lines.size()) * lineHeight;
  for (size_t i = 0; i < lines.size(); i++) {
    const int w = renderer.getTextAdvanceX(SMALL_FONT_ID, lines[i].c_str(), EpdFontFamily::REGULAR);
    const int x = leftMargin + (maxWidth - w) / 2;
    renderer.drawText(SMALL_FONT_ID, x, topY + static_cast<int>(i) * lineHeight, lines[i].c_str(), true);
  }

  return static_cast<int>(lines.size()) * lineHeight + kHintGapAboveBlocks;
}

// ── renderCandidateComboHint ─────────────────────────────────────────────

void T4EntryActivity::renderCandidateComboHint(int blocksBaseY) {
  if (_mode != t4::T4Mode::PREDICT || _upSideCyclesCandidates) return;
  if (_inputEngine.getCandidateCount() < 2) return;

  // The Up hint in T4 has no long-press companion capsule — it hugs the edge.
  const Rect upKey = GUI.getSideButtonUpRect(renderer, /*hasLongPressHint=*/false);

  static constexpr int kTipGap = 8;    // space between arrow tip and target
  static constexpr int kHeadLen = 3;   // arrowhead leg length
  static constexpr int kLabelGap = 4;  // gap between label and line ends
  static constexpr int kLineW = 1;     // connector stroke width

  const char* label = tr(STR_T4_CANDIDATES);
  const int labelW = renderer.getTextWidth(SMALL_FONT_ID, label);

  if (gpio.deviceIsX3()) {
    // X3: L-shaped connector — arrow at the Up capsule, drop down, horizontal
    // run above the blocks split around the label, arrow down at block 4.
    static constexpr int kAboveBlocks = 24;  // horizontal segment height above blocks
    const int* btnX = GUI.getButtonXPositions(true);
    const int btnW = GUI.getButtonHintWidth();

    const int leftTipX = upKey.x + upKey.width + kTipGap;
    const int leftY = upKey.y + upKey.height / 2;
    const int cornerX = leftTipX + 2 * kHeadLen;
    const int horizY = blocksBaseY - kAboveBlocks;
    const int rightX = btnX[3] + btnW / 2;
    const int downTipY = blocksBaseY - kTipGap;

    // Left arrowhead pointing at the Up (Bksp) capsule, stub to the corner
    renderer.drawLine(leftTipX, leftY, leftTipX + kHeadLen, leftY - kHeadLen, kLineW, true);
    renderer.drawLine(leftTipX, leftY, leftTipX + kHeadLen, leftY + kHeadLen, kLineW, true);
    renderer.fillRect(leftTipX, leftY, cornerX - leftTipX + 1, kLineW, true);
    // Vertical drop to the horizontal segment
    renderer.fillRect(cornerX, leftY, kLineW, horizY + kLineW - leftY, true);
    // Right arrowhead pointing down at the 4th letter block (Right button)
    renderer.drawLine(rightX, downTipY, rightX - kHeadLen, downTipY - kHeadLen, kLineW, true);
    renderer.drawLine(rightX, downTipY, rightX + kHeadLen, downTipY - kHeadLen, kLineW, true);
    // Vertical stub down to the block
    renderer.fillRect(rightX, horizY, kLineW, downTipY - horizY + 1, true);

    // Horizontal segment split around the centered label
    const int labelX = cornerX + (rightX - cornerX - labelW) / 2;
    const int labelY = horizY - renderer.getLineHeight(SMALL_FONT_ID) / 2;
    renderer.fillRect(cornerX, horizY, labelX - kLabelGap - cornerX + 1, kLineW, true);
    renderer.fillRect(labelX + labelW + kLabelGap, horizY, rightX - labelX - labelW - kLabelGap + 1, kLineW, true);
    renderer.drawText(SMALL_FONT_ID, labelX, labelY, label, true);
    return;
  }

  // X4: vertical connector in the corridor between the text area and the
  // right-side capsules. Top arrow points right at the Up (Bksp) capsule,
  // the label breaks the vertical, bottom arrow points down at the 4th
  // letter block (Right button).
  const int pageWidth = renderer.getScreenWidth();
  int leftMargin = 0;
  int rightMargin = 0;
  textFieldMargins(pageWidth, leftMargin, rightMargin);
  const int textRight = pageWidth - rightMargin;
  // The vertical run sits in the corridor left of the right-side capsule
  // stack. Its inner edge is the Space short-press capsule, inset by width+gap
  // because Space has a long-press companion — the same inset the Up capsule
  // would take with a long-press hint.
  const int innerX = GUI.getSideButtonUpRect(renderer, /*hasLongPressHint=*/true).x;
  const int lineX = textRight + (innerX - textRight) / 2;
  const int topY = upKey.y + upKey.height / 2;
  const int tipX = upKey.x - kTipGap;
  const int downTipY = blocksBaseY - kTipGap;
  const int labelH = renderer.getTextHeight(SMALL_FONT_ID);
  static constexpr int kLabelRaise = 10;  // extra lift of the label above the bottom arrow
  const int labelY = downTipY - kHeadLen - kLabelGap - labelH - kLabelRaise;

  // Top arrowhead pointing right at the Up capsule + horizontal stub
  renderer.drawLine(tipX, topY, tipX - kHeadLen, topY - kHeadLen, kLineW, true);
  renderer.drawLine(tipX, topY, tipX - kHeadLen, topY + kHeadLen, kLineW, true);
  renderer.fillRect(lineX, topY, tipX - lineX + 1, kLineW, true);
  // Vertical run from the stub down to the label
  renderer.fillRect(lineX, topY, kLineW, labelY - kLabelGap - topY + 1, true);
  // Bottom arrowhead pointing down at the 4th letter block
  renderer.drawLine(lineX, downTipY, lineX - kHeadLen, downTipY - kHeadLen, kLineW, true);
  renderer.drawLine(lineX, downTipY, lineX + kHeadLen, downTipY - kHeadLen, kLineW, true);
  renderer.fillRect(lineX, labelY + labelH + kLabelGap, kLineW, downTipY - labelY - labelH - kLabelGap + 1, true);

  // Label centered on the vertical, breaking it
  renderer.drawText(SMALL_FONT_ID, lineX - labelW / 2, labelY, label, true);
}

// ── renderButtonHints ────────────────────────────────────────────────────

void T4EntryActivity::renderButtonHints(int lineHeight) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  // ── Predict / Multi-tap: letter blocks + long-press hints ──

  // Button-position constants (match drawButtonHints layout)
  const int* btnX = GUI.getButtonXPositions(gpio.deviceIsX3());
  const int btnW = GUI.getButtonHintWidth();
  static constexpr int blockPadY = 6;
  static constexpr int blockRowGap = 6;
  static constexpr int blockGapAbove = 12;
  static constexpr int charsPerRow = 3;

  // Position blocks above the button hints strip
  const int hintTopY = pageHeight - 40;
  const int maxBlockH = _maxBlockH;
  const int blocksBaseY = hintTopY - blockGapAbove - maxBlockH - lineHeight;

  // ── Punctuation popup ──
  if (_wordJustConfirmed) {
    static constexpr int popupPadY = 8;
    const int popupW = metrics.sideButtonHintsWidth;
    const int popupH = PUNCT_COUNT * lineHeight + 2 * popupPadY;
    int popupX = pageWidth - popupW - metrics.sideButtonHintsMargin;
    int popupY;
    if (gpio.deviceIsX3()) {
      popupY = GUI.getSideButtonDownBottomY() + 8;  // below Down button
    } else {
      Rect upRect = GUI.getSideButtonUpRect(renderer);
      popupY = upRect.y - popupH - 4;  // above Up button (X4)
    }

    const int popupCr = metrics.popupCornerRadius;
    renderer.fillRoundedRect(popupX, popupY, popupW, popupH, popupCr, Color::White);
    renderer.drawRoundedRect(popupX, popupY, popupW, popupH, 1, popupCr, true);

    for (int i = 0; i < PUNCT_COUNT; i++) {
      int iy = popupY + popupPadY + i * lineHeight;
      int itemW = renderer.getTextWidth(SMALL_FONT_ID, punctCycle()[i]);
      int textX = popupX + (popupW - itemW) / 2;
      if (i == _punctIndex) {
        renderer.fillRect(popupX + 4, iy, popupW - 8, lineHeight, true);
        renderer.drawText(SMALL_FONT_ID, textX, iy, punctCycle()[i], false);
      } else {
        renderer.drawText(SMALL_FONT_ID, textX, iy, punctCycle()[i], true);
      }
    }
  }

  // ── Letter blocks (hidden in cycle mode) ──
  const bool isCycle = (_mode == t4::T4Mode::PREDICT && _upSideCyclesCandidates);

  if (!isCycle) {
    const bool highlightTap = (_mode == t4::T4Mode::MULTI_TAP);
    const uint8_t activeBtn = highlightTap ? _inputEngine.getActiveButton() : 0;
    uint8_t activeLen = 0;
    const char* activePtr = highlightTap ? _inputEngine.getCurrentTapLetter(activeLen) : nullptr;

    const uint8_t shiftLevel = _inputEngine.getShiftLevel();
    const bool shiftActive = (shiftLevel > 0);

    // Uppercase the active tap letter for comparison when shift/caps is active,
    // since the panel glyphs (chBuf) are already uppercased but the engine
    // returns the raw lowercase group letter.
    char tapCmp[5];
    const char* tapCmpPtr = activePtr;
    uint8_t tapCmpLen = activeLen;
    if (shiftActive && activePtr) {
      tapCmpLen = t4::upperLetterUtf8(activePtr, activeLen, tapCmp);
      tapCmpPtr = tapCmp;
    }

    for (int i = 0; i < 4; i++) {
      const char* group = t4::getGroup(_lang, i + 1);
      if (!group || !group[0]) continue;

      int len = t4::getGroupLength(_lang, i + 1);

      // Uppercase group copy when Shift/Caps active
      char groupUpper[49];
      if (shiftActive) {
        char* out = groupUpper;
        const char* p = group;
        for (int ci = 0; ci < len; ci++) {
          unsigned char c0 = static_cast<unsigned char>(*p);
          uint8_t blen = 1;
          if ((c0 & 0xE0) == 0xC0)
            blen = 2;
          else if ((c0 & 0xF0) == 0xE0)
            blen = 3;
          else if ((c0 & 0xF8) == 0xF0)
            blen = 4;
          uint8_t upLen = t4::upperLetterUtf8(p, blen, out);
          p += blen;
          out += upLen;
        }
        *out = '\0';
        group = groupUpper;
      }

      int rows = (len + charsPerRow - 1) / charsPerRow;
      int bx = btnX[i];
      int by = blocksBaseY;

      const int keyCr = metrics.keyboardKeyCornerRadius;
      renderer.drawRoundedRect(bx, by, btnW, maxBlockH, 1, keyCr, true);

      // Pre-scan UTF-8 character boundaries
      struct ChInfo {
        const char* start;
        uint8_t byteLen;
      };
      ChInfo chInfo[12] = {};
      {
        const char* p = group;
        for (int ci = 0; ci < len; ci++) {
          unsigned char c0 = static_cast<unsigned char>(*p);
          uint8_t blen = 1;
          if ((c0 & 0xE0) == 0xC0)
            blen = 2;
          else if ((c0 & 0xF0) == 0xE0)
            blen = 3;
          else if ((c0 & 0xF8) == 0xF0)
            blen = 4;
          chInfo[ci] = {p, blen};
          p += blen;
        }
      }

      const int highlightBoxW = lineHeight;
      for (int r = 0; r < rows; r++) {
        int rowCount = charsPerRow;
        if (r == rows - 1 && len % charsPerRow != 0) rowCount = len % charsPerRow;
        int ry = by + blockPadY + r * (lineHeight + blockRowGap);

        // Measure row chars for equal-gap distribution. The \x01 newline
        // action renders as a multi-character label (tr(STR_T4_ENTER)).
        int rowCharsW[charsPerRow];
        int totalCW = 0;
        char chBuf[5];
        for (int c = 0; c < rowCount; c++) {
          const ChInfo& ci = chInfo[r * charsPerRow + c];
          if (ci.byteLen == 1 && ci.start[0] == '\x01') {
            rowCharsW[c] = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_T4_ENTER));
          } else {
            memcpy(chBuf, ci.start, ci.byteLen);
            chBuf[ci.byteLen] = '\0';
            rowCharsW[c] = renderer.getTextWidth(UI_12_FONT_ID, chBuf);
          }
          totalCW += rowCharsW[c];
        }
        int gap = (btnW - totalCW) / (rowCount + 1);

        int cx = bx + gap;
        for (int c = 0; c < rowCount; c++) {
          const ChInfo& ci = chInfo[r * charsPerRow + c];
          const bool isNewline = (ci.byteLen == 1 && ci.start[0] == '\x01');
          if (isNewline) {
            chBuf[0] = '\x01';
            chBuf[1] = '\0';
          } else {
            memcpy(chBuf, ci.start, ci.byteLen);
            chBuf[ci.byteLen] = '\0';
          }
          const bool isActive = highlightTap && (activeBtn == i + 1) && activePtr && ci.byteLen == tapCmpLen &&
                                memcmp(chBuf, tapCmpPtr, tapCmpLen) == 0;
          const char* displayBuf = isNewline ? tr(STR_T4_ENTER) : chBuf;
          if (isActive) {
            // Widen the highlight box to fit the multi-character label.
            const int boxW = std::max(highlightBoxW, rowCharsW[c]);
            const int boxX = cx - (boxW - rowCharsW[c]) / 2;
            renderer.fillRect(boxX, ry, boxW, lineHeight, true);
            renderer.drawText(UI_12_FONT_ID, cx, ry, displayBuf, false);
          } else {
            renderer.drawText(UI_12_FONT_ID, cx, ry, displayBuf, true);
          }
          cx += rowCharsW[c] + gap;
        }
      }
    }

  }  // end if (!isCycle) — letter blocks hidden in cycle mode

  // Visual connector hint for the Up+Right candidate combo (X3 only)
  renderCandidateComboHint(blocksBaseY);

  // ── Button hints ──
  if (isCycle) {
    // Cycle mode: Back/Confirm inactive (long-press labels, short press blocked),
    // Left/Right active (candidate navigation). Use per-button inactive flags
    // so each button gets the correct active/inactive style in a single pass.
    const auto labels =
        mappedInput.mapLabels(tr(STR_T4_CANCEL), tr(STR_T4_CONFIRM_BTN), tr(STR_T4_LEFT), tr(STR_T4_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4,
                        /*inactive1=*/true, /*inactive2=*/true, /*inactive3=*/false, /*inactive4=*/false);
  } else {
    // Long-press hints (inactive style).
    //
    // Left long-press → cycle language: show the NEXT language name.  Hidden
    // for Digit input, whose numeric keyboard is fixed and never changes
    // layout.
    const bool langCycleEnabled = (_inputType != InputType::Digit);
    const char* langHint = langCycleEnabled ? t4::getLanguageName(t4::cycleLanguage(_lang)) : "";
    //
    // Right long-press → toggle Predict/Multi-tap: show the NEXT mode.
    // Hidden when Predict is unavailable (non-Text input, no dictionary).
    // For Password input the Right long-press instead toggles visibility
    // (Predict never applies), so the hint shows the NEXT state: "[abc]"
    // (will reveal the typed text) or "[***]" (will re-mask it).
    const char* modeHint = "";
    if (_inputType == InputType::Text && languageSupportsPredict(_lang)) {
      modeHint = (_mode == t4::T4Mode::MULTI_TAP) ? tr(STR_T4_MODE_PREDICT) : tr(STR_T4_MODE_TAP);
    } else if (_inputType == InputType::Password) {
      modeHint = _passwordVisible ? "[***]" : "[abc]";
    }
    const auto labels = mappedInput.mapLabels(tr(STR_T4_CANCEL), tr(STR_T4_CONFIRM_BTN), langHint, modeHint);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4,
                        /*inactive1=*/true, /*inactive2=*/true, /*inactive3=*/true, /*inactive4=*/true);
  }

  // ── Side button hints ──
  // Gray long-press capsule next to Space: holding Down toggles Shift/Caps.
  // The hint shows what the NEXT press will switch to:
  //   Off(0) → "Abc"  (one-shot Shift)
  //   Shift(1) → "ABC" (Caps Lock)
  //   Caps(2) → "abc"  (lowercase)
  const char* shiftHint;
  switch (_inputEngine.getShiftLevel()) {
    case 0:
      shiftHint = "Abc";
      break;
    case 1:
      shiftHint = "ABC";
      break;
    default:  // 2
      shiftHint = "abc";
      break;
  }
  const char* leftLabel = tr(STR_T4_BACKSPACE);
  GUI.drawSideButtonHints(renderer, leftLabel, tr(STR_T4_SPACE), "", shiftHint);
}
