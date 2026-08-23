#include "ThreadMenuRenderer.h"

#include <I18n.h>
#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <string>

#include "../MeshCoreSettings.h"
#include "../StatusMessageOverlay.h"
#include "MeshCoreThreadActivity.h"
#include "components/UITheme.h"

bool ThreadMenuRenderer::renderFontRebuildPopup(MeshCoreThreadActivity& act) {
  if (!act._needsRebuild) return false;
  act._needsRebuild = false;
  act.renderer.clearScreen();
  GUI.drawPopup(act.renderer, tr(STR_MESHCORE_RECALC_LAYOUT));
  act.renderer.displayBuffer();
  act._rebuildMessageHeights();
  act.loadMessages(act._meta.positionId > 0 ? act._meta.positionId : act._meta.startId, false);
  act.requestUpdate();
  return true;
}

bool ThreadMenuRenderer::renderConfirmPopup(MeshCoreThreadActivity& act) {
  if (act._confirmAction == MeshCoreThreadActivity::ConfirmAction::NONE) return false;

  const char* confirmMsg = "";
  const char* confirmLabel = "";
  switch (act._confirmAction) {
    case MeshCoreThreadActivity::ConfirmAction::CLEAR_CONVERSATION:
      confirmMsg = tr(STR_MESHCORE_CLEAR_CONFIRM);
      confirmLabel = tr(STR_CONFIRM);
      break;
    case MeshCoreThreadActivity::ConfirmAction::REMOVE_CONTACT:
      confirmMsg = tr(STR_MESHCORE_REMOVE_CONTACT_CONFIRM);
      confirmLabel = tr(STR_CONFIRM);
      break;
    default:
      break;
  }

  const auto pageWidth = act.renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  char headerSubtitle[64];
  act._toast.getSubtitle(headerSubtitle, sizeof(headerSubtitle));
  GUI.drawHeader(act.renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), act.threadName,
                 headerSubtitle);
  GUI.drawPopup(act.renderer, confirmMsg);
  const auto labels = act.mappedInput.mapLabels(tr(STR_BACK), confirmLabel, "", "");
  GUI.drawButtonHints(act.renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  act.renderer.displayBuffer();
  return true;
}

void ThreadMenuRenderer::renderMenu(MeshCoreThreadActivity& act, const Rect& contentRect) {
  bool connected = (act.client.getState() == BleConnectionState::CONNECTED);

  constexpr int kChannelActionCount = 2;
  constexpr int kDmActionCount = 6;
  int kActionCount = act.isChannel ? kChannelActionCount : kDmActionCount;
  bool hasSettings = act._menuSettings != nullptr;

  const auto& m = UITheme::getInstance().getMetrics();
  const int sepGap = m.verticalSpacing;

  static constexpr StrId kChannelTitles[] = {
      StrId::STR_MESHCORE_SCROLL_TO_END,
      StrId::STR_MESHCORE_CLEAR_CONVERSATION,
  };
  // DM actions after the first (dynamic favourite toggle) slot.
  static constexpr StrId kDmTitles[] = {
      StrId::STR_PATH_RESET,
      StrId::STR_MESHCORE_SCROLL_TO_END,
      StrId::STR_MESHCORE_CLEAR_CONVERSATION,
      StrId::STR_MESHCORE_SHARE_CONTACT,
      StrId::STR_MESHCORE_REMOVE_CONTACT,
  };

  int listSel = act.selectedIndex - 1;
  int actionSel = (listSel >= 0 && listSel < kActionCount) ? listSel : -1;

  Rect actionRect = contentRect;
  GUI.drawList(
      act.renderer, actionRect, kActionCount, actionSel,
      [&](int index) -> std::string {
        if (act.isChannel) {
          if (index < 0 || index >= kChannelActionCount) return {};
          return I18n::getInstance().get(kChannelTitles[index]);
        }
        if (index == 0) {
          // Favourite toggle shows the label of the *next* state.
          return act.contactIsFavourite() ? tr(STR_MESHCORE_REMOVE_FAVOURITE) : tr(STR_MESHCORE_ADD_FAVOURITE);
        }
        if (index < 1 || index >= kDmActionCount) return {};
        return I18n::getInstance().get(kDmTitles[index - 1]);
      },
      nullptr, nullptr, nullptr, false,
      [&](int index) -> bool {
        if (act.isChannel) return false;
        if (index == 1) {
          if (!connected) return true;
          // Dim "Reset Path" when the contact has no learned path (0xFF).
          MeshCoreContact c;
          return !act.store.findContactByPubkey(act.contactPubkey, c) || c.pathLength == 0xFF;
        }
        // Favourite toggle and Unlist require a connected companion.
        if (!connected) return (index == 0 || index == 5);
        return false;
      });

  if (!hasSettings) return;

  int sepY = contentRect.y + kActionCount * m.listRowHeight + sepGap;
  act.renderer.drawLine(contentRect.x + m.contentSidePadding, sepY,
                        contentRect.x + contentRect.width - m.contentSidePadding - 1, sepY, true);

  int settingSel = (listSel >= kActionCount) ? (listSel - kActionCount) : -1;
  int settingsTop = sepY + 1 + sepGap;
  Rect settingsRect(contentRect.x, settingsTop, contentRect.width, contentRect.y + contentRect.height - settingsTop);

  GUI.drawList(
      act.renderer, settingsRect, 1, settingSel,
      [](int) -> std::string { return I18n::getInstance().get(StrId::STR_MESHCORE_USE_READER_FONT); }, nullptr, nullptr,
      [&](int) -> std::string { return act._menuSettings->useReaderFont ? tr(STR_STATE_ON) : tr(STR_STATE_OFF); }, true,
      nullptr);
}
