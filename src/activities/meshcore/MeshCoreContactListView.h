#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <string>

#include "MeshCore/MeshCoreTypes.h"
#include "components/UITheme.h"

/**
 * Renders the Contacts tab content within MeshCoreHubActivity.
 * Displays saved peer contacts with unread counts.
 */
class MeshCoreContactListView {
 public:
  static void render(GfxRenderer& renderer, const Rect& contentRect, const MeshCoreContact* contacts,
                     uint8_t contactCount, int selectedIndex) {
    if (contactCount == 0) {
      GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_CONTACTS));
      return;
    }

    GUI.drawList(
        renderer, contentRect, contactCount, selectedIndex - 1,
        [c = contacts](int index) { return std::string(c[index].name); },
        [c = contacts](int index) {
          if (c[index].unreadCount > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "(%d)", c[index].unreadCount);
            return std::string(buf);
          }
          return std::string();
        });
  }
};
