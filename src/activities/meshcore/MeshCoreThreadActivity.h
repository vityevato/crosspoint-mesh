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
 * MeshCoreThreadActivity shows a pixel-paginated message thread for either a
 * LoRa channel (group chat) or a direct message conversation with a
 * contact, with a two-tab UI:
 *  - MESSAGES — pixel-scrolled message list with send capability.
 *  - MENU — context-sensitive actions that differ for channels vs DMs.
 *
 * Two constructors:
 *  - Channel thread: takes a channel index and name.
 *  - Direct message thread: takes a MeshCoreContact (stores pubkey).
 *
 * All messages are loaded into a std::vector on entry. Navigation advances
 * by exactly one viewport height (contentHeight pixels). A pixel-perfect
 * scrollbar shows the position within the total content. The activity
 * auto-refreshes when the store grows (new messages from hub callbacks).
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

  // All messages loaded into RAM (max MAX_MSGS_PER_THREAD = 200)
  std::vector<MeshCoreMessage> messages;
  std::vector<uint16_t> msgHeights;
  uint16_t totalMessages = 0;
  uint16_t totalPixels = 0;
  uint16_t scrollOffsetPx = 0;
  uint16_t contentWidth = 0;

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
  void recomputeHeights();
  uint32_t firstVisibleGlobalId() const;
  int contentHeight() const;
  void scrollDown();
  void scrollUp();
  void scrollDownByMessage();
  void scrollUpByMessage();
  void scrollToEnd();
  void saveScrollPosition();
  void sendMessage();

  /// Result of resolving scrollOffsetPx into the first visible message index.
  struct VisibleState {
    int startIdx;       ///< First message with any part visible
    uint16_t acc;        ///< Sum of msgHeights[0..startIdx-1]
    int partialOffset;    ///< Pixels of startIdx scrolled off the top
  };
  [[nodiscard]] VisibleState getVisibleState() const;
  void switchTab(Tab tab);
  int getListCountForCurrentTab() const;

  void renderMenu(const Rect& contentRect);

  /** Trampoline for StatusMessageOverlay subtitle provider. */
  static void provideSubtitle(const void* ctx, char* buf, size_t bufSize);
};
