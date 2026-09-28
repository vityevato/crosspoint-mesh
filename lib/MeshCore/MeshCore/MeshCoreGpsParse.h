#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Pure helpers for parsing MeshCore GPS-related companion responses.
// Header-only and free of Arduino/Logging dependencies so the logic can be
// unit-tested on the host (see test/meshcore_gps).

namespace MeshProto {

// CayenneLPP data type for GPS (lat/lon 0.0001 deg, alt 0.01 m).
// Mirrors MeshCore/src/helpers/sensors/LPPDataHelpers.h:29
static constexpr uint8_t LPP_GPS = 136;
// LPP payload size for a GPS entry: 3-byte lat + 3-byte lon + 3-byte alt.
static constexpr size_t LPP_GPS_PAYLOAD_LEN = 9;
// LPP channel used by the companion for its own device ('self').
// Mirrors MeshCore/src/helpers/SensorManager.h:10
static constexpr uint8_t TELEM_CHANNEL_SELF = 1;

// Payload size (bytes after the [channel][type] header) for a CayenneLPP
// data type. Exact copy of the companion firmware's skipData() table
// (MeshCore/src/helpers/sensors/LPPDataHelpers.h:140), so unknown entries
// can be skipped without desynchronising the scan.
inline size_t lppDataSize(uint8_t type) {
  switch (type) {
    case 0:    // LPP_DIGITAL_INPUT
    case 1:    // LPP_DIGITAL_OUTPUT
    case 102:  // LPP_PRESENCE
    case 104:  // LPP_RELATIVE_HUMIDITY
      return 1;
    case 2:    // LPP_ANALOG_INPUT
    case 3:    // LPP_ANALOG_OUTPUT
    case 101:  // LPP_LUMINOSITY
    case 103:  // LPP_TEMPERATURE
    case 115:  // LPP_BAROMETRIC_PRESSURE
    case 116:  // LPP_VOLTAGE
    case 117:  // LPP_CURRENT
    case 121:  // LPP_ALTITUDE
    case 125:  // LPP_CONCENTRATION
    case 128:  // LPP_POWER
    case 132:  // LPP_DIRECTION
      return 2;
    case 135:  // LPP_COLOUR
      return 3;
    case 100:  // LPP_GENERIC_SENSOR
    case 118:  // LPP_FREQUENCY
    case 130:  // LPP_DISTANCE
    case 131:  // LPP_ENERGY
    case 133:  // LPP_UNIXTIME
      return 4;
    case 113:  // LPP_ACCELEROMETER
    case 134:  // LPP_GYROMETER
      return 6;
    case 240:  // LPP_POLYLINE (minimum — firmware skips 8)
      return 8;
    case LPP_GPS:
      return LPP_GPS_PAYLOAD_LEN;
    default:
      return 1;  // firmware default: _pos++
  }
}

// Read a signed 24-bit big-endian value (CayenneLPP 3-byte coordinates).
inline int32_t readInt24(const uint8_t* p) {
  int32_t v = (static_cast<int32_t>(p[0]) << 16) | (static_cast<int32_t>(p[1]) << 8) | static_cast<int32_t>(p[2]);
  if (v & 0x800000) v |= static_cast<int32_t>(0xFF000000);  // sign-extend
  return v;
}

// Parse the RESP_CODE_CUSTOM_VARS payload and extract the GPS setting.
// Wire format: [packet type][ASCII "name:value,name:value,..."], where the
// companion emits "gps:0" or "gps:1" only when a GPS module was detected
// (MyMesh.cpp CMD_GET_CUSTOM_VARS + EnvironmentSensorManager::getSettingName).
// A bare type byte (len == 1) is a valid "no custom vars" reply.
// Returns false only when the frame is empty.
inline bool parseGpsCustomVars(const uint8_t* data, size_t len, bool& hasGps, bool& gpsEnabled) {
  hasGps = false;
  gpsEnabled = false;
  if (len < 1) return false;

  size_t off = 1;  // skip packet type byte
  while (off < len) {
    const size_t nameStart = off;
    while (off < len && data[off] != ':' && data[off] != ',') off++;
    if (off >= len || data[off] != ':') {
      // Malformed token (no separator) — skip to the next comma.
      while (off < len && data[off] != ',') off++;
      if (off < len) off++;
      continue;
    }
    const size_t nameLen = off - nameStart;
    off++;  // skip ':'

    const size_t valueStart = off;
    while (off < len && data[off] != ',') off++;
    const size_t valueLen = off - valueStart;
    if (off < len) off++;  // skip ','

    if (nameLen == 3 && memcmp(data + nameStart, "gps", 3) == 0) {
      hasGps = true;
      gpsEnabled = (valueLen >= 1 && data[valueStart] == '1');
      return true;
    }
  }
  return true;  // parsed, but no GPS key present
}

// Scan a CayenneLPP buffer for the first GPS entry. Sets hasGpsEntry when a
// GPS entry was found and decoded (values may still be 0/0 before the receiver
// has a fix — the protocol carries no fix-validity flag).
// Coordinates are returned as double so the 4-decimal LPP resolution prints
// without float rounding noise (e.g. 55.755800, not 55.755798).
// Returns false on a truncated/malformed buffer.
inline bool parseLppGps(const uint8_t* data, size_t len, double& lat, double& lon, double& alt, bool& hasGpsEntry) {
  hasGpsEntry = false;
  size_t off = 0;
  while (off + 2 <= len) {
    const uint8_t channel = data[off];
    if (channel == 0) break;  // channel 0 = end-of-data marker
    const uint8_t type = data[off + 1];
    off += 2;

    const size_t size = lppDataSize(type);
    if (off + size > len) return false;  // truncated entry

    if (type == LPP_GPS && size == LPP_GPS_PAYLOAD_LEN) {
      lat = static_cast<double>(readInt24(data + off)) / 10000.0;
      lon = static_cast<double>(readInt24(data + off + 3)) / 10000.0;
      alt = static_cast<double>(readInt24(data + off + 6)) / 100.0;
      hasGpsEntry = true;
      return true;
    }
    off += size;
  }
  return true;  // scanned, no GPS entry
}

// Parse a PUSH_CODE_TELEMETRY_RESPONSE frame:
// [0] packet type, [1] reserved, [2..7] sender pubkey prefix, [8..] LPP data.
// When selfPubKey is non-null, responses from any other node are rejected.
inline bool parseTelemetryResponse(const uint8_t* data, size_t len, const uint8_t* selfPubKey, double& lat, double& lon,
                                   double& alt, bool& hasGpsEntry) {
  hasGpsEntry = false;
  static constexpr size_t TELEMETRY_HEADER_LEN = 8;
  if (len < TELEMETRY_HEADER_LEN) return false;
  if (selfPubKey != nullptr && memcmp(data + 2, selfPubKey, 6) != 0) return false;
  return parseLppGps(data + TELEMETRY_HEADER_LEN, len - TELEMETRY_HEADER_LEN, lat, lon, alt, hasGpsEntry);
}

}  // namespace MeshProto
