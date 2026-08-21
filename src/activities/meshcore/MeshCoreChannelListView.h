#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "MeshCore/MeshCoreTypes.h"
#include "components/UITheme.h"
#include "utils/MeshCoreDisplayUtils.h"

/**
 * Renders the Channels tab content within MeshCoreHubActivity.
 * Displays only configured LoRa channels (visibleIdx maps list
 * positions to raw channel indices); channels with unread messages
 * are marked with a leading meshcore::DotSeparator.
 */
class MeshCoreChannelListView {
 public:
  static void render(const GfxRenderer& renderer, const Rect& contentRect, const MeshCoreChannel* channels,
                     const uint8_t* visibleIdx, uint8_t visibleCount, int selectedIndex) {
    if (visibleCount == 0) {
      GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_CHANNELS));
      return;
    }

    GUI.drawList(renderer, contentRect, visibleCount, selectedIndex - 1, [ch = channels, vi = visibleIdx](int index) {
      const uint8_t chIdx = vi[index];
      return meshcore::formatMeshCoreListTitle(ch[chIdx].unreadCount, ch[chIdx].name);
    });
  }
};
