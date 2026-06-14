#include "MeshCoreStatusActivity.h"

#include <I18n.h>
#include <Logging.h>

#ifdef SIMULATOR
#include <MeshCoreMockHotkeys.h>
#endif

#include <cstdio>

#include "MeshCoreBatteryPoller.h"
#include "MeshCoreSubtitle.h"
#include "components/UITheme.h"
#include "fontIds.h"

MeshCoreStatusActivity::MeshCoreStatusActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               MeshCoreClient& client)
    : Activity("MeshCoreStatus", renderer, mappedInput), client(client) {}

void MeshCoreStatusActivity::onEnter() {
  Activity::onEnter();
  lastBatteryRequestMs = 0;  // force immediate poll on enter
  requestUpdate();
}

void MeshCoreStatusActivity::onExit() { Activity::onExit(); }

void MeshCoreStatusActivity::loop() {
  client.poll();

  if (pollMeshCoreBattery(client, lastBatteryRequestMs, lastBatteryMv)) {
    requestUpdate();
  }

#ifdef SIMULATOR
  if (handleMockKey("Status", client.getBleClient())) {
    requestUpdate();
    return;
  }
  pollMock(client.getBleClient(), millis());
#endif

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (client.getState() == BleConnectionState::CONNECTED) {
      disconnectSelected = !disconnectSelected;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    uint32_t now = millis();
    if (client.getState() == BleConnectionState::CONNECTED && disconnectSelected) {
      client.disconnect();
      finish();
      return;
    }
    if (client.getState() == BleConnectionState::CONNECTED && client.requestBattery()) {
      lastBatteryRequestMs = now;
    }
    requestUpdate();
  }
}

void MeshCoreStatusActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  char headerSubtitle[64];
  formatMeshCoreSubtitle(client, headerSubtitle, sizeof(headerSubtitle));
  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), tr(STR_MESHCORE_STATUS),
                 headerSubtitle);

  int contentTop = metrics.topPadding + metrics.headerHeight;
  int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.topPadding;
  Rect contentRect(0, contentTop, pageWidth, contentHeight);

  if (client.getState() != BleConnectionState::CONNECTED) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, tr(STR_MESHCORE_DISCONNECTED));
  } else {
    int y = contentRect.y + metrics.topPadding;

    // Draw disconnect item
    const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
    const int itemCenterY = y + (metrics.listRowHeight - lineH) / 2;
    if (disconnectSelected) {
      renderer.fillRect(contentRect.x, y, contentRect.width, metrics.listRowHeight);
    }
    renderer.drawText(UI_10_FONT_ID, contentRect.x + metrics.contentSidePadding, itemCenterY,
                      tr(STR_MESHCORE_DISCONNECT), !disconnectSelected);

    y += metrics.listRowHeight + metrics.verticalSpacing;
    renderStatusFields(contentRect, y);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_MESHCORE_RETRY), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void MeshCoreStatusActivity::renderStatusFields(const Rect& contentRect, int startY) {
  const auto& comp = client.getCompanion();
  const auto& metrics = UITheme::getInstance().getMetrics();

  int y = startY;
  const int x = contentRect.x + metrics.contentSidePadding;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 4;
  const int maxWidth = contentRect.width - 2 * metrics.contentSidePadding;

  auto drawField = [&](const char* label, const char* value) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: %s", label, value);
    auto lines = renderer.wrappedText(UI_10_FONT_ID, buf, maxWidth, 10);
    for (const auto& line : lines) {
      renderer.drawText(UI_10_FONT_ID, x, y, line.c_str(), true);
      y += lineH;
    }
  };

  drawField("Name", comp.name);
  drawField("Model", comp.model);
  drawField("Firmware", comp.version);

  char battBuf[16];
  snprintf(battBuf, sizeof(battBuf), "%d.%02d V", comp.batteryMv / 1000, (comp.batteryMv % 1000) / 10);
  drawField("Battery", battBuf);

  char storageBuf[32];
  snprintf(storageBuf, sizeof(storageBuf), "%lu / %lu KB", static_cast<unsigned long>(comp.storageUsedKb),
           static_cast<unsigned long>(comp.storageTotalKb));
  drawField("Storage", storageBuf);

  char radioBuf[48];
  snprintf(radioBuf, sizeof(radioBuf), "%.1f MHz BW %.0f kHz SF%d CR%d", comp.radioFreq, comp.radioBw, comp.radioSf,
           comp.radioCr);
  drawField("Radio", radioBuf);
}
