#pragma once
// Simulator stub for NimBLE-Arduino — provides the minimum type surface
// needed by MeshCoreClient to compile. All BLE operations are no-ops;
// the simulator has no real BLE hardware.
#ifdef SIMULATOR

#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// Forward-declared free function (defined in MockSession.cpp) to avoid
// circular include between NimBLEDevice.h ↔ MockSession.h.
class NimBLERemoteCharacteristic;
void mockHandleAddUpdateContact(NimBLERemoteCharacteristic* txChar, const uint8_t* data, size_t len);

// Forward declarations with enough surface for MeshCoreClient
class NimBLEUUID {
 public:
  NimBLEUUID() = default;
  NimBLEUUID(const char*) {}
};

class NimBLEAddress {
 public:
  NimBLEAddress() = default;
  NimBLEAddress(const std::string& addr, uint8_t type = 0) : address(addr), addrType(type) {}
  std::string toString() const { return address; }
  uint8_t getType() const { return addrType; }

 private:
  std::string address;
  uint8_t addrType = 0;
};

class NimBLEConnInfo {
 public:
  bool isEncrypted() const { return false; }
  bool isBonded() const { return false; }
  bool isAuthenticated() const { return false; }
  uint8_t getSecKeySize() const { return 0; }
  uint16_t getConnTimeout() const { return 400; }  // default supervision timeout (×10 ms)
};

class NimBLEClient;  // forward decl for NimBLEClientCallbacks

class NimBLEClientCallbacks {
 public:
  virtual ~NimBLEClientCallbacks() = default;
  virtual void onPassKeyEntry(NimBLEConnInfo&) {}
  virtual void onAuthenticationComplete(NimBLEConnInfo&) {}
  virtual void onDisconnect(NimBLEClient*, int) {}
};

// --- Mock data for MeshCore BLE simulation ---
// Defined early so NimBLE scan classes can use them.
//
// Capacity limits (conservative for heap; 380 KB total RAM target)
static constexpr uint8_t MOCK_MAX_COMPANIONS = 4;
static constexpr uint8_t MOCK_MAX_CONTACTS = 20;
static constexpr uint8_t MOCK_MAX_CHANNELS = 8;
static constexpr uint8_t MOCK_MAX_MESSAGES = 50;
static constexpr uint8_t MOCK_MAX_DISCOVERED_NODES = 8;
static constexpr uint16_t MOCK_MAX_TEXT_LEN = 184;  // matches MAX_MSG_TEXT_LEN

struct MockMessage {
  char text[MOCK_MAX_TEXT_LEN] = {};
  uint8_t direction = 0;  // 0=received, 1=sent
  uint32_t timestamp = 0;
};

struct MockContact {
  char name[64] = {};
  char publicKey[65] = {};  // hex, 64 chars + null
  uint8_t type = 0;         // 0=COMPANION, 1=REPEATER, 2=ROOM_SERVER, 3=SENSOR
  uint8_t flags = 0;        // wire format flags (bit 0 = favourite)
  uint32_t lastSeen = 0;
  uint8_t pathLength = 0;
  int8_t snr = 0;
};

struct MockDiscoveredNode {
  char publicKey[65] = {};  // hex, 64 chars + null
  char name[64] = {};
  uint8_t type = 0;         // 0=COMPANION, 1=REPEATER, 2=ROOM_SERVER, 3=SENSOR
  uint8_t flags = 0;        // wire format flags (bit 0 = favourite)
  uint32_t lastSeen = 0;
  uint8_t pathLength = 0;
  int8_t snr = 0;
};

struct MockChannel {
  char name[33] = {};
  uint8_t type = 0;  // 0=PUBLIC, 1=HASHTAG, 2=PRIVATE_CH
  uint16_t unreadCount = 0;
  MockMessage messages[MOCK_MAX_MESSAGES] = {};
  uint8_t messageCount = 0;
};

struct MockCompanion {
  char name[64] = {};
  char bleAddress[18] = {};
  uint8_t addressType = 0;  // 0=public, 1=random
  int rssi = 0;
  uint32_t blePin = 0;      // 0 = no PIN
  char publicKey[65] = {};  // hex, 64 chars + null
  char firmwareBuild[13] = {};
  char model[41] = {};
  char version[21] = {};
  uint16_t batteryMv = 0;
  uint32_t storageUsedKb = 0;
  uint32_t storageTotalKb = 0;
  float radioFreq = 0;
  float radioBw = 0;
  uint8_t radioSf = 0;
  uint8_t radioCr = 0;
  uint8_t maxContacts = 0;
  uint8_t maxChannels = 8;

