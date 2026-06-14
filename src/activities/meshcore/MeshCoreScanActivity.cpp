#include "MeshCoreScanActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#ifdef SIMULATOR
#include <MeshCoreMockHotkeys.h>
#endif

#include <MeshCore/MeshCoreMessageStore.h>

#include "MeshCoreSubtitle.h"
#include "activities/ActivityResult.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

MeshCoreScanActivity::MeshCoreScanActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           MeshCoreClient& client)
    : Activity("MeshCoreScan", renderer, mappedInput), client(client) {}

void MeshCoreScanActivity::onEnter() {
  Activity::onEnter();

  // Check heap
  if (ESP.getFreeHeap() < 30000) {
    LOG_ERR("MESH", "Heap too low for BLE: %d", ESP.getFreeHeap());
    finish();
    return;
  }

  // BLE is already initialized by MeshCoreHubActivity — start scan directly.
  startScan();
}

void MeshCoreScanActivity::startScan() {
  scanComplete = false;
  connectFailed = false;
  connecting = false;
  selectedIndex = 0;
  client.startScan(10);
  requestUpdate();
}

void MeshCoreScanActivity::connectToSelected() {
  const auto* results = client.getScanResults();
  uint8_t count = client.getScanResultCount();
  if (selectedIndex >= count) return;

  const auto& selected = results[selectedIndex];
  const char* knownAddr = client.getAutoReconnectAddress();
  bool isKnown = knownAddr[0] != '\0' && strcmp(knownAddr, selected.address) == 0;

  // If not the auto-reconnect companion, check if we have stored data (PIN) for this one
  if (!isKnown) {
    uint32_t storedPin = 123456;
    if (MeshCoreMessageStore::loadCompanionPinForAddress(selected.address, &storedPin)) {
      isKnown = true;
      client.setConnectPin(storedPin);
    }
  }

  if (isKnown) {
    // Known device — connect with stored PIN
    connecting = true;
    connectFailed = false;
    client.connectTo(selected.address, selected.addressType);
    requestUpdate();
  } else {
    // Truly unknown device — prompt user for BLE PIN with MeshCore factory default
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_MESHCORE_ENTER_PIN), "123456", 6,
                                                InputType::Text),
        [this, addr = std::string(selected.address), addrType = selected.addressType](const ActivityResult& result) {
          if (result.isCancelled) {
            requestUpdate();
            return;
          }
          const auto& pinStr = std::get<KeyboardResult>(result.data).text;
          uint32_t pin = strtoul(pinStr.c_str(), nullptr, 10);
          client.setConnectPin(pin);
          connecting = true;
          connectFailed = false;
          client.connectTo(addr.c_str(), addrType);
          requestUpdate();
        });
  }
}

void MeshCoreScanActivity::loop() {
#ifdef SIMULATOR
  if (handleMockKey("Scan", nullptr)) {
    requestUpdate();
    return;
  }
  pollMock(nullptr, millis());
#endif

  auto bleState = client.getState();

  // Detect scan completion
  if (!scanComplete && !connecting && bleState != BleConnectionState::SCANNING) {
    scanComplete = true;
    requestUpdate();
  }

  // Detect connect result
  if (connecting) {
    if (bleState == BleConnectionState::CONNECTED) {
      LOG_INF("MESH", "Connected to companion");
      connecting = false;
      setResult(ActivityResult());
      finish();
      return;
    }
    if (bleState == BleConnectionState::DISCONNECTED) {
      LOG_ERR("MESH", "Connection failed");
      connecting = false;
      connectFailed = true;
      client.setAutoReconnectAddress("");

      // If this companion has a stored PIN, show PIN entry immediately
      const auto* results = client.getScanResults();
      if (results && selectedIndex < client.getScanResultCount()) {
        uint32_t storedPin = 123456;
        if (MeshCoreMessageStore::loadCompanionPinForAddress(results[selectedIndex].address, &storedPin)) {
          char defaultPinStr[8];
          snprintf(defaultPinStr, sizeof(defaultPinStr), "%lu", (unsigned long)storedPin);
          startActivityForResult(
              std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_MESHCORE_ENTER_PIN), defaultPinStr,
                                                      6, InputType::Text),
              [this, addr = std::string(results[selectedIndex].address),
               addrType = results[selectedIndex].addressType](const ActivityResult& result) {
                if (result.isCancelled) {
                  requestUpdate();
                  return;
                }
                const auto& pinStr = std::get<KeyboardResult>(result.data).text;
                uint32_t pin = strtoul(pinStr.c_str(), nullptr, 10);
                client.setConnectPin(pin);
                connecting = true;
                connectFailed = false;
                client.connectTo(addr.c_str(), addrType);
                requestUpdate();
              });
          return;
        }
      }

      requestUpdate();
    }
    // Still CONNECTING/INITIALIZING — wait
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (bleState == BleConnectionState::SCANNING) {
      client.stopScan();
    }
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }

  uint8_t resultCount = client.getScanResultCount();

  if (!connecting && scanComplete && resultCount > 0) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      connectToSelected();
      return;
    }

    buttonNavigator.onNextRelease([this, resultCount] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, resultCount);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, resultCount] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, resultCount);
      requestUpdate();
    });
  }

  // Retry scan on Right button when no results or scan complete
  if (!connecting && scanComplete && mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    startScan();
  }
}

void MeshCoreScanActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  char headerSubtitle[64];
  formatMeshCoreSubtitle(client, headerSubtitle, sizeof(headerSubtitle));
  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), tr(STR_MESHCORE),
                 headerSubtitle);

  const int contentTop = metrics.topPadding + metrics.headerHeight;
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.topPadding;

  const auto* results = client.getScanResults();
  uint8_t resultCount = client.getScanResultCount();

  if (connecting) {
    // Connecting to selected device
    const char* connectingText = tr(STR_CONNECTING);
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, connectingText);
  } else if (!scanComplete) {
    // Scanning in progress
    const char* scanning = tr(STR_MESHCORE_SCANNING);
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, scanning);
  } else if (resultCount == 0) {
    // No devices found
    const char* noDevices = tr(STR_MESHCORE_NO_DEVICES);
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, noDevices);
  } else {
    // Draw scan results list
    GUI.drawList(
        renderer, Rect(0, contentTop, pageWidth, contentHeight), resultCount, selectedIndex,
        [results](int index) { return std::string(results[index].name); },
        [results](int index) {
          char subtitle[32];
          snprintf(subtitle, sizeof(subtitle), "RSSI: %d dBm", results[index].rssi);
          return std::string(subtitle);
        });
  }

  // Button hints
  const char* btn2 = "";
  const char* btn4 = "";
  if (!connecting && scanComplete && resultCount > 0) {
    btn2 = tr(STR_CONNECT);
    btn4 = tr(STR_DIR_DOWN);
  }
  const char* btn3 = (!connecting && scanComplete) ? tr(STR_MESHCORE_RETRY) : "";

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), btn2, btn3, btn4);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void MeshCoreScanActivity::onExit() {
  // BLE lifecycle is owned by MeshCoreHubActivity.
  Activity::onExit();
}
