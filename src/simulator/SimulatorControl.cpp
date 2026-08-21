#include "SimulatorControl.h"

#ifdef SIMULATOR

#include <SDL.h>
#include <strings.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "Logging.h"

namespace {

constexpr int NUM_BUTTONS = 7;

// Keep in sync with the scancode table in HalGPIO.cpp of the
// crosspoint-simulator library.
constexpr SDL_Scancode BUTTON_SCANCODE[NUM_BUTTONS] = {
    SDL_SCANCODE_ESCAPE,  // 0 BACK
    SDL_SCANCODE_RETURN,  // 1 CONFIRM
    SDL_SCANCODE_LEFT,    // 2 LEFT
    SDL_SCANCODE_RIGHT,   // 3 RIGHT
    SDL_SCANCODE_UP,      // 4 UP
    SDL_SCANCODE_DOWN,    // 5 DOWN
    SDL_SCANCODE_P,       // 6 POWER
};

constexpr const char* BUTTON_NAME[NUM_BUTTONS] = {"BACK", "CONFIRM", "LEFT", "RIGHT", "UP", "DOWN", "POWER"};

// Short-press duration for TAP: long enough to span several main-loop
// frames (press edge and release edge must land in different frames for
// release-triggered actions), short enough to never trip long-press logic.
constexpr uint32_t TAP_HOLD_MS = 60;

constexpr uint32_t MAX_HOLD_MS = 60000;
constexpr size_t LINE_BUF_SIZE = 256;

std::atomic<bool> screenshotRequested{false};

// SDL_PushEvent synthesizes KEYDOWN/KEYUP events that HalGPIO::update()
// drains like real keyboard input, but pushed events do NOT update SDL's
// internal key-state array (only events from the real input driver do).
// HalGPIO::isPressed() and getHeldTime() read that array, so without this
// patch injected buttons would never register as "held" — breaking
// long-press detection. Writing through the const pointer mutates SDL's
// internal state exactly like its keyboard driver would. Simulator-only
// test infrastructure; never do this in device code.
void setVirtualKeyState(SDL_Scancode scancode, bool down) {
  const Uint8* state = SDL_GetKeyboardState(nullptr);
  if (state) {
    const_cast<Uint8*>(state)[scancode] = down ? 1 : 0;
  }
}

void pushKeyEvent(uint32_t type, SDL_Scancode scancode) {
  SDL_Event e;
  SDL_zero(e);
  e.type = type;
  e.key.state = (type == SDL_KEYDOWN) ? SDL_PRESSED : SDL_RELEASED;
  e.key.repeat = 0;
  e.key.keysym.scancode = scancode;
  e.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
  SDL_PushEvent(&e);
}

void pressButton(int btn) {
  setVirtualKeyState(BUTTON_SCANCODE[btn], true);
  pushKeyEvent(SDL_KEYDOWN, BUTTON_SCANCODE[btn]);
}

void releaseButton(int btn) {
  pushKeyEvent(SDL_KEYUP, BUTTON_SCANCODE[btn]);
  setVirtualKeyState(BUTTON_SCANCODE[btn], false);
}

int parseButton(const char* name) {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (strcasecmp(name, BUTTON_NAME[i]) == 0) return i;
  }
  if (name[0] >= '0' && name[0] < static_cast<char>('0' + NUM_BUTTONS) && name[1] == '\0') {
    return name[0] - '0';
  }
  return -1;
}

void reply(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  putchar('\n');
  fflush(stdout);
}

void printHelp() {
  printf("Commands (replies on stdout, logs on stderr):\n");
  printf("  HELP                 list commands\n");
  printf("  TAP <btn>            short press (~%u ms)\n", TAP_HOLD_MS);
  printf("  PRESS <btn>          press and hold until RELEASE\n");
  printf("  RELEASE <btn>        release a held button\n");
  printf("  HOLD <btn> <ms>      press, hold <ms>, release (long-press)\n");
  printf("  KEY <1..9>           short press of a digit key (mock hotkeys)\n");
  printf("  SCREENSHOT           save framebuffer BMP to fs_/screenshots/\n");
  printf("                       (BMP->PNG: ./src/simulator/convert_screenshot.sh)\n");
  printf("  QUIT                 exit the simulator\n");
  printf("  <btn> = BACK|CONFIRM|LEFT|RIGHT|UP|DOWN|POWER (or 0..6)\n");
  fflush(stdout);
}

