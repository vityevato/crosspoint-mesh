#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * MeshCoreDiscoverActivity shows a list of discovered MeshCore nodes
 * (advertised by nearby devices) and lets the user add them to saved
 * contacts.
 *
 * The node list is populated externally (by Hub callbacks) and passed
 * via constructor. Each entry shows name, public key prefix, hop count,
 * and SNR. Already-saved contacts are dimmed and cannot be re-added.
 */
class MeshCoreDiscoverActivity final : public Activity {
 public:
  MeshCoreDiscoverActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, MeshCoreClient& client,
                           MeshCoreMessageStore& store, MeshCoreContact* discoveredNodes, uint8_t& discoveredNodeCount,
                           MeshCoreContact* savedContacts, uint8_t& savedContactCount);

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
