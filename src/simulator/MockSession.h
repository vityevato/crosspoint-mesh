#pragma once

// Mock session singleton for MeshCore BLE simulation.
// Manages mock lifecycle and provides access to parsed mock data.
// All code is SIMULATOR-only.

#ifdef SIMULATOR

#include <ArduinoJson.h>

#include <cstdint>

#include "NimBLEDevice.h"

// Manages mock lifecycle. All methods are static; only one mock config
// can be loaded at a time.
class MockSession {
 public:
  // Load companions from JSON at `jsonPath` (relative to SD root, i.e.
  // fs_/ in simulator). Returns true on success. On failure, mock stays
  // inactive and error is logged.
  static bool loadMockConfig(const char* jsonPath);

  // True when a valid config has been loaded and at least one companion
  // passed validation (name + bleAddress non-empty).
  static bool isMockActive() { return companions != nullptr && companionCount > 0; }

  // Free all heap-allocated mock data. Safe to call when inactive.
  static void unloadMockConfig();

  // Accessors (valid only when isMockActive())
  static const MockCompanion* getCompanions() { return companions; }
  static uint8_t getCompanionCount() { return companionCount; }

  // Inject counters for hotkey-driven contact/channel creation.
  // Reset on loadMockConfig(). Incremented by MeshCoreMockHotkeys.
  static uint16_t injectContactCounter;
  static uint8_t injectChannelIdx;

  static void resetInjectCounters() {
    injectContactCounter = 0;
    injectChannelIdx = 8;  // start inject channels at idx 8 (above JSON channels)
  }

 private:
  static MockCompanion* companions;
  static uint8_t companionCount;

  // Parse one companion from JSON object. Populates `out`.
  // Returns true (validation done by caller via isValid()).
  static bool parseCompanion(JsonObjectConst obj, MockCompanion& out);
  static void parseContact(JsonObjectConst obj, MockContact& out);
  static void parseChannel(JsonObjectConst obj, MockChannel& out);
  static void parseMessage(JsonObjectConst obj, MockMessage& out);
  static void parseDiscoveredNode(JsonObjectConst obj, MockDiscoveredNode& out);
};

#endif  // SIMULATOR
