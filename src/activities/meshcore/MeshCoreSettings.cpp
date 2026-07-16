#include "MeshCoreSettings.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

namespace meshcore_settings {

bool load(MeshCoreSettings& out) {
  if (!Storage.exists(kFilePath)) {
    // File doesn't exist yet — use defaults
    return true;
  }

  String json = Storage.readFile(kFilePath);
  if (json.isEmpty()) {
    LOG_ERR("MESH", "Failed to read %s", kFilePath);
    return false;
  }

  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("MESH", "JSON parse error in %s: %s", kFilePath, error.c_str());
    return false;
  }

  out.useReaderFont = doc["useReaderFont"] | true;
  return true;
}

bool save(const MeshCoreSettings& settings) {
  Storage.mkdir("/.crosspoint/meshcore");

  JsonDocument doc;
  doc["useReaderFont"] = settings.useReaderFont;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(kFilePath, json);
}

}  // namespace meshcore_settings
