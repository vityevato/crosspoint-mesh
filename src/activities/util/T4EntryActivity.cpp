#include "T4EntryActivity.h"

#include <HalGPIO.h>
#include <I18n.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "KeyboardEntryActivity.h"
#include "Logging.h"
#include "MappedInputManager.h"
#include "Memory.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ── Constructor ──────────────────────────────────────────────────────────

T4EntryActivity::T4EntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                 std::string initialText, size_t maxLength, InputType inputType)
    : Activity("T4Entry", renderer, mappedInput),
      _title(std::move(title)),
      _initialText(std::move(initialText)),
      _maxLength(maxLength),
      _inputType(inputType),
      _lang(t4::T4Language::EN),
      _mode(t4::T4Mode::PREDICT),
      _prevMode(t4::T4Mode::PREDICT),
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
  Activity::onEnter();

  // Allocate reusable render buffer on heap (512 bytes — too large for stack)
  _displayBuf = makeUniqueNoThrow<char[]>(512);
  if (!_displayBuf) {
    LOG_ERR("T4", "OOM: display buffer");
    setResult(KeyboardResult{});
    finish();
    return;
  }

  _inputEngine.setLanguage(_lang);
  const char* err = _inputEngine.getLastError();
  if (err && err[0] != '\0') {
    LOG_ERR("T4", "Dict load failed: %s", err);
    // Fallback: push KeyboardEntryActivity and exit this activity
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, _title, _initialText, _maxLength, _inputType),
        resultHandler);
    finish();
    return;
  }

  // Priming the engine with initial text (e.g., file rename)
  if (!_initialText.empty()) {
    _inputEngine.setConfirmedText(_initialText.c_str());
  }

  _punctIndex = 0;
  _wordJustConfirmed = false;
  _autoCap = false;
  _candidateScrollX = 0;
  requestUpdate();
}

void T4EntryActivity::onExit() {
  LOG_DBG("T4", "onExit");
  _inputEngine.reset();
  _displayBuf.reset();
  Activity::onExit();
}

// ── Button Dispatch ──────────────────────────────────────────────────────

