#include "EpdFontFamily.h"

const EpdFont* EpdFontFamily::getFont(const Style style) const {
  // Extract font style bits; render-time overlay bits do not affect font selection.
  const bool hasBold = (style & BOLD) != 0;
  const bool hasItalic = (style & ITALIC) != 0;

  if (hasBold && hasItalic) {
    if (boldItalic) return boldItalic;
    if (bold) return bold;
    if (italic) return italic;
  } else if (hasBold && bold) {
    return bold;
  } else if (hasItalic && italic) {
    return italic;
  }

  return regular;
}

void EpdFontFamily::getTextDimensions(const char* string, int* w, int* h, const Style style) const {
  getFont(style)->getTextDimensions(string, w, h);
}

const EpdFontData* EpdFontFamily::getData(const Style style) const { return getFont(style)->data; }

const EpdGlyph* EpdFontFamily::getGlyph(const uint32_t cp, const Style style) const {
  return getFont(style)->getGlyph(cp);
}

bool EpdFontFamily::hasCodepoint(const uint32_t cp, const Style style) const {
  return getFont(style)->hasCodepoint(cp);
}

int8_t EpdFontFamily::getKerning(const uint32_t leftCp, const uint32_t rightCp, const Style style) const {
  return getFont(style)->getKerning(leftCp, rightCp);
}

uint32_t EpdFontFamily::applyLigatures(const uint32_t cp, const char*& text, const Style style) const {
  return getFont(style)->applyLigatures(cp, text);
}

void EpdFontFamily::setEmojiFallback(const EpdFontFamily* fallback) {
  // Per-style pairing: each of this family's style fonts falls back to the
  // emoji family's font for the same style. Emoji-only families ship a single
  // regular cut, so getFont() resolves bold/italic requests to it.
  if (regular) regular->setEmojiFallback(fallback ? fallback->getFont(REGULAR) : nullptr);
  if (bold) bold->setEmojiFallback(fallback ? fallback->getFont(BOLD) : nullptr);
  if (italic) italic->setEmojiFallback(fallback ? fallback->getFont(ITALIC) : nullptr);
  if (boldItalic) boldItalic->setEmojiFallback(fallback ? fallback->getFont(BOLD_ITALIC) : nullptr);
}
