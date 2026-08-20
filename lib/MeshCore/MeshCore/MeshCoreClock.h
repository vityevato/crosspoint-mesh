#pragma once

#include <Arduino.h>
#include <HalClock.h>

#include <time.h>

/**
 * Returns the current time as a Unix epoch timestamp in seconds (UTC).
 *
 * MeshCore protocol timestamps are Unix UTC epoch seconds (see the companion
 * protocol docs), so message send/delivery times must be real UTC, not uptime.
 *
 * Source precedence:
 *   1. SNTP-synced system clock (configTzTime("UTC0", ...)) — already UTC.
 *   2. RTC, which the firmware keeps in UTC (NTP sync writes UTC0).
 *   3. Uptime seconds as a degraded fallback on clockless devices (e.g. X4).
 */
inline uint32_t meshcoreNowUtc() {
  const time_t now = time(nullptr);
  if (now > 0) return static_cast<uint32_t>(now);

  uint32_t epoch = 0;
  if (halClock.getEpochUtc(epoch)) return epoch;

  return static_cast<uint32_t>(millis() / 1000);
}
