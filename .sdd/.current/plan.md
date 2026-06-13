# Implementation Plan: MeshCore Companion Integration

**Spec**: `.sdd/.current/spec.md`
**Created**: 2026-05-14
**Status**: Draft

## Technical Context

| Field | Value |
| --- | --- |
| Language/Version | C++20 (`-std=gnu++2a`), no exceptions, no RTTI |
| Primary Dependencies | Arduino-ESP32 (pioarduino), NimBLE-Arduino 2.5.0 (new) |
| Storage | SD card via SdFat (HalStorage). No database. JSON + binary files |
| Testing | Shell-script desktop tests + manual on-device |
| Target Platform | Xteink X3/X4 (ESP32-C3, 380 KB RAM, 16 MB flash) |
| Project Type | Embedded firmware (single device) |
| Performance Goals | 15 s connect flow, 3 s message display, >30 KB free heap |
| Constraints | 380 KB RAM, single-core RISC-V @ 160 MHz, single 48 KB framebuffer, BLE/WiFi share radio |
| Scale/Scope | Single companion node, 8 channel slots, ~100 msgs/thread on SD |

## Research

### R1: BLE Library Choice — NimBLE-Arduino 2.5.0

NimBLE-Arduino (h2zero) is the standard BLE library for
resource-constrained ESP32 projects. It explicitly supports
ESP32-C3 and uses ~100 KB less flash and significantly less
RAM than the default bluedroid stack.

- **PlatformIO install**: `h2zero/NimBLE-Arduino @ 2.5.0`
- **Client API**: `NimBLEDevice::init()`,
  `NimBLEScan`, `NimBLEClient`, `NimBLERemoteService`,
  `NimBLERemoteCharacteristic`
- **Notification callback**: Set via
  `characteristic->subscribe(true, notifyCallback)`
- **MTU negotiation**: Automatic in NimBLE; can request via
  `NimBLEDevice::setMTU(512)`
- **RAM impact**: NimBLE client-only mode uses ~30-40 KB heap
  for BLE stack + connection. This fits within the ~40 KB
  budget if server role is disabled.
- **Configuration**: Disable server role via build flag
  `-DCONFIG_BT_NIMBLE_ROLE_CENTRAL=1` and
  `-DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL=0` and
  `-DCONFIG_BT_NIMBLE_ROLE_BROADCASTER=0` and
  `-DCONFIG_BT_NIMBLE_ROLE_OBSERVER=1` to minimize RAM.
- **WiFi coexistence**: ESP32-C3 can run BLE and WiFi, but
  they share the 2.4 GHz radio. For reliability, WiFi must
  be disconnected before BLE init
  (`WiFi.mode(WIFI_OFF)`).

Source: https://github.com/h2zero/NimBLE-Arduino,
https://registry.platformio.org/libraries/h2zero/NimBLE-Arduino

### R2: MeshCore Companion Protocol v1.12.0+

Protocol uses Nordic UART Service (NUS) over BLE GATT:

- **Service UUID**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- **RX (app→device)**: `6E400002-...`
- **TX (device→app)**: `6E400003-...`
- **Framing**: 1 byte packet type + variable payload,
  little-endian integers, UTF-8 strings
- **Command queue**: Send one command at a time, wait for
  response or 5 s timeout
- **Initialization sequence**: `CMD_APP_START` (0x01) →
  `CMD_DEVICE_QUERY` (0x16 0x03) → `CMD_GET_CONTACTS`
  (0x15) → `CMD_GET_CHANNEL` (0x1F) × 8

Key commands:
- `CMD_APP_START` (0x01) → `PACKET_SELF_INFO` (0x05)
- `CMD_DEVICE_QUERY` (0x16 0x03) → `PACKET_DEVICE_INFO`
  (0x0D)
- `CMD_GET_CONTACTS` (0x15) → `PACKET_CONTACT_START` (0x02)
  / `PACKET_CONTACT` (0x03) / `PACKET_CONTACT_END` (0x04)
- `CMD_GET_CHANNEL` (0x1F idx) → `PACKET_CHANNEL_INFO`
  (0x12)
- `CMD_SET_CHANNEL` (0x20 idx name[32] secret[16]) →
  `PACKET_OK` / `PACKET_ERROR`
- `CMD_SEND_CHANNEL_MESSAGE` (0x03 0x00 idx ts[4] text) →
  `PACKET_MSG_SENT` (0x06)
- `CMD_GET_MESSAGE` (0x0A) → `PACKET_CHANNEL_MSG_RECV` /
  `PACKET_CONTACT_MSG_RECV` / `PACKET_NO_MORE_MSGS`
- `CMD_GET_BATTERY` (0x14) → `PACKET_BATTERY` (0x0C)

Push notifications (async from device):
- `PACKET_MESSAGES_WAITING` (0x83) → poll with
  `CMD_GET_MESSAGE`
- `PACKET_ADVERTISEMENT` (0x80) → discovered node update
- `PACKET_ACK` (0x82) → DM delivery confirmation

Source: https://docs.meshcore.io/companion_protocol/

### R3: WiFi/BLE Radio Conflict

The ESP32-C3 has a single 2.4 GHz radio shared between WiFi
and BLE. The existing `CrossPointWebServerActivity` manages
WiFi lifecycle. Before starting BLE:

1. Check if WiFi is active (`WiFi.getMode() != WIFI_OFF`)
2. If active, warn user and disconnect:
   `WiFi.disconnect(true); WiFi.mode(WIFI_OFF);`
3. Then init NimBLE: `NimBLEDevice::init("CrossPoint")`

On MeshCore activity exit, deinit BLE:
`NimBLEDevice::deinit(true)` to free all BLE resources.

### R4: Existing Activity Patterns

Activities follow a strict lifecycle:
- `onEnter()`: Allocate resources, start tasks, initial render
- `loop()`: Handle input via `MappedInputManager` +
  `ButtonNavigator`
- `render(RenderLock&&)`: Draw to framebuffer via
  `GUI.drawList()`, `GUI.drawHeader()`, etc.
- `onExit()`: Free resources in reverse order, delete tasks

Sub-activities use `startActivityForResult()` with a callback
lambda. `KeyboardEntryActivity` returns `KeyboardResult` with
`.text`. Menu items in `HomeActivity` are dynamic vectors of
`const char*` with corresponding `UIIcon` vectors.

### R5: Message Persistence Strategy

Messages are persisted to SD card under
`.crosspoint/meshcore/`:

- Channel messages: `.crosspoint/meshcore/ch_<idx>/msgs.bin`
- Contact DMs: `.crosspoint/meshcore/dm_<pubkey_hex>/msgs.bin`
- Saved contacts: `.crosspoint/meshcore/contacts.bin`
- Companion info: `.crosspoint/meshcore/companion.json`

Binary format for messages uses the existing
`Serialization/BinaryWriter.h` / `BinaryReader.h` pattern
from the Epub cache system. Each file has a version byte
header for forward compatibility.

Only the current page of messages is held in RAM. Older
messages are read from SD on demand (page back).

### R6: RAM Budget Analysis

Available heap for MeshCore activity: ~80-100 KB (after
framebuffer and core allocations).

- NimBLE client stack: ~30-40 KB
- Message page buffer (10 messages × ~200 bytes): ~2 KB
- Contact list (max ~60 contacts × ~80 bytes): ~5 KB
- Channel list (8 channels × ~50 bytes): ~0.4 KB
- Command queue buffer: ~0.5 KB
- Discovered nodes list (visible page only, 10 × ~80 bytes):
  ~0.8 KB
- **Total estimated**: ~39-49 KB
- **Remaining**: ~30-50 KB (above 30 KB safety threshold)

NimBLE RAM can be further reduced by limiting:
- Max connections to 1: `-DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`
- Max bonds to 1: `-DCONFIG_BT_NIMBLE_MAX_BONDS=1`

## Entities

### E1: MeshCoreCompanion

Represents the connected BLE companion node.

```cpp
struct MeshCoreCompanion {
  char name[64];            // Device name from PACKET_SELF_INFO
  uint8_t publicKey[32];    // 32-byte public key
  char bleAddress[18];      // "AA:BB:CC:DD:EE:FF"
  float radioFreq;          // MHz
  float radioBw;            // kHz
  uint8_t radioSf;          // Spreading factor
  uint8_t radioCr;          // Coding rate
  char firmwareBuild[13];   // From DEVICE_INFO
  char model[41];           // From DEVICE_INFO
  char version[21];         // From DEVICE_INFO
  uint16_t batteryMv;       // Millivolts
  uint32_t storageUsedKb;   // KB
  uint32_t storageTotalKb;  // KB
  uint8_t maxContacts;      // From DEVICE_INFO (raw * 2)
  uint8_t maxChannels;      // From DEVICE_INFO
};
```