void T4EntryActivity::loop() {
  // NOTE: Do NOT call mappedInput.update() here — gpio.update() is already
  // called in the main loop (main.cpp) before activityManager.loop() runs.
  // A second update() on real hardware would clear the press/release events
  // already latched by the first call, making buttons unresponsive.

  // --- Long-press detection (check before short-press to set longHandled flags) ---

  // Back long-press → cancel
  if (_backHeld && !_backLongHandled && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    _backLongHandled = true;
    LOG_DBG("T4", "loop: long-press Back → cancel");
    onCancel();
    return;
  }

  // Confirm long-press → finish with result
  if (_confirmHeld && !_confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    _confirmLongHandled = true;
    LOG_DBG("T4", "loop: long-press Confirm → finish, mode=%d text='%s'", static_cast<int>(_mode),
            _inputEngine.getConfirmedText());
    onComplete();
    return;
  }

  // Left long-press → cycle language
  if (_leftHeld && !_leftLongHandled && mappedInput.isPressed(MappedInputManager::Button::Left) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    _leftLongHandled = true;
    auto prevLang = _lang;
    _lang = t4::cycleLanguage(_lang);
    LOG_DBG("T4", "loop: long-press Left → cycle language to %s", t4::getLanguageName(_lang));

    // Save text before reset destroys it
    std::string savedText(_inputEngine.getConfirmedText());
    if (_mode == t4::T4Mode::PREDICT) {
      const char* cand = _inputEngine.getCurrentCandidate();
      if (cand && cand[0] != '\0') savedText += cand;
    }
    LOG_DBG("T4", "loop: lang cycle saved text='%s'", savedText.c_str());

    _inputEngine.reset();
    _inputEngine.setLanguage(_lang);

    // Restore text
    if (!savedText.empty()) {
      _inputEngine.setConfirmedText(savedText.c_str());
    }

    // DIGIT has no dictionary — force MULTI_TAP; restore PREDICT when leaving
    if (_lang == t4::T4Language::DIGIT && _mode == t4::T4Mode::PREDICT) {
      togglePredictMultiTap();
    } else if (prevLang == t4::T4Language::DIGIT && _mode == t4::T4Mode::MULTI_TAP) {
      togglePredictMultiTap();
    }
    _punctIndex = 0;
    _wordJustConfirmed = false;
    _candidateScrollX = 0;
    requestUpdate();
  }

  // Right long-press → enter/exit Command mode
  if (_rightHeld && !_rightLongHandled && mappedInput.isPressed(MappedInputManager::Button::Right) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    _rightLongHandled = true;
    if (_mode == t4::T4Mode::COMMAND) {
      LOG_DBG("T4", "loop: long-press Right → exit Command (prevMode=%d)", static_cast<int>(_prevMode));
      exitCommandMode();
    } else {
      LOG_DBG("T4", "loop: long-press Right → enter Command (from mode=%d)", static_cast<int>(_mode));
      enterCommandMode();
    }
    requestUpdate();
  }

  // Up long-press → toggle Predict ↔ Multi-tap
  if (_upHeld && !_upLongHandled && mappedInput.isPressed(MappedInputManager::Button::Up) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    _upLongHandled = true;
    LOG_DBG("T4", "loop: long-press Up → toggle Predict/Multi-tap (current=%d)", static_cast<int>(_mode));
    togglePredictMultiTap();
    requestUpdate();
  }

  // --- Press tracking ---

  // Button::Back — press tracking
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    _backHeld = true;
    _backLongHandled = false;
  }

  // Button::Confirm — press tracking
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    _confirmHeld = true;
    _confirmLongHandled = false;
  }

  // Button::Left — press tracking
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    _leftHeld = true;
    _leftLongHandled = false;
  }

  // Button::Right — press tracking
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    _rightHeld = true;
    _rightLongHandled = false;
  }

  // Button::Up — press tracking
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    _upHeld = true;
    _upLongHandled = false;
  }

  // --- Short-press detection on release ---

  // COMMAND mode: front buttons do actions instead of letter groups
  if (_mode == t4::T4Mode::COMMAND) {
    // Back → cancel
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (_backHeld && !_backLongHandled) {
        LOG_DBG("T4", "loop: CMD Back → cancel");
        onCancel();
        _backHeld = false;
        _backLongHandled = false;
        return;
      }
      _backHeld = false;
      _backLongHandled = false;
    }

    // Confirm → finish
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (_confirmHeld && !_confirmLongHandled) {
        LOG_DBG("T4", "loop: CMD Confirm → finish, text='%s'", _inputEngine.getConfirmedText());
        onComplete();
        _confirmHeld = false;
        _confirmLongHandled = false;
        return;
      }
      _confirmHeld = false;
      _confirmLongHandled = false;
    }

    // Left → cycle language
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (_leftHeld && !_leftLongHandled) {
        auto prevLang = _lang;
        _lang = t4::cycleLanguage(_lang);
        LOG_DBG("T4", "loop: CMD Left → cycle language to %s", t4::getLanguageName(_lang));

        // Save text before reset destroys it
        std::string savedText(_inputEngine.getConfirmedText());
        if (_mode == t4::T4Mode::PREDICT) {
          const char* cand = _inputEngine.getCurrentCandidate();
          if (cand && cand[0] != '\0') savedText += cand;
        }
        LOG_DBG("T4", "loop: CMD lang cycle saved text='%s'", savedText.c_str());

        _inputEngine.reset();
        _inputEngine.setLanguage(_lang);

        if (!savedText.empty()) {
          _inputEngine.setConfirmedText(savedText.c_str());
        }
        // DIGIT has no dictionary — force MULTI_TAP; restore PREDICT when leaving
        if (_lang == t4::T4Language::DIGIT && _mode == t4::T4Mode::PREDICT) {
          togglePredictMultiTap();
        } else if (prevLang == t4::T4Language::DIGIT && _mode == t4::T4Mode::MULTI_TAP) {
          togglePredictMultiTap();
        }
        _punctIndex = 0;
        _wordJustConfirmed = false;
        _candidateScrollX = 0;
        requestUpdate();
      }
      _leftHeld = false;
      _leftLongHandled = false;
    }

    // Right → exit Command
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      if (_rightHeld && !_rightLongHandled) {
        LOG_DBG("T4", "loop: CMD Right → exit Command");
        exitCommandMode();
        requestUpdate();
      }
      _rightHeld = false;
      _rightLongHandled = false;
    }
  } else {
    // PREDICT / MULTI_TAP: front buttons = letter groups

    // Button::Back — letter group 1
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (_backHeld && !_backLongHandled) {
        LOG_DBG("T4", "loop: press group 1 (mode=%d)", static_cast<int>(_mode));
        _inputEngine.pressButton(1);
        _punctIndex = 0;
        _wordJustConfirmed = false;
        _candidateScrollX = 0;
        requestUpdate();
      }
      _backHeld = false;
      _backLongHandled = false;
    }

    // Button::Confirm — letter group 2
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (_confirmHeld && !_confirmLongHandled) {
        _inputEngine.pressButton(2);
        _punctIndex = 0;
        _wordJustConfirmed = false;
        _candidateScrollX = 0;
        requestUpdate();
      }
      _confirmHeld = false;
      _confirmLongHandled = false;
    }

    // Button::Left — letter group 3
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (_leftHeld && !_leftLongHandled) {
        _inputEngine.pressButton(3);
        _punctIndex = 0;
        _wordJustConfirmed = false;
        _candidateScrollX = 0;
        requestUpdate();
      }
      _leftHeld = false;
      _leftLongHandled = false;
    }

    // Button::Right — letter group 4
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      if (_rightHeld && !_rightLongHandled) {
        _inputEngine.pressButton(4);
        _punctIndex = 0;
        _wordJustConfirmed = false;
        _candidateScrollX = 0;
        requestUpdate();
      }
      _rightHeld = false;
      _rightLongHandled = false;
    }
  }

  // --- Side button release ---

  // Button::Up release (side left) — mode-dependent
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    bool wasLongPress = _upLongHandled;
    _upHeld = false;
    _upLongHandled = false;
    _backspaceLastActionMs = 0;

    // If long-press was already handled (mode toggle), skip short-press action
    if (!wasLongPress) {
      if (_mode == t4::T4Mode::PREDICT) {
        if (_inputEngine.getCandidateCount() > 0) {
          LOG_DBG("T4", "loop: Up → cycleCandidate (idx=%u/%u)", _inputEngine.getCandidateIndex() + 1,
                  _inputEngine.getCandidateCount());
          _inputEngine.cycleCandidate();
          _candidateScrollX = 0;
          requestUpdate();
        }
      } else if (_mode == t4::T4Mode::MULTI_TAP || _mode == t4::T4Mode::COMMAND) {
        LOG_DBG("T4", "loop: Up → backspace (mode=%d, textLen=%u)", static_cast<int>(_mode),
                _inputEngine.getConfirmedTextLength());
        _inputEngine.backspace();
        _punctIndex = 0;
        _wordJustConfirmed = false;
        requestUpdate();
      }
    } else {
      LOG_DBG("T4", "loop: Up release after long-press, skip short-press action");
    }
  }

  // Button::Down release (side right) — mode-dependent
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (_mode == t4::T4Mode::COMMAND) {
      LOG_DBG("T4", "loop: Down → undoDelete (CMD mode)");
      _inputEngine.undoDelete();
      requestUpdate();
    } else {
      LOG_DBG("T4", "loop: Down → punctuation (mode=%d)", static_cast<int>(_mode));
      handlePunctuation();
      requestUpdate();
    }
  }

  // --- COMMAND mode: long-press+hold Backspace (Side Up) ---
  if (_mode == t4::T4Mode::COMMAND) {
    if (mappedInput.isPressed(MappedInputManager::Button::Up)) {
      unsigned long held = mappedInput.getHeldTime();
      if (held >= BACKSPACE_INITIAL_DELAY_MS) {
        if (_backspaceLastActionMs == 0) {
          // First deletion after initial delay
          LOG_DBG("T4", "loop: CMD backspace hold start (held=%lums)", held);
          _inputEngine.backspace();
          _backspaceLastActionMs = millis();
          requestUpdate();
        } else if (millis() - _backspaceLastActionMs >= BACKSPACE_REPEAT_MS) {
          // Repeat deletion (word-level)
          _inputEngine.backspace();
          _backspaceLastActionMs = millis();
          requestUpdate();
        }
      }
    }
  }

  // Poll predictor for any time-based state changes
  _inputEngine.poll(millis());
}

