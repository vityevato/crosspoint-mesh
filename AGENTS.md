# AGENTS.md

- [Project Overview](#project-overview)
- [Technical Context](#technical-context)
- [Project Structure](#project-structure)
- [Build And Test Commands](#build-and-test-commands)
- [Contribution Instructions](#contribution-instructions)
- [Code Guidelines](#code-guidelines)
    - [System Design](#system-design)
    - [Architecture](#architecture)
    - [Code Quality](#code-quality)
    - [Testing](#testing)
    - [Dependency Management](#dependency-management)
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
    - `lib/MeshCore/` — MeshCore BLE protocol client, message store
    - `lib/Memory/` — `makeUniqueNoThrow` allocation helper
    - `lib/MiniBidi/` — bidirectional text layout (Arabic, Hebrew)
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
├── GOVERNANCE.md            # Project governance
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
│   │   ├── home/            # Home screen, file browser, recent books
│   │   ├── reader/          # EPUB/TXT/XTC reading flows
│   │   ├── settings/        # Settings menus, font download, OTA update
│   │   ├── network/         # WiFi selection, web server activity
│   │   ├── boot_sleep/      # Boot and deep sleep transitions
│   │   ├── browser/         # OPDS book browser
│   │   ├── meshcore/        # MeshCore BLE activities (hub, discover, scan, chat)
│   │   └── util/            # Keyboard entry, full-screen messages
│   ├── components/          # UI theme system, icons, themes
│   │   ├── UITheme.h/cpp    # GUI singleton — orientation-aware rendering
│   │   ├── icons/           # Icon headers (book, bookmark, cover, folder, wifi, …)
│   │   └── themes/          # Lyra, Lyra3Covers, RoundedRaff themes
│   ├── images/              # Image data (logo, loading icon, moon icon)
│   ├── simulator/           # Simulator stubs: NimBLE, FreeRTOS, MeshCore mock
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
├── open-x4-sdk/             # Hardware SDK submodule (display, input, storage, battery)
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

# Build specific environment (gh_release, gh_release_rc, slim, simulator)
pio run -e gh_release

# Build and run desktop simulator (no device required)
pio run -e simulator
.pio/build/simulator/program

# Build + launch simulator in one command
pio run -e simulator -t run_simulator

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

# Run desktop algorithm tests
bash test/run_release_json_parser_test.sh
bash test/run_streaming_json_parser_test.sh
bash test/run_hyphenation_eval.sh
bash test/run_differential_rounding_test.sh

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

## Contribution Instructions

- You MUST verify your changes with the formatter and static analysis.

    Use the following commands:
    - `pio run` to build and check for compile errors
    - `./bin/clang-format-fix` to format code (clang-format 21+)
    - `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
      to run static analysis

- You MUST update tests when changing core algorithm logic covered by
  the desktop test suites (JSON parser, hyphenation, differential
  rounding).

- You MUST run the relevant desktop tests to verify your changes do
  not break existing functionality:

    ```sh
    bash test/run_release_json_parser_test.sh
    bash test/run_streaming_json_parser_test.sh
    bash test/run_hyphenation_eval.sh
    bash test/run_differential_rounding_test.sh
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

**Orientation-aware rendering**:
- Never hardcode `800` or `480`. Use `renderer.getScreenWidth()` and
  `renderer.getScreenHeight()`.
- Use `renderer.getOrientedViewableTRBL()` for bezel margins.

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