**Persisted to**: `.crosspoint/meshcore/companion.json`
(BLE address only, for auto-reconnect).

**Maps to**: New struct in `lib/MeshCore/MeshCore/`.
No existing entity.

### E2: MeshCoreContact

A MeshCore node (discovered or saved).

```cpp
struct MeshCoreContact {
  uint8_t publicKey[32];    // Full 32-byte public key
  char name[64];            // Node name
  uint8_t type;             // 0=companion, 1=repeater, etc.
  uint32_t lastSeen;        // Unix timestamp
  uint8_t pathLength;       // Hop count
  int8_t snr;               // Signal-to-noise (×4)
  bool isSaved;             // In saved contacts list
  uint16_t unreadCount;     // Unread DM count (saved only)
};
```

**Persisted to**:
- Discovered nodes: received via protocol, not persisted
  (volatile, refreshed on connect)
- Saved contacts: `.crosspoint/meshcore/contacts.bin`

**Maps to**: New struct. No existing entity.

### E3: MeshCoreChannel

A group communication channel (0-7).

```cpp
struct MeshCoreChannel {
  uint8_t index;            // 0-7
  char name[33];            // 32 bytes + null
  uint8_t secret[16];       // Channel secret
  uint8_t type;             // 0=public, 1=hashtag, 2=private
  uint16_t unreadCount;     // Unread messages
  bool configured;          // Has name + secret
};
```

**Persisted to**: Stored on companion device. Channel
secrets are NOT persisted locally (re-fetched on connect).
Unread counts persisted in
`.crosspoint/meshcore/unread.bin`.

**Maps to**: New struct. No existing entity.

### E4: MeshCoreMessage

A text message (channel or direct).

```cpp
struct MeshCoreMessage {
  uint8_t direction;        // 0=received, 1=sent
  uint8_t type;             // 0=channel, 1=direct
  uint8_t pubkeyPrefix[6]; // Sender public key prefix
  char senderName[64];      // Resolved sender name
  uint8_t channelIdx;       // Channel index (if channel msg)
  uint32_t timestamp;       // Unix timestamp
  int8_t snr;               // SNR × 4 (V3 only, 0 if N/A)
  uint8_t pathLength;       // Hop count
  uint8_t deliveryStatus;   // 0=sent, 1=ack, 2=failed
  char text[184];           // Message text (max payload)
};
```

**Persisted to**:
- Channel: `.crosspoint/meshcore/ch_<idx>/msgs.bin`
- Direct: `.crosspoint/meshcore/dm_<pubkey_hex>/msgs.bin`

**Maps to**: New struct. No existing entity.

### E5: BleConnectionState

Runtime-only state tracking.

```cpp
enum class BleConnectionState : uint8_t {
  DISCONNECTED,
  SCANNING,
  CONNECTING,
  INITIALIZING,
  CONNECTED
};
```

**Maps to**: New enum. No persistence (runtime only).

## File Structure

### New Library: `lib/MeshCore/`

Protocol layer — no UI, no Activity dependencies. Pure data
structures and BLE protocol logic.

```
lib/MeshCore/
├── MeshCore/
│   ├── MeshCoreTypes.h        # E1-E5 structs and enums
│   ├── MeshCoreProtocol.h     # Command builders, packet parsers
│   ├── MeshCoreProtocol.cpp
│   ├── MeshCoreClient.h       # BLE connection + command queue
│   ├── MeshCoreClient.cpp
│   ├── MeshCoreMessageStore.h # SD card message persistence
│   └── MeshCoreMessageStore.cpp
└── library.json               # PlatformIO library metadata
```

### New Activity: `src/activities/meshcore/`

UI layer — screens, input handling, rendering.

```
src/activities/meshcore/
├── MeshCoreHubActivity.h      # Main hub: channels, contacts, status tabs
├── MeshCoreHubActivity.cpp
├── MeshCoreThreadActivity.h   # Message thread (channel or DM)
├── MeshCoreThreadActivity.cpp
├── MeshCoreScanActivity.h     # BLE scan + connect flow
├── MeshCoreScanActivity.cpp
├── MeshCoreDiscoverActivity.h # Discovered nodes list
├── MeshCoreDiscoverActivity.cpp
├── MeshCoreStatusActivity.h   # Companion status display
└── MeshCoreStatusActivity.cpp
```

### Modified Files

| File | Change |
| --- | --- |
| `platformio.ini` | Add `h2zero/NimBLE-Arduino @ 2.5.0` to `lib_deps`, add NimBLE build flags |
| `src/activities/home/HomeActivity.h` | Add MeshCore menu item, forward-declare launch method |
| `src/activities/home/HomeActivity.cpp` | Add "MeshCore" to menu items vector, add navigation handler |
| `src/activities/ActivityManager.h` | Add `goToMeshCore()` method declaration |
| `src/activities/ActivityManager.cpp` | Implement `goToMeshCore()` with `replaceActivity()` |
| `src/CrossPointSettings.h` | Add `meshCoreAutoReconnect` setting |
| `src/CrossPointSettings.cpp` | Register new setting for JSON persistence |
| `src/SettingsList.h` | Add MeshCore settings entry |
| `lib/I18n/translations/english.yaml` | Add all `STR_MESHCORE_*` i18n keys |

## Tasks

### Task 1: Add NimBLE-Arduino dependency [x]

**File**: `platformio.ini`

**Why**: NimBLE-Arduino provides the BLE client stack for
ESP32-C3. Must be configured as client-only to minimize
RAM usage.

**Steps**:

1.1. Open `platformio.ini` and locate the `[base]` section's
`lib_deps`. Add NimBLE-Arduino after the existing deps:

```ini
  h2zero/NimBLE-Arduino @ 2.5.0
```

1.2. In the `[base]` section's `build_flags`, add NimBLE
configuration flags to restrict to client-only role:

```ini
  -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=1
  -DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL=0
  -DCONFIG_BT_NIMBLE_ROLE_BROADCASTER=0
  -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=1
  -DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
  -DCONFIG_BT_NIMBLE_MAX_BONDS=1
```

1.3. Run `pio run` to verify the build succeeds with the
new dependency. Expected: 0 errors.

---

### Task 2: Create MeshCoreTypes.h [x]

**File**: `lib/MeshCore/MeshCore/MeshCoreTypes.h`

**Why**: All data structures must be defined before any
protocol or UI code can reference them.

**Steps**:

2.1. Create `lib/MeshCore/library.json`:

```json
{
  "name": "MeshCore",
  "version": "1.0.0",
  "description": "MeshCore companion protocol library",
  "dependencies": {
    "Logging": "*",
    "Serialization": "*",
    "hal": "*"
  }
}
```

2.2. Create `lib/MeshCore/MeshCore/MeshCoreTypes.h`:

