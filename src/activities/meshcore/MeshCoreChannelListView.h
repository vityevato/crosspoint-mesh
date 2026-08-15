#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <string>

#include "MeshCore/MeshCoreTypes.h"
#include "components/UITheme.h"

/**
 * Renders the Channels tab content within MeshCoreHubActivity.
 * Displays only configured LoRa channels (visibleIdx maps list
 * positions to raw channel indices) with unread counts.
 */
class MeshCoreChannelListView {
 public:
  static void render(const GfxRenderer& renderer, const Rect& contentRect, const MeshCoreChannel* channels,
                     const uint8_t* visibleIdx, uint8_t visibleCount, int selectedIndex) {
    if (visibleCount == 0) {
      GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_CHANNELS));
      return;
    }

    GUI.drawList(
        renderer, contentRect, visibleCount, selectedIndex - 1,
        [ch = channels, vi = visibleIdx](int index) { return std::string(ch[vi[index]].name); },
        [ch = channels, vi = visibleIdx](int index) {
          if (ch[vi[index]].unreadCount > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "(%d)", ch[vi[index]].unreadCount);
            return std::string(buf);
          }
          return std::string();
        });
  }
};