  MockContact contacts[MOCK_MAX_CONTACTS] = {};
  uint8_t contactCount = 0;

  MockChannel channels[MOCK_MAX_CHANNELS] = {};
  uint8_t channelCount = 0;

  MockDiscoveredNode discoveredNodes[MOCK_MAX_DISCOVERED_NODES] = {};
  uint8_t discoveredNodeCount = 0;

  // Returns false if name or bleAddress is empty
  bool isValid() const { return name[0] != '\0' && bleAddress[0] != '\0'; }
};

class MockSession;  // forward decl — full definition in MockSession.h

class NimBLEAdvertisedDevice {
 public:
  NimBLEAdvertisedDevice() = default;
  // Construct from mock companion — stores pointer (MockSession owns the data)
  explicit NimBLEAdvertisedDevice(const MockCompanion* companion) : mockCompanion(companion) {
    if (companion) {
      mockAddress = NimBLEAddress(std::string(companion->bleAddress), companion->addressType);
    }
  }

  bool isAdvertisingService(const NimBLEUUID&) const {
    // When mock is active, all companions advertise NUS
    return mockCompanion != nullptr;
  }

  std::string getName() const {
    if (mockCompanion) return std::string(mockCompanion->name);
    return {};
  }

  NimBLEAddress getAddress() const {
    if (mockCompanion) return mockAddress;
    return {};
  }

  int getRSSI() const {
    if (mockCompanion) return mockCompanion->rssi + rssiOffset;
    return 0;
  }

  // Set RSSI variation (for rescan)
  void setRssiOffset(int offset) { rssiOffset = offset; }

 private:
  const MockCompanion* mockCompanion = nullptr;
  NimBLEAddress mockAddress{};
  int rssiOffset = 0;
};

class NimBLEScanResults {
 public:
  NimBLEScanResults() = default;

  // Populate from mock companions array. `count` must be ≤ MOCK_MAX_COMPANIONS.
  // `rescanCounter`: non-zero triggers RSSI variation (±5 dBm jitter).
  void populateFromMock(const MockCompanion* companions, uint8_t count, uint8_t rescanCounter) {
    deviceCount = count;
    for (uint8_t i = 0; i < count; ++i) {
      devices[i] = NimBLEAdvertisedDevice(&companions[i]);
      if (rescanCounter > 0) {
        // Pseudo-random RSSI variation: ±5 dBm based on index + counter
        int jitter = ((i * 7 + rescanCounter * 13) % 11) - 5;  // -5..+5
        devices[i].setRssiOffset(jitter);
      }
    }
  }

  int getCount() const { return deviceCount; }

  const NimBLEAdvertisedDevice* getDevice(int i) const {
    if (i >= 0 && i < deviceCount) return &devices[i];
    return nullptr;
  }

 private:
  NimBLEAdvertisedDevice devices[MOCK_MAX_COMPANIONS] = {};
  int deviceCount = 0;
};

class NimBLEScan {
 public:
  void setActiveScan(bool) {}
  void setInterval(uint16_t) {}
  void setWindow(uint16_t) {}
  bool isScanning() const { return scanning; }
  void stop() { scanning = false; }

  NimBLEScanResults getResults(uint32_t durationMs, bool isContinue);

 private:
  bool scanning = false;
  uint8_t rescanCounter = 0;  // increments on each getResults call
};

// Forward declarations for classes defined below
class NimBLERemoteService;
class NimBLEClient;

// --- Pending echo auto-response state (issue 5-AFK) ---
// These must be before NimBLERemoteCharacteristic because writeValue() uses them.
enum class PendingEchoType : uint8_t { NONE = 0, CHANNEL = 1, DM = 2, ADD_CONTACT_OK = 3 };

struct PendingEcho {
  PendingEchoType type = PendingEchoType::NONE;
  uint32_t requestTimeMs = 0;
  uint8_t channelIdx = 0;
  uint8_t pubkeyPrefix[6] = {};
  char text[MOCK_MAX_TEXT_LEN] = {};
};

inline PendingEcho sPendingEcho;

class NimBLERemoteCharacteristic {
 public:
  using notify_callback = void (*)(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool);

  NimBLERemoteCharacteristic() = default;

  void setMockCompanion(const MockCompanion* c) { mockCompanion = c; }

  // When this characteristic doesn't have its own notify callback,
  // fall back to the linked source (e.g. rxChar uses txChar's callback).
  void setNotifySource(NimBLERemoteCharacteristic* src) { notifySource = src; }

  notify_callback effectiveNotifyCb() const {
    if (notifyCb) return notifyCb;
    if (notifySource) return notifySource->notifyCb;
    return nullptr;
  }

