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
enum class MeshNodeType : uint8_t { COMPANION = 0, REPEATER = 1, ROOM_SERVER = 2, SENSOR = 3, UNKNOWN = 255 };

// Message direction
enum class MsgDirection : uint8_t { RECEIVED = 0, SENT = 1 };

// Message type
enum class MsgType : uint8_t { CHANNEL = 0, DIRECT = 1 };

// DM delivery status
enum class DeliveryStatus : uint8_t { SENT = 0, ACKED = 1, FAILED = 2 };

// Channel type
enum class ChannelType : uint8_t { PUBLIC = 0, HASHTAG = 1, PRIVATE_CH = 2 };

struct MeshCoreCompanion {
  char name[64] = {};           ///< Display name of the companion device
  uint8_t publicKey[32] = {};   ///< Ed25519 public key of the companion
  char bleAddress[18] = {};     ///< BLE MAC address as string (e.g. "AA:BB:CC:DD:EE:FF")
  float radioFreq = 0;          ///< LoRa center frequency (Hz)
  float radioBw = 0;            ///< LoRa bandwidth (Hz)
  uint8_t radioSf = 0;          ///< LoRa spreading factor (SF7..SF12)
  uint8_t radioCr = 0;          ///< LoRa coding rate (4/5..4/8)
  char firmwareBuild[13] = {};  ///< Firmware build version (string up to 12 chars)
  char model[41] = {};          ///< Device model
  char version[21] = {};        ///< Firmware version
  uint16_t batteryMv = 0;       ///< Battery voltage in millivolts
  uint32_t storageUsedKb = 0;   ///< Used storage space (KB)
  uint32_t storageTotalKb = 0;  ///< Total storage capacity (KB)
  uint8_t maxContacts = 0;      ///< Maximum number of contacts
  uint8_t maxChannels = 8;      ///< Maximum number of channels
  uint32_t blePin = 0;          ///< PIN displayed on the companion device (0 == no PIN)
};

struct MeshCoreContact {
  uint8_t publicKey[32] = {};                 ///< Ed25519 public key of the contact
  char name[64] = {};                         ///< Contact display name
  MeshNodeType type = MeshNodeType::UNKNOWN;  ///< Mesh node type
  uint32_t lastSeen = 0;                      ///< Last seen time (unix timestamp, sec)
  uint8_t pathLength = 0;                     ///< Number of hops to the node
  int8_t snr = 0;                             ///< SNR of last received packet (dB)
  bool isSaved = false;                       ///< Whether the contact is saved in the address book
  uint16_t unreadCount = 0;                   ///< Number of unread messages

  static constexpr uint8_t PUBLIC_KEY_DISPLAY_LEN = 11;  // "AABB..CCDD\0"

  // Return compact 4-byte hex representation for UI display: "AABB..CCDD"
  void getPublicKeyLabel(char out[PUBLIC_KEY_DISPLAY_LEN]) const {
    static constexpr char hex[] = "0123456789abcdef";
    // First 2 bytes
    for (int i = 0; i < 2; ++i) {
      out[i * 2] = hex[publicKey[i] >> 4];
      out[i * 2 + 1] = hex[publicKey[i] & 0x0F];
    }
    out[4] = '.';
    out[5] = '.';
    // Last 2 bytes
    for (int i = 2; i < 4; ++i) {
      out[6 + (i - 2) * 2] = hex[publicKey[i] >> 4];
      out[7 + (i - 2) * 2] = hex[publicKey[i] & 0x0F];
    }
    out[10] = '\0';
  }
};

struct MeshCoreChannel {
  uint8_t index = 0;                       ///< Channel index (0..n-1)
  char name[33] = {};                      ///< Channel name (up to 32 chars + null)
  uint8_t secret[16] = {};                 ///< 16-byte channel secret (key)
  ChannelType type = ChannelType::PUBLIC;  ///< Channel type (public/hashtag/private)
  uint16_t unreadCount = 0;                ///< Number of unread messages in the channel
  bool configured = false;                 ///< Whether the channel was explicitly configured by the user

  bool isEmpty() const { return name[0] == '\0'; }
};

static constexpr uint16_t MAX_MSG_TEXT_LEN = 184;

struct MeshCoreMessage {
  MsgDirection direction = MsgDirection::RECEIVED;       ///< Message direction (received/sent)
  MsgType type = MsgType::CHANNEL;                       ///< Message type (channel/direct)
  uint8_t pubkeyPrefix[6] = {};                          ///< First 6 bytes of sender's public key
  char senderName[64] = {};                              ///< Sender name
  uint8_t channelIdx = 0;                                ///< Channel index (for channel messages)
  uint32_t timestamp = 0;                                ///< Message timestamp (unix timestamp, sec)
  int8_t snr = 0;                                        ///< SNR at reception (dB)
  uint8_t pathLength = 0;                                ///< Number of hops the message traversed
  DeliveryStatus deliveryStatus = DeliveryStatus::SENT;  ///< Delivery status
  uint32_t globalId = 0;                                 ///< Monotonic ID, never resets on truncate
  char text[MAX_MSG_TEXT_LEN] = {};                      ///< Message text (up to MAX_MSG_TEXT_LEN bytes)
};

// Message store file version (increment on format change)
static constexpr uint8_t MESHCORE_MSG_FILE_VERSION = 2;
// Contact store file version
static constexpr uint8_t MESHCORE_CONTACT_FILE_VERSION = 1;
// Conservative max message text length for send UI
static constexpr uint16_t MESHCORE_SEND_CHAR_LIMIT = 140;
