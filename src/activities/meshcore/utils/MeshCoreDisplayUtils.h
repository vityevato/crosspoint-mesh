#pragma once

namespace meshcore {

// Separator with middle dot for use in lists and status lines
static constexpr const char* DotSeparator = "·";

/**
 * Formats a path length (hop count) into a readable string.
 *
 * In the MeshCore protocol, 0xFF (255) is a sentinel meaning "direct" —
 * the message was sent directly to a neighbor without mesh flooding.
 *
 * @param pathLength  Number of hops (0xFF = direct)
 * @param buf         Destination buffer (must be >= 12 chars)
 * @param bufSize     Size of destination buffer
 */
inline void formatMeshCoreHopCount(uint8_t pathLength, char* buf, size_t bufSize) {
  if (pathLength == 0xFF) {
    snprintf(buf, bufSize, "direct");
  } else if (pathLength == 0) {
    snprintf(buf, bufSize, "direct");
  } else {
    snprintf(buf, bufSize, "%d hops", pathLength);
  }
}

/**
 * Formats the number of repeats (refloods) a sent channel message received.
 *
 * @param pathLength  Number of repeats
 * @param buf         Destination buffer (must be >= 24 chars)
 * @param bufSize     Size of destination buffer
 */
inline void formatMeshCoreHeardRepeats(uint8_t pathLength, char* buf, size_t bufSize) {
  if (pathLength == 0) {
    snprintf(buf, bufSize, "Not heard yet");
  } else if (pathLength == 1) {
    snprintf(buf, bufSize, "Heard 1 Repeat");
  } else {
    snprintf(buf, bufSize, "Heard %d Repeats", pathLength);
  }
}

}  // namespace meshcore
