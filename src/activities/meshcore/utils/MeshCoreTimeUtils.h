#pragma once

#include <I18n.h>
#include <MeshCore/MeshCoreClock.h>
#include <time.h>

#include <cstdint>
#include <cstdio>

#include "CrossPointSettings.h"

namespace {
// Month abbreviations are localized via the i18n system; languages without
// month translations fall back to English via the generator.
static constexpr StrId MESHCORE_MONTH_KEYS[12] = {StrId::STR_MONTH_JAN, StrId::STR_MONTH_FEB, StrId::STR_MONTH_MAR,
                                                  StrId::STR_MONTH_APR, StrId::STR_MONTH_MAY, StrId::STR_MONTH_JUN,
                                                  StrId::STR_MONTH_JUL, StrId::STR_MONTH_AUG, StrId::STR_MONTH_SEP,
                                                  StrId::STR_MONTH_OCT, StrId::STR_MONTH_NOV, StrId::STR_MONTH_DEC};
}  // namespace

/**
 * Unix timestamp shifted to the user's local timezone (clamped clock-offset
 * setting). Shared by all local-time formatting in the MeshCore UI.
 */
inline time_t meshcoreLocalTime(uint32_t timestamp) {
  uint8_t offsetQ = SETTINGS.clockUtcOffsetQ;
  if (offsetQ > 104) offsetQ = 104;  // clamp corrupted persisted values
  const int offsetQuarterHours = static_cast<int>(offsetQ) - 48;
  return static_cast<time_t>(timestamp) + static_cast<time_t>(offsetQuarterHours) * 15 * 60;
}

/**
 * Local-day key of a Unix timestamp (same timezone shift as
 * meshcoreLocalTime). Two messages belong to the same calendar day iff their
 * keys are equal. Returns 0 when the shifted time falls before the epoch.
 */
inline uint32_t meshcoreLocalDayOf(uint32_t timestamp) {
  const int64_t shifted = static_cast<int64_t>(meshcoreLocalTime(timestamp));
  return shifted >= 0 ? static_cast<uint32_t>(shifted / 86400) : 0;
}

/**
 * Floor for "plausible" Unix timestamps (seconds since the epoch, UTC).
 *
 * Devices without an RTC start their clock at the 1970 epoch and count up
 * from boot/uptime, so locally-generated timestamps are epoch-era garbage.
 * A message timestamp below this floor is treated as "no valid clock" and
 * never rendered.
 */
static constexpr uint32_t MESHCORE_MIN_PLAUSIBLE_TIME_UTC = 1704067200;  // 2024-01-01 UTC

/** True when ts is high enough to be a real wall-clock time (post-2023). */
inline bool meshcoreIsPlausibleTime(uint32_t ts) { return ts >= MESHCORE_MIN_PLAUSIBLE_TIME_UTC; }

/**
 * Formats a Unix timestamp into a date-only string for day dividers.
 *
 * Output format: "1/Jan 2026"  (day, 3-letter month, 4-digit year)
 *
 * The month abbreviation is localized via the i18n system (month keys),
 * falling back to English for languages without month translations. The
 * local timezone shift matches meshcoreLocalTime (user's UTC-offset setting).
 *
 * @param timestamp  Unix seconds since 1970-01-01 00:00 UTC
 * @param buf        Destination buffer (must be >= 24 chars)
 * @param bufSize    Size of destination buffer
 * @return true on success, false on invalid input or an implausible
 *         (pre-2024 / epoch-era) timestamp that must not be rendered
 */
inline bool formatMeshCoreDate(uint32_t timestamp, char* buf, size_t bufSize) {
  if (!buf || bufSize < 24) return false;
  if (!meshcoreIsPlausibleTime(timestamp)) return false;

  time_t t = meshcoreLocalTime(timestamp);

  struct tm timeinfo;
  if (!gmtime_r(&t, &timeinfo)) return false;

  int month = timeinfo.tm_mon;  // 0-11
  if (month < 0 || month > 11) month = 0;

  const char* monthName = I18n::getInstance().get(MESHCORE_MONTH_KEYS[month]);

  snprintf(buf, bufSize, "%d/%s %d", timeinfo.tm_mday, monthName, timeinfo.tm_year + 1900);

  return true;
}

/**
 * Formats a Unix timestamp into a compact human-readable string.
 *
 * Output format: "12/Jan 14:45"  (day, 3-letter month, 24-hour HH:MM)
 *
 * The month abbreviation is localized via the i18n system (month keys),
 * falling back to English for languages without month translations.
 * Timestamps are stored/sent in UTC; the rendered time is shifted to the
 * user's local timezone using the UTC-offset setting (SETTINGS.clockUtcOffsetQ,
 * biased quarter-hours, 48 = UTC+0).
 *
 * @param timestamp  Unix seconds since 1970-01-01 00:00 UTC
 * @param buf        Destination buffer (must be >= 24 chars)
 * @param bufSize    Size of destination buffer
 * @return true on success, false on invalid input or an implausible
 *         (pre-2024 / epoch-era) timestamp that must not be rendered
 */
inline bool formatMeshCoreTimestamp(uint32_t timestamp, char* buf, size_t bufSize) {
  if (!buf || bufSize < 24) return false;
  if (!meshcoreIsPlausibleTime(timestamp)) return false;

  time_t t = meshcoreLocalTime(timestamp);

  struct tm timeinfo;
  if (!gmtime_r(&t, &timeinfo)) return false;

  int month = timeinfo.tm_mon;  // 0-11
  if (month < 0 || month > 11) month = 0;

  const char* monthName = I18n::getInstance().get(MESHCORE_MONTH_KEYS[month]);

  snprintf(buf, bufSize, "%d/%s %02d:%02d", timeinfo.tm_mday, monthName, timeinfo.tm_hour, timeinfo.tm_min);

  return true;
}
