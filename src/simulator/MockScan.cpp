// Out-of-line implementation of NimBLEScan::getResults for the simulator.
// Defined here (not in NimBLEDevice.h) to avoid pulling ArduinoJson and
// HalStorage headers into MeshCoreClient.cpp via MockSession.h.
#ifdef SIMULATOR

#include "MockSession.h"
#include "NimBLEDevice.h"

NimBLEScanResults NimBLEScan::getResults(uint32_t /*durationMs*/, bool /*isContinue*/) {
  scanning = false;  // scan completes
  NimBLEScanResults results;
  if (MockSession::isMockActive()) {
    const MockCompanion* comps = MockSession::getCompanions();
    uint8_t count = MockSession::getCompanionCount();
    uint8_t counter = rescanCounter++;
    results.populateFromMock(comps, count, counter);
  }
  return results;
}

#endif  // SIMULATOR
