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

  void startScan();
  void connectToSelected();
};
