#include "MeshCoreMessageRenderer.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "../utils/MeshCoreDisplayUtils.h"
#include "../utils/MeshCoreMessageHeight.h"
#include "../utils/MeshCoreTimeUtils.h"

ThreadRenderCtx::ThreadRenderCtx(const GfxRenderer& renderer, const Rect& rect, int bodyFontId)
    : metrics(UITheme::getInstance().getMetrics()),
      bodyFontId(bodyFontId),
      bodyLineH(renderer.getLineHeight(bodyFontId)),
      metaFontId(bodyFontId),
      metaLineH(renderer.getLineHeight(metaFontId)),
      maxTextWidth(rect.width - 2 * metrics.contentSidePadding) {}

void renderMessageBatch(const GfxRenderer& renderer, Rect rect, const MeshCoreMessage* msgs, uint8_t count,
                        const MeshCoreMessage& filler, ThreadRenderCtx& ctx, bool isChannel, uint32_t totalPx,
                        uint32_t positionPx, bool scanOnly) {
  if (count == 0) return;

  constexpr int maxLines = 100;

  // ── Scan-only pass for font prewarming ──
  if (scanOnly) {
    for (uint8_t i = 0; i < count; ++i) {
      const auto& msg = msgs[i];
      bool showSender = (isChannel && msg.direction != MsgDirection::SENT && msg.senderName[0]);
      if (showSender) {
        renderer.drawText(ctx.bodyFontId, 0, 0, msg.senderName, true);
      }
      if (msg.text[0]) {
        renderer.drawText(ctx.bodyFontId, 0, 0, msg.text, true);
      }
      if (msg.timestamp > 0) {
        char tsBuf[16];
        formatMeshCoreTimestamp(msg.timestamp, tsBuf, sizeof(tsBuf));
        renderer.drawText(ctx.metaFontId, 0, 0, tsBuf, true);
      }
    }
    if (filler.id != 0) {
      bool showSender = (isChannel && filler.direction != MsgDirection::SENT && filler.senderName[0]);
      if (showSender) {
        renderer.drawText(ctx.bodyFontId, 0, 0, filler.senderName, true);
      }
      if (filler.text[0]) {
        renderer.drawText(ctx.bodyFontId, 0, 0, filler.text, true);
      }
      if (filler.timestamp > 0) {
        char tsBuf[16];
        formatMeshCoreTimestamp(filler.timestamp, tsBuf, sizeof(tsBuf));
        renderer.drawText(ctx.metaFontId, 0, 0, tsBuf, true);
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

    // Sender line
    if (showSender && fits(ctx.bodyLineH)) {
      int senderX;
      if (outgoing) {
        int senderW = renderer.getTextWidth(ctx.bodyFontId, msg.senderName);
        senderX = rect.x + rect.width - ctx.metrics.contentSidePadding - senderW;
      } else {
        senderX = rect.x + ctx.metrics.contentSidePadding;
      }
      renderer.drawText(ctx.bodyFontId, senderX, yPos, msg.senderName, true);
      if (!outgoing) {
        int sw = renderer.getTextWidth(ctx.bodyFontId, msg.senderName);
        for (int py = yPos; py < yPos + ctx.bodyLineH; py++)
          for (int px = senderX; px < senderX + sw; px++)
            if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
      }
      yPos += ctx.bodyLineH;
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
      char hopBuf[20];
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
        char tsBuf[16];
        formatMeshCoreTimestamp(msg.timestamp, tsBuf, sizeof(tsBuf));
        meshcore::formatMeshCoreHopCount(msg.pathLength, hopBuf, sizeof(hopBuf));
        snprintf(metaBuf, sizeof(metaBuf), "%s %s %s", tsBuf, meshcore::DotSeparator, hopBuf);
      }
      int metaX;
      if (outgoing) {
        int metaW = renderer.getTextWidth(ctx.metaFontId, metaBuf);
        metaX = rect.x + rect.width - ctx.metrics.contentSidePadding - metaW;
      } else {
        metaX = rect.x + ctx.metrics.contentSidePadding;
      }
      renderer.drawText(ctx.metaFontId, metaX, yPos, metaBuf, true);
      int mw = renderer.getTextWidth(ctx.metaFontId, metaBuf);
      for (int py = yPos; py < yPos + ctx.metaLineH; py++)
        for (int px = metaX; px < metaX + mw; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
      yPos += ctx.metaLineH;
    }
  };

  // ── Main message batch ──
  for (uint8_t i = 0; i < count; ++i) {
    if (y > rect.y + rect.height) break;
    rendered = true;
    renderOneMsg(msgs[i], y, /*clipToFit=*/false);
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