void handleLine(char* line) {
  char* save = nullptr;
  const char* cmd = strtok_r(line, " \t\r\n", &save);
  if (!cmd) return;  // empty line

  if (strcasecmp(cmd, "HELP") == 0) {
    printHelp();
    return;
  }

  if (strcasecmp(cmd, "SCREENSHOT") == 0) {
    screenshotRequested.store(true);
    reply("OK SCREENSHOT fs_/screenshots/");
    return;
  }

  if (strcasecmp(cmd, "QUIT") == 0) {
    reply("OK QUIT");
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_QUIT;
    SDL_PushEvent(&e);
    return;
  }

  if (strcasecmp(cmd, "TAP") == 0 || strcasecmp(cmd, "PRESS") == 0 || strcasecmp(cmd, "RELEASE") == 0 ||
      strcasecmp(cmd, "HOLD") == 0) {
    const char* btnArg = strtok_r(nullptr, " \t\r\n", &save);
    if (!btnArg) {
      reply("ERR USAGE %s <btn>", cmd);
      return;
    }
    const int btn = parseButton(btnArg);
    if (btn < 0) {
      reply("ERR UNKNOWN_BUTTON %s", btnArg);
      return;
    }

    if (strcasecmp(cmd, "TAP") == 0) {
      pressButton(btn);
      SDL_Delay(TAP_HOLD_MS);
      releaseButton(btn);
      reply("OK TAP %s", BUTTON_NAME[btn]);
    } else if (strcasecmp(cmd, "PRESS") == 0) {
      pressButton(btn);
      reply("OK PRESS %s", BUTTON_NAME[btn]);
    } else if (strcasecmp(cmd, "RELEASE") == 0) {
      releaseButton(btn);
      reply("OK RELEASE %s", BUTTON_NAME[btn]);
    } else {  // HOLD <btn> <ms>
      const char* msArg = strtok_r(nullptr, " \t\r\n", &save);
      if (!msArg) {
        reply("ERR USAGE HOLD <btn> <ms>");
        return;
      }
      char* end = nullptr;
      const long ms = strtol(msArg, &end, 10);
      if (!end || *end != '\0' || ms <= 0 || static_cast<unsigned long>(ms) > MAX_HOLD_MS) {
        reply("ERR BAD_DURATION %s", msArg);
        return;
      }
      pressButton(btn);
      SDL_Delay(static_cast<uint32_t>(ms));
      releaseButton(btn);
      reply("OK HOLD %s %ld", BUTTON_NAME[btn], ms);
    }
    return;
  }

  if (strcasecmp(cmd, "KEY") == 0) {
    // Synthesize a short press of a digit key (1..9) so MeshCore mock
    // hotkeys (MeshCoreMockHotkeys.h) work from stdin automation. Digit
    // keys are not mapped to buttons, so they need their own path.
    const char* keyArg = strtok_r(nullptr, " \t\r\n", &save);
    if (!keyArg || keyArg[0] < '1' || keyArg[0] > '9' || keyArg[1] != '\0') {
      reply("ERR USAGE KEY <1..9>");
      return;
    }
    // SDL2 scancodes: digits 1..9 are SDL_SCANCODE_1(30)..SDL_SCANCODE_9(38)
    // — they do NOT run consecutively from SDL_SCANCODE_0 (39).
    const SDL_Scancode sc = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (keyArg[0] - '1'));
    setVirtualKeyState(sc, true);
    pushKeyEvent(SDL_KEYDOWN, sc);
    SDL_Delay(3000);  // span several sim frames (e-ink refresh) for reliable edge detection
    pushKeyEvent(SDL_KEYUP, sc);
    setVirtualKeyState(sc, false);
    reply("OK KEY %s", keyArg);
    return;
  }

  reply("ERR UNKNOWN_CMD %s", cmd);
}

void stdinThreadMain() {
  char buf[LINE_BUF_SIZE];
  while (fgets(buf, sizeof(buf), stdin)) {
    handleLine(buf);
  }
  // stdin closed (EOF) — stop handling, simulator keeps running.
}

}  // namespace

namespace SimulatorControl {

void begin() {
  static std::atomic<bool> started{false};
  if (started.exchange(true)) return;
  std::thread(stdinThreadMain).detach();
  LOG_INF("SIMCTL", "stdin automation ready (HELP lists commands)");
}

bool consumeScreenshotRequest() { return screenshotRequested.exchange(false); }

}  // namespace SimulatorControl

#endif  // SIMULATOR
