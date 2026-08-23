#pragma once

#include <MeshCore/MeshCoreProtocol.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

/**
 * Parse a MeshCore contact URL into a MeshCoreContact.
 *
 * The node `type` uses the MeshCore wire convention in both formats:
 * 1=Companion, 2=Repeater, 3=Room Server, 4=Sensor (see
 * MeshCoreProtocol.h nodeTypeFromWire), and is converted to the internal
 * 0-based MeshNodeType enum.
 *
 * Two formats are supported:
 *
 *   Format 1 — QR code URL (written by saveAdvertToFile):
 *     meshcore://contact/add?name=<name>&public_key=<64hex>&type=<N>
 *
 *   Format 2 — Biz card (raw hex-encoded ADVERT packet):
 *     meshcore://<hex bytes>
 *     The hex data is a serialised MeshCore v1 packet with PAYLOAD_TYPE_ADVERT.
 *     Packet layout:
 *       [header 1B][transport_codes 4B?][path_length 1B][path N B][payload]
 *     ADVERT payload:
 *       [pubkey 32B][timestamp 4B LE][signature 64B][appdata]
 *     Appdata:
 *       [flags 1B][lat 4B?][lon 4B?][feat1 2B?][feat2 2B?][name]
 *       The flags low nibble is the numeric node type (1..4); the high nibble
 *       gates the optional lat/lon/feat/name fields.
 *
 * @return true on success, false if the URL is invalid.
 */
