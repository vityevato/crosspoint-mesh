#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <string>

#include "MeshCore/MeshCoreTypes.h"
#include "components/UITheme.h"

/**
 * Renders the Channels tab content within MeshCoreHubActivity.
 * Displays a list of configured LoRa channels with unread counts.
 */
class MeshCoreChannelListView {
 public:
  static void render(GfxRenderer& renderer, const Rect& contentRect, const MeshCoreChannel* channels,
                     uint8_t channelCount, int selectedIndex) {
    bool hasChannels = false;
    for (uint8_t i = 0; i < channelCount; ++i) {
      if (channels[i].configured) {
        hasChannels = true;
        break;
      }
    }

    if (!hasChannels) {
      GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_CHANNELS));
      return;
    }

    GUI.drawList(
        renderer, contentRect, channelCount, selectedIndex - 1,
        [ch = channels](int index) { return std::string(ch[index].name[0] ? ch[index].name : "---"); },
        [ch = channels](int index) {
          if (ch[index].unreadCount > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "(%d)", ch[index].unreadCount);
            return std::string(buf);
          }
          return std::string();
        });
  }
};
