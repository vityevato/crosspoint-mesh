#include "MeshCoreMessageHeight.h"

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "components/themes/BaseTheme.h"

uint16_t measureMeshCoreMessageHeight(const GfxRenderer& renderer, int fontId, int contentWidth, bool isChannel,
                                      const MeshCoreMessage& msg, const ThemeMetrics& metrics) {
  constexpr int maxLines = 100;
  const uint16_t lineH = renderer.getLineHeight(fontId);
  uint16_t height = 0;

  // ── Sender name line ──
  // Only for channel messages received from others with a known sender
  bool showSender = (isChannel && msg.direction != MsgDirection::SENT && msg.senderName[0]);
  if (showSender) {
    height += lineH;
  }

  // ── Body text ──
  // Word-wrap and count lines, handling \n as hard line breaks
  if (msg.text[0]) {
    // Check for newlines
    bool hasNewline = false;
    for (const char* p = msg.text; *p; ++p) {
      if (*p == '\n') {
        hasNewline = true;
        break;
      }
    }

    if (hasNewline) {
      // Split on \n and wrap each segment
      uint16_t lineCount = 0;
      const char* segStart = msg.text;
      const char* p = msg.text;
      while (*p) {
        if (*p == '\n') {
          size_t segLen = (p > segStart && *(p - 1) == '\r') ? (p - 1 - segStart) : (p - segStart);
          if (segLen == 0) {
            lineCount++;  // empty line
          } else {
            std::string segment(segStart, segLen);
            auto wrapped =
                renderer.wrappedText(fontId, segment.c_str(), contentWidth, maxLines - static_cast<int>(lineCount));
            lineCount += static_cast<uint16_t>(wrapped.size());
          }
          segStart = p + 1;
        }
        ++p;
      }
      // Trailing segment after last \n
      if (*segStart) {
        size_t segLen = p - segStart;
        std::string segment(segStart, segLen);
        auto wrapped =
            renderer.wrappedText(fontId, segment.c_str(), contentWidth, maxLines - static_cast<int>(lineCount));
        lineCount += static_cast<uint16_t>(wrapped.size());
      }
      height += lineCount * lineH;
    } else {
      // No newlines — simple word-wrap
      auto lines = renderer.wrappedText(fontId, msg.text, contentWidth, maxLines);
      height += static_cast<uint16_t>(lines.size()) * lineH;
    }
  }

  // ── Meta line (timestamp + hop count / heard repeats) ──
  if (msg.timestamp > 0) {
    height += lineH;
  }

  // ── Vertical gap between consecutive messages (proportional to font) ──
  height += meshcoreMessageGapPx(lineH);

  return height;
}
