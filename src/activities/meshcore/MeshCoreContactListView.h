#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "MeshCore/MeshCoreTypes.h"
#include "components/UITheme.h"
#include "utils/MeshCoreDisplayUtils.h"

/**
 * Renders the Contacts tab content within MeshCoreHubActivity.
 * Displays saved peer contacts; dialogs with unread messages are marked
 * with a leading meshcore::DotSeparator.
 */
class MeshCoreContactListView {
 public:
  static void render(const GfxRenderer& renderer, const Rect& contentRect, const MeshCoreContact* contacts,
                     uint8_t contactCount, int selectedIndex) {
    if (contactCount == 0) {
      GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_CONTACTS));
      return;
    }

    GUI.drawList(renderer, contentRect, contactCount, selectedIndex - 1, [c = contacts](int index) {
      const char* n = c[index].name;
      if (n[0] == '\0') n = tr(STR_MESHCORE_UNKNOWN);
      return meshcore::formatMeshCoreListTitle(c[index].unreadCount, n);
    });
  }
};
