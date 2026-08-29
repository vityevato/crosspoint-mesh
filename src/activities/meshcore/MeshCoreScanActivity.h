#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreTypes.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * MeshCoreScanActivity scans for MeshCore companion BLE devices and
 * initiates a connection to the selected device.
 *
 * Flow:
 * - Starts a BLE scan on entry (10-second window).
 * - Lists discovered devices for user selection.
 * - Prompts for a BLE PIN if the device is unknown; uses stored
 *   bonding if already paired.
 * - Finishes with success on connection, or shows failure on timeout.
 */
class MeshCoreScanActivity final : public Activity {
 public:
  MeshCoreScanActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, MeshCoreClient& client);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;
  bool preventAutoSleep() override { return true; }

 private:
  MeshCoreClient& client;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool scanComplete = false;
  bool connectFailed = false;
  bool connecting = false;

  /// Minimum free heap required to start a scan. The hub's own reconnect
  /// scan-polling runs `client.startScan()` with ~28 KB free and is stable, so
  /// the scan itself works well below the old 30 KB gate. That gate bailed when
  /// Scan was pressed from the reconnect screen (the hub still holds its
  /// contact/discovery buffers), silently returning to the hub. 20 KB keeps a
  /// safety margin for the scan results + the connect that follows.
  static constexpr uint32_t MIN_SCAN_HEAP_BYTES = 20000;

  void startScan();
  void connectToSelected();
};
