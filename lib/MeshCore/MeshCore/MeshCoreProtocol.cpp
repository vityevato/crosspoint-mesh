#include "MeshCoreProtocol.h"

#include <Logging.h>

#include <cstring>

namespace MeshProto {

// --- Command builders ---

size_t buildAppStart(uint8_t* out, size_t maxLen) {
  static constexpr char APP_NAME[] = "CrossPoint";
  const size_t needed = 8 + sizeof(APP_NAME) - 1;
  if (maxLen < needed) {
    LOG_ERR("MESH", "buildAppStart: buffer too small");
    return 0;
  }
  memset(out, 0, 8);
  out[0] = CMD_APP_START;
  out[1] = 0x03;  // appVer = 3 — request V3 packet format (SNR field, reserved bytes)
  memcpy(out + 8, APP_NAME, sizeof(APP_NAME) - 1);
  return needed;
}

size_t buildDeviceQuery(uint8_t* out, size_t maxLen) {
  if (maxLen < 2) {
    LOG_ERR("MESH", "buildDeviceQuery: buffer too small");
    return 0;
  }
  out[0] = CMD_DEVICE_QUERY;
  out[1] = 0x03;
  return 2;
}

size_t buildGetContacts(uint8_t* out, size_t maxLen) {
  if (maxLen < 1) {
    LOG_ERR("MESH", "buildGetContacts: buffer too small");
    return 0;
  }
  out[0] = CMD_GET_CONTACTS;
  return 1;
}

size_t buildGetChannel(uint8_t* out, size_t maxLen, uint8_t channelIdx) {
  if (maxLen < 2) {
    LOG_ERR("MESH", "buildGetChannel: buffer too small");
    return 0;
  }
  out[0] = CMD_GET_CHANNEL;
  out[1] = channelIdx;
  return 2;
}

size_t buildSetChannel(uint8_t* out, size_t maxLen, uint8_t channelIdx, const char* name, const uint8_t* secret16) {
  // 1 byte cmd + 1 byte idx + 32 bytes name + 16 bytes secret = 50
  if (maxLen < 50) {
    LOG_ERR("MESH", "buildSetChannel: buffer too small");
    return 0;
  }
  out[0] = CMD_SET_CHANNEL;
  out[1] = channelIdx;
  memset(out + 2, 0, 32);
  if (name) {
    size_t nameLen = strlen(name);
    if (nameLen > 32) nameLen = 32;
    memcpy(out + 2, name, nameLen);
  }
  memcpy(out + 34, secret16, 16);
  return 50;
}

size_t buildSendChannelMsg(uint8_t* out, size_t maxLen, uint8_t channelIdx, uint32_t timestamp, const char* text) {
  size_t textLen = strlen(text);
  // 1 cmd + 1 type(0x00) + 1 idx + 4 timestamp + text
  const size_t needed = 7 + textLen;
  if (maxLen < needed) {
    LOG_ERR("MESH", "buildSendChannelMsg: buffer too small");
    return 0;
  }
  out[0] = CMD_SEND_CHAN_MSG;
  out[1] = 0x00;  // channel message subtype
  out[2] = channelIdx;
  memcpy(out + 3, &timestamp, 4);
  memcpy(out + 7, text, textLen);
  return needed;
}

size_t buildSendDirectMsg(uint8_t* out, size_t maxLen, const uint8_t* pubkey32, uint32_t timestamp, const char* text) {
  size_t textLen = strlen(text);
  // Companion CMD_SEND_TXT_MSG format:
  // [0] cmd, [1] txt_type=0, [2] attempt=0, [3..6] ts, [7..12] pubkey_prefix(6), [13..] text
  const size_t needed = 13 + textLen;
  if (maxLen < needed) {
    LOG_ERR("MESH", "buildSendDirectMsg: buffer too small");
    return 0;
  }
  out[0] = CMD_SEND_DM;
  out[1] = 0;  // txt_type = TXT_TYPE_PLAIN = 0
  out[2] = 0;  // attempt = 0 (first attempt)
  memcpy(out + 3, &timestamp, 4);
  memcpy(out + 7, pubkey32, 6);  // companion looks up by 6-byte pubkey prefix
  memcpy(out + 13, text, textLen);
  return needed;
}

size_t buildGetMessage(uint8_t* out, size_t maxLen) {
  if (maxLen < 1) {
    LOG_ERR("MESH", "buildGetMessage: buffer too small");
    return 0;
  }
  out[0] = CMD_GET_MESSAGE;
  return 1;
}

size_t buildSendSelfAdvert(uint8_t* out, size_t maxLen, bool flood) {
  if (maxLen < 2) {
    LOG_ERR("MESH", "buildSendSelfAdvert: buffer too small");
    return 0;
  }
  out[0] = CMD_SEND_SELF_ADVERT;
  out[1] = flood ? 1 : 0;  // 0 = zero-hop, 1 = flood through mesh
  return 2;
}

size_t buildGetBattery(uint8_t* out, size_t maxLen) {
  if (maxLen < 1) {
    LOG_ERR("MESH", "buildGetBattery: buffer too small");
    return 0;
  }
  out[0] = CMD_GET_BATTERY;
  return 1;
}

size_t buildAddUpdateContact(uint8_t* out, size_t maxLen, const MeshCoreContact& contact) {
  // Format: 0x09 <pubkey[32]> <type> <flags> <out_path_len> <out_path[64]> <name[32]> <ts[4]>
  static constexpr size_t NEEDED = 1 + 32 + 1 + 1 + 1 + 64 + 32 + 4;
  if (maxLen < NEEDED) {
    LOG_ERR("MESH", "buildAddUpdateContact: buffer too small");
    return 0;
  }
  size_t off = 0;
  out[off++] = CMD_ADD_UPDATE_CONTACT;
  memcpy(out + off, contact.publicKey, 32);
  off += 32;
  out[off++] = nodeTypeToWire(contact.type);  // internal enum → wire (1=CLIENT, 2=REPEATER, …)
  out[off++] = contact.isSaved ? 1 : 0;       // flags (bit 0 = saved)
  out[off++] = contact.pathLength;            // out_path_len (may be 0)
  memset(out + off, 0, 64);                   // out_path — not tracked locally
  off += 64;
  // Name: up to 31 chars + null
  size_t nameLen = strlen(contact.name);
  if (nameLen > 31) nameLen = 31;
  memset(out + off, 0, 32);
  memcpy(out + off, contact.name, nameLen);
  off += 32;
  uint32_t ts = contact.lastSeen;
  memcpy(out + off, &ts, 4);
  off += 4;
  return off;
}

// --- Packet parsers ---

bool parseSelfInfo(const uint8_t* data, size_t len, MeshCoreCompanion& out) {
  // Minimum: 1 type + 3 skip + 32 pubkey + 12 skip + 10 radio = 58
  if (len < 58) {
    LOG_ERR("MESH", "parseSelfInfo: too short (%d)", (int)len);
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
    if (nameLen > sizeof(out.name) - 1) nameLen = sizeof(out.name) - 1;
    memcpy(out.name, data + off, nameLen);
    out.name[nameLen] = '\0';
  }
  return true;
}

bool parseDeviceInfo(const uint8_t* data, size_t len, MeshCoreCompanion& out) {
  // Format (ver 3+):
  // [0] code=13, [1] firmware_ver, [2] max_contacts_div_2, [3] max_channels,
  // [4..7] ble_pin (uint32 LE), [8..19] build_date(12), [20..59] model(40),
  // [60..79] version(20)  => total 80 bytes
  if (len < 80) {
    LOG_ERR("MESH", "parseDeviceInfo: too short (%d)", (int)len);
    return false;
  }
  size_t off = 1;

  // firmware_ver (unused, just advance)
  off += 1;

  out.maxContacts = data[off] * 2;  // max_contacts_div_2
  off += 1;

  out.maxChannels = data[off];
  off += 1;

  memcpy(&out.blePin, data + off, 4);  // ble_pin (little-endian)
  off += 4;

  memcpy(out.firmwareBuild, data + off, 12);
  out.firmwareBuild[12] = '\0';
  off += 12;

  memcpy(out.model, data + off, 40);
  out.model[40] = '\0';
  off += 40;

  memcpy(out.version, data + off, 20);
  out.version[20] = '\0';

  return true;
}

bool parseChannelInfo(const uint8_t* data, size_t len, MeshCoreChannel& out) {
  // 1 type + 1 idx + 32 name + 16 secret = 50
  if (len < 50) {
    LOG_ERR("MESH", "parseChannelInfo: too short (%d)", (int)len);
    return false;
  }
  size_t off = 1;

  out.index = data[off];
  off += 1;

  memcpy(out.name, data + off, 32);
  out.name[32] = '\0';
  off += 32;

  memcpy(out.secret, data + off, 16);

  out.configured = (out.name[0] != '\0');

  // Determine channel type from index
  if (out.index == 0) {
    out.type = ChannelType::PUBLIC;
  } else {
    // Check if secret is all zeros (hashtag) or not (private)
    bool hasSecret = false;
    for (int i = 0; i < 16; ++i) {
      if (out.secret[i] != 0) {
        hasSecret = true;
        break;
      }
    }
    out.type = hasSecret ? ChannelType::PRIVATE_CH : ChannelType::HASHTAG;
  }

  return true;
}

bool parseBattery(const uint8_t* data, size_t len, MeshCoreCompanion& out) {
  // 1 type + 2 millivolts = 3
  if (len < 3) {
    LOG_ERR("MESH", "parseBattery: too short (%d)", (int)len);
    return false;
  }
  memcpy(&out.batteryMv, data + 1, 2);
  return true;
}

bool parseContact(const uint8_t* data, size_t len, MeshCoreContact& out) {
  // Companion writeContactRespFrame layout (MAX_PATH_SIZE = 64, PUB_KEY_SIZE = 32):
  // [0]     code
  // [1..32] pub_key (32)
  // [33]    type
  // [34]    flags
  // [35]    out_path_len
  // [36..99] out_path (64)
  // [100..131] name (32)
  // [132..135] last_advert_timestamp (4)
  // [136..139] gps_lat (4)
  // [140..143] gps_lon (4)
  // [144..147] lastmod (4)
  // Total: 148 bytes
  static constexpr size_t CONTACT_PKT_MIN = 148;
  static constexpr size_t COMPANION_MAX_PATH_SIZE = 64;
  if (len < CONTACT_PKT_MIN) {
    LOG_ERR("MESH", "parseContact: too short (%d)", (int)len);
    return false;
  }
  size_t off = 1;

  memcpy(out.publicKey, data + off, 32);
  off += 32;

  uint8_t nodeType = data[off];
  off += 1;  // type
  off += 1;  // flags (unused)

  out.pathLength = data[off];
  off += 1;

  off += COMPANION_MAX_PATH_SIZE;  // skip out_path[64]

  memcpy(out.name, data + off, 32);
  out.name[32] = '\0';
  off += 32;

  memcpy(&out.lastSeen, data + off, 4);

  // MeshCore firmware wire types: 1=CLIENT, 2=REPEATER, 3=ROOM, 4=SENSOR.
  out.type = nodeTypeFromWire(nodeType);

  return true;
}

bool parseChannelMessage(const uint8_t* data, size_t len, MeshCoreMessage& out) {
  bool isV3 = (data[0] == PKT_CHANNEL_MSG_V3);

  // Companion onChannelMessageRecv layout:
  // V1: [type][chIdx][pathLen][txtType][ts:4][text...]
  // V3: [type][snr][res][res][chIdx][pathLen][txtType][ts:4][text...]
  // Channel messages have NO pubkey prefix.
  size_t headerLen = isV3 ? 11 : 8;
  if (len < headerLen + 1) {  // at least 1 byte of text
    LOG_ERR("MESH", "parseChannelMsg: too short (%d)", (int)len);
    return false;
  }

  out.type = MsgType::CHANNEL;
  out.direction = MsgDirection::RECEIVED;
  memset(out.pubkeyPrefix, 0, sizeof(out.pubkeyPrefix));

  size_t off = 1;
  if (isV3) {
    out.snr = static_cast<int8_t>(data[off]);
    off += 1;  // snr
    off += 2;  // 2 reserved bytes
  }

  out.channelIdx = data[off];
  off += 1;

  out.pathLength = data[off];
  off += 1;

  off += 1;  // txtType (unused)

  memcpy(&out.timestamp, data + off, 4);
  off += 4;

  // Remaining bytes are the message text (includes "SenderName: body")
  size_t textLen = len - off;
  if (textLen > MAX_MSG_TEXT_LEN - 1) textLen = MAX_MSG_TEXT_LEN - 1;
  memcpy(out.text, data + off, textLen);
  out.text[textLen] = '\0';

  // Try to extract sender name from "SenderName: body" format
  const char* colonSpace = strstr(out.text, ": ");
  if (colonSpace && (colonSpace - out.text) < (int)sizeof(out.senderName) - 1) {
    size_t nameLen = colonSpace - out.text;
    memcpy(out.senderName, out.text, nameLen);
    out.senderName[nameLen] = '\0';
  }

  return true;
}

bool parseContactMessage(const uint8_t* data, size_t len, MeshCoreMessage& out) {
  bool isV3 = (data[0] == PKT_CONTACT_MSG_V3);

  // Companion queueMessage layout:
  // V1: [type][pubkey:6][pathLen][txtType][ts:4][text...]
  // V3: [type][snr][res][res][pubkey:6][pathLen][txtType][ts:4][text...]
  size_t headerLen = isV3 ? 16 : 13;
  if (len < headerLen + 1) {  // at least 1 byte of text
    LOG_ERR("MESH", "parseContactMsg: too short (%d)", (int)len);
    return false;
  }

  out.type = MsgType::DIRECT;
  out.direction = MsgDirection::RECEIVED;

  size_t off = 1;
  if (isV3) {
    out.snr = static_cast<int8_t>(data[off]);
    off += 1;  // snr
    off += 2;  // 2 reserved bytes
  }

  memcpy(out.pubkeyPrefix, data + off, 6);
  off += 6;

  out.pathLength = data[off];
  off += 1;

  off += 1;  // txtType (unused)

  memcpy(&out.timestamp, data + off, 4);
  off += 4;

  size_t textLen = len - off;
  if (textLen > MAX_MSG_TEXT_LEN - 1) textLen = MAX_MSG_TEXT_LEN - 1;
  memcpy(out.text, data + off, textLen);
  out.text[textLen] = '\0';

  return true;
}

bool parseMsgSent(const uint8_t* data, size_t len, uint32_t& expectedAck, uint32_t& suggestedTimeoutMs) {
  // Companion RESP_CODE_SENT layout:
  // [0] code, [1] isSentFlood, [2..5] ack_tag, [6..9] est_timeout
  if (len < 10) {
    LOG_ERR("MESH", "parseMsgSent: too short (%d)", (int)len);
    return false;
  }
  memcpy(&expectedAck, data + 2, 4);  // skip isSentFlood at [1]
  memcpy(&suggestedTimeoutMs, data + 6, 4);
  return true;
}

bool parseAck(const uint8_t* data, size_t len, uint8_t ackHash[4]) {
  // Companion PUSH_CODE_SEND_CONFIRMED layout:
  // [0] code, [1..4] ack_hash, [5..8] trip_time_ms
  if (len < 9) {
    LOG_ERR("MESH", "parseAck: too short (%d)", (int)len);
    return false;
  }
  memcpy(ackHash, data + 1, 4);
  return true;
}

bool parseChannelReflood(const uint8_t* frame, size_t len, uint8_t* outHashes, uint8_t maxHashes, uint8_t& outHashCount,
                         uint32_t& outPayloadHash, uint8_t ourNodeHash, bool& pathContainsOurHash) {
  outHashCount = 0;
  outPayloadHash = 0;
  pathContainsOurHash = false;

  // 0x88 LOG_RX_DATA frame: [0x88][snr*4:int8][rssi:int8][raw LoRa packet...]
  if (len < 3 + 2 || frame[0] != PUSH_LOG_RX_DATA) return false;
  const uint8_t* pkt = frame + 3;
  size_t pktLen = len - 3;

  // Raw packet header: route(2 bits) | type(4 bits) | version(2 bits)
  size_t off = 0;
  uint8_t header = pkt[off++];
  uint8_t route = header & RAW_ROUTE_MASK;
  uint8_t ptype = (header >> RAW_PTYPE_SHIFT) & RAW_PTYPE_MASK;
  if (ptype != RAW_PAYLOAD_GRP_TXT) return false;  // only channel/group text messages

  // Transport-coded routes carry two uint16 transport codes before the path.
  if (route == RAW_ROUTE_TRANSPORT_FLOOD || route == RAW_ROUTE_TRANSPORT_DIRECT) {
    off += 4;
  }
  if (off >= pktLen) return false;

  uint8_t pathLen = pkt[off++];
  uint8_t hashSize = (pathLen >> 6) + 1;  // top 2 bits: 1..4 byte hashes
  uint8_t hashCount = pathLen & 0x3F;     // bottom 6 bits: hop count
  size_t pathBytes = static_cast<size_t>(hashCount) * hashSize;
  if (off + pathBytes > pktLen) return false;

  // Each path element identifies a forwarding repeater by its routing hash
  // (first byte of the element, which is the first byte of its public key).
  // Check whether ourNodeHash appears in the path — if so, this is a re-flood
  // of a message that originated from our companion.
  for (uint8_t i = 0; i < hashCount && outHashCount < maxHashes; ++i) {
    uint8_t firstByte = pkt[off + static_cast<size_t>(i) * hashSize];
    outHashes[outHashCount++] = firstByte;
    if (ourNodeHash != 0 && firstByte == ourNodeHash) {
      pathContainsOurHash = true;
    }
  }
  off += pathBytes;

  // FNV-1a hash of the (encrypted) payload — identical across every re-flood of
  // the same message, so it lets the caller correlate copies of one message.
  uint32_t h = 2166136261u;
  for (size_t i = off; i < pktLen; ++i) {
    h ^= pkt[i];
    h *= 16777619u;
  }
  // Avoid colliding with the "unlocked" sentinel value of 0.
  outPayloadHash = h ? h : 1u;
  return true;
}

}  // namespace MeshProto
