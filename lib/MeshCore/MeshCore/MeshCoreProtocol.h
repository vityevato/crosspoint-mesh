#pragma once

#include <cstddef>
#include <cstdint>

#include "MeshCoreTypes.h"

// MeshCore companion protocol command/packet constants
namespace MeshProto {

// Command bytes (app -> firmware)
static constexpr uint8_t CMD_APP_START = 0x01;
static constexpr uint8_t CMD_SEND_DM = 0x02;
static constexpr uint8_t CMD_SEND_CHAN_MSG = 0x03;
static constexpr uint8_t CMD_GET_CONTACTS = 0x04;
static constexpr uint8_t CMD_ADD_UPDATE_CONTACT = 0x09;
static constexpr uint8_t CMD_REMOVE_CONTACT = 0x0F;
static constexpr uint8_t CMD_SEND_SELF_ADVERT = 0x07;
static constexpr uint8_t CMD_GET_MESSAGE = 0x0A;
static constexpr uint8_t CMD_GET_BATTERY = 0x14;
static constexpr uint8_t CMD_DEVICE_QUERY = 0x16;
static constexpr uint8_t CMD_GET_CHANNEL = 0x1F;
static constexpr uint8_t CMD_SET_CHANNEL = 0x20;

// Packet types (firmware -> app)
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
static constexpr uint8_t PUSH_LOG_RX_DATA = 0x88;  // PUSH_CODE_LOG_RX_DATA: raw RX packet log
static constexpr uint8_t PKT_NEW_ADVERT = 0x8A;    // PUSH_CODE_NEW_ADVERT: full contact

// Raw LoRa packet header fields (for parsing PUSH_LOG_RX_DATA frames).
// header byte = route(2 bits) | type(4 bits) | version(2 bits)
static constexpr uint8_t RAW_ROUTE_MASK = 0x03;
static constexpr uint8_t RAW_ROUTE_TRANSPORT_FLOOD = 0x00;
static constexpr uint8_t RAW_ROUTE_FLOOD = 0x01;
static constexpr uint8_t RAW_ROUTE_DIRECT = 0x02;
static constexpr uint8_t RAW_ROUTE_TRANSPORT_DIRECT = 0x03;
static constexpr uint8_t RAW_PTYPE_SHIFT = 2;
static constexpr uint8_t RAW_PTYPE_MASK = 0x0F;
static constexpr uint8_t RAW_PAYLOAD_GRP_TXT = 0x05;  // group/channel text message
static constexpr uint8_t MESH_MAX_PATH_HASHES = 16;

// Command timeout
static constexpr uint32_t CMD_TIMEOUT_MS = 5000;

// --- Wire ↔ internal type conversion ---
// MeshCore firmware uses: 1=CLIENT, 2=REPEATER, 3=ROOM, 4=SENSOR on the wire.
// Our MeshNodeType enum uses: 0=COMPANION, 1=REPEATER, 2=ROOM_SERVER, 3=SENSOR.
inline uint8_t nodeTypeToWire(MeshNodeType t) {
  switch (t) {
    case MeshNodeType::COMPANION:
      return 1;  // CLIENT
    case MeshNodeType::REPEATER:
      return 2;
    case MeshNodeType::ROOM_SERVER:
      return 3;  // ROOM
    case MeshNodeType::SENSOR:
      return 4;
    default:
      return 0;
  }
}
inline MeshNodeType nodeTypeFromWire(uint8_t wire) {
  switch (wire) {
    case 1:
      return MeshNodeType::COMPANION;  // CLIENT
    case 2:
      return MeshNodeType::REPEATER;
    case 3:
      return MeshNodeType::ROOM_SERVER;  // ROOM
    case 4:
      return MeshNodeType::SENSOR;
    default:
      return MeshNodeType::UNKNOWN;
  }
}

// --- Command builders ---
// All return the number of bytes written to `out`.

// CMD_APP_START: 0x01 + 7 reserved + app name
size_t buildAppStart(uint8_t* out, size_t maxLen);

// CMD_DEVICE_QUERY: 0x16 0x03
size_t buildDeviceQuery(uint8_t* out, size_t maxLen);

// CMD_GET_CONTACTS: 0x04 [optional 4-byte since timestamp]
// Build CMD_GET_CONTACTS. When since==0 the request is a bare 1-byte command
// (full contact list). When since>0 a 4-byte little-endian 'since' filter is
// appended (5 bytes total) so the companion only streams contacts with
// lastmod > since — used for incremental syncs after a bare advert.
size_t buildGetContacts(uint8_t* out, size_t maxLen, uint32_t since = 0);

// CMD_ADD_UPDATE_CONTACT: 0x09 <pubkey[32]> <type> <flags> <out_path_len> <out_path[64]> <name[32]> <ts[4]>
size_t buildAddUpdateContact(uint8_t* out, size_t maxLen, const MeshCoreContact& contact);

// CMD_REMOVE_CONTACT: 0x0F <pubkey[32]>
size_t buildRemoveContact(uint8_t* out, size_t maxLen, const uint8_t* pubkey32);

// CMD_GET_CHANNEL: 0x1F <index>
size_t buildGetChannel(uint8_t* out, size_t maxLen, uint8_t channelIdx);

// CMD_SET_CHANNEL: 0x20 <index> <name[32]> <secret[16]>
size_t buildSetChannel(uint8_t* out, size_t maxLen, uint8_t channelIdx, const char* name, const uint8_t* secret16);

// CMD_SEND_CHANNEL_MESSAGE: 0x03 0x00 <idx> <ts[4]> <text>
size_t buildSendChannelMsg(uint8_t* out, size_t maxLen, uint8_t channelIdx, uint32_t timestamp, const char* text);

// CMD_SEND_DM: 0x02 <txt_type=0> <attempt> <ts[4]> <pubkey_prefix[6]> <text>
size_t buildSendDirectMsg(uint8_t* out, size_t maxLen, const uint8_t* pubkey32, uint32_t timestamp, const char* text,
                          uint8_t attempt = 0);

// CMD_SEND_SELF_ADVERT: 0x07 [flood:1 byte] — 0 = zero-hop, 1 = flood
size_t buildSendSelfAdvert(uint8_t* out, size_t maxLen, bool flood);

// CMD_GET_MESSAGE: 0x0A
size_t buildGetMessage(uint8_t* out, size_t maxLen);

// CMD_GET_BATTERY: 0x14
size_t buildGetBattery(uint8_t* out, size_t maxLen);

// --- Packet parsers ---
// All return true on success, false on parse error.

bool parseSelfInfo(const uint8_t* data, size_t len, MeshCoreCompanion& out);

bool parseDeviceInfo(const uint8_t* data, size_t len, MeshCoreCompanion& out);

bool parseChannelInfo(const uint8_t* data, size_t len, MeshCoreChannel& out);

bool parseBattery(const uint8_t* data, size_t len, MeshCoreCompanion& out);

bool parseContact(const uint8_t* data, size_t len, MeshCoreContact& out);

bool parseChannelMessage(const uint8_t* data, size_t len, MeshCoreMessage& out);

bool parseContactMessage(const uint8_t* data, size_t len, MeshCoreMessage& out);

bool parseMsgSent(const uint8_t* data, size_t len, uint32_t& expectedAck, uint32_t& suggestedTimeoutMs,
                  bool& isSentFlood);

bool parseAck(const uint8_t* data, size_t len, uint8_t ackHash[4]);

// Parse a PUSH_LOG_RX_DATA (0x88) frame. If the embedded raw LoRa packet is a
// group/channel text message (GRP_TXT), extracts the forwarding repeater hashes
// (first byte of each path element) into outHashes, the channel hash (first
// payload byte, unencrypted in GRP_TXT) into outChannelHash, and a content hash
// of the encrypted payload into outPayloadHash. The payload hash is identical
// across every re-flood of the same message, letting the caller count distinct
// repeaters; the channel hash identifies which channel the re-flood belongs to.
// Returns true only for GRP_TXT packets.
bool parseChannelReflood(const uint8_t* frame, size_t len, uint8_t* outHashes, uint8_t maxHashes, uint8_t& outHashCount,
                         uint32_t& outPayloadHash, uint8_t& outChannelHash);

// MeshCore channel routing hash: first byte of SHA256(secret16). This is the
// unencrypted channel_hash byte carried in every GRP_TXT packet (firmware
// BaseChatMesh::setChannel()/addChannel() with a 128-bit key).
uint8_t channelHashFromSecret(const uint8_t* secret16);

}  // namespace MeshProto
