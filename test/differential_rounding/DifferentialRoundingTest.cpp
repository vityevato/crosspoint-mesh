#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>

#include "lib/EpdFont/EpdFont.h"
#include "lib/EpdFont/EpdFontData.h"

// ============================================================================
// Synthetic test font
//
// Glyphs: 'T' (0x54), 'a' (0x61), 'o' (0x6F), 'x' (0x78)
//   - 'x' advance is 136 FP (8.5px) -- frac = 8, exactly at the rounding
//     boundary where absolute vs differential snapping diverges for "oo".
//   - No U+FFFD replacement glyph, so unknown codepoints trigger the
//     null-glyph path in getTextBounds.
//
// Kern pairs (4.4 fixed-point):
//   T->a: -5  (-0.3125px)     T->o: -7  (-0.4375px)
//   o->a: -2  (-0.125px)      o->o: -3  (-0.1875px)
// ============================================================================

namespace {

// clang-format off
const EpdGlyph kGlyphs[] = {
  // idx  width  height  advanceX  left  top  dataLength  dataOffset
  /* 0 'T' */ { 8, 12, 137, 0, 12, 0, 0 },
  /* 1 'a' */ { 7,  8, 130, 0,  8, 0, 0 },
  /* 2 'o' */ { 8,  8, 145, 0,  8, 0, 0 },
  /* 3 'x' */ { 7,  8, 136, 0,  8, 0, 0 },
};

const EpdUnicodeInterval kIntervals[] = {
  { 0x54, 0x54, 0 },  // 'T' -> glyph[0]
  { 0x61, 0x61, 1 },  // 'a' -> glyph[1]
  { 0x6F, 0x6F, 2 },  // 'o' -> glyph[2]
  { 0x78, 0x78, 3 },  // 'x' -> glyph[3]
};

const EpdKernClassEntry kKernLeft[] = {
  { 0x54, 1 },  // 'T' -> left class 1
  { 0x6F, 2 },  // 'o' -> left class 2
};

const EpdKernClassEntry kKernRight[] = {
  { 0x61, 1 },  // 'a' -> right class 1
  { 0x6F, 2 },  // 'o' -> right class 2
};

// Flat matrix: leftClassCount(2) x rightClassCount(2), 4.4 fixed-point
//   [L1,R1]=kern(T,a)  [L1,R2]=kern(T,o)  [L2,R1]=kern(o,a)  [L2,R2]=kern(o,o)
const int8_t kKernMatrix[] = { -5, -7, -2, -3 };

const EpdFontData kTestFontData = {
  .bitmap            = nullptr,
  .glyph             = kGlyphs,
  .intervals         = kIntervals,
  .intervalCount     = 4,
  .advanceY          = 16,
  .ascender          = 12,
  .descender         = 0,
  .is2Bit            = false,
  .groups            = nullptr,
  .groupCount        = 0,
  .glyphToGroup      = nullptr,
  .kernLeftClasses   = kKernLeft,
  .kernRightClasses  = kKernRight,
  .kernMatrix        = kKernMatrix,
  .kernLeftEntryCount  = 2,
  .kernRightEntryCount = 2,
  .kernLeftClassCount  = 2,
  .kernRightClassCount = 2,
  .ligaturePairs     = nullptr,
  .ligaturePairCount = 0,
  .glyphMissHandler  = nullptr,
  .glyphMissCtx      = nullptr,
};
// clang-format on

EpdFont& testFont() {
  static EpdFont font(&kTestFontData);
  return font;
}

int textWidth(const char* str) {
  int w = 0, h = 0;
  testFont().getTextDimensions(str, &w, &h);
  return w;
}

int textHeight(const char* str) {
  int w = 0, h = 0;
  testFont().getTextDimensions(str, &w, &h);
  return h;
}

// Simulate the old absolute-snap gap for comparison
int absoluteGap(int32_t startFP, int32_t advanceFP, int32_t kernFP) {
  int32_t nextFP = startFP + advanceFP + kernFP;
  return fp4::toPixel(nextFP) - fp4::toPixel(startFP);
}

}  // namespace

// ============================================================================
// Part 1: Pure fp4 math tests
// ============================================================================

TEST(Fp4Math, RoundTripIntegerPixels) {
  for (int px = 0; px < 500; px++) {
    EXPECT_EQ(fp4::toPixel(fp4::fromPixel(px)), px) << "px=" << px;
  }
}