```cpp
#pragma once

#include <cstdint>
#include <cstring>

// BLE connection lifecycle states
enum class BleConnectionState : uint8_t {
  DISCONNECTED = 0,
  SCANNING,
  CONNECTING,
  INITIALIZING,  // Running init sequence
  CONNECTED
};

// MeshCore node types (from protocol advertisements)
enum class MeshNodeType : uint8_t {
  COMPANION = 0,
  REPEATER = 1,
  ROOM_SERVER = 2,
  SENSOR = 3,
  UNKNOWN = 255
};

// Message direction
enum class MsgDirection : uint8_t {
  RECEIVED = 0,
  SENT = 1
};

// Message type
enum class MsgType : uint8_t {
  CHANNEL = 0,
  DIRECT = 1
};

// DM delivery status
enum class DeliveryStatus : uint8_t {
  SENT = 0,
  ACKED = 1,
  FAILED = 2
};

// Channel type
enum class ChannelType : uint8_t {
  PUBLIC = 0,
  HASHTAG = 1,
  PRIVATE_CH = 2
};

struct MeshCoreCompanion {
  char name[64] = {};
  uint8_t publicKey[32] = {};
  char bleAddress[18] = {};
  float radioFreq = 0;
  float radioBw = 0;
  uint8_t radioSf = 0;
  uint8_t radioCr = 0;
  char firmwareBuild[13] = {};
  char model[41] = {};
  char version[21] = {};
  uint16_t batteryMv = 0;
  uint32_t storageUsedKb = 0;
  uint32_t storageTotalKb = 0;
  uint8_t maxContacts = 0;
  uint8_t maxChannels = 8;
};

struct MeshCoreContact {
  uint8_t publicKey[32] = {};
  char name[64] = {};
  MeshNodeType type = MeshNodeType::UNKNOWN;
  uint32_t lastSeen = 0;
  uint8_t pathLength = 0;
  int8_t snr = 0;
  bool isSaved = false;
  uint16_t unreadCount = 0;

  // Return 6-byte hex prefix for display
  void getPublicKeyPrefix(char out[13]) const {
    static constexpr char hex[] = "0123456789abcdef";
    for (int i = 0; i < 6; ++i) {
      out[i * 2] = hex[publicKey[i] >> 4];
      out[i * 2 + 1] = hex[publicKey[i] & 0x0F];
    }
    out[12] = '\0';
  }
};

struct MeshCoreChannel {
  uint8_t index = 0;
  char name[33] = {};
  uint8_t secret[16] = {};
  ChannelType type = ChannelType::PUBLIC;
  uint16_t unreadCount = 0;
  bool configured = false;

  bool isEmpty() const {
    return name[0] == '\0';
  }
};

static constexpr uint16_t MAX_MSG_TEXT_LEN = 184;

struct MeshCoreMessage {
  MsgDirection direction = MsgDirection::RECEIVED;
  MsgType type = MsgType::CHANNEL;
  uint8_t pubkeyPrefix[6] = {};
  char senderName[64] = {};
  uint8_t channelIdx = 0;
  uint32_t timestamp = 0;
  int8_t snr = 0;
  uint8_t pathLength = 0;
  DeliveryStatus deliveryStatus = DeliveryStatus::SENT;
  char text[MAX_MSG_TEXT_LEN] = {};
};

// Message store file version (increment on format change)
static constexpr uint8_t MESHCORE_MSG_FILE_VERSION = 1;
// Contact store file version
static constexpr uint8_t MESHCORE_CONTACT_FILE_VERSION = 1;
// Conservative max message text length for send UI
static constexpr uint16_t MESHCORE_SEND_CHAR_LIMIT = 140;
```

2.3. Run `pio run` to verify the header compiles. Expected:
0 errors.

---

### Task 3: Create MeshCoreProtocol — command builders [x]

**File**: `lib/MeshCore/MeshCore/MeshCoreProtocol.h`,
`lib/MeshCore/MeshCore/MeshCoreProtocol.cpp`

**Why**: Encapsulates all binary protocol encoding/decoding
in one module so activities never construct raw bytes.

**Steps**:

3.1. Create `lib/MeshCore/MeshCore/MeshCoreProtocol.h`:

```cpp
#pragma once

#include "MeshCoreTypes.h"
#include <cstddef>
#include <cstdint>

// MeshCore companion protocol command/packet constants
namespace MeshProto {

// Command bytes (app → firmware)
static constexpr uint8_t CMD_APP_START = 0x01;
static constexpr uint8_t CMD_SEND_CHAN_MSG = 0x03;
static constexpr uint8_t CMD_GET_MESSAGE = 0x0A;
static constexpr uint8_t CMD_GET_BATTERY = 0x14;
static constexpr uint8_t CMD_GET_CONTACTS = 0x15;
static constexpr uint8_t CMD_DEVICE_QUERY = 0x16;
static constexpr uint8_t CMD_GET_CHANNEL = 0x1F;
static constexpr uint8_t CMD_SET_CHANNEL = 0x20;

// Packet types (firmware → app)
static constexpr uint8_t PKT_OK = 0x00;
static constexpr uint8_t PKT_ERROR = 0x01;
static constexpr uint8_t PKT_CONTACT_START = 0x02;
static constexpr uint8_t PKT_CONTACT = 0x03;
static constexpr uint8_t PKT_CONTACT_END = 0x04;
static constexpr uint8_t PKT_SELF_INFO = 0x05;
static constexpr uint8_t PKT_MSG_SENT = 0x06;
static constexpr uint8_t PKT_CONTACT_MSG = 0x07;
static constexpr uint8_t PKT_CHANNEL_MSG = 0x08;
static constexpr uint8_t PKT_NO_MORE_MSGS = 0x0A;
static constexpr uint8_t PKT_BATTERY = 0x0C;
static constexpr uint8_t PKT_DEVICE_INFO = 0x0D;
static constexpr uint8_t PKT_CONTACT_MSG_V3 = 0x10;
static constexpr uint8_t PKT_CHANNEL_MSG_V3 = 0x11;
static constexpr uint8_t PKT_CHANNEL_INFO = 0x12;
static constexpr uint8_t PKT_ADVERTISEMENT = 0x80;
static constexpr uint8_t PKT_ACK = 0x82;
static constexpr uint8_t PKT_MSGS_WAITING = 0x83;

// Command timeout
static constexpr uint32_t CMD_TIMEOUT_MS = 5000;

// --- Command builders ---
// All return the number of bytes written to `out`.

// CMD_APP_START: 0x01 + 7 reserved + app name
size_t buildAppStart(uint8_t* out, size_t maxLen);

// CMD_DEVICE_QUERY: 0x16 0x03
size_t buildDeviceQuery(uint8_t* out, size_t maxLen);

// CMD_GET_CONTACTS: 0x15
size_t buildGetContacts(uint8_t* out, size_t maxLen);

// CMD_GET_CHANNEL: 0x1F <index>
size_t buildGetChannel(uint8_t* out, size_t maxLen,
                       uint8_t channelIdx);

// CMD_SET_CHANNEL: 0x20 <index> <name[32]> <secret[16]>
size_t buildSetChannel(uint8_t* out, size_t maxLen,
                       uint8_t channelIdx,
                       const char* name,
                       const uint8_t* secret16);

// CMD_SEND_CHANNEL_MESSAGE: 0x03 0x00 <idx> <ts[4]> <text>
size_t buildSendChannelMsg(uint8_t* out, size_t maxLen,
                           uint8_t channelIdx,
                           uint32_t timestamp,
                           const char* text);

// CMD_GET_MESSAGE: 0x0A
size_t buildGetMessage(uint8_t* out, size_t maxLen);

// CMD_GET_BATTERY: 0x14
size_t buildGetBattery(uint8_t* out, size_t maxLen);

// --- Packet parsers ---
// All return true on success, false on parse error.

bool parseSelfInfo(const uint8_t* data, size_t len,
                   MeshCoreCompanion& out);

bool parseDeviceInfo(const uint8_t* data, size_t len,
                     MeshCoreCompanion& out);

bool parseChannelInfo(const uint8_t* data, size_t len,
                      MeshCoreChannel& out);

bool parseBattery(const uint8_t* data, size_t len,
                  MeshCoreCompanion& out);

bool parseContact(const uint8_t* data, size_t len,
                  MeshCoreContact& out);

bool parseChannelMessage(const uint8_t* data, size_t len,
                         MeshCoreMessage& out);

bool parseContactMessage(const uint8_t* data, size_t len,
                         MeshCoreMessage& out);

bool parseMsgSent(const uint8_t* data, size_t len,
                  uint32_t& expectedAck,
                  uint32_t& suggestedTimeoutMs);

bool parseAck(const uint8_t* data, size_t len,
              uint8_t ackCode[6]);

}  // namespace MeshProto
```

3.2. Create `lib/MeshCore/MeshCore/MeshCoreProtocol.cpp`
implementing all builders and parsers. Each function:
- Validates input length before accessing bytes
- Uses `memcpy` for multi-byte reads (RISC-V alignment)
- Returns false and logs `LOG_ERR("MESH", ...)` on
  malformed packets

Example for `buildAppStart`:

```cpp
#include "MeshCoreProtocol.h"

#include <Logging.h>
#include <cstring>

namespace MeshProto {

size_t buildAppStart(uint8_t* out, size_t maxLen) {
  static constexpr char APP_NAME[] = "CrossPoint";
  const size_t needed = 8 + sizeof(APP_NAME) - 1;
  if (maxLen < needed) {
    LOG_ERR("MESH", "buildAppStart: buffer too small");
    return 0;
  }
  memset(out, 0, 8);
  out[0] = CMD_APP_START;
  memcpy(out + 8, APP_NAME, sizeof(APP_NAME) - 1);
  return needed;
}
```

Example for `parseSelfInfo`:

