#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class MeshCoreThreadActivity final : public Activity {
 public:
  // Channel thread constructor
  MeshCoreThreadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, MeshCoreClient& client,
                         MeshCoreMessageStore& store, uint8_t channelIdx, const char* channelName);

  // Direct message thread constructor
  MeshCoreThreadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, MeshCoreClient& client,
                         MeshCoreMessageStore& store, const MeshCoreContact& contact);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  MeshCoreClient& client;
  MeshCoreMessageStore& store;
  ButtonNavigator buttonNavigator;

  bool isChannel = false;
  uint8_t channelIdx = 0;
  char threadName[64] = {};
  uint8_t contactPubkey[32] = {};

  MeshCoreMessage messages[MSGS_PER_PAGE] = {};
  uint8_t msgCount = 0;
  uint16_t totalMessages = 0;
  uint16_t pageOffset = 0;

  void loadPage();
  void sendMessage();
  void nextPage();
  void prevPage();
};
