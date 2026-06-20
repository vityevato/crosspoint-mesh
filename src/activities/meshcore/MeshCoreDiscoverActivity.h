#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>

#include "StatusMessageOverlay.h"
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

  StatusMessageOverlay _toast;

  MeshCoreContact* discoveredNodes;
  uint8_t& discoveredNodeCount;
  MeshCoreContact* savedContacts;
  uint8_t& savedContactCount;

  // Async BLE command state machine. PENDING = command fired, waiting
  // for companion PKT_OK; loop() polls client.getLastCommandResult().
  enum class PendingOp : uint8_t { IDLE, SAVING, DELETING };
  PendingOp _pendingOp = PendingOp::IDLE;
  MeshCoreContact _pendingContact = {};
  /// Index into savedContacts[] for a delete operation.
  uint8_t _pendingDeleteIndex = 0;
  uint32_t _pendingStartMs = 0;

  void addSelectedToContacts();
  void removeSelectedFromContacts();
  void completeContactSave(bool success);
  bool isAlreadySaved(const MeshCoreContact& node) const;
  bool isSavingInProgress(const MeshCoreContact& node) const;

  static void provideSubtitle(const void* ctx, char* buf, size_t bufSize);
};