```cpp
bool parseSelfInfo(const uint8_t* data, size_t len,
                   MeshCoreCompanion& out) {
  if (len < 58) {
    LOG_ERR("MESH", "parseSelfInfo: too short (%d)", len);
    return false;
  }
  // data[0] == PKT_SELF_INFO already checked by caller
  size_t off = 1;
  // skip advType, txPower, maxTxPower
  off += 3;
  memcpy(out.publicKey, data + off, 32);
  off += 32;
  // skip lat(4), lon(4), multiAcks(1), advLocPolicy(1),
  // telemetryMode(1), manualAddContacts(1)
  off += 12;

  uint32_t freqRaw, bwRaw;
  memcpy(&freqRaw, data + off, 4);
  memcpy(&bwRaw, data + off + 4, 4);
  out.radioFreq = freqRaw / 1000.0f;
  out.radioBw = bwRaw / 1000.0f;
  out.radioSf = data[off + 8];
  out.radioCr = data[off + 9];
  off += 10;

  if (off < len) {
    size_t nameLen = len - off;
    if (nameLen > sizeof(out.name) - 1)
      nameLen = sizeof(out.name) - 1;
    memcpy(out.name, data + off, nameLen);
    out.name[nameLen] = '\0';
  }
  return true;
}
```

Implement all remaining builders and parsers following the
same pattern: validate length, `memcpy` for multi-byte,
`LOG_ERR` on failure.

3.3. Run `pio run` to verify compilation. Expected: 0 errors.

---

### Task 4: Create MeshCoreClient — BLE connection manager [x]

**File**: `lib/MeshCore/MeshCore/MeshCoreClient.h`,
`lib/MeshCore/MeshCore/MeshCoreClient.cpp`

**Why**: Encapsulates NimBLE lifecycle, command queue, and
notification dispatch. Activities call high-level methods
(`connect`, `sendChannelMessage`) without touching BLE
directly.

**Steps**:

4.1. Create `lib/MeshCore/MeshCore/MeshCoreClient.h`:

```cpp
#pragma once

#include "MeshCoreTypes.h"
#include <cstddef>
#include <cstdint>

class NimBLEClient;
class NimBLERemoteCharacteristic;

class MeshCoreClient {
 public:
  // Callback types (raw function pointers, no std::function)
  using StateCallback = void (*)(BleConnectionState state,
                                 void* ctx);
  using MessageCallback = void (*)(const MeshCoreMessage& msg,
                                   void* ctx);
  using ContactCallback = void (*)(const MeshCoreContact& c,
                                   bool isEnd, void* ctx);
  using AdvertCallback = void (*)(const MeshCoreContact& node,
                                  void* ctx);

  MeshCoreClient();
  ~MeshCoreClient();

  // Lifecycle
  bool init();
  void deinit();

  // Scanning
  bool startScan(uint32_t durationSec = 10);
  void stopScan();

  // Connection
  bool connectTo(const char* bleAddress);
  void disconnect();
  BleConnectionState getState() const { return state; }

  // Companion info (valid after CONNECTED)
  const MeshCoreCompanion& getCompanion() const {
    return companion;
  }

  // Commands (queued, async)
  bool requestContacts();
  bool requestChannel(uint8_t idx);
  bool requestBattery();
  bool requestMessages();
  bool sendChannelMessage(uint8_t channelIdx,
                          const char* text);
  bool sendDirectMessage(const uint8_t* pubkey32,
                         const char* text);
  bool setChannel(uint8_t idx, const char* name,
                  const uint8_t* secret16);
  bool deleteChannel(uint8_t idx);

  // Callbacks (set before connect)
  void setStateCallback(StateCallback cb, void* ctx);
  void setMessageCallback(MessageCallback cb, void* ctx);
  void setContactCallback(ContactCallback cb, void* ctx);
  void setAdvertCallback(AdvertCallback cb, void* ctx);

  // Must be called from activity loop() to process
  // responses and timeouts
  void poll();

  // Auto-reconnect target
  void setAutoReconnectAddress(const char* addr);
  const char* getAutoReconnectAddress() const;

 private:
  BleConnectionState state = BleConnectionState::DISCONNECTED;
  MeshCoreCompanion companion = {};
  NimBLEClient* bleClient = nullptr;
  NimBLERemoteCharacteristic* rxChar = nullptr;
  NimBLERemoteCharacteristic* txChar = nullptr;

  // Command queue (simple ring buffer)
  static constexpr size_t CMD_QUEUE_SIZE = 8;
  static constexpr size_t CMD_BUF_SIZE = 64;
  struct CmdEntry {
    uint8_t data[CMD_BUF_SIZE];
    size_t len;
    uint8_t expectedResponse;
  };
  CmdEntry cmdQueue[CMD_QUEUE_SIZE] = {};
  uint8_t cmdHead = 0;
  uint8_t cmdTail = 0;
  uint8_t cmdCount = 0;
  bool cmdPending = false;
  uint32_t cmdSentTime = 0;

  // Callbacks
  StateCallback stateCb = nullptr;
  void* stateCbCtx = nullptr;
  MessageCallback msgCb = nullptr;
  void* msgCbCtx = nullptr;
  ContactCallback contactCb = nullptr;
  void* contactCbCtx = nullptr;
  AdvertCallback advertCb = nullptr;
  void* advertCbCtx = nullptr;

  char autoReconnectAddr[18] = {};

  // Notification receive buffer
  static constexpr size_t RX_BUF_SIZE = 256;
  uint8_t rxBuf[RX_BUF_SIZE] = {};
  volatile size_t rxLen = 0;
  volatile bool rxReady = false;

  bool enqueueCmd(const uint8_t* data, size_t len,
                  uint8_t expectedResp);
  bool sendNextCmd();
  void processResponse(const uint8_t* data, size_t len);
  bool runInitSequence();

  static void notifyCallback(
      NimBLERemoteCharacteristic* pChar,
      uint8_t* pData, size_t length, bool isNotify);

  void setState(BleConnectionState newState);
};
```

4.2. Implement `MeshCoreClient.cpp`:

- `init()`: `NimBLEDevice::init("CrossPoint")`,
  `NimBLEDevice::setMTU(512)`, set power level
- `deinit()`: Disconnect, delete client,
  `NimBLEDevice::deinit(true)`
- `connectTo()`: Create client, connect to address,
  discover NUS service/characteristics, subscribe to TX
  notifications, run init sequence
- `poll()`: Check `rxReady` flag, parse response, match
  to pending command, dispatch to callbacks. Check timeout.
- `runInitSequence()`: Enqueue APP_START, DEVICE_QUERY,
  GET_CONTACTS, GET_CHANNEL×maxChannels
- `notifyCallback()`: Static callback — copies data to
  `rxBuf`, sets `rxReady = true` (called from NimBLE
  task context)

Key implementation detail for `notifyCallback`:

```cpp
void MeshCoreClient::notifyCallback(
    NimBLERemoteCharacteristic* pChar,
    uint8_t* pData, size_t length, bool isNotify) {
  // `this` pointer stored as client callback arg
  auto* self = static_cast<MeshCoreClient*>(
      pChar->getRemoteService()
          ->getClient()
          ->getClientCallbacks());
  // ... actually, NimBLE 2.x approach:
  // Use a static instance pointer since we only have
  // one MeshCoreClient at a time
  if (!sInstance || length == 0 || length > RX_BUF_SIZE)
    return;
  memcpy(sInstance->rxBuf, pData, length);
  sInstance->rxLen = length;
  sInstance->rxReady = true;
}
```

4.3. Run `pio run` to verify compilation. Expected: 0 errors.

---

### Task 5: Create MeshCoreMessageStore [x]

**File**: `lib/MeshCore/MeshCore/MeshCoreMessageStore.h`,
`lib/MeshCore/MeshCore/MeshCoreMessageStore.cpp`

**Why**: Message and contact persistence to SD card.
Separated from BLE client for testability and SRP.

**Steps**:

5.1. Create `lib/MeshCore/MeshCore/MeshCoreMessageStore.h`:

