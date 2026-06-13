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
  uint32_t blePin = 0;  // PIN shown on companion device (0 = no PIN)
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

  bool isEmpty() const { return name[0] == '\0'; }
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
