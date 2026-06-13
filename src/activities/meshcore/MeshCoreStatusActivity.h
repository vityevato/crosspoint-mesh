#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreTypes.h>

#include "activities/Activity.h"

struct Rect;

/**
 * MeshCoreStatusActivity displays companion device status information
 * after a successful BLE connection.
 *
 * Fields shown:
 *  - Device name, model, firmware version
 *  - Battery voltage (requested on enter, refreshable via Confirm)
 *  - Storage usage (used / total KB)
 *  - Radio configuration (frequency, bandwidth, SF, CR)
 *  - BLE address
 *
 * Displays "Disconnected" if the BLE link is lost.
 */
class MeshCoreStatusActivity final : public Activity {
 public:
  MeshCoreStatusActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, MeshCoreClient& client);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;
  bool preventAutoSleep() override { return true; }

 private:
  MeshCoreClient& client;

  void renderStatusFields(const Rect& contentRect);
};
