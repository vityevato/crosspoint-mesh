#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

// Recent-sender cache for the channel reply picker (senders.bin).
//
// The list holds the most recent distinct channel senders, newest first, in
// fixed 64-byte slots. The helpers here are pure so the ordering/dedupe rules
// stay host-testable (see test/meshcore_senders).
static constexpr uint8_t MESHCORE_MAX_RECENT_SENDERS = 8;

namespace meshcore {

/// Adds `name` as the newest entry: moves it to the front, drops the previous
/// occurrence, and caps the list at maxNames. Returns the new count. No-op
/// when `name` is already the front entry.
inline uint8_t recentSendersNote(char (*names)[64], uint8_t count, uint8_t maxNames, const char* name) {
  if (names == nullptr || name == nullptr || name[0] == '\0' || maxNames == 0) return count;
  if (count > maxNames) count = maxNames;
  if (count == 0) {
    snprintf(names[0], 64, "%s", name);
    return 1;
  }
  if (strcmp(names[0], name) == 0) return count;  // already newest

  // Find the previous occurrence (k == count when the name is new).
  uint8_t k = 1;
  while (k < count && strcmp(names[k], name) != 0) ++k;

  // Shift names[0..shiftTo-1] right by one, overwriting the occurrence (or
  // the capped tail when the name is new).
  const uint8_t shiftTo = (k < maxNames) ? k : static_cast<uint8_t>(maxNames - 1);
  for (uint8_t i = shiftTo; i > 0; --i) {
    memcpy(names[i], names[i - 1], 64);
  }
  snprintf(names[0], 64, "%s", name);
  if (k < count) return count;  // replaced the previous occurrence
  return (count < maxNames) ? static_cast<uint8_t>(count + 1) : count;
}

/// Appends `name` if it is not present yet and there is room. Used by the
/// one-time backfill, whose scan order is already newest-first.
inline uint8_t recentSendersAppend(char (*names)[64], uint8_t count, uint8_t maxNames, const char* name) {
  if (names == nullptr || name == nullptr || name[0] == '\0' || count >= maxNames) return count;
  for (uint8_t i = 0; i < count; ++i) {
    if (strcmp(names[i], name) == 0) return count;
  }
  snprintf(names[count], 64, "%s", name);
  return static_cast<uint8_t>(count + 1);
}

}  // namespace meshcore