// ── Completion / Cancellation ────────────────────────────────────────────

void T4EntryActivity::onComplete() {
  const char* text = _inputEngine.getConfirmedText();
  // In PREDICT: append unconfirmed candidate to engine text
  if (_mode == t4::T4Mode::PREDICT) {
    const char* candidate = _inputEngine.getCurrentCandidate();
    if (candidate && candidate[0] != '\0') {
      std::string result(text);
      result += candidate;
      LOG_DBG("T4", "onComplete: PREDICT with cand='%s', result='%s'", candidate, result.c_str());
      setResult(KeyboardResult{std::move(result)});
      finish();
      return;
    }
  }
  LOG_DBG("T4", "onComplete: mode=%d, result='%s'", static_cast<int>(_mode), text);
  setResult(KeyboardResult{std::string(text)});
  finish();
}

void T4EntryActivity::onCancel() {
  LOG_DBG("T4", "onCancel");
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

// ── Punctuation Handling ─────────────────────────────────────────────────

constexpr const char* T4EntryActivity::PUNCT_CYCLE[];
// Note: constexpr static arrays need out-of-class definition in C++17

bool T4EntryActivity::isAutoCapPunct(const char* punct) {
  if (!punct) return false;
  // After ". ", "! ", "? " → auto-capitalize next word
  return (punct[0] == '.' || punct[0] == '!' || punct[0] == '?') && punct[1] == ' ';
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
      const char* prevPunct = PUNCT_CYCLE[_punctIndex];
      size_t prevLen = strlen(prevPunct);
      if (text.length() >= prevLen) {
        text.erase(text.length() - prevLen);
      }
      _punctIndex = (_punctIndex + 1) % PUNCT_COUNT;
      text += PUNCT_CYCLE[_punctIndex];
      _lastConfirmMs = millis();
      LOG_DBG("T4", "handlePunct: MULTI_TAP cycle punct[%d]='%s' → text='%s'", _punctIndex, PUNCT_CYCLE[_punctIndex],
              text.c_str());
    } else {
      // First confirm or timeout expired: add trailing space
      text += ' ';
      _punctIndex = 0;
      _wordJustConfirmed = true;
      _candidateScrollX = 0;
      _lastConfirmMs = millis();
      LOG_DBG("T4", "handlePunct: MULTI_TAP first confirm → text='%s'", text.c_str());
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
      // Remove previous punctuation suffix, then append next in cycle
      const char* prevPunct = PUNCT_CYCLE[_punctIndex];
      size_t prevLen = strlen(prevPunct);
      if (text.length() >= prevLen) {
        text.erase(text.length() - prevLen);
      }
      _punctIndex = (_punctIndex + 1) % PUNCT_COUNT;
      text += PUNCT_CYCLE[_punctIndex];
      _lastConfirmMs = millis();
      LOG_DBG("T4", "handlePunct: PREDICT cycle punct[%d]='%s' → text='%s'", _punctIndex, PUNCT_CYCLE[_punctIndex],
              text.c_str());
      _inputEngine.setConfirmedText(text.c_str());
      requestUpdate();
      return;
    }
  }

  // First press: confirm current candidate into confirmed text
  if (hasCandidate) {
    const char* word = _inputEngine.getCurrentCandidate();
    std::string capitalized;
    if (_autoCap && word && word[0] != '\0') {
      capitalized += static_cast<char>(toupper(static_cast<unsigned char>(word[0])));
      capitalized += (word + 1);
      word = capitalized.c_str();
      _autoCap = false;
    }

    if (!text.empty() && text.back() != ' ') {
      text += ' ';
    }
    text += word;

    if (isAutoCapPunct(PUNCT_CYCLE[_punctIndex])) {
      _autoCap = true;
    }

    // Reset predictor sequence for next word
    _inputEngine.confirmWord();
    text += ' ';  // trailing space after confirmed word
    _punctIndex = 0;
    _wordJustConfirmed = true;
    _candidateScrollX = 0;
    _lastConfirmMs = millis();
    LOG_DBG("T4", "handlePunct: PREDICT confirm '%s' → text='%s' autoCap=%d", word, text.c_str(), _autoCap);
  } else {
    // No candidate: just append space
    text += ' ';
    _punctIndex = 0;
    _wordJustConfirmed = true;
    _lastConfirmMs = millis();
    LOG_DBG("T4", "handlePunct: PREDICT no-candidate → text='%s'", text.c_str());
  }
  _inputEngine.setConfirmedText(text.c_str());
}

