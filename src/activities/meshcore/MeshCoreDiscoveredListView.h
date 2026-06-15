#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <string>

#include "MeshCore/MeshCoreTypes.h"
#include "components/UITheme.h"

/**
 * Renders the Discovered tab content within MeshCoreHubActivity.
 * Displays advertised nodes with hop count and signal info.
 * Already-saved contacts are dimmed.
 */
class MeshCoreDiscoveredListView {
 public:
  static void render(GfxRenderer& renderer, const Rect& contentRect, const MeshCoreContact* nodes, uint8_t nodeCount,
                     const MeshCoreContact* savedContacts, uint8_t savedCount, int selectedIndex) {
    if (nodeCount == 0) {
      GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_DEVICES));
      return;
    }

    GUI.drawList(
        renderer, contentRect, nodeCount, selectedIndex - 1,
        [n = nodes](int index) { return std::string(n[index].name); },
        [n = nodes](int index) {
          char buf[32];
          char prefix[13];
          n[index].getPublicKeyPrefix(prefix);
          snprintf(buf, sizeof(buf), "%s  %dhop", prefix, n[index].pathLength);
          return std::string(buf);
        },
        nullptr, nullptr, false,
        [n = nodes, s = savedContacts, sc = savedCount](int index) {
          for (uint8_t i = 0; i < sc; ++i) {
            if (memcmp(s[i].publicKey, n[index].publicKey, 32) == 0) return true;
          }
          return false;
        });
  }
};
