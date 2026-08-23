// ---- MeshCoreHubActivity.h ----
#pragma once

#include <HalStorage.h>
#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <memory>

#include "StatusMessageOverlay.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

class MeshCoreThreadActivity;

/**
 * MeshCoreHubActivity is the central entry point for all MeshCore
 * functionality. It owns the BLE client and message store for its
 * entire lifetime.
 *
 * On enter:
 * - Initialises BLE, disconnects WiFi (ESP32-C3 shares radio).
 * - Launches MeshCoreScanActivity to find and pair with a companion.
 * - On reconnect-enabled: auto-connects via stored bonding.
 *
 * After connection, it presents a three-tab UI:
 *  - CONTACTS — saved peer contacts with unread counts.
 *  - CHANNELS — LoRa channels from the companion.
 *  - MENU — secondary actions: Discovery Nodes, Send Advert,
 *    Send Flood Advert, Status, Disconnect.
 *
 * Callbacks (static → instance trampolines) handle BLE state changes,
 * incoming messages, contacts, adverts, and channel list updates.
 */
class MeshCoreHubActivity final : public Activity {
 public:
  explicit MeshCoreHubActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MeshCoreHub", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool isMeshCoreActivity() const override { return true; }

 private:
  enum class Tab : uint8_t { CONTACTS = 0, CHANNELS, MENU, TAB_COUNT };

  MeshCoreClient client;
  MeshCoreMessageStore store;
  ButtonNavigator buttonNavigator;

  Tab currentTab = Tab::CONTACTS;
  int selectedIndex = 0;
  bool autoReconnecting = false;
  bool reconnectOnDisconnect = false;
  bool pendingAutoScan = false;

  // Menu navigation states
  bool showingStatus = false;
  bool showingDisconnectPopup = false;
  MeshCoreCompanion lastCompanion = {};  // Cached for disconnected status view
  uint32_t lastBatteryRequestMs = 0;
  uint16_t lastBatteryMv = 0;

  // Advert status feedback
  bool _advertInFlight = false;
  bool _advertIsFlood = false;
  uint32_t _advertSentTime = 0;

  // Ephemeral toast overlay (status messages + standard subtitle)
  StatusMessageOverlay _toast;

  // Data loaded from client/store
  MeshCoreChannel channels[MESHCORE_MAX_CHANNELS] = {};
  uint8_t channelCount = 0;

  // Transient buffers with fixed (small) capacities. savedContacts below grows
  // dynamically up to MESHCORE_MAX_CONTACTS, bounded by free heap.
  static constexpr uint16_t MESHCORE_DISCOVERED_NODES_MAX = 32;
  static constexpr uint16_t MESHCORE_FILE_IMPORT_MAX = 64;

  // Contact-list sync bookkeeping.
  // _contactsDirty: a companion state change happened during the stream — flush
  //   the file once at PKT_CONTACT_END instead of once per contact (each write is
  //   a full SD rewrite and slowed the main loop, aggravating RX overflow).
  // _contactSyncSeen/Total/Retries: verify a full GET_CONTACTS delivered every
  //   frame the companion reported; re-fetch the list when it fell short.
  bool _contactsDirty = false;
  uint16_t _contactSyncSeen = 0;
  uint32_t _contactSyncTotal = 0;
  uint8_t _contactSyncRetries = 0;
  static constexpr uint8_t MAX_CONTACT_SYNC_RETRIES = 3;

  // Batch contact loading from file (async via BLE, like DiscoverActivity)
  std::unique_ptr<MeshCoreContact[]> _pendingFileContacts;
  uint16_t _pendingFileContactCount = 0;
  uint16_t _pendingFileContactIndex = 0;
  uint16_t _pendingFileContactSuccessCount = 0;
  bool _contactsFileLoadPending = false;
  uint32_t _contactsFileLoadStartMs = 0;

  // Saved contacts (authoritative in-RAM copy, persisted to contacts.bin).
  // Capacity grows on demand (see ensureContactsCapacity) so we never reserve
  // MESHCORE_MAX_CONTACTS up front — that would starve the reconnect scan.
  std::unique_ptr<MeshCoreContact[]> savedContacts;
  uint16_t savedContactCount = 0;
  uint16_t savedContactsCapacity = 0;

  // Sorting keyed by last-message activity (contacts + channels).
  // contactLastActivity[i] is the timestamp of the most recent *received* direct
  // message from savedContacts[i] (0 = none yet). contactSortIndex maps a display
  // position to a savedContacts index: favourites (flags bit 0) first, then by
  // activity desc, name asc. contactSortIndex has identity order until the
  // background sweep (loop()) populates the activity cache.
  std::unique_ptr<uint32_t[]> contactLastActivity;
  std::unique_ptr<uint16_t[]> contactSortIndex;
  uint32_t channelLastActivity[MESHCORE_MAX_CHANNELS] = {};
  bool _activitySweepPending = false;
  uint16_t _activitySweepIndex = 0;
  bool _channelActivityNeedsLoad = true;