// ── Mode Transitions ─────────────────────────────────────────────────────

bool T4EntryActivity::enterCommandMode() {
  LOG_DBG("T4", "enterCommandMode: mode=%d → COMMAND, prevMode=%d, text='%s'", static_cast<int>(_mode),
          static_cast<int>(_prevMode), _inputEngine.getConfirmedText());
  if (_mode == t4::T4Mode::COMMAND) return false;
  _prevMode = _mode;  // Remember source mode for exit
  _inputEngine.setMode(t4::T4Mode::COMMAND);
  _mode = _inputEngine.getMode();
  _wordJustConfirmed = false;
  _punctIndex = 0;
  _candidateScrollX = 0;
  _backspaceLastActionMs = 0;
  LOG_DBG("T4", "enterCommandMode: done, engine text='%s' len=%u", _inputEngine.getConfirmedText(),
          _inputEngine.getConfirmedTextLength());
  return true;
}

bool T4EntryActivity::exitCommandMode() {
  LOG_DBG("T4", "exitCommandMode: mode=%d → prevMode=%d, text='%s'", static_cast<int>(_mode),
          static_cast<int>(_prevMode), _inputEngine.getConfirmedText());
  if (_mode != t4::T4Mode::COMMAND) return false;
  _inputEngine.setMode(_prevMode);  // Return to source mode
  _mode = _inputEngine.getMode();
  _candidateScrollX = 0;
  _backspaceLastActionMs = 0;
  LOG_DBG("T4", "exitCommandMode: done, _mode=%d, text='%s'", static_cast<int>(_mode), _inputEngine.getConfirmedText());
  return true;
}

