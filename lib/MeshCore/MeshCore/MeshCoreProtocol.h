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
static constexpr uint8_t PKT_NEW_ADVERT = 0x8A;  // PUSH_CODE_NEW_ADVERT: full contact

// Command timeout
static constexpr uint32_t CMD_TIMEOUT_MS = 5000;

// --- Command builders ---
// All return the number of bytes written to `out`.

// CMD_APP_START: 0x01 + 7 reserved + app name
size_t buildAppStart(uint8_t* out, size_t maxLen);

// CMD_DEVICE_QUERY: 0x16 0x03
size_t buildDeviceQuery(uint8_t* out, size_t maxLen);

// CMD_GET_CONTACTS: 0x04 [optional 4-byte since timestamp]
size_t buildGetContacts(uint8_t* out, size_t maxLen);

// CMD_GET_CHANNEL: 0x1F <index>
size_t buildGetChannel(uint8_t* out, size_t maxLen, uint8_t channelIdx);

// CMD_SET_CHANNEL: 0x20 <index> <name[32]> <secret[16]>
size_t buildSetChannel(uint8_t* out, size_t maxLen, uint8_t channelIdx, const char* name, const uint8_t* secret16);

// CMD_SEND_CHANNEL_MESSAGE: 0x03 0x00 <idx> <ts[4]> <text>
size_t buildSendChannelMsg(uint8_t* out, size_t maxLen, uint8_t channelIdx, uint32_t timestamp, const char* text);

// CMD_SEND_DM: 0x02 <txt_type=0> <attempt=0> <ts[4]> <pubkey_prefix[6]> <text>
size_t buildSendDirectMsg(uint8_t* out, size_t maxLen, const uint8_t* pubkey32, uint32_t timestamp, const char* text);

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

bool parseMsgSent(const uint8_t* data, size_t len, uint32_t& expectedAck, uint32_t& suggestedTimeoutMs);

bool parseAck(const uint8_t* data, size_t len, uint8_t ackHash[4]);

}  // namespace MeshProto