  // Recently seen nodes (adverts) — transient, small fixed buffer.
  std::unique_ptr<MeshCoreContact[]> discoveredNodes;
  uint16_t discoveredNodeCount = 0;
  uint16_t discoveredNodesCapacity = 0;

  // The currently-open Thread activity, set by Thread::onEnter().
  // Used to forward delivery callbacks for UI updates.
  MeshCoreThreadActivity* _activeThread = nullptr;

  // Callbacks (static -> instance via context pointer)
  static void onStateChanged(BleConnectionState state, void* ctx);
  static void onMessageReceived(const MeshCoreMessage& msg, void* ctx);
  static void onContactReceived(const MeshCoreContact& c, bool isEnd, void* ctx);
  static void onAdvertReceived(const MeshCoreContact& node, void* ctx);
  static void onChannelReceived(const MeshCoreChannel& ch, void* ctx);
  static void onChannelHeard(uint8_t channelIdx, uint8_t heardCount, const uint8_t* hashes, void* ctx);
  static void onDeliveryStatic(uint32_t msgId, const uint8_t* pubkey32, DeliveryStatus status, void* ctx);

  void handleStateChange(BleConnectionState state);
  void handleMessage(const MeshCoreMessage& msg);
  void handleContact(const MeshCoreContact& c, bool isEnd);
  void handleAdvert(const MeshCoreContact& node);
  void handleChannel(const MeshCoreChannel& ch);
  void handleChannelHeard(uint8_t channelIdx, uint8_t heardCount);
  void handleDelivery(uint32_t msgId, const uint8_t* pubkey32, DeliveryStatus status);

  void renderChannelList(const Rect& contentRect);
  void renderContactList(const Rect& contentRect);
  void renderMenu(const Rect& contentRect);

  void openChannelThread(uint8_t channelIdx);
  void openContactThread(const MeshCoreContact& contact);

 public:
  /// Called by Thread::onEnter() to register for delivery UI updates.
  void setActiveThread(MeshCoreThreadActivity* t) { _activeThread = t; }
  /// Called by Thread::onExit() to unregister.
  void clearActiveThread(const MeshCoreThreadActivity* t) {
    if (_activeThread == t) _activeThread = nullptr;
  }

  /// Zeroes the unread counter of a channel conversation (called by the
  /// Thread when new messages arrive while the user is viewing the end of
  /// the conversation, so they are not "unread").
  void markChannelRead(uint8_t channelIdx);
  /// Zeroes the unread counter of a direct-message conversation, matching
  /// the contact by public key.
  void markContactRead(const uint8_t* pubkey32);

  /// Applies a committed favourite (flags bit 0) change: updates the in-RAM
  /// contact, persists, re-sorts and re-selects the contact at its new sorted
  /// position. Called by the DM Thread when a toggle completes (the companion
  /// already ACKed it), so the user never leaves the conversation.
  void handleContactFavouriteResult(const uint8_t* pubkey32, bool favourite);

 public:
  /// File path for saving/loading contacts (shared constant).
  static constexpr const char* MESHCORE_CONTACTS_FILE = "/meshcore_contacts.txt";

 private:
  // cppcheck-suppress unusedPrivateFunction; used in issue 2-AFK
  void openDiscover();
  void launchScanActivity();
  void switchTab(Tab tab);

  void saveAdvertToFile();
  void loadContactsFromFile();
  void advanceFileContactLoad(bool success);
  void shareContactQr();

  /** Grows savedContacts so it can hold at least needed entries. Bounded by
   *  MESHCORE_MAX_CONTACTS and by free heap (reconnect scan must stay viable).
   *  Returns true when the array is big enough (possibly by doing nothing). */
  bool ensureContactsCapacity(uint16_t needed);
  /** Loads savedContacts from the store, growing the buffer as needed. */
  void reloadContactsFromStore();
  /** Sorts contactSortIndex by (favourite, lastActivity desc, name asc).
   *  Before reordering, anchors the cursor to the highlighted contact (by
   *  public key) and re-selects it afterwards, so a sort change that happens
   *  while a conversation is open (message activity, favourite toggle, sweep
   *  completion) keeps the cursor on the same contact and the list scrolls to
   *  it — instead of leaving selectedIndex pointing at a different row. */
  void rebuildContactSortIndex();
  /** Selects the contact identified by @p pubkey32 at its current sorted
   *  position (0 = tab bar, i+1 = sorted contact row) so the list scrolls to
   *  it. Falls back to @p fallbackSelected when the contact is absent (e.g.
   *  removed). No-op unless the Contacts tab is active. */
  void selectContactInList(const uint8_t* pubkey32, int fallbackSelected);
  /** Begins the background per-contact last-message sweep (runs in loop()). */
  void startContactActivitySweep();

  int getListCountForCurrentTab() const;

  /** Fills outIdx with raw indices of configured channels; returns count. */
  uint8_t collectVisibleChannels(uint8_t* outIdx) const;

  /** Trampoline for StatusMessageOverlay subtitle provider. */
  static void provideSubtitle(const void* ctx, char* buf, size_t bufSize);
};
