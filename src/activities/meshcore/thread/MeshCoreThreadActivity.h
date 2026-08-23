#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>
#include <cstring>
#include <memory>

#include "../MeshCoreSettings.h"
#include "../StatusMessageOverlay.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

class MeshCoreHubActivity;

#include "ThreadScroller.h"

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

  /// Whether the DM contact is marked favourite (flags bit 0). Snapshot taken
  /// at construction from the contact record; the Hub is the reconciler.
  bool contactIsFavourite() const { return _isFavourite; }

  /// True when this thread is the direct-message conversation with @p pubkey32.
  bool matchesContact(const uint8_t* pubkey32) const { return !isChannel && memcmp(contactPubkey, pubkey32, 32) == 0; }

  /// True when this thread is the channel conversation @p chIdx.
  bool matchesChannel(uint8_t chIdx) const { return isChannel && channelIdx == chIdx; }

  friend struct ThreadMessenger;
  friend struct ThreadMenuRenderer;

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
  /// Favourite state snapshot (flags bit 0) taken at construction.
  bool _isFavourite = false;

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
  uint32_t _accHeight = 0;
  uint32_t _firstVisibleId = 0;
  uint32_t _lastVisibleId = 0;

  // Render
  int _bodyFontId = 0;
  int _contentAreaWidth = 0;
  int _contentAreaHeight = 0;
  bool _needsRebuild = false;

  // Async BLE operations (mirror Discovery's pattern): the UI shows a
  // persistent toast, then polls for the companion's PKT_OK/error before
  // committing any local state.
  enum class PendingOp : uint8_t { IDLE, DELETING_CONTACT, SETTING_FAVOURITE };
  PendingOp _pendingOp = PendingOp::IDLE;
  uint32_t _pendingStartMs = 0;
  /// Target favourite state for an in-flight SETTING_FAVOURITE op.
  bool _pendingFavouriteTarget = false;
  void completeUnlistOp(bool success);
  void completeFavouriteOp(bool success);

  // Scroll state machine — created in onEnter, deleted in onExit.
  ThreadScroller* _scroller = nullptr;

  // Confirmation popup state (shown before destructive menu actions)
  enum class ConfirmAction : uint8_t { NONE, CLEAR_CONVERSATION, REMOVE_CONTACT };
  ConfirmAction _confirmAction = ConfirmAction::NONE;

  // Tab state
  Tab currentTab = Tab::MESSAGES;
  int selectedIndex = 0;

  // Ephemeral toast overlay
  StatusMessageOverlay _toast;

  // Menu settings — loaded when MENU tab is opened, freed on tab switch or exit
  std::unique_ptr<MeshCoreSettings> _menuSettings;

  int contentHeight() const;

  /**
   * Load a batch of messages from the store and calculate _accHeight.
   * @return true if any messages were loaded.
   */
  bool loadMessages(uint32_t startId, bool up);
  void drawVisibleMessages(const GfxRenderer& renderer, Rect rect, int bodyFontId, bool scanOnly = false);

  // ── loop() decomposition ──
  void _loopBleStateMachine();
  void _loopDetectNewMessages();
  bool _loopConfirmPopup();
  void _loopInput();
  bool _loopInputBack();
  bool _loopInputConfirm();
  void _loopInputNav();

  void scrollDownPage();
  void scrollUpPage();
  void scrollDownByMessage();
  void scrollUpByMessage();

  /** Menu action: jump to end of conversation. */
  void scrollToEnd();
  /** Menu action: clear all messages in this conversation. */
  void clearConversation();
  /** Menu action: show this contact's share QR (DM threads only). */
  void shareContactQr();

  void savePosition();
  void sendMessage();

  void _rebuildMessageHeights();

  void resolveBodyFont();
  void switchTab(Tab tab);
  int getListCountForCurrentTab() const;

  void renderMenu(const Rect& contentRect);

  // ── render() decomposition ──
  bool _renderFontRebuildPopup();
  bool _renderConfirmPopup();
  void _renderNormal();

  /** Trampoline for StatusMessageOverlay subtitle provider. */
  static void provideSubtitle(const void* ctx, char* buf, size_t bufSize);
};