  bool subscribe(bool, notify_callback cb, bool) {
    notifyCb = cb;
    return true;
  }

  bool writeValue(const uint8_t* data, size_t len, bool) {
    if (len == 0 || !mockCompanion) return false;
    uint8_t cmd = data[0];
    if (cmd == 0x01) {  // CMD_APP_START → PKT_SELF_INFO + discovered node adverts
      injectSelfInfo();
      injectAdvertList();
      return true;
    }
    if (cmd == 0x16) {  // CMD_DEVICE_QUERY → PKT_DEVICE_INFO
      injectDeviceInfo();
      return true;
    }
    if (cmd == 0x04) {  // CMD_GET_CONTACTS → PKT_CONTACT_*
      injectContactList();
      return true;
    }
    if (cmd == 0x1F) {  // CMD_GET_CHANNEL → PKT_CHANNEL_INFO
      uint8_t idx = (len >= 2) ? data[1] : 0;
      injectChannelInfo(idx);
      return true;
    }
    if (cmd == 0x0A) {  // CMD_GET_MESSAGE → PKT_CHANNEL_MSG_V3 × N + PKT_NO_MORE_MSGS
      injectChannelMessages();
      return true;
    }
    if (cmd == 0x14) {  // CMD_GET_BATTERY → PKT_BATTERY
      injectBattery();
      return true;
    }
    if (cmd == 0x03) {  // CMD_SEND_CHAN_MSG → PKT_MSG_SENT + schedule echo
      injectMsgSent();

      // Parse the command to extract channel index and text for echo
      // Format: [0x03][isFlood][chIdx][ts LE 4][text...]
      if (len >= 8) {
        uint8_t chIdx = data[1 + 1];  // skip cmd + isFlood
        const char* textStart = reinterpret_cast<const char*>(data + 7);
        size_t textLen = len - 7;
        sPendingEcho.type = PendingEchoType::CHANNEL;
        sPendingEcho.channelIdx = chIdx;
        if (textLen > MOCK_MAX_TEXT_LEN - 1) textLen = MOCK_MAX_TEXT_LEN - 1;
        memcpy(sPendingEcho.text, textStart, textLen);
        sPendingEcho.text[textLen] = '\0';
        sPendingEcho.requestTimeMs = millis();
        LOG_DBG("MOCK", "scheduled channel echo for ch=%d: %.40s", chIdx, sPendingEcho.text);
      }
      return true;
    }
    if (cmd == 0x02) {  // CMD_SEND_DM → PKT_MSG_SENT + schedule DM echo
      injectMsgSent();

      // Parse the command to extract pubkey prefix and text for echo
      // Format: [0x02][isFlood][0x00][ts LE 4][pubkey 6][text...]
      if (len >= 14) {
        memcpy(sPendingEcho.pubkeyPrefix, data + 7, 6);
        const char* textStart = reinterpret_cast<const char*>(data + 13);
        size_t textLen = len - 13;
        sPendingEcho.type = PendingEchoType::DM;
        if (textLen > MOCK_MAX_TEXT_LEN - 1) textLen = MOCK_MAX_TEXT_LEN - 1;
        memcpy(sPendingEcho.text, textStart, textLen);
        sPendingEcho.text[textLen] = '\0';
        sPendingEcho.requestTimeMs = millis();
        LOG_DBG("MOCK", "scheduled DM echo: %.40s", sPendingEcho.text);
      }
      return true;
    }
    if (cmd == 0x09) {  // CMD_ADD_UPDATE_CONTACT → mode-driven response
      mockHandleAddUpdateContact(this, data, len);
      return true;
    }
    // All other commands: ACK write, no response
    return true;
  }

  // For disconnect injection: test callback plumbing
  bool hasNotifyCallback() const { return effectiveNotifyCb() != nullptr; }

  // Inject a raw packet via notify callback. Returns true if callback exists.
  bool injectRawPacket(const uint8_t* data, size_t len) {
    auto cb = effectiveNotifyCb();
    if (!cb) return false;
    cb(this, const_cast<uint8_t*>(data), len, true);
    return true;
  }

  void resetMessagesSent() { messagesSent = false; }

 private:
  notify_callback notifyCb = nullptr;
  NimBLERemoteCharacteristic* notifySource = nullptr;
  const MockCompanion* mockCompanion = nullptr;
  bool messagesSent = false;  // track whether CMD_GET_MESSAGE already responded

  // Build and inject PKT_OK (0x00, 1 byte) via notify callback.
  void injectPktOk() {
    auto cb = effectiveNotifyCb();
    if (!cb) return;
    uint8_t ok = 0x00;
    cb(this, &ok, 1, true);
  }

