#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <string>

#include "MeshCore/MeshCoreClient.h"
#include "MeshCore/MeshCoreTypes.h"
#include "components/UITheme.h"
#include "fontIds.h"

/**
 * Renders the Status tab content within MeshCoreHubActivity.
 * Shows a disconnect action row followed by companion device info fields.
 */
class MeshCoreStatusView {
 public:
  static void render(GfxRenderer& renderer, const Rect& contentRect, MeshCoreClient& client, int selectedIndex) {
    if (client.getState() != BleConnectionState::CONNECTED) {
      GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_DISCONNECTED));
      return;
    }

    const auto& comp = client.getCompanion();
    const auto& m = UITheme::getInstance().getMetrics();
    const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 4;

    // Disconnect row — rendered via GUI.drawList for consistent list-item styling.
    // Rect is 2× listRowHeight so that pageItems > 1, avoiding a drawList edge
    // case with selectedIndex = -1 when the tab bar has focus.
    Rect disconnectRect(contentRect.x, contentRect.y + m.topPadding, contentRect.width, m.listRowHeight * 2);
    GUI.drawList(renderer, disconnectRect, 1, (selectedIndex == 1) ? 0 : -1,
                 [](int) { return std::string(tr(STR_MESHCORE_DISCONNECT)); });

    // Prepare info field values
    char battBuf[16];
    snprintf(battBuf, sizeof(battBuf), "%d.%02d V", comp.batteryMv / 1000, (comp.batteryMv % 1000) / 10);
    char storageBuf[32];
    snprintf(storageBuf, sizeof(storageBuf), "%lu / %lu KB", static_cast<unsigned long>(comp.storageUsedKb),
             static_cast<unsigned long>(comp.storageTotalKb));
    char radioBuf[48];
    snprintf(radioBuf, sizeof(radioBuf), "%.1f MHz BW %.0f kHz SF%d CR%d", comp.radioFreq, comp.radioBw, comp.radioSf,
             comp.radioCr);

    const char* labels[] = {"Name",    "Model",   "Firmware", "Battery",
                            "Storage", "Radio"};
    const char* values[] = {comp.name, comp.model, comp.version, battBuf,
                            storageBuf, radioBuf};
    constexpr int fieldCount = 6;

    // Inverted info block: black rounded rect with white text
    constexpr int kInnerPadding = 8;
    constexpr int kCornerRadius = 8;
    const int blockMargin = m.contentSidePadding;
    const int blockX = contentRect.x + blockMargin;
    const int blockW = contentRect.width - 2 * blockMargin;
    const int maxWidth = blockW - 2 * kInnerPadding;
    const int textX = blockX + kInnerPadding;

    // Estimate height: all values are short, assume 1 line each
    const int blockY = disconnectRect.y + m.listRowHeight + m.contentSidePadding / 2;
    const int blockH = fieldCount * lineH + 2 * kInnerPadding;

    // Draw black rounded background
    renderer.fillRoundedRect(blockX, blockY, blockW, blockH, kCornerRadius, Color::Black);

    // Draw info fields in white on the inverted background
    int y = blockY + kInnerPadding;
    for (int i = 0; i < fieldCount; i++) {
      char buf[256];
      snprintf(buf, sizeof(buf), "%s: %s", labels[i], values[i]);
      auto lines = renderer.wrappedText(UI_10_FONT_ID, buf, maxWidth, 10);
      for (const auto& line : lines) {
        renderer.drawText(UI_10_FONT_ID, textX, y, line.c_str(), false);
        y += lineH;
      }
    }
  }
};
