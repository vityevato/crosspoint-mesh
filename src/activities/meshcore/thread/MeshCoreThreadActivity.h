#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../MeshCoreDisconnectPopup.h"
#include "../MeshCoreSettings.h"
#include "../StatusMessageOverlay.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
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
  /// Reloads visible messages from store and triggers a repaint, keeping the
  /// in-RAM scroll position (status flips never move the view).
  void onDeliveryUpdate(uint32_t msgId, const uint8_t* pubkey32, DeliveryStatus status);

  /// Called by Hub when the repeater heard-count for this channel changes.
  /// Reloads visible messages from store and triggers a repaint, keeping the
  /// in-RAM scroll position (heard-count flips never move the view).
  void onChannelHeardUpdate(uint8_t chIdx, uint8_t heardCount);

  /**
   * Show the disconnect popup and return to the hub shortly after.
   * @param sendFailed true when a just-typed message could not be sent
   *        (popup shows the "message not sent" text instead of the
   *        generic connection-lost text).
   */
  void notifyDisconnect(bool sendFailed);

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
  friend struct ThreadReply;

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

  // Reusable scratch for the MENU scans (refreshLastSent / refreshTargets).
  // Scanning one message at a time replaces the old MeshCoreMessage[16] heap
  // batch (~4.3 KB transient) whose allocation split the largest free block
  // and starved the font prewarm after the reply/repeat flow.
  MeshCoreMessage _scanMsg = {};

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
  enum class PendingOp : uint8_t { IDLE, DELETING_CONTACT, SETTING_FAVOURITE, SENDING_LOCATION };
  PendingOp _pendingOp = PendingOp::IDLE;
  uint32_t _pendingStartMs = 0;
  /// Target favourite state for an in-flight SETTING_FAVOURITE op.
  bool _pendingFavouriteTarget = false;
  void completeUnlistOp(bool success);
  void completeFavouriteOp(bool success);
  /// Completion handler for SENDING_LOCATION: formats the fresh companion fix
  /// as "lat,lon" and opens the composer prefilled, or toasts "no fix".
  void completeLocationOp(bool success);
  /// Shared menu action for channel and DM menus: validates GPS availability
  /// and queues CMD_SEND_TELEMETRY_REQ ('self'). Returns true when handled.
  bool startSendCoordinates();

  // Scroll state machine — created in onEnter, released in onExit.
  std::unique_ptr<ThreadScroller> _scroller;

  // Confirmation popup state (shown before destructive menu actions)
  enum class ConfirmAction : uint8_t { NONE, CLEAR_CONVERSATION, REMOVE_CONTACT };
  ConfirmAction _confirmAction = ConfirmAction::NONE;

  // Tab state
  Tab currentTab = Tab::MESSAGES;
  int selectedIndex = 0;

  // Ephemeral toast overlay
  StatusMessageOverlay _toast;

  // Disconnect detection: popup + auto-return to the hub (hub reconnects).
  MeshCoreDisconnectPopup _dcPopup;

  // Menu settings — loaded when MENU tab is opened, freed on tab switch or exit
  std::unique_ptr<MeshCoreSettings> _menuSettings;

  // Reply picker (channel threads): recent senders, newest first, collected on
  // MENU entry by ThreadReply::refreshTargets(). The popup overlays the MENU
  // tab while active; _replyNames backs its options and the select callback.
  OptionPopup _replyPopup;
  std::vector<std::string> _replyNames;
  /// True when the last refresh found at least one reply target. Kept after
  /// _replyNames is released before opening the composer, so the MENU item
  /// stays enabled and openPicker() can rebuild the list lazily.
  bool _hasReplyTargets = false;
  /// True while the Confirm press that opens the picker is still held; the
  /// picker itself opens on the release (see _loopInput).
  bool _replyPickerPending = false;

  // Text of the newest outgoing (SENT) message in this conversation, cached on
  // MENU entry by refreshLastSent(). Empty means "Repeat Last Message" is
  // dimmed — the user has not sent anything here yet.
  std::string _lastSentText;

  // Scratch for the contact-share URL built by shareContactQr(): a member
  // instead of a 384-byte stack local (heap-discipline stack budget).
  char _shareUrl[384] = {};

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
  /** Menu action: cache the newest outgoing message for Repeat Last. */
  void refreshLastSent();
  /** Menu action: clear all messages in this conversation. */
  void clearConversation();
  /** Menu action: show this contact's share QR (DM threads only). */
  void shareContactQr();

  void savePosition();
  /** Open the send keyboard; @p initialText prefills it (reply mention). */
  void sendMessage(const char* initialText = "");

  void _rebuildMessageHeights();

  void resolveBodyFont();
  void switchTab(Tab tab);
  int getListCountForCurrentTab() const;

  void renderMenu(const Rect& contentRect);

  // ── render() decomposition ──
  bool _renderFontRebuildPopup();
  bool _renderConfirmPopup();
  /** @param display false draws the frame without pushing it to the panel
   *         (the reply popup overlays it and refreshes once). */
  void _renderNormal(bool display = true);

  /** Trampoline for StatusMessageOverlay subtitle provider. */
  static void provideSubtitle(const void* ctx, char* buf, size_t bufSize);
};
