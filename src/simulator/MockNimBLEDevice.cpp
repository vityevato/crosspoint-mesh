// Mock NimBLE device implementations for desktop simulator.
// Separated from NimBLEDevice.h to break circular include dependency:
// MockSession.h includes NimBLEDevice.h, and NimBLEDevice.h needs
// MockSession for companion lookup in NimBLEClient::connect().

#ifdef SIMULATOR

#include <Logging.h>

#include <cstring>
#include <string>

#include "MockSession.h"
#include "NimBLEDevice.h"

bool NimBLEClient::connect(const NimBLEAddress& addr) {
  if (!MockSession::isMockActive()) return false;
  const auto* comps = MockSession::getCompanions();
  uint8_t count = MockSession::getCompanionCount();
  std::string addrStr = addr.toString();
  for (uint8_t i = 0; i < count; ++i) {
    if (addrStr == std::string(comps[i].bleAddress)) {
      mockCompanion = &comps[i];
      nusService.setMockCompanion(mockCompanion);
      connected = true;
      return true;
    }
  }
  LOG_ERR("MOCK", "connect(): no companion with address %s", addrStr.c_str());
  return false;
}

NimBLEClient* NimBLEDevice::createClient() {
  if (!MockSession::isMockActive()) return nullptr;
  auto* client = new (std::nothrow) NimBLEClient();
  if (!client) {
    LOG_ERR("MOCK", "createClient(): OOM");
    return nullptr;
  }
  return client;
}

#endif  // SIMULATOR