TEST(Fp4Math, RoundingBoundaries) {
  EXPECT_EQ(fp4::toPixel(0), 0);
  EXPECT_EQ(fp4::toPixel(7), 0);    // 0.4375 -> 0
  EXPECT_EQ(fp4::toPixel(8), 1);    // 0.5 -> 1 (round half up)
  EXPECT_EQ(fp4::toPixel(15), 1);   // 0.9375 -> 1
  EXPECT_EQ(fp4::toPixel(16), 1);   // 1.0 -> 1
  EXPECT_EQ(fp4::toPixel(24), 2);   // 1.5 -> 2
  EXPECT_EQ(fp4::toPixel(-8), 0);   // -0.5 -> 0
  EXPECT_EQ(fp4::toPixel(-9), -1);  // -0.5625 -> -1
  EXPECT_EQ(fp4::toPixel(-16), -1);

  EXPECT_EQ(fp4::toPixel(137 + (-9)), 8);  // 128 = 8.0 exact
  EXPECT_EQ(fp4::toPixel(137 + (-5)), 8);  // 132 = 8.25
  EXPECT_EQ(fp4::toPixel(137 + (-1)), 9);  // 136 = 8.5 (half rounds up)
}

TEST(Fp4Math, OldApproachInconsistency) {
  // 'oo' pair: advance=145 (9.0625px), kern=-3 (-0.1875px), combined=142 (8.875px)
  const int32_t advance = 145;
  const int32_t kern = -3;

  int minGap = 999, maxGap = -999;
  for (int startPx = 0; startPx < 100; startPx++) {
    for (int frac = 0; frac < 16; frac++) {
      int32_t startFP = fp4::fromPixel(startPx) + frac;
      int gap = absoluteGap(startFP, advance, kern);
      if (gap < minGap) minGap = gap;
      if (gap > maxGap) maxGap = gap;
    }
  }

  // Absolute snap produces inconsistent gaps depending on subpixel phase.
  EXPECT_GE(maxGap - minGap, 1);
}

TEST(Fp4Math, ExhaustiveKernRange) {
  const int32_t baseAdvance = 128;

  for (int advFrac = 0; advFrac < 16; advFrac++) {
    int32_t advance = baseAdvance + advFrac;
    for (int kern = -128; kern <= 127; kern++) {
      int step = fp4::toPixel(advance + static_cast<int32_t>(kern));
      float idealPx = fp4::toFloat(advance + kern);
      EXPECT_LT(std::abs(step - idealPx), 1.0f) << "advance=" << advance << " kern=" << kern;
    }
  }
}

// ============================================================================
// Part 2: Integration tests using real EpdFont::getTextDimensions
// ============================================================================

TEST(EpdFont, KernLookup) {
  EXPECT_EQ(testFont().getKerning('T', 'a'), -5);
  EXPECT_EQ(testFont().getKerning('T', 'o'), -7);
  EXPECT_EQ(testFont().getKerning('o', 'a'), -2);
  EXPECT_EQ(testFont().getKerning('o', 'o'), -3);
  EXPECT_EQ(testFont().getKerning('a', 'o'), 0);  // 'a' has no left class
  EXPECT_EQ(testFont().getKerning('x', 'o'), 0);  // 'x' has no left class
  EXPECT_EQ(testFont().getKerning('T', 'x'), 0);  // 'x' has no right class
  EXPECT_EQ(testFont().getKerning('T', 'T'), 0);  // 'T' has no right class
}

TEST(EpdFont, GlyphLookup) {
  ASSERT_NE(testFont().getGlyph('T'), nullptr);
  ASSERT_NE(testFont().getGlyph('a'), nullptr);
  ASSERT_NE(testFont().getGlyph('o'), nullptr);
  ASSERT_NE(testFont().getGlyph('x'), nullptr);
  EXPECT_EQ(testFont().getGlyph('T')->advanceX, 137);
  EXPECT_EQ(testFont().getGlyph('a')->advanceX, 130);
  EXPECT_EQ(testFont().getGlyph('o')->advanceX, 145);
  EXPECT_EQ(testFont().getGlyph('x')->advanceX, 136);

  // No U+FFFD in font, so unknown codepoints return nullptr
  EXPECT_EQ(testFont().getGlyph('Z'), nullptr);
  EXPECT_EQ(testFont().getGlyph('b'), nullptr);
}

