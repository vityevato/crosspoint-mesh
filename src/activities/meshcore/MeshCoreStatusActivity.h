#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreTypes.h>

#include "activities/Activity.h"

struct Rect;

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
