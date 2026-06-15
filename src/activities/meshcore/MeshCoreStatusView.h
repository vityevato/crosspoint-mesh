#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <string>

#include "MeshCore/MeshCoreTypes.h"
#include "components/UITheme.h"
#include "fontIds.h"

/**
 * Renders companion status info within MeshCoreHubActivity.
 * Shows 6 info fields: Name, Model, Firmware, Battery, Storage, Radio
 * as a centered popup overlay (renderAsPopup).
 * When no companion data is available, shows a "No data" help message.
 */
class MeshCoreStatusView {
 public:
  /**
   * Renders companion status info as a centered popup overlay, styled like
   * GUI.drawPopup but supporting multi-line wrapped text.
   * Draws a white framed rounded rect with a black interior and white text.
   */
  static void renderAsPopup(GfxRenderer& renderer, const MeshCoreCompanion& comp) {
    if (comp.name[0] == '\0') {
      GUI.drawHelpText(renderer, Rect(0, renderer.getScreenHeight() / 3, renderer.getScreenWidth(), 0),
                       tr(STR_MESHCORE_STATUS_NO_DATA));
      return;
    }

    // Prepare info field values (same formatting as render())
    char battBuf[16];
    snprintf(battBuf, sizeof(battBuf), "%d.%02d V", comp.batteryMv / 1000, (comp.batteryMv % 1000) / 10);
    char storageBuf[32];
    snprintf(storageBuf, sizeof(storageBuf), "%lu / %lu KB", static_cast<unsigned long>(comp.storageUsedKb),
             static_cast<unsigned long>(comp.storageTotalKb));
    char radioBuf[48];
    snprintf(radioBuf, sizeof(radioBuf), "%.1f MHz BW %.0f kHz SF%d CR%d", comp.radioFreq, comp.radioBw, comp.radioSf,
             comp.radioCr);

    const char* labels[] = {"Name", "Model", "Firmware", "Battery", "Storage", "Radio"};
    const char* values[] = {comp.name, comp.model, comp.version, battBuf, storageBuf, radioBuf};
    constexpr int fieldCount = 6;

    // Build display lines: "label: value"
    char lines[6][128];
    for (int i = 0; i < fieldCount; i++) {
      snprintf(lines[i], sizeof(lines[i]), "%s: %s", labels[i], values[i]);
    }

    const auto& popupMetrics = UITheme::getInstance().getMetrics();
    const int fontId = UI_10_FONT_ID;
    const int lineH = renderer.getLineHeight(fontId);
    const int rowH = lineH + 2;

    // Measure the widest line to size the popup
    int maxLineW = 0;
    for (int i = 0; i < fieldCount; i++) {
      int w = renderer.getTextWidth(fontId, lines[i]);
      if (w > maxLineW) maxLineW = w;
    }

    // Popup layout — same visual conventions as GUI.drawPopup
    const int pageW = renderer.getScreenWidth();
    const int pageH = renderer.getScreenHeight();
    const int innerPadX = popupMetrics.popupMarginX;
    const int innerPadY = popupMetrics.popupMarginY;
    const int frameTh = popupMetrics.popupFrameThickness;
    const int popupW = maxLineW + innerPadX * 2 + frameTh * 2;
    const int popupH = fieldCount * rowH + innerPadY * 2 + frameTh * 2;
    const int popupX = (pageW - popupW) / 2;
    const int popupY = static_cast<int>(pageH * popupMetrics.popupTopOffsetRatio);

    const int innerX = popupX + frameTh;
    const int innerY = popupY + frameTh;
    const int innerW = popupW - frameTh * 2;
    const int innerH = popupH - frameTh * 2;
    const int textX = innerX + innerPadX;
    const int textY0 = innerY + innerPadY;

    // Draw frame (white) and interior (black) with optional rounded corners
    if (popupMetrics.popupCornerRadius > 0) {
      renderer.fillRoundedRect(popupX, popupY, popupW, popupH, popupMetrics.popupCornerRadius + frameTh, Color::White);
      renderer.fillRoundedRect(innerX, innerY, innerW, innerH, popupMetrics.popupCornerRadius, Color::Black);
    } else {
      renderer.fillRect(popupX, popupY, popupW, popupH, true);
      renderer.fillRect(innerX, innerY, innerW, innerH, false);
    }

    // Draw text — color matches the interior (theme-dependent via popupTextInverted)
    for (int i = 0; i < fieldCount; i++) {
      renderer.drawText(fontId, textX, textY0 + i * rowH, lines[i], popupMetrics.popupTextInverted);
    }
  }
};