// Known-value regression tests.  Expected widths are computed by hand using
// differential rounding.  If someone reverts to absolute snapping, specific
// test cases will fail.
//
// Layout trace for each string (all glyphs have left=0):
//   width = max glyph right edge = lastBaseX + glyph.width
//
// Differential step from glyph A to glyph B:
//   step = fp4::toPixel(advanceA + kern(A,B))
TEST(EpdFont, KnownWidths) {
  // "o": single glyph at x=0, width=8 -> w = 0 + 8 = 8
  EXPECT_EQ(textWidth("o"), 8);

  // "oo": step = toPixel(145 + (-3)) = toPixel(142) = 9
  //   o1 at 0, o2 at 9.  w = 9 + 8 = 17
  EXPECT_EQ(textWidth("oo"), 17);

  // "ooo": two steps of 9 -> o3 at 18, w = 18 + 8 = 26
  EXPECT_EQ(textWidth("ooo"), 26);

  // "To": step = toPixel(137 + (-7)) = 8 -> o at 8, w = 8 + 8 = 16
  EXPECT_EQ(textWidth("To"), 16);

  // "Ta": step = toPixel(137 + (-5)) = 8 -> a at 8, w = 8 + 7 = 15
  EXPECT_EQ(textWidth("Ta"), 15);

  // "oa": step = toPixel(145 + (-2)) = 9 -> a at 9, w = 9 + 7 = 16
  EXPECT_EQ(textWidth("oa"), 16);

  // "Too": T at 0, o1 at 8 (T->o step), o2 at 17 (o->o step). w = 17 + 8 = 25
  EXPECT_EQ(textWidth("Too"), 25);

  // "xo": step = toPixel(136 + 0) = 9 (no kern: x has no left class)
  //   x at 0, o at 9.  w = 9 + 8 = 17
  EXPECT_EQ(textWidth("xo"), 17);
}

// "oo" pair consistency: the pixel gap between two o's must be the same
// regardless of what prefix precedes them.  This is THE key property of
// differential rounding.  With absolute snapping, "xoo" would produce a
// different oo gap than "oo" because 'x' advance (136 FP) puts the first
// 'o' at fractional phase 8, crossing the rounding boundary differently.
TEST(EpdFont, PairConsistencyViaFont) {
  // The oo gap = width(prefix + "oo") - width(prefix + "o")
  // This isolates the pixel distance contributed by the second 'o'.
  const int oo_gap_bare = textWidth("oo") - textWidth("o");
  const int oo_gap_after_x = textWidth("xoo") - textWidth("xo");
  const int oo_gap_after_T = textWidth("Too") - textWidth("To");
  const int oo_gap_after_o = textWidth("ooo") - textWidth("oo");

  EXPECT_EQ(oo_gap_after_x, oo_gap_bare);
  EXPECT_EQ(oo_gap_after_T, oo_gap_bare);
  EXPECT_EQ(oo_gap_after_o, oo_gap_bare);
}

// Null-glyph handling: when a codepoint has no glyph (and no replacement
// glyph), the pending advance from the previous glyph must still be flushed.
// Without the flush fix, the glyph after the null would overlap the one before.
TEST(EpdFont, NullGlyphAdvancePreserved) {
  // 'Z' (0x5A) is not in our font and there's no U+FFFD, so getGlyph returns null.
  // "oZo" should lay out as: o1 at 0, Z skipped (advance flushed), o2 at 9.
  //   toPixel(145) = 9 (o's advance, no kern since Z resets prevCp).
  //   w = 9 + 8 = 17
  EXPECT_EQ(textWidth("oZo"), 17);

  // Multi-null: "oZZo" -- two consecutive nulls, advance still preserved.
  EXPECT_EQ(textWidth("oZZo"), 17);

  // Null at start: "Zo" -- no pending advance to flush, o renders at 0.
  EXPECT_EQ(textWidth("Zo"), 8);
}

TEST(EpdFont, HeightCalculation) {
  // 'T' is tallest: top=12, height=12 -> extent [0, 12)
  // 'o' and 'a': top=8, height=8 -> extent [0, 8)
  EXPECT_EQ(textHeight("o"), 8);
  EXPECT_EQ(textHeight("T"), 12);
  EXPECT_EQ(textHeight("To"), 12);
  EXPECT_EQ(textHeight("oo"), 8);
}

// ============================================================================
// Part 3: Per-glyph emoji fallback
// ============================================================================