  // Build and inject PKT_ERROR (0x01, 1 byte) via notify callback.
  void injectPktError() {
    auto cb = effectiveNotifyCb();
    if (!cb) return;
    uint8_t err = 0x01;
    cb(this, &err, 1, true);
  }

  // Build and inject PKT_SELF_INFO (0x05) via notify callback.
  // Format: [0x05][3 skip][32 pubkey][12 skip][10 radio][name...]
  void injectSelfInfo() {
    auto cb = effectiveNotifyCb();
    if (!cb || !mockCompanion) return;
    uint8_t buf[128] = {};
    size_t off = 0;

    buf[off++] = 0x05;        // PKT_SELF_INFO
    memset(buf + off, 0, 3);  // advType, txPower, maxTxPower
    off += 3;
    hexToBytes(mockCompanion->publicKey, buf + off, 32);
    off += 32;
    memset(buf + off, 0, 12);  // lat, lon, multiAcks, advLocPolicy,
                               // telemetryMode, manualAddContacts
    off += 12;
    uint32_t freqRaw = static_cast<uint32_t>(mockCompanion->radioFreq * 1000.0f);
    uint32_t bwRaw = static_cast<uint32_t>(mockCompanion->radioBw * 1000.0f);
    memcpy(buf + off, &freqRaw, 4);
    off += 4;
    memcpy(buf + off, &bwRaw, 4);
    off += 4;
    buf[off++] = mockCompanion->radioSf;
    buf[off++] = mockCompanion->radioCr;
    size_t nameLen = strlen(mockCompanion->name);
    if (nameLen > sizeof(buf) - off - 1) nameLen = sizeof(buf) - off - 1;
    memcpy(buf + off, mockCompanion->name, nameLen);
    off += nameLen;

    cb(this, buf, off, true);
  }

  // Build and inject PKT_DEVICE_INFO (0x0D) via notify callback.
  // Format: [0x0D][fw_ver][max_contacts/2][max_channels][ble_pin LE]
  //         [build(12)][model(40)][version(20)] => 80 bytes
  void injectDeviceInfo() {
    auto cb = effectiveNotifyCb();
    if (!cb || !mockCompanion) return;
    uint8_t buf[80] = {};
    size_t off = 0;

    buf[off++] = 0x0D;  // PKT_DEVICE_INFO
    buf[off++] = 0x00;  // fw_ver (unused by client)
    buf[off++] = mockCompanion->maxContacts / 2;
    buf[off++] = mockCompanion->maxChannels;
    uint32_t pin = mockCompanion->blePin;
    memcpy(buf + off, &pin, 4);
    off += 4;

    memset(buf + off, 0, 12);
    size_t fwLen = strlen(mockCompanion->firmwareBuild);
    if (fwLen > 12) fwLen = 12;
    memcpy(buf + off, mockCompanion->firmwareBuild, fwLen);
    off += 12;

    memset(buf + off, 0, 40);
    size_t modelLen = strlen(mockCompanion->model);
    if (modelLen > 40) modelLen = 40;
    memcpy(buf + off, mockCompanion->model, modelLen);
    off += 40;

    memset(buf + off, 0, 20);
    size_t verLen = strlen(mockCompanion->version);
    if (verLen > 20) verLen = 20;
    memcpy(buf + off, mockCompanion->version, verLen);
    off += 20;

    cb(this, buf, off, true);
  }

  // Build and inject PKT_CONTACT_START + PKT_CONTACT × N + PKT_CONTACT_END
  // via notify callback. Packets match parseContact() format (148 bytes).
  void injectContactList() {
    auto cb = effectiveNotifyCb();
    if (!cb || !mockCompanion) return;

    // 1. PKT_CONTACT_START (just the code byte)
    uint8_t start = 0x02;
    cb(this, &start, 1, true);

    // 2. PKT_CONTACT for each contact
    for (uint8_t i = 0; i < mockCompanion->contactCount; ++i) {
      injectOneContact(cb, mockCompanion->contacts[i]);
    }

    // 3. PKT_CONTACT_END (just the code byte)
    uint8_t end = 0x04;
    cb(this, &end, 1, true);
  }

