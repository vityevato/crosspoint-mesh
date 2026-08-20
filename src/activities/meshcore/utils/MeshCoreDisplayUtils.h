#pragma once

#include <I18n.h>

#include <cstdio>

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
 * @param buf         Destination buffer (must fit the localized strings)
 * @param bufSize     Size of destination buffer
 */
inline void formatMeshCoreHopCount(uint8_t pathLength, char* buf, size_t bufSize) {
  if (pathLength == 0xFF || pathLength == 0) {
    snprintf(buf, bufSize, "%s", tr(STR_MESHCORE_MSG_DIRECT));
  } else {
    snprintf(buf, bufSize, tr(STR_MESHCORE_MSG_HOPS), pathLength);
  }
}

/**
 * Formats the number of repeats (refloods) a sent channel message received.
 *
 * @param pathLength  Number of repeats
 * @param buf         Destination buffer (must fit the localized strings)
 * @param bufSize     Size of destination buffer
 */
inline void formatMeshCoreHeardRepeats(uint8_t pathLength, char* buf, size_t bufSize) {
  if (pathLength == 0) {
    snprintf(buf, bufSize, "%s", tr(STR_MESHCORE_MSG_SENT));
  } else if (pathLength == 1) {
    snprintf(buf, bufSize, "%s", tr(STR_MESHCORE_MSG_HEARD_ONE));
  } else {
    snprintf(buf, bufSize, tr(STR_MESHCORE_MSG_HEARD_MANY), pathLength);
  }
}

}  // namespace meshcore
