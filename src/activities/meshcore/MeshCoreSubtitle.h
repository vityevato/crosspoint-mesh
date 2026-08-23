#pragma once

#include <I18n.h>
#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdio>

#include "utils/MeshCoreDisplayUtils.h"

/**
 * Formats the MeshCore connection status subtitle into a buffer.
 *
 * When connected:   "<battery%> - <node name>"
 * When disconnected: localized "Disconnected" string.
 *
 * Battery percentage is derived from companion.batteryMv with a
 * linear mapping: 3.2V = 0%, 4.2V = 100%.
 */
inline void formatMeshCoreSubtitle(const MeshCoreClient& client, char* buf, size_t bufSize) {
  if (client.getState() == BleConnectionState::CONNECTED) {
    const auto& comp = client.getCompanion();
    int battPct = 0;
    if (comp.batteryMv > 3200) {
      battPct = static_cast<int>(comp.batteryMv - 3200) / 10;
      if (battPct > 100) battPct = 100;
    }
    const char* name = comp.name[0] != '\0' ? comp.name : "???";
    snprintf(buf, bufSize, "%d%% %s %s", battPct, meshcore::DotSeparator, name);
  } else {
    snprintf(buf, bufSize, "%s", tr(STR_MESHCORE_DISCONNECTED));
  }
}
