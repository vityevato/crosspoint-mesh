#pragma once

#include <cstdint>
#include <cstdio>
#include <map>

#include "EpdFontFamily.h"

class FontDecompressor {
 public:
  struct PrewarmCall {
    const EpdFontData* fontData = nullptr;
    char text[32] = {};
  };

  // Test hook: per-font max group size driving the prewarm-ordering logic.
  // Fonts not registered here report 0 (uncompressed / SD-font behaviour).
  static std::map<const EpdFontData*, uint32_t>& maxGroupBytesOverrides() {
    static std::map<const EpdFontData*, uint32_t> overrides;
    return overrides;
  }
  static uint32_t maxGroupBytes(const EpdFontData* fontData) {
    const auto it = maxGroupBytesOverrides().find(fontData);
    return it == maxGroupBytesOverrides().end() ? 0 : it->second;
  }

  void clearCache() {}
  int prewarmCache(const EpdFontData* fontData, const char* text) {
    auto& call = prewarmCalls[prewarmCallCount++];
    call.fontData = fontData;
    std::snprintf(call.text, sizeof(call.text), "%s", text);
    return 0;
  }
  void logStats(const char*) {}
  void resetStats() {}

  PrewarmCall prewarmCalls[4] = {};
  int prewarmCallCount = 0;
};