bool T4EntryActivity::togglePredictMultiTap() {
  t4::T4Mode newMode = (_mode == t4::T4Mode::MULTI_TAP) ? t4::T4Mode::PREDICT : t4::T4Mode::MULTI_TAP;
  LOG_DBG("T4", "togglePredictMultiTap: %d → %d, text='%s'", static_cast<int>(_mode), static_cast<int>(newMode),
          _inputEngine.getConfirmedText());
  if (newMode == _mode) return false;

  if (_mode == t4::T4Mode::COMMAND) {
    // In COMMAND mode, toggle the source mode instead.
    _prevMode = (_prevMode == t4::T4Mode::MULTI_TAP) ? t4::T4Mode::PREDICT : t4::T4Mode::MULTI_TAP;
    LOG_DBG("T4", "togglePredictMultiTap: in COMMAND, toggled _prevMode to %d", static_cast<int>(_prevMode));
    return true;
  }

  // Commit unconfirmed candidate when leaving PREDICT
  if (_mode == t4::T4Mode::PREDICT && newMode == t4::T4Mode::MULTI_TAP) {
    const char* cand = _inputEngine.getCurrentCandidate();
    LOG_DBG("T4", "togglePredictMultiTap: PREDICT→MULTI_TAP, cand='%s' seqLen=%u", cand ? cand : "(null)",
            _inputEngine.getSequenceLength());
    if (cand && cand[0] != '\0') {
      std::string t(_inputEngine.getConfirmedText());
      t += cand;
      _inputEngine.setConfirmedText(t.c_str());
    }
  }

  _inputEngine.setMode(newMode);
  _mode = _inputEngine.getMode();
  _wordJustConfirmed = false;
  _punctIndex = 0;
  _candidateScrollX = 0;
  LOG_DBG("T4", "togglePredictMultiTap: done, _mode=%d text='%s'", static_cast<int>(_mode),
          _inputEngine.getConfirmedText());
  return true;
}

// ── Rendering ────────────────────────────────────────────────────────────

