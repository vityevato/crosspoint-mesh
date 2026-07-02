#pragma once

#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>

struct ThemeMetrics;
class GfxRenderer;

/**
 * Measure the total pixel height of a single MeshCore message bubble
 * given the active font and content area width.
 *
 * Accounts for:
 *  - sender name line (channel messages from others)
 *  - word-wrapped body text (handles \n line breaks)
 *  - meta line (timestamp + hop count)
 *  - vertical spacing between consecutive messages
 *
 * @param renderer     GfxRenderer for font metrics and word-wrapping
 * @param fontId       Body font ID (used for both body and meta text)
 * @param contentWidth Available width for wrapped text in pixels
 *                     (screenWidth - 2 * contentSidePadding)
 * @param isChannel    true = channel (may show sender name);
 *                     false = DM (never shows sender name)
 * @param msg          The message to measure
 * @param metrics      ThemeMetrics for verticalSpacing
 * @return Total pixel height of the message bubble
 */
uint16_t measureMeshCoreMessageHeight(const GfxRenderer& renderer, int fontId, int contentWidth, bool isChannel,
                                      const MeshCoreMessage& msg, const ThemeMetrics& metrics);
