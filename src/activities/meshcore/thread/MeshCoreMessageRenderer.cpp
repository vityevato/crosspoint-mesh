#include "MeshCoreMessageRenderer.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <fontIds.h>

#include <string>
#include <vector>

#include "../utils/MeshCoreDisplayUtils.h"
#include "../utils/MeshCoreMessageHeight.h"
#include "../utils/MeshCoreTimeUtils.h"

ThreadRenderCtx::ThreadRenderCtx(const GfxRenderer& renderer, const Rect& rect, int bodyFontId)
    : metrics(UITheme::getInstance().getMetrics()),
      bodyFontId(bodyFontId),
      bodyLineH(renderer.getLineHeight(bodyFontId)),
      metaFontId(meshcore::MESHCORE_META_FONT_ID),
      metaLineH(renderer.getLineHeight(metaFontId)),
      metaDimmed(metaFontId == bodyFontId),
      maxTextWidth(rect.width - 2 * metrics.contentSidePadding) {}

void renderMessageBatch(const GfxRenderer& renderer, Rect rect, const MeshCoreMessage* msgs, uint8_t count,
                        const MeshCoreMessage& filler, ThreadRenderCtx& ctx, bool isChannel, uint32_t totalPx,
                        uint32_t positionPx, bool scanOnly) {
  if (count == 0) return;

  constexpr int maxLines = 100;
  // Gap between the horizontal rule and the centered date text
  constexpr int kDayDividerTextGapPx = 8;

  // ── Day-divider row ──
  // Drawn inside the existing vertical gap between two messages when the
  // local day changes. The divider is exactly as tall as the gap itself, so
  // it replaces the gap's whitespace instead of adding to it — the layout
  // and scroll math (heightPx / totalPx) stay untouched.
  const int dividerPx = meshcoreMessageGapPx(ctx.bodyLineH);

  auto drawDayDivider = [&](int yDiv, uint32_t dateTs) {
    // The date is drawn with the theme's standard UI font (not the body
    // font, which may be a reader font), vertically centered in the gap.
    const int dateLineH = renderer.getLineHeight(SMALL_FONT_ID);
    char dateBuf[24];
    formatMeshCoreDate(dateTs, dateBuf, sizeof(dateBuf));
    const int textW = renderer.getTextWidth(SMALL_FONT_ID, dateBuf);
    const int textX = rect.x + (rect.width - textW) / 2;
    const int textY = yDiv + (dividerPx - dateLineH) / 2;
    const int ruleY = yDiv + dividerPx / 2;
    const int left = rect.x + ctx.metrics.contentSidePadding;
    const int right = rect.x + rect.width - ctx.metrics.contentSidePadding;
    renderer.drawText(SMALL_FONT_ID, textX, textY, dateBuf, true);
    // Light-gray dither over the date, matching the meta-line style
    for (int py = textY; py < textY + dateLineH; py++)
      for (int px = textX; px < textX + textW; px++)
        if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    // Rules on both sides of the date. Drawn manually: a 1-pixel-tall
    // fillRectDither(Color::LightGray) writes nothing on odd logical rows
    // (its pattern only sets black where both logical coords are even), so
    // the line would vanish depending on the row the divider lands on.
    if (left < textX - kDayDividerTextGapPx) {
      for (int px = left; px < textX - kDayDividerTextGapPx; ++px)
        if ((px & 1) == 0) renderer.drawPixel(px, ruleY, true);
    }
    const int textRight = textX + textW + kDayDividerTextGapPx;
    if (textRight < right) {
      for (int px = textRight; px < right; ++px)
        if ((px & 1) == 0) renderer.drawPixel(px, ruleY, true);
    }
  };

  // ── Scan-only pass for font prewarming ──
  if (scanOnly) {
    for (uint8_t i = 0; i < count; ++i) {
      const auto& msg = msgs[i];
      bool showSender = (isChannel && msg.direction != MsgDirection::SENT && msg.senderName[0]);
      if (showSender) {
        renderer.drawText(ctx.metaFontId, 0, 0, msg.senderName, true);
      }
      if (msg.text[0]) {
        renderer.drawText(ctx.bodyFontId, 0, 0, msg.text, true);
      }
      if (meshcoreIsPlausibleTime(msg.timestamp)) {
        char tsBuf[24];
        formatMeshCoreTimestamp(msg.timestamp, tsBuf, sizeof(tsBuf));
        renderer.drawText(ctx.metaFontId, 0, 0, tsBuf, true);
        // Day-divider date is drawn with the standard theme font
        char dateBuf[24];
        formatMeshCoreDate(msg.timestamp, dateBuf, sizeof(dateBuf));
        renderer.drawText(SMALL_FONT_ID, 0, 0, dateBuf, true);
      }
    }
    if (filler.id != 0) {
      bool showSender = (isChannel && filler.direction != MsgDirection::SENT && filler.senderName[0]);
      if (showSender) {
        renderer.drawText(ctx.metaFontId, 0, 0, filler.senderName, true);
      }
      if (filler.text[0]) {
        renderer.drawText(ctx.bodyFontId, 0, 0, filler.text, true);
      }
      if (meshcoreIsPlausibleTime(filler.timestamp)) {
        char tsBuf[24];
        formatMeshCoreTimestamp(filler.timestamp, tsBuf, sizeof(tsBuf));
        renderer.drawText(ctx.metaFontId, 0, 0, tsBuf, true);
        // Day-divider date is drawn with the standard theme font
        char dateBuf[24];
        formatMeshCoreDate(filler.timestamp, dateBuf, sizeof(dateBuf));
        renderer.drawText(SMALL_FONT_ID, 0, 0, dateBuf, true);
      }
    }
    return;
  }

  int y = rect.y;
  bool rendered = false;

  // ── Helper: render one message (sender → body → meta), advancing y ──
  // clipToFit=true  → only lines that fit fully (y + lineH <= bottom) are drawn.
  // clipToFit=false → draws lines normally, caller stops when y > bottom.
  auto renderOneMsg = [&](const MeshCoreMessage& msg, int& yPos, bool clipToFit) {
    const int bottom = rect.y + rect.height;
    const bool outgoing = (msg.direction == MsgDirection::SENT);
    const bool showSender = (isChannel && !outgoing && msg.senderName[0]);

    auto fits = [&](int lineH) { return clipToFit ? (yPos + lineH <= bottom) : (yPos <= bottom); };

    // Sender line — always the meta (system) font; dimmed only when the
    // body font coincides with it (i.e. "use reader font" is off).
    if (showSender && fits(ctx.metaLineH)) {
      int senderX;
      if (outgoing) {
        int senderW = renderer.getTextWidth(ctx.metaFontId, msg.senderName);
        senderX = rect.x + rect.width - ctx.metrics.contentSidePadding - senderW;
      } else {
        senderX = rect.x + ctx.metrics.contentSidePadding;
      }
      renderer.drawText(ctx.metaFontId, senderX, yPos, msg.senderName, true);
      if (!outgoing && ctx.metaDimmed) {
        int sw = renderer.getTextWidth(ctx.metaFontId, msg.senderName);
        for (int py = yPos; py < yPos + ctx.metaLineH; py++)
          for (int px = senderX; px < senderX + sw; px++)
            if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
      }
      yPos += ctx.metaLineH;
    }

    // Body text — line by line
    if (msg.text[0] && fits(0)) {
      auto lines = wrapMessageBody(renderer, ctx.bodyFontId, msg.text, ctx.maxTextWidth, maxLines);
      for (const auto& line : lines) {
        if (!fits(ctx.bodyLineH)) break;

        // In non-clipToFit mode, if this line won't fit fully, replace it
        // with "..." so the user knows there's more content to scroll.
        bool willOverflow = (!clipToFit && yPos + ctx.bodyLineH > bottom);

        if (!line.empty()) {
          if (outgoing) {
            int textW = renderer.getTextWidth(ctx.bodyFontId, willOverflow ? "..." : line.c_str());
            renderer.drawText(ctx.bodyFontId, rect.x + rect.width - ctx.metrics.contentSidePadding - textW, yPos,
                              willOverflow ? "..." : line.c_str(), true);
          } else {
            renderer.drawText(ctx.bodyFontId, rect.x + ctx.metrics.contentSidePadding, yPos,
                              willOverflow ? "..." : line.c_str(), true);
          }
        }
        yPos += ctx.bodyLineH;

        if (willOverflow) break;
      }
    }

    // Meta line: incoming messages show "timestamp · hops"; outgoing messages
    // show only the delivery status / heard-repeats (the send time is redundant
    // for a message sent just now, so it is not rendered).
    const bool showMeta = outgoing || msg.timestamp > 0;
    if (showMeta && fits(ctx.metaLineH)) {
      char metaBuf[64];
      char hopBuf[32];
      if (outgoing) {
        if (isChannel) {
          meshcore::formatMeshCoreHeardRepeats(msg.pathLength, hopBuf, sizeof(hopBuf));
        } else {
          // Show delivery status instead of hop count for outgoing DMs
          switch (msg.deliveryStatus) {
            case DeliveryStatus::ACKED:
              snprintf(hopBuf, sizeof(hopBuf), "%s", tr(STR_MESHCORE_MSG_ACKED));
              break;
            case DeliveryStatus::FAILED:
              snprintf(hopBuf, sizeof(hopBuf), "%s", tr(STR_MESHCORE_MSG_FAILED));
              break;
            case DeliveryStatus::SENT:
            default:
              snprintf(hopBuf, sizeof(hopBuf), "%s", tr(STR_MESHCORE_MSG_SENT));
              break;
          }
        }
        snprintf(metaBuf, sizeof(metaBuf), "%s", hopBuf);
      } else {
        meshcore::formatMeshCoreHopCount(msg.pathLength, hopBuf, sizeof(hopBuf));
        if (meshcoreIsPlausibleTime(msg.timestamp)) {
          // Valid clock — show "timestamp · hops" as before.
          char tsBuf[24];
          formatMeshCoreTimestamp(msg.timestamp, tsBuf, sizeof(tsBuf));
          snprintf(metaBuf, sizeof(metaBuf), "%s %s %s", tsBuf, meshcore::DotSeparator, hopBuf);
        } else {
          // Epoch-era timestamp (device/companion without a valid clock) —
          // render hops only, never a bogus 1970 date. Same line count, so
          // height/scroll math is unaffected.
          snprintf(metaBuf, sizeof(metaBuf), "%s", hopBuf);
        }
      }
      int metaX;
      if (outgoing) {
        int metaW = renderer.getTextWidth(ctx.metaFontId, metaBuf);
        metaX = rect.x + rect.width - ctx.metrics.contentSidePadding - metaW;
      } else {
        metaX = rect.x + ctx.metrics.contentSidePadding;
      }
      renderer.drawText(ctx.metaFontId, metaX, yPos, metaBuf, true);
      if (ctx.metaDimmed) {
        int mw = renderer.getTextWidth(ctx.metaFontId, metaBuf);
        for (int py = yPos; py < yPos + ctx.metaLineH; py++)
          for (int px = metaX; px < metaX + mw; px++)
            if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
      }
      yPos += ctx.metaLineH;
    }
  };

  // ── Main message batch ──
  for (uint8_t i = 0; i < count; ++i) {
    if (y > rect.y + rect.height) break;
    rendered = true;
    renderOneMsg(msgs[i], y, /*clipToFit=*/false);

    // Day-divider row in the gap above the next message (or the filler)
    // when the local day changes. Only drawn when it fits fully in the
    // viewport — a partially cut row at the bottom edge would look broken.
    const MeshCoreMessage* next = (i + 1 < count) ? &msgs[i + 1] : (filler.id != 0 ? &filler : nullptr);
    if (next && y + dividerPx <= rect.y + rect.height) {
      // Only render a date divider when both sides carry a plausible clock —
      // a device without an RTC stamps messages with epoch-era times, which
      // would otherwise render a bogus "1 Jan 1970" divider.
      if (meshcoreIsPlausibleTime(msgs[i].timestamp) && meshcoreIsPlausibleTime(next->timestamp) &&
          meshcoreLocalDayOf(msgs[i].timestamp) != meshcoreLocalDayOf(next->timestamp)) {
        drawDayDivider(y, next->timestamp);
      }
    }

    // Vertical gap between messages (proportional to font size)
    if (y < rect.y + rect.height) {
      y += meshcoreMessageGapPx(ctx.bodyLineH);
    }
  }

  // ── Filler message (partial render in remaining space at bottom) ──
  if (filler.id != 0 && y < rect.y + rect.height) {
    renderOneMsg(filler, y, /*clipToFit=*/true);
  }

  // ── Render pass ──
  GUI.drawScrollBar(renderer, rect, totalPx, positionPx);

  // Clear overflow areas
  if (rendered) {
    const int screenW = renderer.getScreenWidth();
    const int screenH = renderer.getScreenHeight();
    if (rect.y > 0) renderer.fillRect(0, 0, screenW, rect.y, false);
    // Use actual render Y rather than rect boundary — a tall message
    // may extend past rect.y+rect.height (clipped with "...").
    const int belowY = (y > rect.y + rect.height) ? y : rect.y + rect.height;
    if (belowY < screenH) renderer.fillRect(0, belowY, screenW, screenH - belowY, false);
  }
}