  // Build and inject a single PKT_CONTACT (0x03, 148 bytes).
  // Format: [0x03][32 pubkey][type][flags][pathLen][64 path][32 name]
  //         [4 lastSeen][4 gpsLat(0)][4 gpsLon(0)][4 lastmod]
  void injectOneContact(notify_callback cb, const MockContact& c) {
    uint8_t buf[148] = {};
    size_t off = 0;

    buf[off++] = 0x03;  // PKT_CONTACT
    hexToBytes(c.publicKey, buf + off, 32);
    off += 32;
    // MockContact.type is 0-based (0=COMPANION, 1=REPEATER, 2=ROOM_SERVER).
    // MeshCore wire protocol uses 1-based (1=CLIENT, 2=REPEATER, 3=ROOM).
    buf[off++] = c.type + 1;    // type (0-based → wire)
    buf[off++] = c.flags;       // flags (bit 0 = favourite)
    buf[off++] = c.pathLength;  // out_path_len
    memset(buf + off, 0, 64);   // out_path (64 zero bytes)
    off += 64;
    memset(buf + off, 0, 32);  // name (32 bytes, null-padded)
    size_t nameLen = strlen(c.name);
    if (nameLen > 31) nameLen = 31;
    memcpy(buf + off, c.name, nameLen);
    off += 32;
    uint32_t ts = c.lastSeen ? c.lastSeen : 1640995200;  // 2022-01-01 UTC
    memcpy(buf + off, &ts, 4);                           // last_advert_timestamp LE
    off += 4;
    memset(buf + off, 0, 4);  // gps_lat = 0
    off += 4;
    memset(buf + off, 0, 4);  // gps_lon = 0
    off += 4;
    memcpy(buf + off, &ts, 4);  // lastmod = same timestamp

    cb(this, buf, 148, true);
  }

  // Build and inject PKT_CHANNEL_INFO (0x12, 50 bytes) for channel index.
  // Format: [0x12][idx][32 name][16 secret]
  // When the companion has no channel at this index, send an empty one
  // (name[0] = '\0', configured=false) so the client doesn't hang waiting.
  void injectChannelInfo(uint8_t idx) {
    auto cb = effectiveNotifyCb();
    if (!cb || !mockCompanion) return;

    uint8_t buf[50] = {};
    size_t off = 0;

    buf[off++] = 0x12;  // PKT_CHANNEL_INFO
    buf[off++] = idx;   // channel index

    memset(buf + off, 0, 32);  // name (32 bytes, null-padded)
    if (idx < mockCompanion->channelCount) {
      size_t nameLen = strlen(mockCompanion->channels[idx].name);
      if (nameLen > 31) nameLen = 31;
      memcpy(buf + off, mockCompanion->channels[idx].name, nameLen);
    }
    off += 32;

    // secret (16 bytes): all zeros = HASHTAG (or PUBLIC if idx==0)
    memset(buf + off, 0, 16);
    off += 16;

    cb(this, buf, 50, true);
    LOG_DBG("MOCK", "injectChannelInfo(%d): %s", idx,
            idx < mockCompanion->channelCount ? mockCompanion->channels[idx].name : "(empty)");
  }

  // --- Issue 5-AFK: Message, battery, and send acknowledgment methods ---

  // Build and inject PKT_CHANNEL_MSG_V3 for each message in the companion's
  // channels[], then PKT_NO_MORE_MSGS. Only sends messages on first call
  // after connect; subsequent calls just send PKT_NO_MORE_MSGS.
  void injectChannelMessages() {
    auto cb = effectiveNotifyCb();
    if (!cb || !mockCompanion) return;

    // Only send the full message dump once per connection.
    if (messagesSent) {
      uint8_t noMore = 0x0A;  // PKT_NO_MORE_MSGS
      cb(this, &noMore, 1, true);
      return;
    }
    messagesSent = true;

    // Use first contact name as default sender for received messages
    const char* defaultSender = "Mock User";
    if (mockCompanion->contactCount > 0) {
      defaultSender = mockCompanion->contacts[0].name;
    }
    size_t senderLen = strlen(defaultSender);

    // Iterate all channels and send their messages
    for (uint8_t ch = 0; ch < mockCompanion->channelCount; ++ch) {
      const MockChannel& channel = mockCompanion->channels[ch];
      for (uint8_t m = 0; m < channel.messageCount; ++m) {
        const MockMessage& msg = channel.messages[m];

        // Build text in "SenderName: body" format for received messages
        char formattedText[MOCK_MAX_TEXT_LEN];
        size_t bodyLen = strlen(msg.text);
        if (msg.direction == 0) {  // received
          // Prepend "SenderName: "
          size_t prefixLen = snprintf(formattedText, sizeof(formattedText), "%s: ", defaultSender);
          size_t copyLen = bodyLen;
          if (prefixLen + copyLen > sizeof(formattedText) - 1) {
            copyLen = sizeof(formattedText) - 1 - prefixLen;
          }
          memcpy(formattedText + prefixLen, msg.text, copyLen);
          formattedText[prefixLen + copyLen] = '\0';
        } else {
          // Sent messages: just copy the text (displayed as "> You")
          size_t copyLen = bodyLen;
          if (copyLen > sizeof(formattedText) - 1) {
            copyLen = sizeof(formattedText) - 1;
          }
          memcpy(formattedText, msg.text, copyLen);
          formattedText[copyLen] = '\0';
        }

        // Build PKT_CHANNEL_MSG_V3 (0x11)
        // [0x11][snr][2 res][chIdx][pathLen][txtType][ts LE 4][text...]
        size_t textLen = strlen(formattedText);
        size_t pktLen = 11 + textLen;
        uint8_t buf[256];
        size_t off = 0;
        buf[off++] = 0x11;  // PKT_CHANNEL_MSG_V3
        buf[off++] = 0;     // snr
        buf[off++] = 0;
        buf[off++] = 0;   // 2 reserved
        buf[off++] = ch;  // channel index
        buf[off++] = 0;   // pathLen
        buf[off++] = 0;   // txtType
        uint32_t ts = msg.timestamp;
        memcpy(buf + off, &ts, 4);
        off += 4;  // timestamp LE
        memcpy(buf + off, formattedText, textLen);
        off += textLen;
        cb(this, buf, off, true);
      }
    }

    // Signal end of message list
    uint8_t noMore = 0x0A;  // PKT_NO_MORE_MSGS
    cb(this, &noMore, 1, true);
  }

