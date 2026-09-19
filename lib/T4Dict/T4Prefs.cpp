#include "T4Prefs.h"

#include <HalStorage.h>
#include <Logging.h>

namespace t4 {

bool loadT4Prefs(T4Prefs& out) {
  HalFile file;
  if (!Storage.openFileForRead("T4", T4_PREFS_PATH, file)) {
    return false;  // no file yet — expected on the first run
  }
  uint8_t buf[T4_PREFS_FILE_SIZE];
  if (file.read(buf, sizeof(buf)) != static_cast<int>(sizeof(buf))) {
    LOG_ERR("T4", "Short read on T4 prefs");
    return false;
  }
  return decodeT4Prefs(buf, sizeof(buf), out);
}

bool saveT4Prefs(const T4Prefs& prefs) {
  uint8_t buf[T4_PREFS_FILE_SIZE];
  const size_t len = encodeT4Prefs(prefs, buf, sizeof(buf));
  if (len == 0) return false;

  Storage.mkdir("/t4dicts");
  HalFile file;
  if (!Storage.openFileForWrite("T4", T4_PREFS_PATH, file)) {
    LOG_ERR("T4", "Failed to open T4 prefs for writing");
    return false;
  }
  if (file.write(buf, len) != len) {
    LOG_ERR("T4", "Short write on T4 prefs");
    return false;
  }
  return true;
}

}  // namespace t4
