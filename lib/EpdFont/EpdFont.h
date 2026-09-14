#pragma once
#include "EpdFontData.h"

class EpdFont {
  void getTextBounds(const char* string, int startX, int startY, int* minX, int* minY, int* maxX, int* maxY) const;

 public:
  const EpdFontData* data;
  explicit EpdFont(const EpdFontData* data) : data(data) {}
  ~EpdFont() = default;
  void getTextDimensions(const char* string, int* w, int* h) const;

  const EpdGlyph* getGlyph(uint32_t cp) const;

  /// Returns true if this font covers `cp`: either via its in-RAM interval
  /// table or, for SD card fonts, via the coverageHandler that consults the
  /// full RAM-resident coverage index. Unlike getGlyph(), it never performs
  /// storage I/O and never falls back to the replacement glyph — it reports
  /// only what this font can render. Used by the CJK UI font fallback to
  /// decide whether a string needs to be routed to another font.
  bool hasCodepoint(uint32_t cp) const;

  /// Returns the kerning adjustment (4.4 fixed-point in pixels) between two codepoints.
  /// Returns 0 if no kerning data exists for the pair.
  int8_t getKerning(uint32_t leftCp, uint32_t rightCp) const;

  /// Returns the ligature codepoint for a pair, or 0 if no ligature exists.
  uint32_t getLigature(uint32_t leftCp, uint32_t rightCp) const;

  /// Greedily applies ligature substitutions starting from cp, consuming
  /// as many following codepoints from text as possible. Returns the
  /// (possibly substituted) codepoint; advances text past consumed chars.
  uint32_t applyLigatures(uint32_t cp, const char*& text) const;

  /// Per-glyph emoji fallback: an optional same-pixel-size font consulted
  /// when this font lacks an emoji codepoint. Glyph bitmaps are pre-rendered
  /// at a fixed pixel size, so the fallback MUST be generated at the same
  /// size as this font. Managed by GfxRenderer::setEmojiFallbackFont().
  void setEmojiFallback(const EpdFont* fallback) const { emojiFallback = fallback; }
  const EpdFont* getEmojiFallback() const { return emojiFallback; }

  /// Resolve the glyph to use for `cp` with emoji fallback:
  /// - this font covers cp → its glyph;
  /// - emoji codepoint missing here but covered by the fallback → fallback glyph;
  /// - otherwise → this font's replacement glyph (U+FFFD box), preserving
  ///   pre-emoji behaviour for anything unrenderable.
  /// When `usedFont` is provided, it receives the font that owns the returned
  /// glyph (this font or the emoji fallback) — the caller must decode the
  /// bitmap with that font's EpdFontData.
  const EpdGlyph* getGlyphWithEmojiFallback(uint32_t cp, const EpdFont** usedFont = nullptr) const;

 private:
  // Mutable: registered late via EpdFontFamily::setEmojiFallback(), through
  // const EpdFont* pointers held by the family.
  mutable const EpdFont* emojiFallback = nullptr;
};