  // Build and inject PKT_BATTERY (0x0C, 3 bytes) with randomized values.
  // Varies battery by ±50 mV and storage by ±10 KB from JSON baseline.
  void injectBattery() {
    auto cb = effectiveNotifyCb();
    if (!cb || !mockCompanion) return;

    // Simple LCG for deterministic-but-varied randomization
    uint32_t seed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    auto randInRange = [&seed](int range) -> int {
      seed = seed * 1103515245 + 12345;
      return static_cast<int>((seed >> 16) % static_cast<uint32_t>(range));
    };

    uint16_t batteryMv = mockCompanion->batteryMv;
    if (batteryMv > 0) {
      int jitter = randInRange(101) - 50;  // -50..+50 mV
      int newMv = static_cast<int>(batteryMv) + jitter;
      if (newMv < 3000) newMv = 3000;  // plausible floor
      if (newMv > 4300) newMv = 4300;  // plausible ceiling (LiPo)
      batteryMv = static_cast<uint16_t>(newMv);
    } else {
      batteryMv = 3700;  // default if JSON had 0
    }

    uint8_t buf[3];
    buf[0] = 0x0C;                   // PKT_BATTERY
    memcpy(buf + 1, &batteryMv, 2);  // mV LE
    cb(this, buf, 3, true);
    LOG_DBG("MOCK", "injectBattery: %d mV", batteryMv);
  }

  // Build and inject PKT_MSG_SENT (0x06, 10 bytes).
  // Acknowledges that the companion received the message.
  void injectMsgSent() {
    auto cb = effectiveNotifyCb();
    if (!cb) return;

    uint8_t buf[10] = {};
    buf[0] = 0x06;  // PKT_MSG_SENT
    // buf[1] = 0;  // isSentFlood
    // buf[2..5] = 0;  // ack_tag
    // buf[6..9] = 0;  // est_timeout
    cb(this, buf, 10, true);
  }

  // Build and inject PKT_NEW_ADVERT (0x8A) for each discovered node.
  // Uses writeContactRespFrame format (148 bytes) parsed by parseContact().
  // These populate the Hub's contact list via contactCb with full node info.
  void injectAdvertList() {
    auto cb = effectiveNotifyCb();
    if (!cb || !mockCompanion || mockCompanion->discoveredNodeCount == 0) return;

    // writeContactRespFrame layout (148 bytes):
    // [0]=code [1..32]=pubkey [33]=type [34]=flags [35]=pathLen
    // [36..99]=path(64) [100..131]=name(32) [132..135]=lastSeen
    // [136..139]=gpsLat [140..143]=gpsLon [144..147]=lastmod
    static constexpr size_t CONTACT_PKT_LEN = 148;
    uint8_t buf[CONTACT_PKT_LEN] = {};
    buf[0] = 0x8A;  // PKT_NEW_ADVERT

    for (uint8_t i = 0; i < mockCompanion->discoveredNodeCount; ++i) {
      const auto& node = mockCompanion->discoveredNodes[i];

      memset(buf + 1, 0, CONTACT_PKT_LEN - 1);  // reset payload

      hexToBytes(node.publicKey, buf + 1, 32);
      // MockDiscoveredNode.type is 0-based internal enum → +1 for wire format
      // (matches injectOneContact convention: 0=COMPANION→1=CLIENT, etc.)
      buf[33] = node.type + 1;
      buf[35] = node.pathLength;

      // name: up to 32 bytes, null-terminated
      size_t nameLen = strlen(node.name);
      if (nameLen > 32) nameLen = 32;
      memcpy(buf + 100, node.name, nameLen);

      memcpy(buf + 132, &node.lastSeen, 4);

      cb(this, buf, CONTACT_PKT_LEN, true);
    }
    LOG_INF("MOCK", "injected %d PKT_NEW_ADVERT(s) for discovered nodes", mockCompanion->discoveredNodeCount);
  }

