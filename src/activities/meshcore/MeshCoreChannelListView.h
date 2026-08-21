#pragma once

#include <GfxRenderer.h>
#include <I18n.h>
#include <MeshCore/MeshCoreMessageStore.h>

#include <string>

#include "MeshCore/MeshCoreTypes.h"
#include "components/UITheme.h"
#include "utils/MeshCoreDisplayUtils.h"
#include "utils/MeshCoreTimeUtils.h"

/**
 * Renders the Channels tab content within MeshCoreHubActivity.
 * Displays only configured LoRa channels (visibleIdx maps list
 * positions to raw channel indices); channels with unread messages
 * are marked with a leading meshcore::DotSeparator.
 *
 * Each row shows a date/time subtitle of the last message in the
 * channel (read from the message store), regardless of direction.
 * Channels with no messages show an empty subtitle.
 */
class MeshCoreChannelListView {
 public:
  static void render(const GfxRenderer& renderer, const Rect& contentRect, const MeshCoreChannel* channels,
                     const uint8_t* visibleIdx, uint8_t visibleCount, int selectedIndex, MeshCoreMessageStore& store) {
    if (visibleCount == 0) {
      GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_CHANNELS));
      return;
    }

    GUI.drawList(
        renderer, contentRect, visibleCount, selectedIndex - 1,
        [ch = channels, vi = visibleIdx](int index) {
          const uint8_t chIdx = vi[index];
          return meshcore::formatMeshCoreListTitle(ch[chIdx].unreadCount, ch[chIdx].name);
        },
        [ch = channels, vi = visibleIdx, &store](int index) {
          const uint8_t chIdx = vi[index];
          char buf[26] = "";
          MeshCoreMessage last;
          char ts[24] = "";
          if (store.loadNewestChannelMessage(chIdx, last) && formatMeshCoreTimestamp(last.timestamp, ts, sizeof(ts))) {
            snprintf(buf, sizeof(buf), "%s%s", meshcore::ListSlotBlank, ts);
            return std::string(buf);
          }
          return std::string("");
        });
  }
};