```cpp
#pragma once

#include "MeshCoreTypes.h"
#include <cstdint>

// Maximum messages stored per thread on SD card
static constexpr uint16_t MAX_MSGS_PER_THREAD = 200;
// Messages per visible page (RAM)
static constexpr uint8_t MSGS_PER_PAGE = 10;

class MeshCoreMessageStore {
 public:
  // Initialize store (creates directories if needed)
  bool init();

  // Channel messages
  bool appendChannelMessage(uint8_t channelIdx,
                            const MeshCoreMessage& msg);
  uint16_t getChannelMessageCount(uint8_t channelIdx);
  bool loadChannelMessages(uint8_t channelIdx,
                           uint16_t offset,
                           MeshCoreMessage* out,
                           uint8_t count,
                           uint8_t& loaded);

  // Direct messages
  bool appendDirectMessage(const uint8_t* pubkey32,
                           const MeshCoreMessage& msg);
  uint16_t getDirectMessageCount(const uint8_t* pubkey32);
  bool loadDirectMessages(const uint8_t* pubkey32,
                          uint16_t offset,
                          MeshCoreMessage* out,
                          uint8_t count,
                          uint8_t& loaded);

  // Saved contacts
  bool saveContacts(const MeshCoreContact* contacts,
                    uint8_t count);
  uint8_t loadContacts(MeshCoreContact* out,
                       uint8_t maxCount);

  // Companion auto-reconnect address
  bool saveCompanionAddress(const char* bleAddr);
  bool loadCompanionAddress(char* out, size_t maxLen);

  // Unread counts
  bool saveUnreadCounts(const uint16_t* channelUnread,
                        uint8_t channelCount,
                        const MeshCoreContact* contacts,
                        uint8_t contactCount);
  bool loadUnreadCounts(uint16_t* channelUnread,
                        uint8_t channelCount,
                        MeshCoreContact* contacts,
                        uint8_t contactCount);

 private:
  bool ensureDir(const char* path);
  void buildChannelPath(uint8_t idx, char* out,
                        size_t maxLen);
  void buildContactPath(const uint8_t* pubkey32,
                        char* out, size_t maxLen);
};
```

5.2. Implement `MeshCoreMessageStore.cpp`:

- Uses `HalStorage` for all SD card I/O
- Binary format: version byte + count (uint16_t LE) +
  array of fixed-size `MeshCoreMessage` structs
- `appendChannelMessage()`: Opens file, seeks to end,
  writes message, increments count header. Truncates
  oldest messages if count exceeds `MAX_MSGS_PER_THREAD`.
- `loadChannelMessages()`: Opens file, seeks to offset,
  reads `count` messages into `out` array.
- Contact persistence: Similar binary format with
  `MeshCoreContact` structs.
- All paths under `.crosspoint/meshcore/`

5.3. Run `pio run`. Expected: 0 errors.

---

### Task 6: Add i18n strings [x]

**File**: `lib/I18n/translations/english.yaml`

**Why**: All user-facing text must use `tr()` macro. Strings
must be defined before UI activities reference them.

**Steps**:

6.1. Add the following keys to `english.yaml` (in
alphabetical order among existing keys):

```yaml
STR_MESHCORE: "MeshCore"
STR_MESHCORE_ADD_CHANNEL: "Add Channel"
STR_MESHCORE_ADD_CONTACT: "Add to Contacts"
STR_MESHCORE_CHANNEL_LIST: "Channels"
STR_MESHCORE_CHANNEL_NAME: "Channel Name"
STR_MESHCORE_CHANNEL_SECRET: "Channel Secret"
STR_MESHCORE_COMPANION_STATUS: "Companion Status"
STR_MESHCORE_CONFIRM_DELETE: "Delete channel?"
STR_MESHCORE_CONNECTED: "Connected"
STR_MESHCORE_CONNECTING: "Connecting..."
STR_MESHCORE_CONTACTS: "Contacts"
STR_MESHCORE_DELETE_CHANNEL: "Delete Channel"
STR_MESHCORE_DIRECT_MESSAGES: "Messages"
STR_MESHCORE_DISCONNECTED: "Disconnected"
STR_MESHCORE_DISCOVERED: "Discovered Nodes"
STR_MESHCORE_HEAP_LOW: "Not enough memory for BLE"
STR_MESHCORE_INITIALIZING: "Initializing..."
STR_MESHCORE_MSG_ACKED: "Delivered"
STR_MESHCORE_MSG_FAILED: "Failed"
STR_MESHCORE_MSG_SENT: "Sent"
STR_MESHCORE_NO_CHANNELS: "No channels configured"
STR_MESHCORE_NO_CONTACTS: "No saved contacts"
STR_MESHCORE_NO_DEVICES: "No devices found"
STR_MESHCORE_NO_MESSAGES: "No messages"
STR_MESHCORE_REMOVE_CONTACT: "Remove from Contacts"
STR_MESHCORE_RETRY: "Retry"
STR_MESHCORE_SCANNING: "Scanning..."
STR_MESHCORE_SEND: "Send"
STR_MESHCORE_SEND_FAILED: "Send failed"
STR_MESHCORE_STATUS: "Status"
STR_MESHCORE_WIFI_WARNING: "WiFi will be disconnected"
```

6.2. Run `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
to regenerate i18n headers.

6.3. Run `pio run` to verify the generated headers compile.
Expected: 0 errors.

---

### Task 7: Add MeshCore entry to HomeActivity [x]

**Files**: `src/activities/home/HomeActivity.h`,
`src/activities/home/HomeActivity.cpp`,
`src/activities/ActivityManager.h`,
`src/activities/ActivityManager.cpp`

**Why**: Users must be able to navigate to the MeshCore
feature from the home screen.

**Steps**:

7.1. In `ActivityManager.h`, add method declaration:

```cpp
void goToMeshCore();
```

7.2. In `ActivityManager.cpp`, implement `goToMeshCore()`:

```cpp
#include "activities/meshcore/MeshCoreHubActivity.h"

