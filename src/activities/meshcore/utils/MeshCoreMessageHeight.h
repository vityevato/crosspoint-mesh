#pragma once

#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>
#include <string>
#include <vector>

struct ThemeMetrics;
class GfxRenderer;

/**
 * Vertical gap between consecutive messages, proportional to line height.
 * Returns lineHeight — matches the font's natural inter-line rhythm
 * (e.g. ~16 px at 12pt, ~24 px at 18pt).
 */
inline uint16_t meshcoreMessageGapPx(int lineHeight) { return static_cast<uint16_t>(lineHeight); }

/**
 * Word-wrap message body text, respecting \n as hard line breaks.
 * Normalizes \r\n → \n.  Preserves empty lines from consecutive newlines.
 * This is the single source of truth for \n-aware word-wrapping — both
 * the render code and the height-measurement code call this function so
 * they can never diverge.
 */
std::vector<std::string> wrapMessageBody(const GfxRenderer& renderer, int fontId, const char* text, int maxWidth,
                                         int maxLines);

/**
 * Measure the total pixel height of a single MeshCore message bubble
 * given the active font and content area width.
 *
 * Accounts for:
 *  - sender name line (channel messages from others)
 *  - word-wrapped body text (handles \n line breaks via wrapMessageBody)
 *  - meta line (timestamp + hop count)
 *  - vertical gap between consecutive messages (meshcoreMessageGapPx)
 *
 * @param renderer     GfxRenderer for font metrics and word-wrapping
 * @param fontId       Body font ID (used for both body and meta text)
 * @param contentWidth Available width for wrapped text in pixels
 *                     (screenWidth - 2 * contentSidePadding)
 * @param isChannel    true = channel (may show sender name);
 *                     false = DM (never shows sender name)
 * @param msg          The message to measure
 * @param metrics      ThemeMetrics for contentSidePadding etc.
 * @return Total pixel height of the message bubble
 */
uint16_t measureMeshCoreMessageHeight(const GfxRenderer& renderer, int fontId, int contentWidth, bool isChannel,
                                      const MeshCoreMessage& msg, const ThemeMetrics& metrics);
