#pragma once

#include <time.h>

#include <cstdio>

/**
 * Formats a Unix timestamp into a compact human-readable string.
 *
 * Output format: "12/Jan 14:45"  (day, 3-letter month, 24-hour HH:MM)
 *
 * The month is always English abbreviation regardless of locale.
 * Timestamp is interpreted as UTC.
 *
 * @param timestamp  Unix seconds since 1970-01-01 00:00 UTC
 * @param buf        Destination buffer (must be >= 13 chars)
 * @param bufSize    Size of destination buffer
 * @return true on success, false on invalid input
 */
inline bool formatMeshCoreTimestamp(uint32_t timestamp, char* buf, size_t bufSize) {
  if (!buf || bufSize < 13) return false;

  time_t t = static_cast<time_t>(timestamp);
  struct tm timeinfo;
  if (!gmtime_r(&t, &timeinfo)) return false;

  static constexpr const char* MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  int month = timeinfo.tm_mon;  // 0-11
  if (month < 0 || month > 11) month = 0;

  snprintf(buf, bufSize, "%d/%s %02d:%02d", timeinfo.tm_mday, MONTHS[month], timeinfo.tm_hour, timeinfo.tm_min);

  return true;
}