void ActivityManager::goToMeshCore() {
  replaceActivity(std::make_unique<MeshCoreHubActivity>(
      renderer, mappedInput));
}
```

7.3. In `HomeActivity.h`, add a private method declaration:

```cpp
void onMeshCoreOpen();
```

7.4. In `HomeActivity.cpp`, in the `render()` method where
menu items are built, add a "MeshCore" entry after
"File Transfer" (or before Settings):

```cpp
menuItems.push_back(tr(STR_MESHCORE));
menuIcons.push_back(UIIcon::Radio);  // or appropriate icon
```

7.5. In `HomeActivity.cpp`, in the `loop()` confirm handler,
add a case for the MeshCore index:

```cpp
} else if (menuLabel == STR_MESHCORE) {
  onMeshCoreOpen();
}
```

7.6. Implement `onMeshCoreOpen()`:

```cpp
void HomeActivity::onMeshCoreOpen() {
  activityManager.goToMeshCore();
}
```

7.7. Run `pio run`. Expected: 0 errors (requires stub
`MeshCoreHubActivity` — created in Task 8).

---

### Task 8: Create MeshCoreScanActivity [x]

**File**: `src/activities/meshcore/MeshCoreScanActivity.h`,
`src/activities/meshcore/MeshCoreScanActivity.cpp`

**Why**: The BLE scan + connect flow is the prerequisite for
all MeshCore functionality (US1). This is a sub-activity
launched from the hub.

**Steps**:

8.1. Create `src/activities/meshcore/MeshCoreScanActivity.h`:

```cpp
#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreTypes.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class MeshCoreScanActivity final : public Activity {
 public:
  MeshCoreScanActivity(GfxRenderer& renderer,
                       MappedInputManager& mappedInput,
                       MeshCoreClient& client);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;
  bool preventAutoSleep() override { return true; }

 private:
  MeshCoreClient& client;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  // Scan results (max 8 devices displayed)
  static constexpr uint8_t MAX_SCAN_RESULTS = 8;
  struct ScanResult {
    char name[64];
    char address[18];
    int rssi;
  };
  ScanResult scanResults[MAX_SCAN_RESULTS] = {};
  uint8_t scanResultCount = 0;
  bool scanning = false;

  void startScan();
  void connectToSelected();

  static void onScanResult(/* NimBLE scan callback */);
};
```

8.2. Implement `MeshCoreScanActivity.cpp`:

- `onEnter()`: Check heap (`ESP.getFreeHeap() < 30000` →
  show error, `finish()`). Check WiFi (if active, show
  warning, disconnect). Call `client.init()`. Start scan.
- `render()`: Draw header "MeshCore" with scan state.
  If scanning: show "Scanning..." with spinner.
  If results: `GUI.drawList()` with device names + RSSI.
  If no results: show "No devices found" + retry hint.
  Button hints: Back / Confirm / Retry.
- `loop()`: `buttonNavigator.onNext/onPrevious` for list
  navigation. Confirm → `connectToSelected()`.
  Back → `finish()` (cancel). `client.poll()`.
- `connectToSelected()`: Call
  `client.connectTo(scanResults[selectedIndex].address)`.
  On success, set result and finish.
- `onExit()`: If not connected, `client.deinit()`.

8.3. Run `pio run`. Expected: 0 errors.

---

### Task 9: Create MeshCoreHubActivity [x]

**File**: `src/activities/meshcore/MeshCoreHubActivity.h`,
`src/activities/meshcore/MeshCoreHubActivity.cpp`

**Why**: The main MeshCore screen — hub with tabs for
Channels (US2a), Contacts (US2b/US4b), Discovered (US4a),
and Status (US5). This is the primary activity replaced
from Home.

**Steps**:

9.1. Create `src/activities/meshcore/MeshCoreHubActivity.h`:

```cpp
#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class MeshCoreHubActivity final : public Activity {
 public:
  MeshCoreHubActivity(GfxRenderer& renderer,
                      MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;
  bool preventAutoSleep() override { return true; }

 private:
  enum class Tab : uint8_t {
    CHANNELS = 0,
    CONTACTS,
    DISCOVERED,
    STATUS,
    TAB_COUNT
  };

  MeshCoreClient client;
  MeshCoreMessageStore store;
  ButtonNavigator buttonNavigator;

  Tab currentTab = Tab::CHANNELS;
  int selectedIndex = 0;
  bool connected = false;

  // Data loaded from client/store
  MeshCoreChannel channels[8] = {};
  uint8_t channelCount = 0;

  static constexpr uint8_t MAX_VISIBLE_CONTACTS = 20;
  MeshCoreContact savedContacts[MAX_VISIBLE_CONTACTS] = {};
  uint8_t savedContactCount = 0;

  MeshCoreContact discoveredNodes[MAX_VISIBLE_CONTACTS] = {};
  uint8_t discoveredNodeCount = 0;

  // Callbacks (static → instance via context pointer)
  static void onStateChanged(BleConnectionState state,
                             void* ctx);
  static void onMessageReceived(const MeshCoreMessage& msg,
                                void* ctx);
  static void onContactReceived(const MeshCoreContact& c,
                                bool isEnd, void* ctx);
  static void onAdvertReceived(const MeshCoreContact& node,
                               void* ctx);

  void handleStateChange(BleConnectionState state);
  void handleMessage(const MeshCoreMessage& msg);
  void handleContact(const MeshCoreContact& c, bool isEnd);
  void handleAdvert(const MeshCoreContact& node);

  void renderChannelList(const Rect& contentRect);
  void renderContactList(const Rect& contentRect);
  void renderDiscoveredList(const Rect& contentRect);
  void renderStatus(const Rect& contentRect);
  void renderConnectionBar(const Rect& barRect);

  void openChannelThread(uint8_t channelIdx);
  void openContactThread(const MeshCoreContact& contact);
  void openDiscover();
  void launchScanActivity();
  void switchTab(Tab tab);
};
```

9.2. Implement `MeshCoreHubActivity.cpp`:

- `onEnter()`: Init `store`. Set callbacks on `client`.
  Load saved contacts from store. Load companion address.
  Launch `MeshCoreScanActivity` as sub-activity:

```cpp
void MeshCoreHubActivity::onEnter() {
  Activity::onEnter();
  store.init();
  savedContactCount = store.loadContacts(
      savedContacts, MAX_VISIBLE_CONTACTS);

  client.setStateCallback(onStateChanged, this);
  client.setMessageCallback(onMessageReceived, this);
  client.setContactCallback(onContactReceived, this);
  client.setAdvertCallback(onAdvertReceived, this);

  char addr[18] = {};
  if (store.loadCompanionAddress(addr, sizeof(addr))) {
    client.setAutoReconnectAddress(addr);
  }

  launchScanActivity();
}
```

- `loop()`: `client.poll()`. Handle tab switching with
  Left/Right buttons. Handle list navigation with
  Up/Down. Handle Confirm to open threads. Handle Back
  to go home.
- `render()`: Draw connection bar at top (state +
  companion name). Draw tab bar (Channels/Contacts/
  Discovered/Status). Draw current tab content via
  delegate methods.
- `onExit()`: `client.disconnect()`, `client.deinit()`,
  save unread counts, save contacts.

9.3. Run `pio run`. Expected: 0 errors.

---

### Task 10: Create MeshCoreThreadActivity [x]

**File**: `src/activities/meshcore/MeshCoreThreadActivity.h`,
`src/activities/meshcore/MeshCoreThreadActivity.cpp`

**Why**: Displays message thread for a channel (US2a) or
contact DM (US2b). Also handles sending (US3).

**Steps**:

10.1. Create `MeshCoreThreadActivity.h`:

```cpp
#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class MeshCoreThreadActivity final : public Activity {
 public:
  // Channel thread constructor
  MeshCoreThreadActivity(GfxRenderer& renderer,
                         MappedInputManager& mappedInput,
                         MeshCoreClient& client,
                         MeshCoreMessageStore& store,
                         uint8_t channelIdx,
                         const char* threadTitle);

  // DM thread constructor
  MeshCoreThreadActivity(GfxRenderer& renderer,
                         MappedInputManager& mappedInput,
                         MeshCoreClient& client,
                         MeshCoreMessageStore& store,
                         const MeshCoreContact& contact);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;
  bool preventAutoSleep() override { return true; }

 private:
  MeshCoreClient& client;
  MeshCoreMessageStore& store;
  ButtonNavigator buttonNavigator;

  // Thread identity
  bool isChannelThread;
  uint8_t channelIdx = 0;
  uint8_t contactPubkey[32] = {};
  char title[64] = {};

  // Message page
  MeshCoreMessage messages[MSGS_PER_PAGE] = {};
  uint8_t messageCount = 0;
  uint16_t totalMessages = 0;
  uint16_t pageOffset = 0;

  void loadPage();
  void sendMessage();
  void onNewMessage(const MeshCoreMessage& msg);

  static void onMessageCb(const MeshCoreMessage& msg,
                           void* ctx);

  void renderMessages(const Rect& contentRect);
  void renderSingleMessage(const MeshCoreMessage& msg,
                           int x, int y, int width);
};
```

10.2. Implement `MeshCoreThreadActivity.cpp`:

- `onEnter()`: Load total message count from store.
  Set page offset to show most recent messages.
  `loadPage()`. Register message callback.
- `loadPage()`: Call `store.loadChannelMessages()` or
  `store.loadDirectMessages()` with current offset.
- `render()`: Draw header with thread title + connection
  indicator. Draw messages (newest at bottom). Each
  message: sender name (bold), timestamp, text.
  Sent messages right-aligned, received left-aligned.
  Button hints: Back / Send / PageUp / PageDown.
- `loop()`: PageForward/PageBack for scrolling.
  Confirm → `sendMessage()`. Back → `finish()`.
  `client.poll()`.
- `sendMessage()`: Launch `KeyboardEntryActivity` via
  `startActivityForResult()` with
  `MESHCORE_SEND_CHAR_LIMIT`. On result, call
  `client.sendChannelMessage()` or
  `client.sendDirectMessage()`. Append sent message to
  store. Reload page.

```cpp
void MeshCoreThreadActivity::sendMessage() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(
          renderer, mappedInput,
          tr(STR_MESHCORE_SEND),
          "",
          MESHCORE_SEND_CHAR_LIMIT,
          InputType::Text),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }
        auto& text =
            std::get<KeyboardResult>(result.data).text;
        bool ok;
        if (isChannelThread) {
          ok = client.sendChannelMessage(
              channelIdx, text.c_str());
        } else {
          ok = client.sendDirectMessage(
              contactPubkey, text.c_str());
        }
        if (ok) {
          MeshCoreMessage sent = {};
          sent.direction = MsgDirection::SENT;
          sent.type = isChannelThread ? MsgType::CHANNEL
                                      : MsgType::DIRECT;
          sent.timestamp =
              static_cast<uint32_t>(millis() / 1000);
          sent.deliveryStatus = DeliveryStatus::SENT;
          snprintf(sent.text, sizeof(sent.text), "%s",
                   text.c_str());
          if (isChannelThread) {
            store.appendChannelMessage(channelIdx, sent);
          } else {
            store.appendDirectMessage(contactPubkey, sent);
          }
        }
        totalMessages = isChannelThread
            ? store.getChannelMessageCount(channelIdx)
            : store.getDirectMessageCount(contactPubkey);
        pageOffset = totalMessages > MSGS_PER_PAGE
            ? totalMessages - MSGS_PER_PAGE : 0;
        loadPage();
        requestUpdate();
      });
}
```

10.3. Run `pio run`. Expected: 0 errors.

---

### Task 11: Create MeshCoreDiscoverActivity [x]

**File**:
`src/activities/meshcore/MeshCoreDiscoverActivity.h`,
`src/activities/meshcore/MeshCoreDiscoverActivity.cpp`

**Why**: Displays discovered nodes list (US4a) and allows
adding nodes to saved contacts (US4a acceptance #3).

**Steps**:

11.1. Create `MeshCoreDiscoverActivity.h`:

```cpp
#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class MeshCoreDiscoverActivity final : public Activity {
 public:
  MeshCoreDiscoverActivity(
      GfxRenderer& renderer,
      MappedInputManager& mappedInput,
      MeshCoreClient& client,
      MeshCoreMessageStore& store,
      MeshCoreContact* discoveredNodes,
      uint8_t& discoveredCount,
      MeshCoreContact* savedContacts,
      uint8_t& savedCount);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;

 private:
  MeshCoreClient& client;
  MeshCoreMessageStore& store;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  MeshCoreContact* nodes;
  uint8_t& nodeCount;
  MeshCoreContact* contacts;
  uint8_t& contactCount;

  void addToContacts();
  bool isAlreadySaved(const uint8_t* pubkey) const;
};
```

11.2. Implement the activity:

- `render()`: List of discovered nodes with name, type
  icon, pubkey prefix, last seen, path length.
  Selected node shows "Add to Contacts" hint.
- `loop()`: Navigate list. Confirm → `addToContacts()`.
  Back → `finish()`.
- `addToContacts()`: Copy node to `contacts` array, set
  `isSaved = true`, increment `contactCount`, persist
  via `store.saveContacts()`.

11.3. Run `pio run`. Expected: 0 errors.

---

### Task 12: Create MeshCoreStatusActivity [x]

**File**:
`src/activities/meshcore/MeshCoreStatusActivity.h`,
`src/activities/meshcore/MeshCoreStatusActivity.cpp`

**Why**: Displays companion device status (US5).

**Steps**:

12.1. Create `MeshCoreStatusActivity.h`:

```cpp
#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreTypes.h>

