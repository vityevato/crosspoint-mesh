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
 * Compute the vertical content-area height available for the thread message
 * list (screen height minus header, tab bar, spacing, button hints and
 * subtitle). This is the maximum height a single message bubble can visually
 * occupy — taller messages are clipped with "..." at render time — so it is
 * the single source of truth used both by the thread activity's layout and by
 * measureMeshCoreMessageHeight() to cap measured heights.
 *
 * @param renderer GfxRenderer for the current screen height
 * @param metrics  ThemeMetrics for header/tab-bar/hint dimensions
 * @return Content-area height in pixels
 */
int meshCoreThreadContentHeight(const GfxRenderer& renderer, const ThemeMetrics& metrics);

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
 * @param maxHeight    Precomputed content-area height used to cap the result.
 *                     Pass a value >= 0 (e.g. a cached contentHeight) when
 *                     measuring many messages in a loop to avoid recomputing
 *                     it per call. Pass -1 (default) to derive it internally
 *                     from renderer + metrics.
 * @return Total pixel height of the message bubble (capped to maxHeight)
 */
uint16_t measureMeshCoreMessageHeight(const GfxRenderer& renderer, int fontId, int contentWidth, bool isChannel,
                                      const MeshCoreMessage& msg, const ThemeMetrics& metrics, int maxHeight = -1);