void T4EntryActivity::render(RenderLock&& lock) {
  (void)lock;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // --- 1. Header with language badge and mode indicator ---
  char subtitle[32];
  const char* modeLabel;
  switch (_mode) {
    case t4::T4Mode::MULTI_TAP:
      modeLabel = tr(STR_T4_MODE_TAP);
      break;
    case t4::T4Mode::COMMAND:
      modeLabel = tr(STR_T4_MODE_CMD);
      break;
    default:
      modeLabel = tr(STR_T4_MODE_PREDICT);
  }
  snprintf(subtitle, sizeof(subtitle), "%s %s", t4::getLanguageName(_lang), modeLabel);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, _title.c_str(), subtitle);

  // --- 2. Text field: confirmed text + current candidate ---
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing +
                          metrics.verticalSpacing * 4 + metrics.keyboardVerticalOffset;
  int inputHeight = 0;

  const char* candidate = _inputEngine.getCurrentCandidate();
  bool hasCandidate = (candidate != nullptr) && (candidate[0] != '\0');

  // In MULTI_TAP/COMMAND, the predictor owns the confirmed text
  const char* confirmedText = _inputEngine.getConfirmedText();

  // Build full text (real content, not masked)
  char fullText[512];
  if (_inputType == InputType::Password) {
    size_t confLen = strlen(confirmedText);
    size_t i;
    for (i = 0; i < confLen && i < 511; i++) fullText[i] = '*';
    if (hasCandidate) {
      size_t candLen = strlen(candidate);
      for (size_t j = 0; j < candLen && (i + j) < 511; j++) fullText[i + j] = '*';
      fullText[i + (candLen < 511 - i ? candLen : 511 - i)] = '\0';
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
  int availableWidth = pageWidth;
  if (gpio.deviceIsX3()) {
    availableWidth -= 2 * metrics.sideButtonHintsWidth;
  }
  const int toggleReserve =
      isPassword
          ? std::max(renderer.getTextWidth(UI_12_FONT_ID, "[abc]"), renderer.getTextWidth(UI_12_FONT_ID, "[***]")) + 4
          : 0;
  const int effectiveMargin = (pageWidth - availableWidth * metrics.keyboardTextFieldWidthPercent / 100) / 2;
  const int textAreaWidth = pageWidth - 2 * effectiveMargin - toggleReserve;
  const int maxLineWidth = textAreaWidth;

  // Word-wrap: break displayText into lines
  int lineStartIdx = 0;
  int lineEndIdx = displayText.length();
  int textWidth = 0;
  int cursorPixelX = effectiveMargin;
  int cursorLineY = inputStartY;

  while (true) {
    std::string lineText = displayText.substr(lineStartIdx, lineEndIdx - lineStartIdx);
    textWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
    if (textWidth <= maxLineWidth) {
      const int lineStartX = effectiveMargin;
      const bool isLastLine = (lineEndIdx == static_cast<int>(displayText.length()));

      if (isLastLine) {
        // Last line: handle candidate/tap-letter highlighting + cursor
        size_t confLen = strlen(confirmedText);
        int confLenInLine = static_cast<int>(confLen) - lineStartIdx;

        if (!isPassword && hasCandidate && _mode == t4::T4Mode::PREDICT && confLenInLine > 0) {
          // Draw confirmed portion normally, candidate inverted
          std::string confPart = lineText.substr(0, confLenInLine);
          renderer.drawText(UI_12_FONT_ID, lineStartX, inputStartY + inputHeight, confPart.c_str(), true);
          int confW = renderer.getTextAdvanceX(UI_12_FONT_ID, confPart.c_str(), EpdFontFamily::REGULAR);
          std::string candPart = lineText.substr(confLenInLine);
          int candW = renderer.getTextAdvanceX(UI_12_FONT_ID, candPart.c_str(), EpdFontFamily::REGULAR);
          if (candW > 0) {
            int candX = lineStartX + confW;
            renderer.fillRect(candX, inputStartY + inputHeight, candW + 4, lineHeight, true);
            renderer.drawText(UI_12_FONT_ID, candX + 2, inputStartY + inputHeight, candPart.c_str(), false);
          }
          int beforeWidth = confW + (candW > 0 ? candW + 4 : 0);
          cursorPixelX = lineStartX + beforeWidth;
        } else if (!isPassword && hasCandidate && _mode == t4::T4Mode::PREDICT && confLenInLine <= 0) {
          // Confirmed text ended on previous line; entire last line is candidate
          int candW = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
          renderer.fillRect(lineStartX, inputStartY + inputHeight, candW + 4, lineHeight, true);
          renderer.drawText(UI_12_FONT_ID, lineStartX + 2, inputStartY + inputHeight, lineText.c_str(), false);
          cursorPixelX = lineStartX + candW + 4;
        } else if (!isPassword && _mode == t4::T4Mode::MULTI_TAP) {
          // Multi-tap: confirmed text + cycling letter highlighted
          renderer.drawText(UI_12_FONT_ID, lineStartX, inputStartY + inputHeight, lineText.c_str(), true);
          char tapLetter = _inputEngine.getCurrentTapLetter();
          int confW = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
          if (tapLetter != '\0') {
            const char tapStr[2] = {tapLetter, '\0'};
            int tapW = renderer.getTextWidth(UI_12_FONT_ID, tapStr);
            int tapX = lineStartX + confW;
            renderer.fillRect(tapX, inputStartY + inputHeight, tapW + 4, lineHeight, true);
            renderer.drawText(UI_12_FONT_ID, tapX + 2, inputStartY + inputHeight, tapStr, false);
            cursorPixelX = tapX + tapW + 4;
          } else {
            cursorPixelX = lineStartX + confW;
          }
        } else {
          // Password or no candidate: draw all text normally
          renderer.drawText(UI_12_FONT_ID, lineStartX, inputStartY + inputHeight, lineText.c_str(), true);
          int beforeWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
          cursorPixelX = lineStartX + beforeWidth;
        }
        cursorLineY = inputStartY + inputHeight;
        break;
      }

      // Non-last line: draw normally
      renderer.drawText(UI_12_FONT_ID, lineStartX, inputStartY + inputHeight, lineText.c_str(), true);
      inputHeight += lineHeight;
      lineStartIdx = lineEndIdx;
      lineEndIdx = displayText.length();
    } else {
      lineEndIdx -= 1;
    }
  }

  const int fieldWidth = (inputHeight > 0) ? maxLineWidth : textWidth;
  GUI.drawTextField(renderer, Rect{0, inputStartY, pageWidth, inputHeight}, fieldWidth, false, effectiveMargin,
                    pageWidth - 2 * effectiveMargin);

  // Serif cursor at end of text (always visible, even in empty field)
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

  // Password toggle
  if (isPassword) {
    const char* toggleLabel = _passwordVisible ? "[***]" : "[abc]";
    const int toggleWidth = renderer.getTextWidth(UI_12_FONT_ID, toggleLabel);
    const int toggleX = pageWidth - effectiveMargin - toggleWidth;
    const int toggleY = inputStartY + inputHeight;
    renderer.drawText(UI_12_FONT_ID, toggleX, toggleY, toggleLabel, true);
  }

  int y = inputStartY + inputHeight + lineHeight + metrics.verticalSpacing;

  // --- 3. Candidate row (full scrolling) — Predict mode only ---
  uint16_t candCount = _inputEngine.getCandidateCount();
  uint16_t activeIdx = _inputEngine.getCandidateIndex();

  if (_mode == t4::T4Mode::PREDICT && candCount > 0) {
    int candRowY = y;
    int viewW = textAreaWidth;

    // First pass: measure total width and find active candidate position
    int totalW = 0;
    int activeStartX = 0;
    int candWidths[20];
    int candCountMeasured = candCount < 20 ? (int)candCount : 20;

    for (int i = 0; i < candCountMeasured; i++) {
      const char* name = _inputEngine.getCandidate(i);
      if (!name) continue;
      if (i > 0) totalW += renderer.getTextWidth(SMALL_FONT_ID, "  ");
      candWidths[i] = renderer.getTextWidth(SMALL_FONT_ID, name);
      if (i == (int)activeIdx) activeStartX = totalW;
      totalW += candWidths[i];
    }

    // Compute scroll offset to keep active candidate visible
    if (totalW > viewW) {
      _candidateScrollX = activeStartX - (viewW / 2) + (candWidths[activeIdx] / 2);
      if (_candidateScrollX < 0) _candidateScrollX = 0;
      if (_candidateScrollX > totalW - viewW) _candidateScrollX = totalW - viewW;
    } else {
      _candidateScrollX = 0;
    }

    // Draw candidates with scroll and clipping
    int drawX = effectiveMargin - _candidateScrollX;

    // Left ellipsis if scrolled
    if (_candidateScrollX > 0) {
      const char* ellipsis = "\u2026 ";
      renderer.drawText(SMALL_FONT_ID, effectiveMargin, candRowY, ellipsis, true);
    }

    for (int i = 0; i < candCountMeasured; i++) {
      const char* name = _inputEngine.getCandidate(i);
      if (!name) continue;

      // Skip if fully off-screen left
      if (drawX + candWidths[i] < effectiveMargin) {
        drawX += candWidths[i] + renderer.getTextWidth(SMALL_FONT_ID, "  ");
        continue;
      }
      // Stop if fully off-screen right
      if (drawX > effectiveMargin + viewW - 30) {
        // Draw right ellipsis and stop
        renderer.drawText(SMALL_FONT_ID, effectiveMargin + viewW - 30, candRowY, "\u2026", true);
        break;
      }

      if (i == (int)activeIdx) {
        // Active candidate: draw with underline
        renderer.drawText(SMALL_FONT_ID, drawX, candRowY, name, true);
        int underlineY = candRowY + renderer.getTextHeight(SMALL_FONT_ID) + 2;
        renderer.fillRect(drawX, underlineY, candWidths[i], 2, true);
      } else {
        renderer.drawText(SMALL_FONT_ID, drawX, candRowY, name, true);
      }

      drawX += candWidths[i] + renderer.getTextWidth(SMALL_FONT_ID, "  ");
    }
  }

  // --- 4. Button hints ---
  if (_mode == t4::T4Mode::COMMAND) {
    const auto labels =
        mappedInput.mapLabels(tr(STR_T4_CANCEL), tr(STR_T4_CONFIRM_BTN), tr(STR_T4_LANG), tr(STR_T4_MODE_BTN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, tr(STR_T4_BACKSPACE), tr(STR_T4_UNDO));
  } else {
    // Predict / Multi-tap: letter blocks + long-press hints (inactive button hints)

    // ── 4a. Button-position constants (match drawButtonHints layout) ──
    const int* btnX = GUI.getButtonXPositions(gpio.deviceIsX3());
    const int btnW = GUI.getButtonHintWidth();  // Theme-dependent button width
    static constexpr int blockPadY = 6;         // Vertical padding inside block
    static constexpr int blockRowGap = 6;       // Vertical gap between rows of characters
    static constexpr int blockGapAbove = 12;    // Gap between blocks and button hints
    static constexpr int charsPerRow = 3;       // Characters per row, evenly distributed

    // ── 4b. Letter blocks (3 chars per row, UI_12 font, above button hints) ──
    // Compute max block height; all blocks share the same height for visual uniformity
    int maxBlockH = 0;
    for (int i = 0; i < 4; i++) {
      int len = t4::getGroupLength(_lang, i + 1);
      int rows = (len + charsPerRow - 1) / charsPerRow;
      int h = rows * lineHeight + (rows - 1) * blockRowGap + 2 * blockPadY;
      if (h > maxBlockH) maxBlockH = h;
    }

    // Position blocks above the button hints strip, offset upward by subtitle height
    const int hintTopY = pageHeight - 40;
    const int blocksBaseY = hintTopY - blockGapAbove - maxBlockH - lineHeight;

    for (int i = 0; i < 4; i++) {
      const char* group = t4::getGroup(_lang, i + 1);
      if (!group || !group[0]) continue;

      int len = t4::getGroupLength(_lang, i + 1);
      int rows = (len + charsPerRow - 1) / charsPerRow;
      int bx = btnX[i];
      int by = blocksBaseY;

      renderer.drawRect(bx, by, btnW, maxBlockH);

      // Pre-scan UTF-8 character boundaries (handles multi-byte cyrillic)
      struct ChInfo {
        const char* start;
        uint8_t byteLen;
      };
      ChInfo chInfo[12];  // Max group length
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

      // Render each row
      for (int r = 0; r < rows; r++) {
        int rowCount = charsPerRow;
        if (r == rows - 1 && len % charsPerRow != 0) rowCount = len % charsPerRow;
        int ry = by + blockPadY + r * (lineHeight + blockRowGap);

        // Measure row chars for equal-gap distribution: edge gap == inter-char gap
        int rowCharsW[charsPerRow];
        int totalCW = 0;
        char chBuf[5];
        for (int c = 0; c < rowCount; c++) {
          const ChInfo& ci = chInfo[r * charsPerRow + c];
          memcpy(chBuf, ci.start, ci.byteLen);
          chBuf[ci.byteLen] = '\0';
          rowCharsW[c] = renderer.getTextWidth(UI_12_FONT_ID, chBuf);
          totalCW += rowCharsW[c];
        }
        int gap = (btnW - totalCW) / (rowCount + 1);

        int cx = bx + gap;
        for (int c = 0; c < rowCount; c++) {
          const ChInfo& ci = chInfo[r * charsPerRow + c];
          memcpy(chBuf, ci.start, ci.byteLen);
          chBuf[ci.byteLen] = '\0';
          renderer.drawText(UI_12_FONT_ID, cx, ry, chBuf, true);
          cx += rowCharsW[c] + gap;
        }
      }
    }

    // ── 4c. Long-press hints: theme-drawn, inactive (grayed out) style ──
    const auto labels =
        mappedInput.mapLabels(tr(STR_T4_CANCEL), tr(STR_T4_CONFIRM_BTN), tr(STR_T4_LANG), tr(STR_T4_MODE_BTN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, /*inactive=*/true);

    // ── 4d. Side button hints ──
    const char* leftLabel = (_mode == t4::T4Mode::MULTI_TAP) ? tr(STR_T4_BACKSPACE) : tr(STR_T4_CYCLE);
    GUI.drawSideButtonHints(renderer, leftLabel, tr(STR_T4_SPACE));
  }

  renderer.displayBuffer();
}
