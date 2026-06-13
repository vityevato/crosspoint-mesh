#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class MeshCoreDiscoverActivity final : public Activity {
 public:
  MeshCoreDiscoverActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, MeshCoreClient& client,
                           MeshCoreMessageStore& store, MeshCoreContact* discoveredNodes,
                           uint8_t& discoveredNodeCount, MeshCoreContact* savedContacts,
                           uint8_t& savedContactCount);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  MeshCoreClient& client;
  MeshCoreMessageStore& store;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  MeshCoreContact* discoveredNodes;
  uint8_t& discoveredNodeCount;
  MeshCoreContact* savedContacts;
  uint8_t& savedContactCount;

  void addSelectedToContacts();
  bool isAlreadySaved(const MeshCoreContact& node) const;
};