#include "activities/Activity.h"

class MeshCoreStatusActivity final : public Activity {
 public:
  MeshCoreStatusActivity(GfxRenderer& renderer,
                         MappedInputManager& mappedInput,
                         MeshCoreClient& client);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;

 private:
  MeshCoreClient& client;
  bool refreshRequested = false;

  void renderStatusFields(const Rect& contentRect);
};
```

12.2. Implement:

- `onEnter()`: Request battery update from client.
- `render()`: Display all `MeshCoreCompanion` fields:
  - Name, Model, Firmware
  - Battery voltage (formatted as "X.XX V")
  - Storage (used/total KB, formatted as "X.X / Y.Y MB")
  - Radio: Freq MHz, BW kHz, SF, CR
  - BLE address
  Button hints: Back / Refresh.
- `loop()`: Back → `finish()`. Confirm →
  `client.requestBattery()` + refresh flag.
  `client.poll()`.

12.3. Run `pio run`. Expected: 0 errors.

---

### Task 13: Channel management in MeshCoreHubActivity [x]

**Why**: US6 — Add/Delete channels from the channel list.

**Steps**:

13.1. In `MeshCoreHubActivity`, add a "manage" action on
the channel list (e.g., Right button opens a context
menu or long-press):

- "Add Channel" (if empty slots exist): Launch
  `KeyboardEntryActivity` for name, then for secret
  (16-byte hex). Call `client.setChannel()`.
- "Delete Channel" (on selected channel): Confirm dialog,
  then `client.deleteChannel(idx)`.

13.2. Implement `addChannel()`:

```cpp
void MeshCoreHubActivity::addChannel() {
  // Find first empty slot
  int emptyIdx = -1;
  for (int i = 1; i < 8; ++i) {
    if (channels[i].isEmpty()) {
      emptyIdx = i;
      break;
    }
  }
  if (emptyIdx < 0) {
    // No empty slots — show message
    return;
  }

  // Step 1: Get channel name
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(
          renderer, mappedInput,
          tr(STR_MESHCORE_CHANNEL_NAME),
          "", 32, InputType::Text),
      [this, emptyIdx](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }
        auto name =
            std::get<KeyboardResult>(result.data).text;

        // Step 2: Get channel secret
        startActivityForResult(
            std::make_unique<KeyboardEntryActivity>(
                renderer, mappedInput,
                tr(STR_MESHCORE_CHANNEL_SECRET),
                "", 32, InputType::Text),
            [this, emptyIdx, name = std::move(name)](
                const ActivityResult& r2) {
              if (r2.isCancelled) {
                requestUpdate();
                return;
              }
              auto secretHex =
                  std::get<KeyboardResult>(r2.data).text;
              uint8_t secret[16] = {};
              // Parse hex string to bytes
              for (int i = 0;
                   i < 16 && i * 2 + 1 < secretHex.size();
                   ++i) {
                char byte[3] = {secretHex[i * 2],
                                secretHex[i * 2 + 1], 0};
                secret[i] = strtoul(byte, nullptr, 16);
              }
              client.setChannel(emptyIdx, name.c_str(),
                                secret);
              // Refresh channel list
              client.requestChannel(emptyIdx);
              requestUpdate();
            });
      });
}
```

13.3. Implement `deleteChannel()`:

```cpp
void MeshCoreHubActivity::deleteChannel(uint8_t idx) {
  client.deleteChannel(idx);
  channels[idx] = {};
  requestUpdate();
}
```

13.4. Run `pio run`. Expected: 0 errors.

---

### Task 14: Add MeshCore settings [x]

**Files**: `src/CrossPointSettings.h`,
`src/CrossPointSettings.cpp`, `src/SettingsList.h`,
`lib/I18n/translations/english.yaml`

**Why**: Auto-reconnect preference. Minimal settings for v1.

**Steps**:

14.1. In `CrossPointSettings.h`, add member:

```cpp
uint8_t meshCoreAutoReconnect = 1;
```

14.2. In `SettingsList.h`, add to the settings list array:

```cpp
SettingInfo::Toggle(
    StrId::STR_MESHCORE_AUTO_RECONNECT,
    &CrossPointSettings::meshCoreAutoReconnect,
    "meshCoreAutoReconnect",
    StrId::STR_MESHCORE)
```

14.3. Add i18n key to `english.yaml`:

```yaml
STR_MESHCORE_AUTO_RECONNECT: "Auto-reconnect"
```

14.4. Regenerate i18n:
`python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/`

14.5. In `MeshCoreHubActivity::onEnter()`, check the
setting:

```cpp
if (SETTINGS.meshCoreAutoReconnect &&
    autoReconnectAddr[0] != '\0') {
  // Try auto-reconnect instead of launching scan
  client.connectTo(autoReconnectAddr);
} else {
  launchScanActivity();
}
```

14.6. Run `pio run`. Expected: 0 errors.

---

### Task 15: WiFi conflict handling [x]

**Why**: FR-020 — Must disconnect WiFi before BLE and warn
user.

**Steps**:

15.1. In `MeshCoreScanActivity::onEnter()`, before
`client.init()`:

```cpp
#include <WiFi.h>