  // Convert hex string (max 64 chars) to binary bytes.
  static void hexToBytes(const char* hex, uint8_t* out, size_t maxBytes) {
    for (size_t i = 0; i < maxBytes && hex[i * 2] && hex[i * 2 + 1]; ++i) {
      out[i] = static_cast<uint8_t>((hexCharToNibble(hex[i * 2]) << 4) | hexCharToNibble(hex[i * 2 + 1]));
    }
  }

  static uint8_t hexCharToNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  }
};

class NimBLERemoteService {
 public:
  NimBLERemoteService() = default;

  void setMockCompanion(const MockCompanion* c) {
    rxChar.setMockCompanion(c);
    txChar.setMockCompanion(c);
    rxChar.setNotifySource(&txChar);  // rxChar uses txChar's notify callback
  }

  // doConnect() requests RX first, then TX. Use call counter to match.
  NimBLERemoteCharacteristic* getCharacteristic(const NimBLEUUID&) {
    if (callCount == 0) {
      ++callCount;
      return &rxChar;
    }
    if (callCount == 1) {
      ++callCount;
      return &txChar;
    }
    return nullptr;
  }

  void resetCallCount() { callCount = 0; }

  void resetMessageSentFlag() {
    rxChar.resetMessagesSent();
    txChar.resetMessagesSent();
  }

  // Inject a raw packet via TX notify callback (for hotkey-driven injection).
  // Returns true if the notify callback exists and was called.
  bool injectPacket(const uint8_t* data, size_t len) {
    if (!txChar.hasNotifyCallback()) return false;
    return txChar.injectRawPacket(data, len);
  }

 private:
  NimBLERemoteCharacteristic rxChar{};
  NimBLERemoteCharacteristic txChar{};
  uint8_t callCount = 0;
};

class NimBLEClient {
 public:
  NimBLEClient() = default;

  // connect() looks up companion by BLE address in MockSession data.
  // Defined in MockNimBLEDevice.cpp to break circular include with MockSession.h.
  bool connect(const NimBLEAddress& addr);

  bool secureConnection() { return connected; }
  bool isConnected() const { return connected; }
  NimBLEConnInfo getConnInfo() const { return NimBLEConnInfo(); }

  void disconnect() {
    connected = false;
    mockCompanion = nullptr;
    nusService.resetCallCount();
    nusService.resetMessageSentFlag();
    nusService.setMockCompanion(nullptr);
  }

  void setClientCallbacks(NimBLEClientCallbacks* cb, bool) { callbacks = cb; }

  NimBLERemoteService* getService(const NimBLEUUID&) {
    if (!connected) return nullptr;
    return &nusService;
  }

  // Force disconnect and fire callback (key-1 hotkey).
  void injectDisconnect(int reason) {
    if (!connected) return;
    connected = false;
    mockCompanion = nullptr;
    nusService.resetMessageSentFlag();
    if (callbacks) callbacks->onDisconnect(this, reason);
  }

  const MockCompanion* getMockCompanion() const { return mockCompanion; }

  // Inject a raw packet via the TX characteristic's notify callback.
  // Returns true if connected and packet was delivered.
  bool injectPacket(const uint8_t* data, size_t len) {
    if (!connected) return false;
    return nusService.injectPacket(data, len);
  }

 private:
  friend class NimBLEDevice;  // createClient accesses nusService
  const MockCompanion* mockCompanion = nullptr;
  bool connected = false;
  NimBLEClientCallbacks* callbacks = nullptr;
  NimBLERemoteService nusService{};
};

