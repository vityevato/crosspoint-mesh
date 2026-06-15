#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <functional>
#include <string>

#include "components/UITheme.h"

/**
 * Renders the Menu tab content within MeshCoreHubActivity.
 * Displays 5 secondary actions. Items that require a connected
 * companion (Send Advert, Send Flood Advert, Disconnect) are
 * dimmed when the companion is disconnected.
 *
 * Item indices (0-based, relative to menu list):
 *   0 = Discovery Nodes (navigation)
 *   1 = Send Advert       (action, requires connected)
 *   2 = Send Flood Advert (action, requires connected)
 *   3 = Status            (navigation)
 *   4 = Disconnect        (action, requires connected)
 */
class MeshCoreMenuView {
 public:
  static void render(const GfxRenderer& renderer, const Rect& contentRect, int selectedIndex, bool isConnected) {
    // All 5 items are always present — no empty state needed.
    constexpr int kItemCount = 5;

    GUI.drawList(
        renderer, contentRect, kItemCount, selectedIndex - 1,
        /*rowTitle*/
        [](int index) -> std::string {
          switch (index) {
            case 0:
              return tr(STR_MESHCORE_DISCOVERY_NODES);
            case 1:
              return tr(STR_MESHCORE_SEND_ADVERT);
            case 2:
              return tr(STR_MESHCORE_SEND_FLOOD_ADVERT);
            case 3:
              return tr(STR_MESHCORE_STATUS);
            case 4:
              return tr(STR_MESHCORE_DISCONNECT);
            default:
              return {};
          }
        },
        /*rowSubtitle*/ nullptr,
        /*rowIcon*/ nullptr,
        /*rowValue*/ nullptr,
        /*highlightValue*/ false,
        /*rowDimmed*/
        [isConnected](int index) -> bool {
          // Items 1, 2, 4 require a connected companion
          if (isConnected) return false;
          return (index == 1 || index == 2 || index == 4);
        });
  }
};
