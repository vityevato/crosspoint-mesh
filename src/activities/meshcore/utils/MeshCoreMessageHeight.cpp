#include "MeshCoreMessageHeight.h"

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "components/themes/BaseTheme.h"

// ── Shared content-area height ──

int meshCoreThreadContentHeight(const GfxRenderer& renderer, const ThemeMetrics& metrics) {
  const int pageHeight = renderer.getScreenHeight();
  const int tabBarTop = metrics.topPadding + metrics.headerHeight;
  const int contentTop = tabBarTop + metrics.tabBarHeight + metrics.verticalSpacing;
  return pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - metrics.subtitleBottomMargin -
         metrics.bottomSubtitleHeight;
}

// ── Shared word-wrap helper ──

std::vector<std::string> wrapMessageBody(const GfxRenderer& renderer, int fontId, const char* text, int maxWidth,
                                         int maxLines) {
  std::vector<std::string> result;
  if (!text || !*text) return result;

  // Fast path: no newlines → delegate directly to wrappedText (no extra copy)
  bool hasNewline = false;
  for (const char* p = text; *p; ++p) {
    if (*p == '\n') {
      hasNewline = true;
      break;
    }
  }
  if (!hasNewline) {
    return renderer.wrappedText(fontId, text, maxWidth, maxLines);
  }

  // Slow path: split on \n and word-wrap each segment
  const char* segStart = text;
  const char* p = text;
  while (*p && static_cast<int>(result.size()) < maxLines) {
    if (*p == '\n') {
      // Handle \r\n: adjust end to exclude preceding \r
      const char* segEnd = (p > segStart && *(p - 1) == '\r') ? p - 1 : p;
      size_t segLen = segEnd - segStart;
      if (segLen == 0) {
        result.emplace_back();  // empty line marker
      } else {
        std::string segment(segStart, segLen);
        auto wrapped =
            renderer.wrappedText(fontId, segment.c_str(), maxWidth, maxLines - static_cast<int>(result.size()));
        auto n = std::min(wrapped.size(), static_cast<size_t>(std::max(0, maxLines - static_cast<int>(result.size()))));
        result.insert(result.end(), std::make_move_iterator(wrapped.begin()),
                      std::make_move_iterator(wrapped.begin() + n));
      }
      segStart = p + 1;  // skip past \n
    }
    ++p;
  }

  // Trailing segment after the last \n (or whole text if no \n ended the loop)
  if (segStart < p && static_cast<int>(result.size()) < maxLines) {
    size_t segLen = p - segStart;
    std::string segment(segStart, segLen);
    auto wrapped = renderer.wrappedText(fontId, segment.c_str(), maxWidth, maxLines - static_cast<int>(result.size()));
    result.insert(result.end(), std::make_move_iterator(wrapped.begin()), std::make_move_iterator(wrapped.end()));
  }

  return result;
}

// ── Height measurement (uses wrapMessageBody to avoid duplication) ──

uint16_t measureMeshCoreMessageHeight(const GfxRenderer& renderer, int fontId, int contentWidth, bool isChannel,
                                      const MeshCoreMessage& msg, const ThemeMetrics& metrics, int maxHeight) {
  constexpr int maxLines = 100;
  const uint16_t lineH = renderer.getLineHeight(fontId);
  uint16_t height = 0;

  // ── Sender name line ──
  bool showSender = (isChannel && msg.direction != MsgDirection::SENT && msg.senderName[0]);
  if (showSender) {
    height += lineH;
  }

  // ── Body text — uses shared wrapMessageBody to count lines ──
  if (msg.text[0]) {
    auto lines = wrapMessageBody(renderer, fontId, msg.text, contentWidth, maxLines);
    height += static_cast<uint16_t>(lines.size()) * lineH;
  }

  // ── Meta line ──
  if (msg.timestamp > 0) {
    height += lineH;
  }

  // ── Vertical gap between consecutive messages ──
  height += meshcoreMessageGapPx(lineH);

  // ── Cap to the content-area height ──
  // Messages taller than the viewport are clipped with "..." at render time,
  // so scroll math must treat them as at most one screenful tall to stay
  // consistent with what is actually drawn. Batch callers pass a precomputed
  // maxHeight to avoid recomputing it per message.
  if (maxHeight < 0) {
    maxHeight = meshCoreThreadContentHeight(renderer, metrics);
  }
  if (maxHeight > 0 && height > static_cast<uint16_t>(maxHeight)) {
    height = static_cast<uint16_t>(maxHeight);
  }

  return height;
}