// NimBLEDevice static class
class NimBLEDevice {
 public:
  static void init(const char*) {}
  static void deinit(bool) {}
  static void setMTU(uint16_t) {}
  static void setPower(int) {}
  static void setSecurityAuth(bool, bool, bool) {}
  static void setSecurityIOCap(uint8_t) {}
  static void injectPassKey(NimBLEConnInfo&, uint32_t) {}
  static void deleteAllBonds() {}  // no-op on simulator
  static void setConnectPin(uint32_t pin) { sConnectPin = pin; }
  static NimBLEScan* getScan() {
    static NimBLEScan s;
    return &s;
  }
  // Defined in MockNimBLEDevice.cpp to break circular include with MockSession.h.
  static NimBLEClient* createClient();
  static void deleteClient(NimBLEClient* client) { delete client; }
  static uint32_t getConnectPin() { return sConnectPin; }

 private:
  static uint32_t sConnectPin;
};

// Key-1 hotkey helper: inject BLE disconnect into a mock client.
// Called from MeshCoreMockHotkeys.h when key 1 is pressed.
inline void mockInjectDisconnect(NimBLEClient* client) {
  if (client && client->isConnected()) {
    client->injectDisconnect(0x08);  // BLE_HCI_CONNECTION_TIMEOUT
  }
}

// Process pending echo auto-response whose delay has elapsed.
// Call from each MeshCore activity loop() after handleMockKey().
// `client`: the active NimBLEClient (may be nullptr if not connected).
// `nowMs`: current time from millis().
inline void pollMock(NimBLEClient* client, uint32_t nowMs) {
  if (sPendingEcho.type == PendingEchoType::NONE) return;
  if (!client || !client->isConnected()) {
    sPendingEcho.type = PendingEchoType::NONE;
    return;
  }
  // Wait 1500ms before injecting echo
  if (nowMs - sPendingEcho.requestTimeMs < 1500) return;

  // Build echo text: "Echo: Re: <original>"
  char echoText[MOCK_MAX_TEXT_LEN];
  size_t origLen = strlen(sPendingEcho.text);
  size_t prefixLen = snprintf(echoText, sizeof(echoText), "Echo: Re: ");
  size_t copyLen = origLen;
  if (prefixLen + copyLen > sizeof(echoText) - 1) {
    copyLen = sizeof(echoText) - 1 - prefixLen;
  }
  memcpy(echoText + prefixLen, sPendingEcho.text, copyLen);
  echoText[prefixLen + copyLen] = '\0';

  uint32_t ts = nowMs / 1000;

  if (sPendingEcho.type == PendingEchoType::CHANNEL) {
    // Build PKT_CHANNEL_MSG_V3 (0x11)
    size_t textLen = strlen(echoText);
    size_t pktLen = 11 + textLen;
    if (pktLen > 256) pktLen = 256;
    uint8_t buf[256];
    size_t off = 0;
    buf[off++] = 0x11;
    buf[off++] = 0;
    buf[off++] = 0;
    buf[off++] = 0;
    buf[off++] = sPendingEcho.channelIdx;
    buf[off++] = 0;
    buf[off++] = 0;
    memcpy(buf + off, &ts, 4);
    off += 4;
    memcpy(buf + off, echoText, textLen);
    off += textLen;
    client->injectPacket(buf, off);
    LOG_INF("MOCK", "echo channel msg ch=%d: %.40s", sPendingEcho.channelIdx, echoText);
  } else if (sPendingEcho.type == PendingEchoType::DM) {
    // Build PKT_CONTACT_MSG_V3 (0x10)
    size_t textLen = strlen(echoText);
    size_t pktLen = 16 + textLen;
    if (pktLen > 256) pktLen = 256;
    uint8_t buf[256];
    size_t off = 0;
    buf[off++] = 0x10;
    buf[off++] = 0;
    buf[off++] = 0;
    buf[off++] = 0;
    memcpy(buf + off, sPendingEcho.pubkeyPrefix, 6);
    off += 6;
    buf[off++] = 0;
    buf[off++] = 0;
    memcpy(buf + off, &ts, 4);
    off += 4;
    memcpy(buf + off, echoText, textLen);
    off += textLen;
    client->injectPacket(buf, off);
    LOG_INF("MOCK", "echo DM msg: %.40s", echoText);
  } else if (sPendingEcho.type == PendingEchoType::ADD_CONTACT_OK) {
    // Delayed PKT_OK for CMD_ADD_UPDATE_CONTACT (DELAY_OK mode)
    uint8_t ok = 0x00;
    client->injectPacket(&ok, 1);
    LOG_INF("MOCK", "echo delayed PKT_OK for contact save");
  }

  sPendingEcho.type = PendingEchoType::NONE;
}

// NimBLE-related ESP constants used by MeshCoreClient
#define ESP_PWR_LVL_P9 9
#define BLE_HS_IO_KEYBOARD_ONLY 4

#endif  // SIMULATOR
