#pragma once

#include <cstdint>

#include "components/UITheme.h"

struct GfxRenderer;
struct MeshCoreMessage;
struct Rect;

/// Pre-computed font/layout metrics for a thread view.  Factors out the
/// repeated (renderer, rect, fontSetting) → (bodyFontId, lineHeights,
/// maxTextWidth) derivation so it lives in one place.
struct ThreadRenderCtx {
  const ThemeMetrics& metrics;
  int bodyFontId;
  int bodyLineH;
  int metaFontId;
  int metaLineH;
  int maxTextWidth;

  ThreadRenderCtx(const GfxRenderer& renderer, const Rect& rect, int bodyFontId);
};

/// Render a batch of MeshCore messages within a rectangular viewport.
///
/// When scanOnly is true, this only touches the font cache (glyph prewarming)
/// and skips actual pixel output — used in a two-pass render: scan → prewarm → draw.
///
/// The filler message (if its id != 0) is rendered with clipToFit=true in the
/// remaining space at the bottom of the viewport.
void renderMessageBatch(const GfxRenderer& renderer, Rect rect, const MeshCoreMessage* msgs, uint8_t count,
                        const MeshCoreMessage& filler, ThreadRenderCtx& ctx, bool isChannel, uint16_t totalPx,
                        uint16_t positionPx, bool scanOnly);