inline bool parseMeshCoreContactUrl(const char* url, MeshCoreContact& out) {
  // All URLs must start with meshcore://
  if (memcmp(url, "meshcore://", 11) != 0) return false;
  const char* body = url + 11;
  if (*body == '\0') return false;

  // ── Format 1: QR code with query params ──
  if (memcmp(body, "contact/add", 11) == 0 || memcmp(body, "channel/add", 11) == 0) {
    // Find query parameters
    const char* nameStart = strstr(body, "name=");
    const char* keyStart = strstr(body, "public_key=");
    if (!nameStart || !keyStart) return false;

    // Extract name
    nameStart += 5;
    const char* nameEnd = strchr(nameStart, '&');
    if (!nameEnd) nameEnd = nameStart + strlen(nameStart);
    size_t nameLen = nameEnd - nameStart;
    if (nameLen > 32) nameLen = 32;  // MeshCoreContact::name is 32 chars + NUL

    char nameBuf[33] = {};
    size_t ni = 0;
    for (size_t i = 0; i < nameLen && ni < 32; ++i) {
      char ch = nameStart[i];
      if (ch == '+') {
        nameBuf[ni++] = ' ';
      } else if (ch == '%' && i + 2 < nameLen) {
        const char hex[3] = {nameStart[i + 1], nameStart[i + 2], '\0'};
        nameBuf[ni++] = static_cast<char>(strtoul(hex, nullptr, 16));
        i += 2;
      } else {
        nameBuf[ni++] = ch;
      }
    }
    nameBuf[ni] = '\0';

    // Extract public_key (64 hex chars)
    keyStart += 11;  // "public_key="
    const char* keyEnd = strchr(keyStart, '&');
    if (!keyEnd) keyEnd = keyStart + strlen(keyStart);
    size_t keyLen = keyEnd - keyStart;
    if (keyLen < 64) return false;

    uint8_t pubkey[32] = {};
    for (int i = 0; i < 32; ++i) {
      const char byte[3] = {keyStart[i * 2], keyStart[i * 2 + 1], '\0'};
      pubkey[i] = static_cast<uint8_t>(strtoul(byte, nullptr, 16));
    }

    // Determine type. `type` uses the wire convention (1=Companion, 2=Repeater,
    // 3=Room Server, 4=Sensor); default to Companion when the parameter is absent.
    MeshNodeType nodeType = MeshNodeType::COMPANION;
    const char* typeStart = strstr(body, "type=");
    if (typeStart) {
      typeStart += 5;
      int typeVal = atoi(typeStart);
      if (typeVal >= 1 && typeVal <= 4) {
        nodeType = MeshProto::nodeTypeFromWire(static_cast<uint8_t>(typeVal));
      }
    }

    memset(&out, 0, sizeof(out));
    memcpy(out.publicKey, pubkey, 32);
    memcpy(out.name, nameBuf, ni + 1);
    out.type = nodeType;
    out.isSaved = true;
    return true;
  }

  // ── Format 2: Biz card (hex-encoded raw ADVERT packet) ──

  // Hex-decode the body (allocate worst-case: body length / 2)
  size_t bodyLen = strlen(body);
  if (bodyLen % 2 != 0) return false;
  size_t decodedLen = bodyLen / 2;
  if (decodedLen < 35) return false;  // header + path_len + pubkey(32) + flags(1)

  // Stack-allocate decoded buffer (biz cards are typically < 200 bytes)
  uint8_t pkt[256];
  if (decodedLen > sizeof(pkt)) return false;
  for (size_t i = 0; i < decodedLen; ++i) {
    const char byte[3] = {body[i * 2], body[i * 2 + 1], '\0'};
    pkt[i] = static_cast<uint8_t>(strtoul(byte, nullptr, 16));
  }

  // Parse packet header
  uint8_t hdr = pkt[0];
  uint8_t routeType = hdr & 0x03;
  uint8_t payloadType = (hdr >> 2) & 0x0F;
  if (payloadType != 0x04) return false;  // Only ADVERT payloads

  size_t off = 1;  // past header

  // Skip transport codes if present
  if (routeType == 0x00 || routeType == 0x03) {
    off += 4;
    if (off >= decodedLen) return false;
  }

  // Parse path_length
  uint8_t pl = pkt[off++];
  uint8_t hopCount = pl & 0x3F;
  uint8_t hashSize = ((pl >> 6) & 0x03) + 1;
  size_t pathBytes = static_cast<size_t>(hopCount) * hashSize;
  off += pathBytes;
  if (off + 97 > decodedLen) return false;  // Need pubkey(32) + ts(4) + sig(64) + flags(1)

  // ── ADVERT payload ──
  // public key (32 bytes)
  const uint8_t* pubkey = pkt + off;
  off += 32;

  // skip timestamp (4 bytes LE)
  off += 4;

  // skip signature (64 bytes)
  off += 64;

  // appdata starts here
  if (off >= decodedLen) return false;
  uint8_t flags = pkt[off++];

  // Flags low nibble is the numeric node type (wire convention):
  // 1=Companion, 2=Repeater, 3=Room Server, 4=Sensor — not individual bit flags.
  MeshNodeType nodeType = MeshProto::nodeTypeFromWire(flags & 0x0F);

  // Skip optional fields
  if (flags & 0x10) {
    // ADV_LATLON_MASK: a single bit gates both lat and lon (4 bytes each)
    off += 8;
    if (off > decodedLen) return false;
  }
  if (flags & 0x20) {
    off += 2;
    if (off > decodedLen) return false;
  }  // feature 1
  if (flags & 0x40) {
    off += 2;
    if (off > decodedLen) return false;
  }  // feature 2

  // Remaining bytes are the node name (even without HAS_NAME flag,
  // some firmwares embed a name without setting the flag).
  size_t nameLen = decodedLen - off;
  if (nameLen > 32) nameLen = 32;  // MeshCoreContact::name is 32 chars + NUL

  memset(&out, 0, sizeof(out));
  memcpy(out.publicKey, pubkey, 32);
  if (nameLen > 0) {
    memcpy(out.name, pkt + off, nameLen);
    out.name[nameLen] = '\0';
    // Trim trailing whitespace / non-printable chars
    while (nameLen > 0 && (out.name[nameLen - 1] < ' ' || out.name[nameLen - 1] > '~')) {
      out.name[--nameLen] = '\0';
    }
  }
  out.type = nodeType;
  out.isSaved = true;
  return true;
}
