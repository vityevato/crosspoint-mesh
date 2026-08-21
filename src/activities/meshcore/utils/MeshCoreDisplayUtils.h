#pragma once

#include <I18n.h>

#include <cstdio>
#include <string>

namespace meshcore {

// Separator with middle dot for use in lists and status lines
static constexpr const char* DotSeparator = "·";

// Blank marker slot, same width as "· " used by formatMeshCoreListTitle.
// Row subtitles must start with it so their text lines up under the row
// title's name (the title is drawn with a fixed 2-char unread-marker slot
// prepended to the name).
static constexpr const char* ListSlotBlank = "  ";

/**
 * Builds a dialog list row title with a fixed-width unread-marker slot so
 * names always start at the same position whether or not a dialogue has
 * unread messages (a leading DotSeparator for unread, an equal-width blank
 * placeholder otherwise). The marker slot ("· " / double space) measures
 * identically in the list fonts, so dialog names do not jump when the
 * unread state changes.
 *
 * @param unreadCount  Number of unread messages
 * @param name         Dialog display name
 * @return  Title string with the fixed-width marker slot prepended
 */
inline std::string formatMeshCoreListTitle(uint16_t unreadCount, const char* name) {
  const char* marker = (unreadCount > 0) ? DotSeparator : " ";
  return std::string(marker) + " " + name;
}

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
