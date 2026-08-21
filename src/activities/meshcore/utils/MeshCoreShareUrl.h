#pragma once

#include <MeshCore/MeshCoreTypes.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace meshcore {

/// Build a `meshcore://contact/add` share link (QR / file format) matching
/// the MeshCore mobile-app convention (see MeshCore docs/qr_codes.md):
///
///   meshcore://contact/add?name=<name>&public_key=<64hex>&type=<N>
///
/// The name is URL-encoded (space -> '+', unreserved characters kept,
/// everything else percent-encoded byte by byte). The node type is mapped
/// into the QR-code numeric space: Companion=1, Repeater=2, RoomServer=3,
/// Sensor=4.
///
/// @return Number of bytes written (excluding the trailing NUL), or 0 if
///         the URL would not fit in `bufSize`.
inline size_t buildMeshCoreContactShareUrl(const char* name, const uint8_t* pubkey32, MeshNodeType nodeType, char* buf,
                                           size_t bufSize) {
  int typeCode = 1;
  switch (nodeType) {
    case MeshNodeType::COMPANION:
      typeCode = 1;
      break;
    case MeshNodeType::REPEATER:
      typeCode = 2;
      break;
    case MeshNodeType::ROOM_SERVER:
      typeCode = 3;
      break;
    case MeshNodeType::SENSOR:
      typeCode = 4;
      break;
    default:
      typeCode = 1;
      break;
  }

  size_t n = 0;
  auto append = [&](const char* s, size_t len) -> bool {
    if (n + len + 1 > bufSize) return false;
    memcpy(buf + n, s, len);
    n += len;
    return true;
  };

  static constexpr const char kPrefix[] = "meshcore://contact/add?name=";
  if (!append(kPrefix, sizeof(kPrefix) - 1)) {
    if (bufSize > 0) buf[0] = '\0';
    return 0;
  }

  // URL-encode the name
  const char* nptr = (name != nullptr) ? name : "";
  for (const char* p = nptr; *p != '\0'; ++p) {
    const char c = *p;
    if (c == ' ') {
      if (!append("+", 1)) return 0;
    } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
               c == '.') {
      if (!append(&c, 1)) return 0;
    } else {
      char enc[4] = {};
      snprintf(enc, sizeof(enc), "%%%02X", static_cast<unsigned char>(c));
      if (!append(enc, 3)) return 0;
    }
  }

  static constexpr const char kKeyParam[] = "&public_key=";
  if (!append(kKeyParam, sizeof(kKeyParam) - 1)) return 0;

  // Public key as 64 lower-case hex chars
  for (int i = 0; i < 32; ++i) {
    char hex[3] = {};
    snprintf(hex, sizeof(hex), "%02x", pubkey32[i]);
    if (!append(hex, 2)) return 0;
  }

  char typeParam[12] = {};
  int typeLen = snprintf(typeParam, sizeof(typeParam), "&type=%d", typeCode);
  if (typeLen <= 0 || !append(typeParam, static_cast<size_t>(typeLen))) return 0;

  buf[n] = '\0';
  return n;
}

}  // namespace meshcore