void MeshCoreScanActivity::onEnter() {
  Activity::onEnter();

  // Check heap
  if (ESP.getFreeHeap() < 30000) {
    LOG_ERR("MESH", "Heap too low: %d",
            ESP.getFreeHeap());
    // Show error screen and return
    // (render will show STR_MESHCORE_HEAP_LOW)
    finish();
    return;
  }

  // Disconnect WiFi if active (BLE/WiFi share radio)
  if (WiFi.getMode() != WIFI_OFF) {
    LOG_INF("MESH", "Disconnecting WiFi for BLE");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    // Brief delay for radio mode switch
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (!client.init()) {
    LOG_ERR("MESH", "BLE init failed");
    finish();
    return;
  }

  startScan();
}
```

15.2. In `MeshCoreHubActivity::onExit()`:

```cpp
void MeshCoreHubActivity::onExit() {
  store.saveContacts(savedContacts, savedContactCount);
  store.saveCompanionAddress(
      client.getAutoReconnectAddress());
  client.disconnect();
  client.deinit();
  Activity::onExit();
}
```

15.3. Run `pio run`. Expected: 0 errors.

---

### Task 16: Message routing and push notifications [x]

**Why**: FR-006 — Handle PACKET_MESSAGES_WAITING push
notification and route incoming messages to correct
thread.

**Steps**:

16.1. In `MeshCoreClient::processResponse()`, add handling
for push notifications:

```cpp
void MeshCoreClient::processResponse(
    const uint8_t* data, size_t len) {
  if (len == 0) return;
  uint8_t pktType = data[0];

  switch (pktType) {
    case MeshProto::PKT_MSGS_WAITING:
      // Poll for queued messages
      enqueueCmd(/* CMD_GET_MESSAGE */, 1,
                 0);  // 0 = accept any response type
      break;

    case MeshProto::PKT_CHANNEL_MSG:
    case MeshProto::PKT_CHANNEL_MSG_V3: {
      MeshCoreMessage msg = {};
      if (MeshProto::parseChannelMessage(data, len, msg)) {
        msg.direction = MsgDirection::RECEIVED;
        msg.type = MsgType::CHANNEL;
        if (msgCb) msgCb(msg, msgCbCtx);
      }
      break;
    }

    case MeshProto::PKT_CONTACT_MSG:
    case MeshProto::PKT_CONTACT_MSG_V3: {
      MeshCoreMessage msg = {};
      if (MeshProto::parseContactMessage(data, len, msg)) {
        msg.direction = MsgDirection::RECEIVED;
        msg.type = MsgType::DIRECT;
        if (msgCb) msgCb(msg, msgCbCtx);
      }
      break;
    }

    case MeshProto::PKT_ADVERTISEMENT: {
      MeshCoreContact node = {};
      if (MeshProto::parseContact(data, len, node)) {
        if (advertCb) advertCb(node, advertCbCtx);
      }
      break;
    }

    case MeshProto::PKT_ACK: {
      uint8_t ackCode[6];
      if (MeshProto::parseAck(data, len, ackCode)) {
        // Update pending DM delivery status
        // (match by expected ACK tag from MSG_SENT)
      }
      break;
    }

    case MeshProto::PKT_NO_MORE_MSGS:
      // Done polling
      break;

    // ... handle other response types ...
  }
}
```

16.2. In `MeshCoreHubActivity::handleMessage()`:

```cpp
void MeshCoreHubActivity::handleMessage(
    const MeshCoreMessage& msg) {
  if (msg.type == MsgType::CHANNEL) {
    store.appendChannelMessage(msg.channelIdx, msg);
    if (msg.channelIdx < 8) {
      channels[msg.channelIdx].unreadCount++;
    }
  } else {
    // Check if sender is in saved contacts
    bool found = false;
    for (uint8_t i = 0; i < savedContactCount; ++i) {
      if (memcmp(savedContacts[i].publicKey,
                 msg.pubkeyPrefix, 6) == 0) {
        store.appendDirectMessage(
            savedContacts[i].publicKey, msg);
        savedContacts[i].unreadCount++;
        found = true;
        break;
      }
    }
    if (!found) {
      // FR-011: Discard DMs from non-contacts
      LOG_DBG("MESH", "Discarding DM from non-contact");
      return;
    }
  }
  requestUpdate();
}
```

16.3. Run `pio run`. Expected: 0 errors.

---

### Task 17: DM discard for non-contacts (FR-011) [x]

**Why**: Direct messages from nodes not in saved contacts
must be silently discarded.

**Steps**:

17.1. Already handled in Task 16, step 16.2 — the
`handleMessage()` method checks if sender is in saved
contacts before storing. Non-contacts are logged at
DBG level and discarded.

17.2. Verify the logic: `memcmp` of 6-byte pubkey prefix
matches the first 6 bytes of each saved contact's
32-byte public key. This matches the protocol format
where incoming messages carry only a 6-byte prefix.

17.3. Run `pio run`. Expected: 0 errors.

---

### Task 18: Orientation-aware rendering [x]

**Why**: FR-023 — All MeshCore screens must use dynamic
screen dimensions and bezel margins.

**Steps**:

18.1. In all MeshCore activity `render()` methods, use:

```cpp
const auto screenW = renderer.getScreenWidth();
const auto screenH = renderer.getScreenHeight();
const auto trbl = renderer.getOrientedViewableTRBL();
const Rect contentRect = {
    trbl.left, trbl.top,
    screenW - trbl.left - trbl.right,
    screenH - trbl.top - trbl.bottom
};
```

18.2. Never hardcode `800` or `480`. Use `screenW` and
`screenH` for all layout calculations.

18.3. Use `UITheme::getNumberOfItemsPerPage()` for list
pagination to adapt to different screen sizes.

18.4. Audit all render methods in Tasks 8-12 to verify
compliance. Fix any hardcoded dimensions.

18.5. Run `pio run`. Expected: 0 errors.

---

### Task 19: Full build verification [x]

**Why**: Ensure all tasks integrate correctly.

**Steps**:

19.1. Clean build:

```bash
pio run -t clean && pio run
```

Expected: 0 errors, 0 warnings.

19.2. Run code formatter:

```bash
./bin/clang-format-fix
```

19.3. Run static analysis:

```bash
pio check --fail-on-defect low \
          --fail-on-defect medium \
          --fail-on-defect high
```

Expected: 0 defects.

19.4. Verify firmware size:

```bash
pio run -e gh_release
```

Check that firmware fits within the partition layout
(~1.5 MB typical, 4 MB max per partitions.csv).
NimBLE adds ~100-150 KB to flash — verify this is
acceptable.

19.5. Verify i18n generation:

```bash
python3 scripts/gen_i18n.py lib/I18n/translations \
    lib/I18n/
```

Expected: 0 errors, all `STR_MESHCORE_*` keys present
in generated `I18nKeys.h`.

---

### Task 20: Manual hardware testing checklist

**Why**: On-device verification cannot be automated. This
task defines the manual test procedure.

**Steps**:

20.1. Flash firmware: `pio run -t upload`

20.2. Test scan flow (US1):
- Navigate Home → MeshCore
- Verify BLE scan starts
- Place companion node nearby
- Verify it appears in scan list with name + RSSI
- Select and connect
- Verify "Connected" status on hub screen

20.3. Test channel messages (US2a):
- Open Channels tab
- Verify channel list loads
- Send a message from another MeshCore device to public
  channel
- Verify message appears in channel thread

20.4. Test DM (US2b):
- Add a node from Discovered list to Contacts
- Open contact's DM thread
- Send a direct message
- Verify delivery status updates

20.5. Test send (US3):
- Open channel thread
- Press Send → keyboard opens
- Type message, confirm
- Verify message appears on other MeshCore client

20.6. Test discovered nodes (US4a):
- Open Discovered tab
- Verify nodes appear with correct info
- Add a node to contacts
- Verify it appears in Contacts tab

20.7. Test companion status (US5):
- Open Status tab
- Verify battery, radio settings, firmware info displayed
- Press Refresh, verify values update

20.8. Test channel management (US6):
- Open channel list management menu
- Add a private channel with name + secret
- Verify channel appears in list
- Delete the channel
- Verify it's removed

20.9. Test memory (SC-003):
- Monitor serial output during all tests
- Verify `ESP.getFreeHeap()` stays above 30 KB
- Enter/exit MeshCore activity multiple times
- Verify heap returns to baseline (±1 KB)

20.10. Test orientations (SC-006):
- Rotate device through all 4 orientations
- Verify all screens render correctly in each

## Assumptions

1. **NimBLE 2.5.0 client-only RAM**: Estimated ~30-40 KB.
   Actual measurement needed on device (Task 20.9). If
   exceeds budget, reduce with NimBLE config flags or
   by limiting scan results.

2. **PACKET_CONTACT format**: The companion protocol docs
   show PACKET_CONTACT_START/CONTACT/CONTACT_END for the
   GET_CONTACTS response, but do not provide the exact
   byte layout of the CONTACT packet body. Implementation
   will need to reference the meshcore.js or meshcore_py
   libraries for the exact field offsets. The struct in
   E2 is a best estimate.

3. **Direct message send command**: The spec mentions
   FR-008 (send DM) but the companion protocol docs
   only show `CMD_SEND_CHANNEL_MESSAGE`. The DM send
   command format must be verified from the official JS
   or Python libraries. It likely uses a different
   command byte with the contact's public key.

4. **UIIcon for MeshCore**: The `UIIcon::Radio` enum may
   not exist. Need to check available icons in
   `src/components/` and either reuse an existing icon
   or add a new one.

5. **SCOPE.md exception**: MeshCore is off-grid messaging
   (no internet). The spec notes this needs maintainer
   approval since SCOPE.md lists "Active Connectivity"
   as out-of-scope. Implementation proceeds assuming
   approval is granted, with the feature behind a menu
   entry (not background-active).
