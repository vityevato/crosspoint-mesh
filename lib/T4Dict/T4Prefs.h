#pragma once

#include <cstddef>
#include <cstdint>

namespace t4 {

/// Persisted T4 keyboard preferences.
///
/// These two fields change on a keypress (language cycle, Predict/Multi-tap
/// toggle) — including while a MeshCore BLE session is connected, the state in
/// which the heap is tightest. Keeping them in /settings.json made every
/// toggle rebuild the whole settings list (~10 KB of temporary heap) and could
/// abort the firmware on OOM. They live in their own 3-byte file instead; see
/// docs/file-formats.md.
struct T4Prefs {
  uint8_t userMode = 0;      ///< T4Mode: 0 = Predict, 1 = Multi-tap.
  uint8_t lastLanguage = 0;  ///< T4Language: 0 = EN, 1 = Additional, 2 = Digit.
};

// ── File format (see docs/file-formats.md) ──────────────────────────────
static constexpr uint8_t T4_PREFS_VERSION = 1;
static constexpr size_t T4_PREFS_FILE_SIZE = 3;
static constexpr const char* T4_PREFS_PATH = "/t4dicts/t4prefs.bin";

/// Serialize @p prefs into @p out.
/// @return bytes written (T4_PREFS_FILE_SIZE), or 0 when @p cap is too small.
inline size_t encodeT4Prefs(const T4Prefs& prefs, uint8_t* out, size_t cap) {
  if (!out || cap < T4_PREFS_FILE_SIZE) return 0;
  out[0] = T4_PREFS_VERSION;
  out[1] = prefs.userMode;
  out[2] = prefs.lastLanguage;
  return T4_PREFS_FILE_SIZE;
}

/// Parse a serialized buffer. Out-of-range values fall back to their defaults,
/// matching how CrossPointSettings::fromJson clamped the legacy JSON keys.
/// @return false when the buffer is truncated or the version is unknown.
inline bool decodeT4Prefs(const uint8_t* data, size_t len, T4Prefs& out) {
  if (!data || len < T4_PREFS_FILE_SIZE || data[0] != T4_PREFS_VERSION) return false;
  T4Prefs prefs;
  prefs.userMode = data[1] > 1 ? 0 : data[1];
  prefs.lastLanguage = data[2] > 2 ? 0 : data[2];
  out = prefs;
  return true;
}

/// Load prefs from T4_PREFS_PATH. Returns false when the file is missing or
/// invalid; the caller keeps its current values and may seed the file.
bool loadT4Prefs(T4Prefs& out);

/// Overwrite T4_PREFS_PATH with @p prefs. Heap-free SD write.
bool saveT4Prefs(const T4Prefs& prefs);

}  // namespace t4
