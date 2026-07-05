#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>
#include <string>
#include <vector>

#include "StatusMessageOverlay.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

class MeshCoreHubActivity;

/**
 * MeshCoreThreadActivity shows a pixel-paginated message thread for either a
 * LoRa channel (group chat) or a direct message conversation with a
 * contact, with a two-tab UI:
 *  - MESSAGES — batch-loaded message list with send capability.
 *  - MENU — context-sensitive actions that differ for channels vs DMs.
 *
 * Only the visible batch (~10 messages) is kept in RAM. Messages are loaded
 * on demand from SD card using height-based batch queries. Scroll state is
 * managed via ConvMeta (positionId / positionPx / totalPx / fontId).
 *
 * Two constructors:
 *  - Channel thread: takes a channel index and name.
 *  - Direct message thread: takes a MeshCoreContact (stores pubkey).
 *
 * Tab navigation follows the same Settings-style pattern as the hub:
 *   selectedIndex == 0 → tab bar is highlighted, Confirm cycles tabs.
 *   selectedIndex > 0  → list item is highlighted, Confirm selects it.
 */
class MeshCoreThreadActivity final : public Activity {
 public:
  // Channel thread constructor
  MeshCoreThreadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, MeshCoreClient& client,
                         MeshCoreMessageStore& store, uint8_t channelIdx, const char* channelName,
                         MeshCoreHubActivity* hub);

  // Direct message thread constructor
  MeshCoreThreadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, MeshCoreClient& client,
                         MeshCoreMessageStore& store, const MeshCoreContact& contact, MeshCoreHubActivity* hub);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

  /// Called by Hub when a delivery callback fires for this conversation.
  /// Reloads visible messages from store and triggers a repaint.
  void onDeliveryUpdate(uint32_t msgId, const uint8_t* pubkey32, DeliveryStatus status);

  /// Expose pubkey for Hub to match delivery callbacks to this conversation.
  const uint8_t* contactPubkeyForDelivery() const { return contactPubkey; }

 private:
  enum class Tab : uint8_t { MESSAGES = 0, MENU, TAB_COUNT };

  MeshCoreClient& client;
  MeshCoreMessageStore& store;
  MeshCoreHubActivity* _hub = nullptr;
  ButtonNavigator buttonNavigator;

  bool isChannel = false;
  uint8_t channelIdx = 0;
  char threadName[64] = {};
  uint8_t contactPubkey[32] = {};

  // Batch loading — only visible messages, not the whole thread
  static constexpr uint8_t MAX_VISIBLE_BATCH = 10;
  MeshCoreMessage _visibleMsgs[MAX_VISIBLE_BATCH] = {};
  uint8_t _visibleCount = 0;

  // Filler message: the next message after the visible batch (used to fill
  // empty space at the bottom of the viewport). id == 0 means no filler.
  MeshCoreMessage _fillerMsg = {};

  // Cached conversation metadata (scroll state lives here)
  ConvMeta _meta = {};

  // Rendering bookkeeping (derived during draw, reset on scroll)
  uint16_t _accHeight = 0;
  uint32_t _firstVisibleId = 0;
  uint32_t _lastVisibleId = 0;

  // Render
  int _bodyFontId = 0;
  int _contentAreaWidth = 0;
  int _contentAreaHeight = 0;
  bool _needsRebuild = false;

  // Async BLE unlist/delete operation (mirrors Discovery's pattern)
  enum class PendingOp : uint8_t { IDLE, DELETING_CONTACT, DELETING_CHANNEL };
  PendingOp _pendingOp = PendingOp::IDLE;
  uint32_t _pendingStartMs = 0;
  void completeUnlistOp(bool success);

  // Tab state
  Tab currentTab = Tab::MESSAGES;
  int selectedIndex = 0;

  // Ephemeral toast overlay
  StatusMessageOverlay _toast;

  int contentHeight() const;

  /**
   * Load a batch of messages from the store and calculate _accHeight.
   * @return true if any messages were loaded.
   */
  bool loadMessages(uint32_t startId, bool up);
  void drawVisibleMessages(const GfxRenderer& renderer, Rect rect, bool useReaderFontSettings, bool scanOnly = false);
  void scrollDownPage();
  void scrollUpPage();
  void scrollDownByMessage();
  void scrollUpByMessage();

  /** Menu action: jump to end of conversation. */
  void scrollToEnd();
  /** Menu action: clear all messages in this conversation. */
  void clearConversation();

  void savePosition();
  void sendMessage();

  void _rebuildMessageHeights();

  void switchTab(Tab tab);
  int getListCountForCurrentTab() const;

  void renderMenu(const Rect& contentRect);

  /** Trampoline for StatusMessageOverlay subtitle provider. */
  static void provideSubtitle(const void* ctx, char* buf, size_t bufSize);
};
