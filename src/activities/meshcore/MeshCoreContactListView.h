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
 * Renders the Contacts tab content within MeshCoreHubActivity.
 * Displays saved peer contacts; dialogs with unread messages are marked
 * with a leading meshcore::DotSeparator.
 *
 * order (may be null): maps a display position to an index into contacts[].
 * When null, identity order is used. The Hub feeds a sorted index (favourites
 * first, then last-message activity) so the list ranks by "who spoke last".
 *
 * Each row shows a meta-info subtitle mirroring the Discovered Nodes
 * list — "keyLabel · date/time · hops" — using the last *received*
 * message from the contact (read from the message store), not the
 * radio-level contact fields. Contacts with no received messages show
 * only their key label.
 */
class MeshCoreContactListView {
 public:
  static void render(const GfxRenderer& renderer, const Rect& contentRect, const MeshCoreContact* contacts,
                     uint16_t contactCount, const uint16_t* order, int selectedIndex, MeshCoreMessageStore& store) {
    if (contactCount == 0) {
      GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_CONTACTS));
      return;
    }

    auto mapIdx = [order](int index) -> uint16_t { return order ? order[index] : static_cast<uint16_t>(index); };

    GUI.drawList(
        renderer, contentRect, contactCount, selectedIndex - 1,
        [c = contacts, mapIdx](int index) {
          const char* n = c[mapIdx(index)].name;
          if (n[0] == '\0') n = tr(STR_MESHCORE_UNKNOWN);
          return meshcore::formatMeshCoreListTitle(c[mapIdx(index)].unreadCount, n);
        },
        [c = contacts, mapIdx, &store](int index) {
          char buf[52];
          char keyLabel[MeshCoreContact::PUBLIC_KEY_DISPLAY_LEN];
          c[mapIdx(index)].getPublicKeyLabel(keyLabel);

          MeshCoreMessage last;
          if (store.loadNewestReceivedDirectMessage(c[mapIdx(index)].publicKey, last)) {
            char ts[24] = "---";
            formatMeshCoreTimestamp(last.timestamp, ts, sizeof(ts));
            char hopBuf[24];
            meshcore::formatMeshCoreHopCount(last.pathLength, hopBuf, sizeof(hopBuf));
            snprintf(buf, sizeof(buf), "%s %s %s %s %s", keyLabel, meshcore::DotSeparator, ts, meshcore::DotSeparator,
                     hopBuf);
          } else {
            snprintf(buf, sizeof(buf), "%s", keyLabel);
          }
          return std::string(buf);
        });
  }
};
