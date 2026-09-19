# AGENTS.md

- [Project Overview](#project-overview)
- [Technical Context](#technical-context)
- [Project Structure](#project-structure)
- [Build And Test Commands](#build-and-test-commands)
- [Simulator Visual Debugging](#simulator-visual-debugging)
- [Temporary Files & Agent Scratch](#temporary-files--agent-scratch)
- [Contribution Instructions](#contribution-instructions)
- [Code Guidelines](#code-guidelines)
    - [System Design](#system-design)
    - [Architecture](#architecture)
    - [Code Quality](#code-quality)
    - [Testing](#testing)
    - [Dependency Management](#dependency-management)
    - [UI Components](#ui-components)
    - [Configuration & Documentation](#configuration--documentation)
    - [Markdown Formatting](#markdown-formatting)
    - [Other](#other)

## Project Overview

CrossPoint Reader is open-source e-reader firmware for the Xteink X4
device (unaffiliated with Xteink). It targets the ESP32-C3
microcontroller (single-core RISC-V @ 160 MHz, ~380 KB usable RAM,
no PSRAM, 16 MB flash) with an 800x480 e-ink display. The firmware
provides EPUB 2/3 parsing and rendering, a file browser, WiFi-based
book upload, OTA firmware updates, KOReader sync integration, and
configurable typography and layout settings.

The project's mission is a lightweight, high-performance reading
experience — it is a dedicated e-reader, not a general-purpose
platform. See [SCOPE.md](SCOPE.md) for feature boundaries.

## Technical Context

- **Language**: C++20 (`-std=gnu++2a`), no exceptions, no RTTI
- **Framework**: Arduino-ESP32 via PlatformIO
- **Platform**: pioarduino ESP32-C3 (platform-espressif32)
- **Build system**: PlatformIO (`platformio.ini`)
- **Primary dependencies**:
    - `open-x4-sdk` — low-level hardware SDK (display, input, storage,
      battery) as a git submodule
    - `crosspoint-simulator` — desktop simulator library (SDL2-based
      HAL for macOS/Linux)
    - `ArduinoJson 7.4.2` — JSON parsing for settings
    - `PNGdec 1.1.6` — PNG decoding
    - `JPEGDEC` (pinned commit) — JPEG decoding
    - `QRCode 0.0.1` — QR code generation
    - `WebSockets 2.7.3` — WebSocket server for file upload
    - `NimBLE-Arduino 2.5.0` — BLE client for MeshCore
    - `expat` — XML parsing (vendored in `lib/expat/`)
    - `uzlib` — zlib decompression (vendored in `lib/uzlib/`)
- **Internal libraries**:
    - `lib/MeshCore/` — MeshCore BLE protocol client, message store.
      Reference client (protocol exchange, BLE companion behaviour):
      <https://github.com/dz0ny/meshcore-sar>
      Companion BLE firmware (ground truth for the BLE protocol):
      <https://github.com/meshcore-dev/MeshCore/tree/main/examples/companion_radio>

      Key companion_radio files for understanding the protocol:
      - `MyMesh.cpp`/`MyMesh.h` — core framework: `handleCmdFrame()` dispatches
        incoming commands from the client (CMD_*), `writeContactRespFrame()`
        serialises responses. `onDiscoveredContact()` sends PUSH_CODE_ADVERT
        (known contact) or PUSH_CODE_NEW_ADVERT (new contact).
        `processAck()` parses delivery acknowledgements, `queueMessage()`
        enqueues incoming messages.
      - `NodePrefs.h` — persisted node config structure (BLE PIN, node name,
        radio parameters, auto-add contact settings).
      - `DataStore.h`/`DataStore.cpp` — persistence: load/save contacts,
        channels, prefs file, identity key.
      - `main.cpp` — initialisation: transport selection (BLE via
        `SerialBLEInterface`, WiFi via `SerialWifiInterface`, or direct UART
        via `ArduinoSerialInterface`).
      - `src/helpers/BaseSerialInterface.h` — base transport class:
        `writeFrame()` / `checkRecvFrame()` — all BLE exchange goes through
        this interface over a serial stream.
      - `src/helpers/esp32/SerialBLEInterface.h` — BLE transport for ESP32:
        implements GATT server with TX/RX characteristics.
    - `lib/Memory/` — `makeUniqueNoThrow` allocation helper
    - `lib/MiniBidi/` — bidirectional text layout (Arabic, Hebrew)
    - `lib/T4Dict/` — T9‑style predictive text input engine (dictionary
      lookup, trie layout, input engine, learned-word lexicon).
      Pre‑built dictionaries are in
      `t4dicts/` (`en.trie`, `ru.trie`); source word lists can be obtained
      from <https://github.com/hermitdave/FrequencyWords>. Words the user
      types are learned into `/t4dicts/user_words.bin` and keyboard
      preferences persist in `/t4dicts/t4prefs.bin` (see
      [docs/file-formats.md](docs/file-formats.md)).
- **Storage**: SD card (SdFat via `HalStorage`). No database.
  Settings persist as `/settings.json`. EPUB caches persist as
  binary files under `.crosspoint/` on the SD card.
- **Testing**: Shell-script-driven desktop tests for core algorithms
  (JSON parser, hyphenation, differential rounding). Desktop simulator
  (`pio run -e simulator`) provides UI-level testing on macOS/Linux
  without a device. No on-device unit test framework — hardware
  testing is manual.
- **Target platform**: Xteink X4 hardware only (ESP32-C3 + SD card +
  800x480 e-ink + physical buttons)
- **Project type**: Embedded firmware (single device)
- **Constraints**: 380 KB RAM hard ceiling. Single 48 KB framebuffer
  (not double-buffered). Single-core CPU at 160 MHz. E-ink refresh
  takes 1-2 seconds.

## Project Structure

```text
├── platformio.ini           # Build configuration (environments, flags, deps)
├── partitions.csv           # ESP32 flash partition layout
├── CLAUDE.md                # Detailed AI agent development guide
├── SCOPE.md                 # Feature scope boundaries
├── README.md                # User-facing project overview and install guide
├── USER_GUIDE.md            # End-user operating instructions
├── bin/                     # Developer scripts (clang-format-fix)
├── .githooks/               # Git hooks (pre-commit runs clang-format)
├── .github/workflows/       # CI: build, format check, cppcheck, releases
├── docs/                    # Technical and contributor documentation
│   ├── contributing/        # Getting started, architecture, workflow, testing
│   └── images/              # Documentation images (comparison, focus-reading, wifi)
├── scripts/                 # Build-time and utility scripts (Python/Bash)
├── src/
│   ├── main.cpp             # Entry point, boot sequence, activity orchestration
│   ├── CrossPointSettings.h # SETTINGS singleton — user preferences
│   ├── CrossPointSettings.cpp
│   ├── CrossPointState.h    # APP_STATE singleton — runtime state
│   ├── CrossPointState.cpp
│   ├── MappedInputManager.h # Logical-to-physical button mapping
│   ├── MappedInputManager.cpp
│   ├── fontIds.h            # Global font ID constants
│   ├── BookmarkEntry.h      # Bookmark data structure
│   ├── FontInstaller.h/cpp  # SD card font installation
│   ├── JsonSettingsIO.h/cpp # Settings JSON serialization
│   ├── OpdsServerStore.h/cpp # OPDS server bookmark storage
│   ├── RecentBooksStore.h/cpp # Recent books persistence
│   ├── SdCardFontSystem.h/cpp # SD card font management
│   ├── SettingsList.h       # Settings list definitions
│   ├── SilentRestart.h      # Silent restart helper
│   ├── WifiCredentialStore.h/cpp # WiFi credential persistence
│   ├── activities/          # Activity lifecycle, ActivityManager, all activities
│   │   ├── UiListActivity.h/cpp    # FreeInkUI single-list base (touch + buttons)
│   │   ├── UiTabListActivity.h/cpp # FreeInkUI tabbed-list base (ring navigation)
│   │   ├── home/            # Home screen, file browser, recent books
│   │   ├── reader/          # EPUB/TXT/XTC reading flows
│   │   ├── settings/        # Settings menus, font download, OTA update
│   │   ├── network/         # WiFi selection, web server, USB Drive activity
│   │   ├── boot_sleep/      # Boot and deep sleep transitions
│   │   ├── browser/         # OPDS book browser
│   │   ├── meshcore/        # MeshCore BLE activities (hub, discover, scan, chat, thread)
│   │   └── util/            # Keyboard entry (T4/touch), frontlight panel, messages
│   ├── components/          # UI theme system, icons, themes
│   │   ├── UITheme.h/cpp    # GUI singleton — orientation-aware rendering
│   │   ├── ThemeTabBar.h    # Shared themed tab band (UiTabListActivity + MeshCore)
│   │   ├── UiAppHost.h/cpp  # FreeInkApp hosting protocol (render target + routing)
│   │   ├── UIThemeTokens.h  # ThemeMetrics → FreeInkUI ThemeTokens bridge
│   │   ├── UIScale.h        # UI scale → font id mapping
│   │   ├── icons/           # Icon headers (book, bookmark, cover, folder, wifi, …)
│   │   └── themes/          # Lyra, RoundedRaff, Classic themes
│   ├── images/              # Image data (logo, loading icon, moon icon)
│   ├── simulator/           # Simulator stubs (NimBLE, FreeRTOS, MeshCore mock),
│                            #   stdin automation + screenshot converter
│   ├── network/             # Web server, OTA updater, WebDAV, HTTP downloader
│   │   └── html/            # HTML page sources (→ *.generated.h at build time)
│   ├── util/                # Button navigator, string/URL/QR/screenshot utils
│   └── platform/            # Platform-level patches (efuse check skip)
├── lib/
│   ├── hal/                 # HAL: HalDisplay, HalGPIO, HalStorage, HalPowerManager, ...
│   ├── Epub/                # EPUB parsing, layout, CSS, caching, hyphenation
│   ├── GfxRenderer/         # E-ink framebuffer rendering, orientation transforms
│   ├── EpdFont/             # Font data structures, glyph rendering, built-in fonts
│   ├── I18n/                # Internationalization (YAML translations → generated C++)
│   ├── MeshCore/            # MeshCore BLE protocol client, message store
│   ├── Memory/              # `makeUniqueNoThrow` allocation helper
│   ├── MiniBidi/            # Bidirectional text layout (Arabic, Hebrew)
│   ├── Txt/                 # Plain text file reader
│   ├── Xtc/                 # XTC format reader
│   ├── ZipFile/             # ZIP extraction (for EPUB)
│   ├── Serialization/       # Binary I/O helpers for cache files
│   ├── Logging/             # LOG_ERR/LOG_INF/LOG_DBG macros
│   ├── JsonParser/          # Streaming and release JSON parsers
│   ├── Utf8/                # UTF-8 string utilities
│   ├── FsHelpers/           # File system path and directory utilities
│   ├── OpdsParser/          # OPDS XML catalog parsing
│   ├── KOReaderSync/        # KOReader progress sync integration
│   ├── PngToBmpConverter/   # PNG to BMP conversion (cover images)
│   ├── JpegToBmpConverter/  # JPEG to BMP conversion (cover images)
│   ├── InflateReader/       # DEFLATE decompression wrapper
│   ├── XmlParserUtils/      # XML parsing helpers (wraps expat)
│   ├── expat/               # Vendored XML parser library
│   └── uzlib/               # Vendored zlib decompression
├── ├── meshcore_mock.json   # MeshCore mock data for simulator
│   ├── screenshots/         # Simulator screenshot BMPs (SCREENSHOT command)
│   └── tmp/                 # Simulator scratch space: control FIFO, logs, PNGs input, storage, battery)
├── fs_/                     # Simulator virtual SD card (./fs_/books/ maps to /books/)
│   └── meshcore_mock.json   # MeshCore mock data for simulator
└── test/                    # Desktop algorithm tests (JSON, hyphenation, rounding)
    ├── epubs/               # Sample EPUB files for manual testing
    └── language/             # Language-specific tests (RTL)
```

## Build And Test Commands

```sh
# Build firmware (default development environment)
pio run

# Build specific environment (gh_release, gh_release_rc, slim, plus the
# ESP32-S3 targets sticky, sticky-gh_release, x4pro, x4c, papermono, and the
# simulator envs simulator / simulator_4)
pio run -e gh_release

# Build and run desktop simulator (no device required). There are two
# simulator envs: `simulator` (X3 panel, 792x528, tilt/RTC active) and
# `simulator_4` (X4 target panel, 800x480, no tilt/RTC).
pio run -e simulator
.pio/build/simulator/program

# Build + launch simulator in one command
pio run -e simulator -t run_simulator
# X4 target panel (same controls, X4 framebuffer)
pio run -e simulator_4 -t run_simulator

# Convert newest simulator screenshot BMP to PNG (see below)
./src/simulator/convert_screenshot.sh

# Clean build artifacts
pio run -t clean

# Flash firmware to device
pio run -t upload

# Serial monitor
pio device monitor

# Enhanced serial monitor (color-coded)
python3 scripts/debugging_monitor.py

# Format code (clang-format 21+)
./bin/clang-format-fix

# Format only git-modified files
./bin/clang-format-fix -g

# Static analysis
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high

# Configure, build, and run desktop unit tests (Google Test + CTest)
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test -j
ctest --test-dir build/test --output-on-failure -j

# Run a single test suite
ctest --test-dir build/test --output-on-failure -R T4Layout

# Run a single test directly
build/test/t4_dict/T4LayoutTest

# Older shell-script test runners (alternative to CTest)
bash test/run_release_json_parser_test.sh
bash test/run_streaming_json_parser_test.sh
bash test/run_hyphenation_eval.sh
bash test/run_differential_rounding_test.sh
bash test/run_t4_dict_test.sh

# Regenerate i18n files (also runs automatically during build)
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/
```

### Language Server (clangd) Setup

The project uses ESP32-C3 cross-compiler (`riscv32-esp-elf-g++`) which macOS
clang cannot parse. To avoid false-positive errors in VS Code (red squiggles
from RISC-V flags like `-march=rv32imc_zicsr_zifencei`):

1. **Generate a clang-compatible compilation database**:
   ```sh
   pio run -e simulator -t compiledb
   ```
   This overwrites `compile_commands.json` with simulator (native clang)
   commands instead of ESP32-C3 cross-compiler commands.

2. **After `pio run -t compiledb` (default env)**: the file gets overwritten
   back to ESP32-C3. Just re-run step 1.

3. **`.clangd`** is configured with `Compiler: clang++` and `Remove` rules for
   incompatible flags as a safety net. Both `.clangd` and `compile_commands*.json`
   are gitignored — local dev setup only.

## Simulator Visual Debugging

The desktop simulator can be driven headlessly over stdin, which turns
it into a closed-loop visual debugging tool for AI agents and test
scripts: inject button presses → capture the framebuffer → convert
the screenshot → inspect the image. Implementation lives in
`src/simulator/SimulatorControl.{h,cpp}` (compiled only when
`-DSIMULATOR` is set; device builds are unaffected).

**Protocol** — one command per line on stdin, case-insensitive.
Replies go to **stdout** (`OK ...` / `ERR <reason>`), firmware logs
go to **stderr** — the streams never mix, so stdout is safe to parse.

| Command            | Effect                                        |
| ------------------ | --------------------------------------------- |
| `HELP`             | List commands                                 |
| `TAP <btn>`        | Short press (~60 ms)                          |
| `PRESS <btn>`      | Press and hold until `RELEASE`                |
| `RELEASE <btn>`    | Release a held button                         |
| `HOLD <btn> <ms>`  | Press, hold `<ms>`, release (long-press)      |
| `SCREENSHOT`       | Save framebuffer BMP to `fs_/screenshots/`    |
| `QUIT`             | Exit the simulator                            |

`<btn>` = `BACK | CONFIRM | LEFT | RIGHT | UP | DOWN | POWER`
(or digits `0..6`).

**Agent session recipe** — the control FIFO and logs live under
`fs_/tmp/` (the simulator's own data dir) to avoid any `/tmp`
sandboxing or permission issues. The FIFO is opened read-write so
stdin never hits EOF:

```sh
pio run -e simulator
mkdir -p fs_/tmp && mkfifo fs_/tmp/xp_in
exec 9<>fs_/tmp/xp_in
.pio/build/simulator/program < fs_/tmp/xp_in > fs_/tmp/xp_sim.log 2>&1 &

echo "TAP CONFIRM" >&9          # interact
echo "SCREENSHOT" >&9           # capture framebuffer
./src/simulator/convert_screenshot.sh   # newest BMP -> fs_/tmp/*.png
echo "QUIT" >&9
exec 9>&-                       # close the FIFO when done
```

`convert_screenshot.sh` (in `src/simulator/`, no arguments) picks the
newest BMP in `fs_/screenshots/` by modification time, converts it to
PNG in `fs_/tmp/` (macOS `sips`, ImageMagick fallback), and prints the
absolute PNG path — feed that path to any image viewer or AI vision
model.

**Visual debugging sequence** for verifying a UI change:

1. Build and start the simulator attached to a FIFO (recipe above).
2. Navigate to the screen under test with `TAP`/`HOLD` commands
   (e.g. `TAP CONFIRM` to open a list item, `HOLD POWER 1200` for
   sleep).
3. `SCREENSHOT` after each action, convert with
   `convert_screenshot.sh`, inspect the PNG.
4. Compare before/after frames to confirm the expected change.
5. Check `fs_/tmp/xp_sim.log` (stderr) for `LOG_ERR` entries.
6. `QUIT` and close the FIFO.

**Notes**:

- Screenshot filenames use `millis()` and reset on simulator restart —
  always pick the newest file by *modification time* (`ls -t`), never
  by the number in the filename.
- `SCREENSHOT` also flashes a border on the SDL window and waits
  ~1 s, matching on-device behaviour.
- Long presses (`HOLD`) are real timed holds — useful for sleep/wake
  and other hold-triggered actions.
- MeshCore activities can be exercised with mock data via
  `fs_/meshcore_mock.json` (see CLAUDE.md, "MeshCore on simulator").

## Temporary Files & Agent Scratch

- ALL temporary/scratch work (driver scripts, control FIFOs, converted
  screenshots, scratch notes, dumps) MUST live under `fs_/tmp/`
  (gitignored via `/fs_/`). This is the simulator's own data dir and is
  already a safe/inside-workspace location — do NOT use `/tmp`,
  `$TMPDIR`, or OS temp dirs.
- Keep scratch self-contained: drive the simulator through
  `fs_/tmp/xp_in` with one-shot scripts placed in `fs_/tmp/`, and remove
  them when the session ends. Never leave throwaway files in `/tmp` or
  in tracked source directories.

## Contribution Instructions

- At session start, if a user-global `AGENTS.md` exists (e.g.
  `~/.config/opencode/AGENTS.md`), you MUST read it and follow its rules
  together with this file. It may define cross-repo conventions such as
  clickable source-link formatting, screenshot/vision tooling, and
  scratch-file placement.

- You MUST verify your changes with the formatter and static analysis.

    Use the following commands:
    - `pio run` to build and check for compile errors
    - `./bin/clang-format-fix` to format code (clang-format 21+)
    - `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
      to run static analysis

- You MUST update tests when changing core algorithm logic covered by
  the desktop test suites (`streaming_json_parser`,
  `release_json_parser`, `differential_rounding`, `hyphenation_eval`,
  `utf8_compose`, `t4_dict`).

- You MUST build and run the desktop unit tests to verify your changes
  do not break existing functionality:

    ```sh
    cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
    cmake --build build/test -j
    ctest --test-dir build/test --output-on-failure -j
    ```

- When making changes to the project structure, ensure the Project
  Structure section in `AGENTS.md` is updated and remains valid.

- If the prompt essentially asks you to refactor or improve existing
  code, check if you can phrase it as a code guideline. If it's
  possible, add it to the relevant Code Guidelines section in
  `AGENTS.md`.

- After completing a task you MUST verify that the code you've
  written follows the Code Guidelines in this file and the
  conventions in [CLAUDE.md](CLAUDE.md).

- Do NOT manually edit generated files. These are regenerated at
  build time:
    - `src/network/html/*.generated.h` (source: `src/network/html/`)
    - `lib/I18n/I18nKeys.h`, `lib/I18n/I18nStrings.h`,
      `lib/I18n/I18nStrings.cpp` (source:
      `lib/I18n/translations/*.yaml`)
    - `lib/EpdFont/builtinFonts/` (script-generated)
    - `lib/Epub/Epub/hyphenation/generated/` (script-generated)

- All user-facing strings MUST use the `tr()` macro with `STR_*`
  keys (defined in generated `I18nKeys.h`). Never hardcode UI text.
  Log messages (`LOG_DBG`/`LOG_INF`/`LOG_ERR`) may be hardcoded.

- Use `HalStorage`, `HalDisplay`, `HalGPIO`, and other HAL classes
  instead of accessing SDK classes (`SDCardManager`, `EInkDisplay`,
  `InputManager`) directly.

- For detailed embedded-specific rules (memory management, RTTI,
  FreeRTOS patterns, cache formats, ISR safety, alignment), see
  [CLAUDE.md](CLAUDE.md).

## Code Guidelines

### System Design

Design for an embedded, single-device environment:

- The firmware runs on a single-core RISC-V MCU with ~380 KB RAM and
  no PSRAM. Every heap allocation must be justified. Prefer stack
  or static allocation; use `malloc` only for buffers >256 bytes
  that cannot fit on the stack.
- There is ONE 48 KB framebuffer — not double-buffered. Grayscale
  rendering requires temporary buffer allocation and explicit
  cleanup (`storeBwBuffer()` / `restoreBwBuffer()`).
- E-ink refresh is slow (1-2 seconds). Batch drawing operations;
  minimize full-screen refreshes.
- The main loop must remain responsive. Never block for more than
  a few hundred milliseconds. Add `vTaskDelay(1)` in tight loops
  to avoid watchdog timeouts.
- Persist expensive computed data (parsed layouts, metadata) to the
  SD card cache (`.crosspoint/`) rather than holding it in RAM.
- No exceptions, no RTTI — disabled at the compiler level.
- No `std::shared_ptr` — atomic overhead is unnecessary on a
  single-core CPU. Use `std::unique_ptr`.
- No `std::string` or Arduino `String` in hot paths. Use
  `std::string_view` for read-only access, `snprintf` with fixed
  `char[]` buffers for construction.
- `constexpr` first — compile-time constants and lookup tables must
  be `constexpr` to guarantee flash placement and enable dead-branch
  elimination.
- Pre-allocate `std::vector` with `.reserve(N)` before `push_back()`
  loops to avoid fragmentation from repeated reallocation.
- Throttle SPIFFS/SD writes — guard with value-change checks,
  debounce progress saves. SPIFFS sectors have finite erase cycles.
- Handle lifecycle correctly — resources allocated in `onEnter()`
  MUST be freed in `onExit()`. FreeRTOS tasks MUST be deleted in
  `onExit()` before the activity is destroyed. Member `FsFile`
  handles MUST be closed in `onExit()`.
- Local `FsFile` variables do NOT need explicit `close()` calls —
  `DESTRUCTOR_CLOSES_FILE=1` handles it at scope exit.

### Architecture

Universal design principles the codebase follows:

- **Separation of Concerns** — each module handles one aspect of
  the system (parsing, rendering, input, storage)
- **Single Responsibility Principle** — every file, class, or
  function has one reason to change
- **Dependency Direction** — dependencies point inward/downward;
  never from lower layers to higher ones
- **Explicit Boundaries** — module interfaces are intentional;
  activities interact with libraries through defined APIs, not
  internal headers
- **Data Flow Clarity** — data moves through the system in a
  predictable, traceable path: input → activity → library →
  renderer → display
- **Minimize Coupling, Maximize Cohesion** — modules are
  self-contained and interact through narrow interfaces (HAL
  singletons, Activity base class)
- **Make Invalid States Impossible** — use `constexpr`, enums, and
  typed settings to prevent illegal combinations at compile time.
  Less critical on this embedded target where runtime flexibility
  is limited, but still applied where practical.
- **Observability Built-in** — logging via `LOG_ERR`/`LOG_INF`/
  `LOG_DBG` macros is first-class; module prefixes in all log
  messages for traceability
- **Keep It Boring** — prefer well-understood patterns (activity
  lifecycle, singleton access, FreeRTOS tasks) over clever or
  novel solutions

The project follows a **4-layer architecture** from UI down to
hardware:

```text
Layer 4: Activities (UI screens, input handling)
    ↓
Layer 3: Application Services (settings, state, input mapping, i18n, network)
    ↓
Layer 2: Content & Rendering (Epub, Txt, Xtc, GfxRenderer, ZipFile, fonts)
    ↓
Layer 1: HAL (HalDisplay, HalGPIO, HalStorage, HalPowerManager, HalTiltSensor, HalSystem)
    ↓
Layer 0: Hardware SDK (open-x4-sdk: EInkDisplay, InputManager, SDCardManager)
```

Layer N may call Layer N-1 (or lower). No layer may depend on a
layer above it.

**Key dependency examples**:
- Activities → `SETTINGS`, `GUI`, `MappedInputManager` (Layer 4→3)
- Activities → `GfxRenderer`, `Epub` (Layer 4→2)
- `GfxRenderer` → `HalDisplay` (Layer 2→1)
- `HalDisplay` → `EInkDisplay` from SDK (Layer 1→0)

**Known exclusions** (acceptable trade-offs):

- `CrossPointSettings` imports `HalStorage` (Layer 3→1) because
  settings must persist to SD card. This skips Layer 2 but is a
  thin, necessary coupling.
- `Activity.h` includes `ActivityManager.h` for the
  `ActivityResultHandler` callback type. Risk is low since
  `ActivityManager` is a boot-time singleton.

### Code Quality

**Logging**:
- Always use `LOG_ERR`, `LOG_INF`, or `LOG_DBG` from `Logging.h`.
  Never use raw `Serial` output.
- Every log call must include a module prefix tag (e.g., `"EPUB"`,
  `"HAL"`, `"MAIN"`).
- Always log before returning an error.

**Error handling** (in order of preference):
1. `LOG_ERR("MOD", "reason"); return false;` — 90% of cases
2. `LOG_ERR("MOD", "reason"); useDefault();` — graceful fallback
3. `assert(false)` — fatal impossible states only
4. `ESP.restart()` — unrecoverable errors (OTA complete, etc.)

No exceptions. No `abort()`.

**Naming conventions**:
- Classes: `PascalCase`
- Methods and variables: `camelCase`
- Constants and macros: `UPPER_SNAKE_CASE`
- Private members: `camelCase` (no prefix)
- Files: match class name (`EpubReaderActivity.cpp`)

**Header guards**: `#pragma once` for all headers.

**Memory safety**:
- Prefer `std::unique_ptr`. No `std::shared_ptr`.
- Always check `malloc` return for `nullptr`.
- Set pointers to `nullptr` after `free()`.
- Free in reverse allocation order in `onExit()`.

**`std::string_view` and null termination**:
- `string_view` is NOT null-terminated. Never pass `.data()` to C
  APIs (`drawText`, `snprintf`, `strcmp`, SdFat paths) unless the
  view is known to be null-terminated.
- For C API boundaries, convert to `std::string(view).c_str()` or
  use `snprintf(buf, sizeof(buf), "%.*s", (int)view.size(),
  view.data())`.

**Formatting**: clang-format 21+ enforced by CI and pre-commit hook.
Configuration in `.clang-format` at repo root. Run
`./bin/clang-format-fix` before committing. Do not modify
`.clang-format` without team discussion.

**Static analysis**: `pio check` with cppcheck, enforced in CI.
Failures on low/medium/high defects block merge.

**Magic numbers**: Never hardcode numeric literals in logic (timeout
durations, thresholds, sizes). Always use a named `static constexpr`
constant in class scope. The only exceptions are 0, 1, and
renderer-coordinate math where the semantics are self-evident (e.g.,
`x + 2` as padding, `fontSize - 4` as insets).

**Orientation-aware rendering**:
- Never hardcode `800` or `480`. Use `renderer.getScreenWidth()` and
  `renderer.getScreenHeight()`.
- Use `renderer.getOrientedViewableTRBL()` for bezel margins.

**UI components**:
- Maximise use of existing `GUI.*` components (`drawList`, `drawHeader`,
  `drawTabBar`, `drawButtonHints`, `drawHelpText`, `drawPopup`, etc.)
  before resorting to raw `renderer.drawText()` or `renderer.fillRect()`.
  The `GUI` macro delegates to the active theme, ensuring visual
  consistency and orientation-aware layout.
- If no existing component fits, prefer `UITheme::drawCenteredText()`
  over `GfxRenderer::drawCenteredText()` — it respects screen safe-area.
- Use `renderer.wrappedText()` for multi-line text wrapping when a
  label:value pair or description may exceed one line.

**Status message overlay (toast notifications)**:
- Use `StatusMessageOverlay` (`src/activities/meshcore/StatusMessageOverlay.h`)
  whenever an activity needs ephemeral status messages (toasts) that
  temporarily replace the header subtitle, then auto-revert.
- The class is header-only, zero-heap, and framework-agnostic (clock
  injected via `setClock(&millis)`, subtitle via `setSubtitleProvider(fn, ctx)`).

  Setup in `onEnter()`:
  ```cpp
  _toast.setClock(&millis);
  _toast.setSubtitleProvider([](const void* ctx, char* buf, size_t n) {
    formatMeshCoreSubtitle(*static_cast<const MeshCoreClient*>(ctx), buf, n);
  }, &client);
  ```

  Poll in `loop()`:
  ```cpp
  if (_toast.poll()) requestUpdate();
  ```

  Render in `render()`:
  ```cpp
  char sub[64];
  _toast.getSubtitle(sub, sizeof(sub));
  GUI.drawHeader(…, sub);
  ```

  Fire a toast anywhere:
  ```cpp
  _toast.show(tr(STR_SAVED), 3000);  // 3-second flash, then auto-revert
  _toast.show(tr(STR_SENDING), 0);   // persistent (stays until clear() or replaced)
  requestUpdate();
  ```

- Key API: `show(msg, timeoutMs)` (timeoutMs=0 for persistent),
  `getSubtitle(buf, n)` (returns toast or standard subtitle),
  `poll()` (returns true on timeout expiry), `clear()`, `isActive()`.

**Logical button mapping**:
- Always use `MappedInputManager::Button::*` enums, never raw
  `HalGPIO::BTN_*` indices.

### Testing

- **Desktop algorithm tests**: Shell scripts in `test/` verify core
  algorithms (JSON parsing, hyphenation, differential rounding) on
  the host CPU before embedding. Run these after modifying the
  corresponding library code.
- **On-device testing**: Manual. Flash firmware, test on hardware,
  check serial output. No on-device unit test framework (typical
  for embedded with ~380 KB RAM).
- **Test file placement**: `test/<suite_name>/` directories with a
  corresponding `test/run_<suite_name>_test.sh` entry point.
- **Test data**: Sample EPUB files in `test/epubs/` for manual
  testing.
- **CI gates**: Build, clang-format, and cppcheck must pass. All
  three are required checks on pull requests.

### Dependency Management

- **Pin all dependency versions explicitly** — do not use version
  ranges that allow automatic upgrades to untested versions. The
  PlatformIO platform itself is pinned to a specific release URL.
- **Prefer vanilla solutions** — use C++ standard library and
  built-in APIs when they adequately solve the problem. Only add
  a dependency when it provides significant value over a vanilla
  implementation.
- **Reputable sources only** — dependencies MUST come from
  well-established, actively maintained projects. Evaluate by
  download counts, repository activity, and known maintainers.
- **Avoid unpopular libraries** — do NOT add niche or obscure
  packages with limited community adoption. These pose security
  risks and may become unmaintained.
- **Minimize dependency count** — each new dependency increases
  binary size and maintenance burden. On a 16 MB flash device,
  firmware size directly affects available space. Justify every
  addition.
- **Use the latest stable version** — when adding a new dependency,
  explicitly check the package registry for the latest stable
  release and use it. Do not copy outdated version numbers from
  memory or training data.
- **Vendor critical libraries** — `expat` and `uzlib` are vendored
  in `lib/` for stability and to avoid PlatformIO registry churn.

**Rationale**: Fewer, well-vetted dependencies reduce binary size,
security vulnerabilities, and long-term maintenance costs on a
constrained embedded target.

**Known exclusions**:

- `JPEGDEC` is pinned to a specific git commit hash — acceptable
  for reproducibility, but should migrate to a tagged release when
  available.

### Configuration & Documentation

**Runtime configuration**:
- User settings persist as `/settings.json` on the SD card, managed
  by the `SETTINGS` singleton (`CrossPointSettings`).
- Runtime state (current book, sleep context) persists as binary
  files on SD, managed by `APP_STATE` (`CrossPointState`).
- Build-time configuration is in `platformio.ini` (shared) and
  `platformio.local.ini` (personal, gitignored).
- There are no environment variables or `.env` files. Configuration
  is compile-time flags or SD-card-persisted settings.

**Local overrides**: `platformio.local.ini` is gitignored. Use it
for serial port configuration, personal debug flags, and local
build overrides. Never put personal settings in `platformio.ini`.

**Documentation to update when code changes**:
- Changes to build commands or flags →
  [AGENTS.md](AGENTS.md) Build And Test Commands section and
  [docs/contributing/getting-started.md](docs/contributing/getting-started.md)
- Changes to project structure →
  [AGENTS.md](AGENTS.md) Project Structure section
- Changes to binary cache formats →
  [docs/file-formats.md](docs/file-formats.md), increment version
  constant in source
- Changes to web server endpoints →
  [docs/webserver-endpoints.md](docs/webserver-endpoints.md)
- Changes to i18n → edit YAML in `lib/I18n/translations/`, not
  generated files
- Changes to HTML UI → edit source in `src/network/html/`, not
  `*.generated.h`

### Markdown Formatting

All Markdown files MUST follow these formatting rules:

- **Line length**: Keep lines at most 80 characters. This is not a
  hard lint gate, but SHOULD be followed for readability. Lines
  inside fenced code blocks are exempt from this limit.
- **Unordered lists**: Use dashes (`-`) for bullet points. Indent
  nested list items by 4 spaces.
- **Emphasis**: Use asterisks (`*`) for emphasis (`*italic*`,
  `**bold**`). Do NOT use underscores.
- **Headings**: Duplicate heading names are allowed only among
  sibling headings (same parent level). Avoid duplicates across
  different levels.
- **Inline HTML**: Avoid raw HTML in Markdown. The only allowed
  elements are `<a>`, `<p>`, `<details>`, `<summary>`, and `<img>`.
- **Trailing spaces**: Do NOT leave trailing whitespace on any line.
  Do NOT use two-space line breaks — use a blank line instead.
- **Bare URLs**: Bare URLs are permitted and do not need to be
  wrapped in angle brackets.
- **Table formatting**: Align table columns with padding when the
  table fits within 80 characters. If the table exceeds 80
  characters or triggers an MD060 linter warning, switch to a
  compact format using single spaces only. This applies to the
  separator row as well — it should be written as `| --- |`, not
  `|--|`.

    Example of correct layout:

    ```markdown
    | Col1   | Col2   |
    | ---    | ---    |
    | Value1 | Value2 |
    ```

    Do NOT use extra padding or alignment characters beyond single
    spaces.

**Rationale**: Uniform Markdown formatting improves readability for
both humans and AI agents that consume project documentation.

### Other

**Commit message format**:

```text
<type>: <short summary (50 chars max)>

<optional detailed description>
```

Types: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`.

**FreeRTOS task stack sizing** (in bytes):
- 2048: Simple rendering (most activities)
- 4096: Network, EPUB parsing
- Monitor with `uxTaskGetStackHighWaterMark()` if crashes occur

**Singleton access macros**:

```cpp
SETTINGS    // CrossPointSettings::getInstance()
APP_STATE   // CrossPointState::getInstance()
GUI         // UITheme::getInstance()
Storage     // HalStorage::getInstance()
I18N        // I18n::getInstance()
```

**ESP32-C3 platform pitfalls**:
- RISC-V faults on unaligned multi-byte loads — use `memcpy` for
  any unaligned buffer-to-struct casting.
- All ISR handlers must be `IRAM_ATTR`. Data accessed from ISR code
  must be `DRAM_ATTR`.
- `xSemaphoreTake()` cannot be called from ISR context — use
  `xQueueSendFromISR()` or `xSemaphoreGiveFromISR()`.
- Each `std::function<>` adds ~2-4 KB per unique signature and
  heap-allocates. Prefer raw function pointers or
  `struct { void* ctx; void (*fn)(void*); }`.

---

# Merged Upstream Development Guide

The sections below are ported from the upstream CrossPoint development guide and apply to the shared reader core. The fork-specific guidance above takes precedence where the two overlap.
## AI Agent Identity and Cognitive Rules

* Role: Senior Embedded Systems Engineer (ESP-IDF/Arduino-ESP32 specialized).
* Primary Constraint: 380KB RAM is the hard ceiling. Stability is non-negotiable.
* Evidence-Based Reasoning: Before proposing a change, you MUST cite the specific file path and line numbers that justify the modification.
* Anti-Hallucination: Do not assume the existence of libraries or ESP-IDF functions. If you are unsure of an API's availability for the ESP32-C3 RISC-V target, check the freeink-sdk source or the FreeInk SDK docs (https://freeink.org/llms.txt for an LLM-readable index) first.
* No Unfounded Claims: Do not claim performance gains or memory savings without explaining the technical mechanism (e.g., DRAM vs IRAM usage).
* Resource Justification: You must justify any new heap allocation (new, malloc, std::vector) or explain why a stack/static alternative was rejected.
* Verification: After suggesting a fix, instruct the user on how to verify it (e.g., monitoring heap via Serial or checking a specific cache file).

---

## Development Environment Awareness

**CRITICAL**: Detect the host platform at session start to choose appropriate tools and commands.

### Platform Detection

```bash
# Detect platform (run once per session)
uname -s
# Returns: MINGW64_NT-* (Windows Git Bash), Linux, Darwin (macOS)
```

**Detection Required**: Run `uname -s` at session start to determine platform

### Platform-Specific Behaviors

- **Windows (Git Bash)**: Unix commands, `C:\` paths in Windows but `/` in bash, limited glob (use `find`+`xargs`)
- **Linux/WSL**: Full bash, Unix paths, native glob support

**Cross-Platform Code Formatting**:

```bash
./bin/clang-format-fix -g
```

Never invoke or probe `clang-format` directly. The repository wrapper is the only sanctioned entry point.

---

## Platform and Hardware Constraints

### Hardware Specs

* MCUs: ESP32-C3 (single-core RISC-V @ 160MHz) and ESP32-S3 (`sticky`, dual-core Xtensa LX7)
* RAM: ~380KB usable on ESP32-C3 (VERY LIMITED - primary project constraint)
  * **NO PSRAM on C3**.
  * **Single Buffer Mode**: Only ONE 48KB framebuffer (not double-buffered)
* Flash: 16MB (Instruction storage and static data)
* Display: 800x480 E-Ink (Slow refresh, monochrome, 1-2s full update)
  * Framebuffer: 48,000 bytes (800 × 480 ÷ 8)
* Storage: SD Card (Used for books and aggressive caching)

### The Resource Protocol

1. Stack Safety: Limit local function variables to < 256 bytes. The ESP32-C3 default stack is small; use std::unique_ptr or static pools for larger buffers.
2. Heap Fragmentation: Avoid repeated new/delete in loops. Allocate buffers once during onEnter() and reuse them.
3. Flash Persistence: Large constant data (UI strings, lookup tables) MUST be marked static const to stay in Flash (Instruction Bus), freeing DRAM.
4. String Policy: Prohibit std::string and Arduino String in hot paths. Use std::string_view for read-only access and snprintf with fixed char[] buffers for construction.
5. UI Strings: All user-facing text must use the `tr()` macro (e.g., `tr(STR_LOADING)`) for i18n support. Never hardcode UI strings directly. For the avoidance of doubt, logging messages (LOG_DBG/LOG_ERR) can be hardcoded, but user-facing text must use `tr()`.
6. `constexpr` First: Compile-time constants and lookup tables must be `constexpr`, not just `static const`. This moves computation to compile time, enables dead-branch elimination, and guarantees flash placement. Use `static constexpr` for class-level constants.
7. `std::vector` Pre-allocation: Always call `.reserve(N)` before any `push_back()` loop. Each growth event allocates a new block (2×), copies all elements, then frees the old one — three heap operations that fragment DRAM. When the final size is unknown, estimate conservatively.
8. SD Persistence Throttling: Settings, state, credentials, and other `PersistableStore` JSON files live on SD under `/.crosspoint/` through `HalStorage`; SPIFFS is not mounted. Guard redundant writes and debounce progress saves to avoid serialization, SD I/O, and `storageMutex` cost.
9. `new` is not nothrow on ESP32: With `-fno-exceptions`, bare `new` that fails calls `abort()` — it does NOT return `nullptr`. Always use `new (std::nothrow)` and null-check the result, or use `makeUniqueNoThrow<T>()` from `lib/Memory/Memory.h`. Never write bare `new` for any fallible allocation.

---

### Critical Build Flags

These flags in `platformio.ini` fundamentally affect firmware behavior:

```cpp
-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1  // Single framebuffer (saves 48KB RAM!)
-DARDUINO_USB_MODE=1                 // Enable USB CDC
-DARDUINO_USB_CDC_ON_BOOT=1          // Serial available immediately at boot
-DXML_CONTEXT_BYTES=1024             // XML parser memory limit (EPUB parsing)
-DUSE_UTF8_LONG_NAMES=1              // SD card long filename support
-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES=1   // Avoid zlib name conflicts
-DXML_GE=0                           // Disable XML general entities (security)
-DDESTRUCTOR_CLOSES_FILE=1           // FsFile destructor auto-closes (SdFat)
```

**DESTRUCTOR_CLOSES_FILE implications**:

- SdFat's `FsBaseFile` destructor calls `close()` automatically when the object goes out of scope
- **Do NOT add explicit `file.close()` calls** for local `FsFile` variables — the destructor handles it
- Explicit `close()` is still required in these cases:
  
  1. **Close before delete**: Must close before `Storage.remove()` on the same path
  
  2. **Close before reopen**: Must close before reopening the same `FsFile` variable (e.g., write then reopen for read, or rewrite the same path)
  
  3. **Member variables**: `FsFile` members persist beyond any single function scope, so close at the intended release point (e.g., in `onExit()`)

**SINGLE_BUFFER_MODE implications**:

- Only ONE framebuffer exists (not double-buffered)
- Grayscale rendering requires temporary buffer allocation (`renderer.storeBwBuffer()`)
- Must call `renderer.restoreBwBuffer()` to free temporary buffers
- See [lib/GfxRenderer/GfxRenderer.cpp:439-440](lib/GfxRenderer/GfxRenderer.cpp) for malloc usage
### Error Handling Philosophy

**Source**: [src/main.cpp:132-143](src/main.cpp), [lib/GfxRenderer/GfxRenderer.cpp:10](lib/GfxRenderer/GfxRenderer.cpp)

**Pattern Hierarchy**:

1. **LOG_ERR + return false** (90%): `LOG_ERR("MOD", "Failed: %s", reason); return false;`
2. **LOG_ERR + fallback**: `LOG_ERR("MOD", "Unavailable"); useDefault();`
3. **assert(false)**: Only for fatal "impossible" states (framebuffer missing)
4. **ESP.restart()**: Only for recovery (OTA complete)

**Rules**: NO exceptions, NO abort(), ALWAYS log before error return

### Heap Buffer Allocation

**Prefer `makeUniqueNoThrow` over `malloc`.** Both are nothrow (return `nullptr` on OOM rather than calling `abort()`), but `malloc` requires a manual `free` on every return path — a common source of leaks. `makeUniqueNoThrow<uint8_t[]>(size)` from `lib/Memory/Memory.h` frees automatically when it goes out of scope.

**Preferred pattern**:

```cpp
#include <Memory.h>

auto buffer = makeUniqueNoThrow<uint8_t[]>(bufferSize);
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}

processData(buffer.get(), bufferSize);
// freed automatically — no manual free needed, no leak on early return
```

**`malloc` or `new (std::nothrow)` are still acceptable** when the buffer must be passed to a C API that takes ownership and frees it itself (e.g., certain SDK callbacks). In that case follow the manual pattern:

```cpp
auto* buffer = static_cast<uint8_t*>(malloc(bufferSize));  // or new (std::nothrow) uint8_t[bufferSize]
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}
sdkApiThatTakesOwnership(buffer, bufferSize);  // SDK calls free() / delete[]
```

**Rules**:

- **Prefer `makeUniqueNoThrow`** — automatic cleanup eliminates leak risk on error paths
- **ALWAYS check for nullptr** after any allocation and `LOG_ERR` before returning false
- **Raw allocation only** when a C API takes ownership; document why in a comment

**Examples in codebase**:

- Memory utilities: [Memory.h](lib/Memory/Memory.h) (`makeUniqueNoThrow`)
- Cover image buffers: [HomeActivity.cpp:166](src/activities/home/HomeActivity.cpp)
- Bitmap rendering: [GfxRenderer.cpp:439-440](lib/GfxRenderer/GfxRenderer.cpp)

### Heap Allocation with `new`: Always Use `makeUniqueNoThrow`

**CRITICAL**: With `-fno-exceptions`, bare `new` on OOM calls `abort()` — it does NOT return `nullptr`. Always use `makeUniqueNoThrow` from `lib/Memory/Memory.h`, which wraps `new (std::nothrow)` and returns a `std::unique_ptr` that is null on OOM and automatically frees on scope exit.

**Preferred pattern**:

```cpp
#include <Memory.h>

auto obj = makeUniqueNoThrow<MyClass>(args);
if (!obj) { LOG_ERR("MOD", "OOM: MyClass"); return false; }

auto buf = makeUniqueNoThrow<uint8_t[]>(size);
if (!buf) { LOG_ERR("MOD", "OOM: %d bytes", size); return false; }

// Pass to C APIs via .get(); unique_ptr frees automatically on return
someApi(buf.get(), size);
```

**`new (std::nothrow)` directly is acceptable** when the object must be passed to a C API that takes ownership and calls `delete` itself:

```cpp
auto* obj = new (std::nothrow) MyClass(args);
if (!obj) { LOG_ERR("MOD", "OOM: MyClass"); return false; }
sdkApiThatTakesOwnership(obj);  // SDK calls delete
```

**Rules**:

- **Prefer `makeUniqueNoThrow`** — automatic cleanup eliminates leak risk on error paths
- **NEVER use bare `new`** — always `makeUniqueNoThrow` or `new (std::nothrow)`
- **ALWAYS `LOG_ERR` before returning false** on OOM
- **Use `.get()`** to pass the raw pointer to C-style APIs; ownership stays with the `unique_ptr`
- **`new (std::nothrow)` directly only** when a C API takes ownership; document why in a comment

**Examples in codebase**:

- Memory utilities: [Memory.h](lib/Memory/Memory.h) (`makeUniqueNoThrow`)

---

## Git Workflow and Repository Awareness

### Repository Detection Protocol

**CRITICAL**: ALWAYS verify repository context before git operations. This could be:

- A **fork** with `origin` pointing to personal repo, `upstream` to main repo
- A **direct clone** with `origin` pointing to main repo
- Multiple collaborator remotes

**Verification Commands** (run at session start):

```bash
# Check current branch
git branch --show-current

# Check all remotes
git remote -v

# Check working tree status
git status --short
```

**Example Output** (forked repository):

```text
origin      https://github.com/<your-username>/crosspoint-reader.git (fetch/push)
upstream    https://github.com/crosspoint-reader/crosspoint-reader.git (fetch/push)
```

### Git Operation Rules

1. Integration branches and PR comparisons target `develop`, not `master` or the remote's symbolic HEAD.
2. Never push to any remote or open/close a PR without explicit user approval. Complete local work and any requested local commit, then stop.
3. If the user explicitly approves a push, inspect remotes again and use `fork` for the feature branch unless the user specifies otherwise.
4. Never add Claude, Codex, or assistant self-attribution as a commit co-author or generated-by trailer.
5. When a change supersedes or adapts another person's PR, verify the original human author from Git/GitHub and add that person as `Co-Authored-By`; skip bot authors.

### Branch Naming Convention

**For feature/fix branches**:

```text
feature/<short-description>       # New features
fix/<issue-number>-<description>  # Bug fixes
refactor/<component-name>         # Code refactoring
docs/<topic>                      # Documentation updates
```

**Examples**:

- `feature/sd-download-progress`
- `fix/123-orientation-crash`
- `refactor/hal-storage`

### Commit Message Format

**Pattern**:

```text
<type>: <short summary (50 chars max)>

<optional detailed description>
```

**Types**: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`

**Example**:

```text
feat: add real-time SD download progress bar

Implements progress tracking for book downloads using
UITheme progress bar component with heap-safe updates.

Tested in all 4 orientations with 5MB+ files.
```

### When to Commit

**DO commit when**:

- User explicitly requests: "commit these changes"
- Feature is complete and tested on device
- Bug fix is verified working
- Refactoring preserves all functionality
- All tests pass (`pio run` succeeds)

**DO NOT commit when**:

- Changes are untested on actual hardware
- Build fails or has warnings
- Experimenting or debugging in progress
- User hasn't explicitly requested commit
- Files excluded by `.gitignore` would be included — always run `git status` and cross-check against `.gitignore` before staging (e.g., `*.generated.h`, `.pio/`, `compile_commands.json`, `platformio.local.ini`)

**Rule**: **If uncertain, ASK before committing.**

---

## Generated Files and Build Artifacts

### Files Generated by Build Scripts

**NEVER manually edit these files** - they are regenerated automatically:

1. **HTML Headers** (generated by `scripts/build_html.py`):
   
   - `src/network/html/*.generated.h`
   
   - **Source**: HTML templates in `data/html/` directory
   
   - **Triggered**: During PlatformIO `pre:` build step
   
   - **To modify**: Edit source HTML in `data/html/`, not generated headers

2. **I18n Headers** (generated by `scripts/gen_i18n.py`):
   
   - `lib/I18n/I18nKeys.h`, `lib/I18n/I18nStrings.h`, `lib/I18n/I18nStrings.cpp`
   
   - **Source**: YAML translation files in `lib/I18n/translations/` (one per language)
   
   - **To modify**: Edit source YAML files, then run `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
   
   - **Commit**: Source YAML files only. All three generated files (`I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`) are in `.gitignore` and regenerated at build time.

3. **Build Artifacts** (in `.gitignore`):
   
   - `.pio/` - PlatformIO build output
   
   - `build/` - Compiled binaries
   
   - `*.generated.h` - Any auto-generated headers
   
   - `compile_commands.json` - LSP/IDE metadata

### Modifying Generated Content Workflow

**To change HTML pages**:

1. Edit source: `data/html/<pagename>.html`
2. Build: `pio run` (auto-triggers `scripts/build_html.py`)
3. Generated headers update: `src/network/html/<pagename>Html.generated.h`
4. **Commit ONLY** source HTML, NOT generated `.generated.h` files

**To add/modify translations (i18n)**:

1. Edit or add YAML file: `lib/I18n/translations/<language>.yaml`
   - Each file must contain: `_language_name`, `_language_code`, `_order`, `_bcp47`, and `STR_*` keys
   - English (`english.yaml`) is the reference; missing keys in other languages fall back to English
2. Run generator: `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
3. Generated files update: `I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`
4. **Commit** source YAML files only. All three generated files are in `.gitignore` and regenerated at build time.

**To use translated strings in code**:

```cpp
#include <I18n.h>
// Use tr() macro with StrId enum (defined in generated I18nKeys.h)
renderer.drawText(FONT_UI, x, y, tr(STR_LOADING), true);
```

**To add custom fonts**:

1. Place source fonts in `lib/EpdFont/fontsrc/` (gitignored)
2. Run conversion script (see `lib/EpdFont/README`)
3. Update global font objects in `src/main.cpp:40-115`
4. Add font ID constant to `src/fontIds.h`

---

## Local Development Configuration

### platformio.local.ini (Personal Overrides)

**Purpose**: Personal development settings that should NEVER be committed.

**Use Cases**:

- Serial port configuration (varies by machine)
- Debug flags for specific testing
- Local build optimizations
- Developer-specific paths

**Example** `platformio.local.ini`:

```ini
# platformio.local.ini (gitignored)
[env:default]
upload_port = COM7              # Windows: COMx, Linux: /dev/ttyUSBx
monitor_port = COM7

build_flags =
  ${base.build_flags}
  -DMY_DEBUG_FLAG=1             # Personal debug flags
  -DTEST_FEATURE_ENABLED=1
```

**Configuration Hierarchy**:

1. `platformio.ini` - **Committed**, shared project settings
2. `platformio.local.ini` - **Gitignored**, personal overrides
3. Local file extends/overrides base config

**Rules**:

- **NEVER commit** `platformio.local.ini`
- **NEVER put** personal info (serial ports, credentials) in main `platformio.ini`
- Use `${base.build_flags}` to extend (not replace) base flags

---

## Testing and Verification Workflow

### Testing Checklist

**AI agent scope** (what you CAN verify):

1. ✅ **Build**: Build once after the last code edit with the relevant `pio run` target. Do not clean by default, repeat a target that already passed, or rebuild after formatting/comment-only/documentation-only changes.
2. ✅ **Quality**: `pio check` when relevant + `./bin/clang-format-fix -g`
3. ✅ **Format**: Commit messages (`feat:`/`fix:`), no `.gitignore`-excluded files staged (e.g., `*.generated.h`, `.pio/`, `platformio.local.ini`)
4. ✅ **CI**: Fix GitHub Actions failures before review
5. ✅ **Code review**: Ensure orientation-aware logic is correct in all 4 modes by inspecting switch/case coverage

**Human tester scope** (flag these for the user):
6. 🔲 **Device**: Test on hardware
7. 🔲 **Orientations**: Verify all 4 modes (Portrait/Inverted/Landscape CW/CCW)
8. 🔲 **Heap**: `ESP.getFreeHeap()` > 50KB, no leaks
9. 🔲 **Cache**: If EPUB modified, delete `.crosspoint/` and verify re-parse

### CI/CD Pipeline Awareness

**GitHub Actions** run automatically on pull requests:

| Workflow      | File                                        | Purpose                |
| ------------- | ------------------------------------------- | ---------------------- |
| Build Check   | `.github/workflows/ci.yml`                  | Verifies code compiles |
| Format Check  | `.github/workflows/pr-formatting-check.yml` | Validates clang-format |
| Release Build | `.github/workflows/release.yml`             | Production releases    |
| RC Build      | `.github/workflows/release_candidate.yml`   | Release candidates     |

**Rules**:

- **Fix CI failures BEFORE** requesting review
- CI runs on: Push to PR, PR updates
- Format check fails → Run `./bin/clang-format-fix -g`
- Build check fails → Fix compile errors

---

## Serial Monitoring and Live Debugging

### Serial Monitor Options

1. **Enhanced**: `python3 scripts/debugging_monitor.py` (color-coded, recommended)
2. **Standard**: `pio device monitor` (basic, no colors)
3. **VS Code**: Monitor (🔌) button (IDE-integrated)

### Live Debugging Patterns

**Heap**: `LOG_DBG("MEM", "Free: %d", ESP.getFreeHeap());` (every 5s in loop)
**Stack**: `uxTaskGetStackHighWaterMark(nullptr)` (< 512 bytes → increase stack)
**Flush**: `logSerial.flush();` (force output before crash)

**Port Detection**: Windows: `mode` | Linux: `ls /dev/ttyUSB* /dev/ttyACM*` or `dmesg | grep tty`

---

## Cache Management and Invalidation

### Cache Structure on SD Card

**Location**: `.crosspoint/` directory on SD card root

**Structure**: `.crosspoint/epub_<hash>/{book.bin, progress.bin, cover.bmp, sections/*.bin}`

**Hash**: `std::hash<std::string>{}(filepath)` → Moving/renaming file = new hash = lost progress

### Cache Invalidation Rules

**Cache is automatically invalidated when**:

1. **File format version changes** (see `docs/file-formats.md`)
   
   - `book.bin` version number incremented
   
   - `section.bin` version number incremented
2. **Render settings change**:
   
   - Font family or size (`SETTINGS.fontFamily`, `SETTINGS.fontSize`)
   
   - Line spacing (`SETTINGS.lineSpacing`)
   
   - Paragraph spacing (`SETTINGS.extraParagraphSpacing`)
   
   - Screen margins (`SETTINGS.screenMargin`)
3. **Viewport dimensions change**:
   
   - Screen orientation change
   
   - Display resolution change
4. **Book file modified**:
   
   - Moved, renamed, or content changed (new hash)

**Manual Cache Clear** (safe operations):

```bash
# Delete ALL caches (forces full regeneration)
rm -rf /path/to/sd/.crosspoint/

# Delete specific book cache
rm -rf /path/to/sd/.crosspoint/epub_<hash>/

# Keep progress, delete only rendered sections
rm -rf /path/to/sd/.crosspoint/epub_<hash>/sections/
```

**When to Clear Cache**:

- EPUB parsing errors after code changes to `lib/Epub/`
- Corrupt rendering (missing text, wrong layout)
- Testing cache generation logic
- After modifying:
  - `lib/Epub/Epub/Section.cpp`
  - `lib/Epub/Epub/BookMetadataCache.cpp`
  - Render settings in `CrossPointSettings`

### Cache File Format Versioning

**Source**: `lib/Epub/Epub/Section.cpp`, `lib/Epub/Epub/BookMetadataCache.cpp`

**Current Versions** (as of docs/file-formats.md):

- `book.bin`: **Version 7** (metadata structure)
- `section.bin`: **Version 25** (layout structure)

**Version Increment Rules**:

1. **ALWAYS increment version** BEFORE changing binary structure
2. Version mismatch → Cache auto-invalidated and regenerated
3. Document format changes in `docs/file-formats.md`

**Example** (incrementing section format version):

```cpp
// lib/Epub/Epub/Section.cpp
static constexpr uint8_t SECTION_FILE_VERSION = 26;  // Was 25, now 26

// Add new field to structure
struct PageLine {
  // ... existing fields ...
  uint16_t newField;  // New field added
};
```

---

Philosophy: We are building a dedicated e-reader, not a Swiss Army knife. If a feature adds RAM pressure without significantly improving the reading experience, it is Out of Scope.
