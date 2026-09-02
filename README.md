# CrossPoint Mesh

CrossPoint Mesh is a fork of CrossPoint Reader — open-source firmware
that combines a full-featured e-reader with a MeshCore client.
Read EPUBs, manage your library, and stay connected to the MeshCore
decentralized mesh network for off-grid communication — all from a
single device. Community-built, fully hackable, free forever.

**Now running on:** ESP32C3-based Xteink [X4](https://www.xteink.com/products/xteink-x4) and [X3](https://www.xteink.com/products/xteink-x3).

[![Watch CrossPoint Mesh demo on YouTube](https://img.youtube.com/vi/4BtQzdU9oqg/maxresdefault.jpg)](https://www.youtube.com/watch?v=4BtQzdU9oqg)

> If you're planning to buy an Xteink device, consider purchasing an **X3/X4 Developer Edition** through https://crosspointreader.com. CrossPoint receives a small share of each sale, helping fund development costs.

## What can CrossPoint do?

- **Reader engine**: EPUB 2/3 rendering with embedded-style option, image handling, hyphenation, kerning, chapter navigation, footnotes, bookmarks, dictionary lookups ([StarDict](docs/dictionary.md)), go-to-percent, auto page turn, orientation control, focus reading, KOReader progress sync and more.

- **Various formats**: native handling for `.epub`, `.xtc/.xtch`, `.txt`, and `.bmp`.

- **Screenshots.**

- **Custom fonts**: install your favorite fonts on the SD card.

- **Tilt page turn (X3 only)**.

- **Library workflow**: folder browser, hidden-file toggle, long-press delete, recent books, SD-cache management.

- **Wireless workflows**:

  - File transfer web UI
  - EPUB Optimizer
  - Web settings UI/API (edit many device settings from browser)
  - WebSocket fast uploads
  - WebDAV handler
  - AP mode (hotspot) and STA mode (join existing Wi-Fi), both with QR helpers
  - Calibre wireless connect flow
  - OPDS browser with saved servers (up to 8), search, pagination, and direct download
  - OTA update checks and installs from GitHub releases

- **Customization**: multiple themes (Classic, Lyra, Lyra Extended, RoundedRaff), sleep screen modes, front/side button remapping, status bar controls, power-button behavior, refresh cadence, and more.

- **Localization**: 24 UI languages and counting. RTL support.

- **MeshCore Client**: Built-in BLE client for the [MeshCore](https://meshcore.co.uk/) decentralized
  mesh network — communicate without internet access, even in remote areas.
  This is not a full MeshCore client: **the companion node must be configured beforehand
  with a full-featured client, such as the MeshCore smartphone app.** CrossPoint
  connects to that already-configured companion over BLE and focuses on
  messaging.

  Included messaging features:

  - **Three-tab hub**: **Contacts**, **Channels**, and **Menu** — primary functions
    are front and centre; secondary actions are grouped under Menu.
  - **BLE companion connection** — scan for and pair with MeshCore companion devices (T-Beam,
    etc.) via encrypted BLE with PIN authentication
  - **Auto-reconnect** — automatically reconnects to the last paired companion when in range
  - **Channels** — join and send messages on public channels, hashtag channels, and
    encrypted private channels (up to 40)
  - **Contacts & direct messages** — view saved contacts with unread counts,
    send private messages with paginated conversation history, per-message delivery
    status (Sent / Delivered / Failed), bounded retry escalation (direct → flood),
    and manual route reset for stale paths
  - **Menu tab** — Discovery Nodes (browse nearby mesh nodes and add as contacts),
    Send Advert / Send Flood Advert (broadcast presence on the mesh),
    Status (companion device info), Disconnect
  - **QR contact sharing** — display your node as a `meshcore://contact/add` QR
    code on the e-ink screen so another MeshCore client can add your node
  - **Contact file exchange** — save your node's contact link to an SD file
    (`/meshcore_contacts.txt`) and import contacts from it
  - **SD persistence** — messages, contacts, and unread counts survive reboots via SD card
    storage

  Not included (compared with the full-featured smartphone client — set these
  up with the app beforehand):

  - Initial node setup and configuration: node name, BLE PIN, and radio
    parameters (frequency, bandwidth, spreading factor, TX power, GPS), as well
    as provisioning a new node's identity.
  - QR channel sharing/joining (`meshcore://channel/add`) and QR *scanning* — the
    device has no camera, so it can only emit the contact QR code, not import
    channels or contacts by scanning them.
  - Configuring a node as a repeater, and other advanced node
    administration that only the full app exposes.
  - Room servers — you cannot join a Room Server or chat through it; rooms
    are reachable only from the full-featured smartphone client.

> [!WARNING]
> **Disconnect on sleep.** To save power, CrossPoint drops the BLE
> connection to the companion when the device sleeps or leaves the MeshCore
> mode. While disconnected, you cannot tell whether a message has arrived,
> so a companion with its own message indicator (such as a buzzer) is
> recommended — for example the WisMesh Tag or the Seeed Studio SenseCAP
> T1000-E.
>
> **Contacts list scope.** The Contacts tab lists chat-type nodes only;
> sensors, repeaters, and room servers are not shown.

- **T4 text input**: button-only devices (Xteink X3/X4) type through a
  T9-style button keyboard instead of a touchscreen keyboard. Predictive word
  suggestions currently support **English** and **Russian** only — copy the
  `.trie` dictionaries from the [`t4dicts/`](t4dicts/) directory in this repo
  to `/t4dicts/` on the SD card to enable them, or build your own (see
  [docs/t4-dictionary.md](docs/t4-dictionary.md)).

---

## USB-locked devices (Xteink Unlocker)

Some Xteink units purchased from third-party stores (e.g. AliExpress) ship with USB flashing locked from the factory.
If your device is locked, you will need to use the **Xteink Unlocker** tool available at
https://crosspointreader.com/#unlock-tool before you can flash CrossPoint.

**You do not need this tool if you bought your device directly from xteink.com.** Those units are not locked.

**Not sure if your device is locked?** Power it on, connect the USB-C cable, and try flashing via the web flasher first (see
[Install firmware](#install-firmware) below). If the browser's serial device picker does not show your device, try a different
USB port or browser before assuming the device is locked. Only reach for the unlocker if the device still doesn't appear.

> ### ⚠️ WARNING: READ THIS BEFORE USING THE UNLOCKER ⚠️
>
> **The only officially supported firmwares in the unlock tool are CrossPoint and CrossInk.**
>
> Flashing any other firmware on a USB-locked device may **permanently brick the device** or leave it **permanently
> stuck on that firmware with no recovery path**. Once USB flashing is re-locked, your only way back is via OTA, and if
> the firmware you flashed doesn't support OTA, **there is no way out**.
>
> **The Papyrix fork has removed OTA update support from its code.** If you flash Papyrix onto a
> USB-locked unit, you will have **zero update or recovery path** and will be stuck on it forever. **Do not flash
> Papyrix (or any other unsupported firmware) on a locked device.**

## Install firmware

### Web installer (recommended)

1. Connect your device to your computer via USB-C and wake/unlock the device
2. Go to https://crosspointreader.com/#flash-tools, select device (X3 or X4), and choose an official CrossPoint release.

### Web installer (specific version)

1. Connect your device to your computer via USB-C and wake/unlock the device
2. Download a `firmware.bin` from [Releases](https://github.com/crosspoint-reader/crosspoint-reader/releases), local build, or continuous integration artifact.
3. Go to https://crosspointreader.com/#flash-tools, select device (X3 or X4), click "Custom .bin" and upload a `firmware.bin`.

### Revert to Official Firmware

To revert to the official firmware, you can also flash the latest official firmware using https://crosspointreader.com/#flash-tools.

### Command line

1. Install [`esptool`](https://github.com/espressif/esptool):

```bash
pip install esptool
```

2. Download `firmware.bin` from the [releases page](https://github.com/crosspoint-reader/crosspoint-reader/releases).
3. Connect your device via USB-C.
4. Find the device port. On Linux, run `dmesg` after connecting. On macOS:

```bash
log stream --predicate 'subsystem == "com.apple.iokit"' --info
```

5. Flash:

```bash
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
```

Adjust `/dev/ttyACM0` to match your system.

### Manual

See [Development quick start](#development-quick-start) below.

---

## Custom SD-card fonts

Convert your own TTF/OTF files into `.cpfont` files that load from the SD card. No firmware reflash is needed.

1. Go to https://crosspointreader.com/fonts and open the "SD-card font builder" form.
2. Upload up to four styles (regular, bold, italic, bold-italic), set the family name, point sizes, and Unicode range.
3. Download the generated `.cpfont` files.
4. Copy them to your SD card under `/fonts/YourFont/` (or `/.fonts/YourFont/` to hide the folder).
5. Select the font on the device from the font settings.

Conversion runs the firmware repo's `lib/EpdFont/scripts/fontconvert_sdcard.py` script unmodified, so output matches a local host build.

---

## Documentation

- [User Guide](./USER_GUIDE.md)
- [T4 predictive text input](./docs/t4-dictionary.md) - the button-driven keyboard and predictive dictionary format
- [Web server usage](./docs/webserver.md)
- [Web server endpoints](./docs/webserver-endpoints.md)
- [Project scope](./SCOPE.md)
- [Contributing docs](./docs/contributing/README.md)
- [Touch and UI development](./docs/contributing/touch-and-ui.md) - FreeInkUI components for new screens, the touch bridge for existing ones, and build envs for the non-Xteink touch devices

---

## Desktop Simulator

A desktop simulator lets you test firmware UI without a device. It
compiles natively (macOS/Linux) and renders the e-ink display in an
SDL2 window.

**Prerequisites**:
- macOS: `brew install sdl2`
- Linux (Debian/Ubuntu): `sudo apt install libsdl2-dev libssl-dev`

**Build and run**:
```bash
# Build + launch in one command
pio run -e simulator -t run_simulator

# Or build only, then run
pio run -e simulator
.pio/build/simulator/program
```

**Setup**: Place EPUB books in `./fs_/books/` (maps to SD card
`/books/`).

**Keyboard controls**:

| Key | Action |
| --- | --- |
| ↑ / ↓ | Page back / forward (side buttons) |
| ← / → | Left / right front buttons |
| Return | Confirm / Select |
| Escape | Back |
| P | Power |
| S | Simulate sleep |

For architecture details, stub descriptions, and cache management
under the simulator, see the [Desktop Simulator](./CLAUDE.md) section
in CLAUDE.md.

**MeshCore simulation**: MeshCore activities (hub, discover, scan,
status, thread) compile and render in the simulator. Place a
`meshcore_mock.json` file in `fs_/` to activate the mock BLE layer —
BLE operations return data from JSON instead of no-ops. Without the
JSON file, all BLE operations are no-ops (the simulator has no real
BLE hardware). All UI screens remain interactive regardless.

Mock hotkeys for injecting BLE events:

| Key | Action |
| --- | --- |
| 1 | Inject BLE disconnect |
| 2 | Inject new node (PKT_NEW_ADVERT)   |
| 3 | Inject channel created |
| 4 | Inject channel message |
| 5 | Inject direct message |
| 6 | Inject companion status update |
| 7 | Inject advert success (PKT_OK) |
| 8 | Inject flood advert success (PKT_OK) |
| 9 | Inject DM delivery ACK (PKT_ACK) |

See [CLAUDE.md](./CLAUDE.md) for detailed simulator architecture
and the mock session lifecycle.

---

## Development quick start

### Prerequisites

- [pioarduino](https://github.com/pioarduino/pioarduino) or VS Code + pioarduino plugin
- Python 3.8+
- `clang-format` 21
- USB-C cable supporting data transfer

### Setup

```bash
git clone --recursive https://github.com/crosspoint-reader/crosspoint-reader
cd crosspoint-reader

# if cloned without --recursive:
git submodule update --init --recursive
```

### Nix/NixOS

Nix/NixOS users can enter the development shell with either `nix develop` (flakes) or `nix-shell`:

```bash
nix develop -f nix
# or
nix-shell nix
```

To flash a connected ESP32-C3 device, enable PlatformIO's udev rules in your NixOS configuration:

```nix
services.udev.packages = with pkgs; [ platformio-core.udev ];
```

After rebuilding the system configuration, reconnect the device or reload udev rules.

### Build / flash / monitor

```bash
pio run --target upload
```

### Contributor pre-PR checks

```bash
./bin/clang-format-fix
pio check -e default
pio run -e default
```

### Debugging

After flashing the new features, it’s recommended to capture detailed logs from the serial port.

First, make sure all required Python packages are installed:

```python
python3 -m pip install pyserial colorama matplotlib
```

After that run the script:

```sh
# For Linux
# This was tested on Debian and should work on most Linux systems.
python3 scripts/debugging_monitor.py

# For macOS
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```

Minor adjustments may be required for Windows.

---

## Internals

CrossPoint Mesh is pretty aggressive about caching data down to the SD card to minimise RAM usage. The ESP32-C3 only has ~380KB of usable RAM, so we have to be careful. A lot of the decisions made in the design of the firmware were based on this constraint.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the
cache. This cache directory exists at `.crosspoint` on the SD card. The structure is as follows:

```text
.crosspoint/
├── epub_<hash>/         # one directory per book, named by content hash
│   ├── progress.bin     # reading position (chapter, page, etc.)
│   ├── cover.bmp        # generated cover image
│   ├── book.bin         # metadata: title, author, spine, TOC
│   ├── css_rules.cache  # parsed CSS rule cache
│   ├── img_*            # rendered image cache files
│   └── sections/        # per-chapter layout cache
│       ├── 0.bin
│       ├── 1.bin
│       └── ...
├── settings.json        # device settings
├── state.json           # resume/runtime state
└── recent.json          # recent books list
```

Removing `/.crosspoint` clears all cached metadata and forces a full regeneration on next open. Book deletes, overwrites, and moves done through the firmware or web UI clear or re-key matching caches; manual SD-card edits may leave stale cache directories behind.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

---

## Contributing

Contributions are welcome. If you're new to the codebase, start with the [contributing docs](./docs/contributing/README.md). For things to work on, check the [ideas discussion board](https://github.com/crosspoint-reader/crosspoint-reader/discussions/categories/ideas) — leave a comment before starting so we don't duplicate effort.

Everyone here is a volunteer, so please be respectful and patient.

---

## Community forks

One of the best things about open source is that anyone can take the code in a different direction. If you need something outside CrossPoint's [scope](./SCOPE.md), check out the community forks:

- [CrossInk](https://github.com/uxjulia/CrossInk) — Typography and reading tracking: Bionic Reading (bolds word stems to create fixation points), guide dots between words, improved paragraph indents, and replaces the default fonts with ChareInk/Lexend/Bitter.

- [papyrix-reader](https://github.com/bigbag/papyrix-reader) — Adds FB2 and MD format support. Actively maintained with Arabic script support. Custom themes via SD card.

- ~~[crosspet](https://github.com/trilwu/crosspet) — A Vietnamese fork that adds a Tamagotchi-style virtual chicken that grows based on your reading milestones (pages read, streaks, care). Also: Flashcards, Weather, Pomodoro timer, and mini-games.~~ (Unmaintained)

- [crosspoint-reader-cjk](https://github.com/aBER0724/crosspoint-reader-cjk) — Purpose-built for Chinese, Japanese, and Korean reading.

- [inx](https://github.com/obijuankenobiii/inx) — Completely reimagines the user interface with tabbed navigation.

- ~~[PlusPoint](https://github.com/ngxson/pluspoint-reader) — custom JS apps support.~~ (Unmaintained)

- [crosspoint-reader-papers3](https://github.com/juicecultus/crosspoint-reader-papers3) — Crosspoint port for M5Stack Paper S3.

- [t5s3-reader](https://github.com/ShallowGreen123/t5s3-reader) — Crosspoint port for LilyGo T5 ePaper S3 / T5S3 4.7-inch e-paper device.

**Note:** Many of these features will make their way into CrossPoint over time. We maintain a slower pace to ensure rock-solid stability and squash bugs before they reach your device.

Want to build your own device? Be sure to check out the [de-link](https://github.com/iandchasse/de-link) project.

---

CrossPoint Mesh is **not affiliated with Xteink or any device manufacturer**.

Huge shoutout to [diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader), which inspired this project.
