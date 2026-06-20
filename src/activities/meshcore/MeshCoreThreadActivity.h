#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>
#include <vector>

#include "StatusMessageOverlay.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

/**
 * MeshCoreThreadActivity shows a paginated message thread for either a
 * LoRa channel (group chat) or a direct message conversation with a
 * contact, with a two-tab UI:
 *  - MESSAGES — paginated message list with send capability.
 *  - MENU — context-sensitive actions that differ for channels vs DMs.
 *
 * Two constructors:
 *  - Channel thread: takes a channel index and name.
 *  - Direct message thread: takes a MeshCoreContact (stores pubkey).
 *
 * Messages are loaded from MeshCoreMessageStore in pages. The activity
 * auto-refreshes when the store grows (new messages from hub callbacks)
 * and automatically scrolls to the newest page.
 *
 * Tab navigation follows the same Settings-style pattern as the hub:
 *   selectedIndex == 0 → tab bar is highlighted, Confirm cycles tabs.
 *   selectedIndex > 0  → list item is highlighted, Confirm selects it.
 */
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
  enum class Tab : uint8_t { MESSAGES = 0, MENU, TAB_COUNT };

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

  // Tab state
  Tab currentTab = Tab::MESSAGES;
  int selectedIndex = 0;

  // Menu navigation states
  bool showingStatus = false;
  MeshCoreCompanion lastCompanion = {};
  uint32_t lastBatteryRequestMs = 0;
  uint16_t lastBatteryMv = 0;

  // Advert status feedback
  bool _advertInFlight = false;
  bool _advertIsFlood = false;
  uint32_t _advertSentTime = 0;

  // Ephemeral toast overlay
  StatusMessageOverlay _toast;

  void loadPage();
  void sendMessage();
  void nextPage();
  void prevPage();
  void switchTab(Tab tab);
  int getListCountForCurrentTab() const;

  void renderMenu(const Rect& contentRect);

  /** Trampoline for StatusMessageOverlay subtitle provider. */
  static void provideSubtitle(const void* ctx, char* buf, size_t bufSize);
};
