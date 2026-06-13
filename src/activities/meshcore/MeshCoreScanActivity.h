#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreTypes.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

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
