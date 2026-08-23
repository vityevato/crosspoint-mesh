#pragma once
#include <cstdint>

/**
 * POD settings for MeshCore thread UI. Not a singleton — loaded on demand
 * (e.g. when the MENU tab is opened) and freed when the tab is left or the
 * activity exits. This keeps heap usage minimal.
 */
struct MeshCoreSettings {
  bool useReaderFont = true;  // Use reader font settings in conversation view
};

namespace meshcore_settings {

bool load(MeshCoreSettings& out);
bool save(const MeshCoreSettings& settings);

constexpr const char* kFilePath = "/.crosspoint/meshcore/settings.json";

}  // namespace meshcore_settings