namespace {

// Synthetic emoji fallback font: a single U+1F600 (😀) glyph, 10px square,
// advance exactly 10px (160 FP). No kerning, no replacement glyph.
// clang-format off
const EpdGlyph kEmojiGlyphs[] = {
  /* 0 U+1F600 */ { 10, 10, 160, 0, 10, 0, 0 },
};
const EpdUnicodeInterval kEmojiIntervals[] = {
  { 0x1F600, 0x1F600, 0 },
};
const EpdFontData kEmojiFontData = {
  .bitmap            = nullptr,
  .glyph             = kEmojiGlyphs,
  .intervals         = kEmojiIntervals,
  .intervalCount     = 1,
  .advanceY          = 16,
  .ascender          = 10,
  .descender         = 0,
  .is2Bit            = false,
  .groups            = nullptr,
  .groupCount        = 0,
  .glyphToGroup      = nullptr,
  .kernLeftClasses   = nullptr,
  .kernRightClasses  = nullptr,
  .kernMatrix        = nullptr,
  .kernLeftEntryCount  = 0,
  .kernRightEntryCount = 0,
  .kernLeftClassCount  = 0,
  .kernRightClassCount = 0,
  .ligaturePairs     = nullptr,
  .ligaturePairCount = 0,
  .glyphMissHandler  = nullptr,
  .glyphMissCtx      = nullptr,
};

// Base test font plus a U+FFFD replacement glyph (width 7) — used to verify
// that unrenderable emoji keep drawing the replacement box.
const EpdGlyph kFffdGlyphs[] = {
  { 8, 12, 137, 0, 12, 0, 0 },  // 'T'
  { 7,  8, 130, 0,  8, 0, 0 },  // 'a'
  { 8,  8, 145, 0,  8, 0, 0 },  // 'o'
  { 7,  8, 136, 0,  8, 0, 0 },  // 'x'
  { 7,  8, 120, 0,  8, 0, 0 },  // U+FFFD
};
const EpdUnicodeInterval kFffdIntervals[] = {
  { 0x54, 0x54, 0 },
  { 0x61, 0x61, 1 },
  { 0x6F, 0x6F, 2 },
  { 0x78, 0x78, 3 },
  { 0xFFFD, 0xFFFD, 4 },
};
const EpdFontData kFffdFontData = {
  .bitmap            = nullptr,
  .glyph             = kFffdGlyphs,
  .intervals         = kFffdIntervals,
  .intervalCount     = 5,
  .advanceY          = 16,
  .ascender          = 12,
  .descender         = 0,
  .is2Bit            = false,
  .groups            = nullptr,
  .groupCount        = 0,
  .glyphToGroup      = nullptr,
  .kernLeftClasses   = kKernLeft,
  .kernRightClasses  = kKernRight,
  .kernMatrix        = kKernMatrix,
  .kernLeftEntryCount  = 2,
  .kernRightEntryCount = 2,
  .kernLeftClassCount  = 2,
  .kernRightClassCount = 2,
  .ligaturePairs     = nullptr,
  .ligaturePairCount = 0,
  .glyphMissHandler  = nullptr,
  .glyphMissCtx      = nullptr,
};
// clang-format on

const EpdFont& emojiTestFont() {
  static EpdFont font(&kEmojiFontData);
  return font;
}

}  // namespace

TEST(EpdFont, EmojiFallbackNotRegistered) {
  testFont().setEmojiFallback(nullptr);
  // Base font has no FFFD: an emoji with no fallback contributes nothing.
  EXPECT_EQ(textWidth("\xF0\x9F\x98\x80"), 0);  // U+1F600
}

TEST(EpdFont, EmojiFallbackUsesFallbackGlyph) {
  testFont().setEmojiFallback(&emojiTestFont());
  EXPECT_EQ(textWidth("\xF0\x9F\x98\x80"), 10);  // U+1F600 from fallback font
  testFont().setEmojiFallback(nullptr);
}

TEST(EpdFont, EmojiFallbackAdvanceIsAdditive) {
  testFont().setEmojiFallback(&emojiTestFont());
  // "😀x": emoji advance is exactly 10px (no kerning to/from emoji — codepoints
  // above 0xFFFF are excluded from kern lookup), 'x' occupies [10, 17).
  EXPECT_EQ(textWidth("\xF0\x9F\x98\x80x"), 17);
  // Text-only strings are unaffected by the fallback registration.
  EXPECT_EQ(textWidth("ax"), 15);
  testFont().setEmojiFallback(nullptr);
}

TEST(EpdFont, EmojiFallbackSkipsZwjAndVs16) {
  testFont().setEmojiFallback(&emojiTestFont());
  // ZWJ (U+200D) and VS16 (U+FE0F) are invisible controls: zero width, no box.
  EXPECT_EQ(textWidth("\xF0\x9F\x98\x80\xE2\x80\x8D\xF0\x9F\x98\x80"), 20);  // 😀 ZWJ 😀
  EXPECT_EQ(textWidth("\xF0\x9F\x98\x80\xEF\xB8\x8F"), 10);                  // 😀 VS16
  testFont().setEmojiFallback(nullptr);
}

TEST(EpdFont, EmojiFallbackUncoveredKeepsReplacementBox) {
  // Primary WITH a U+FFFD glyph: an emoji neither font covers must keep the
  // replacement box (pre-emoji behaviour preserved on purpose).
  EpdFont fffdFont(&kFffdFontData);
  fffdFont.setEmojiFallback(&emojiTestFont());

  int w = 0, h = 0;
  fffdFont.getTextDimensions("\xF0\x9F\x98\x81", &w, &h);  // U+1F601 uncovered
  EXPECT_EQ(w, 7);                                         // FFFD box width
  fffdFont.getTextDimensions("\xF0\x9F\x98\x80", &w, &h);  // U+1F600 covered
  EXPECT_EQ(w, 10);                                        // fallback glyph
  // Non-emoji missing codepoints keep the box too.
  fffdFont.getTextDimensions("Z", &w, &h);
  EXPECT_EQ(w, 7);
}
