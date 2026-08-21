#pragma once

#include <HalStorage.h>
#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

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
  MeshCoreChannel channels[8] = {};
  uint8_t channelCount = 0;

  static constexpr uint8_t MAX_VISIBLE_CONTACTS = 20;

  // Batch contact loading from file (async via BLE, like DiscoverActivity)
  MeshCoreContact _pendingFileContacts[MAX_VISIBLE_CONTACTS] = {};
  uint8_t _pendingFileContactCount = 0;
  uint8_t _pendingFileContactIndex = 0;
  uint8_t _pendingFileContactSuccessCount = 0;
  bool _contactsFileLoadPending = false;
  uint32_t _contactsFileLoadStartMs = 0;
  MeshCoreContact savedContacts[MAX_VISIBLE_CONTACTS] = {};
  uint8_t savedContactCount = 0;

  MeshCoreContact discoveredNodes[MAX_VISIBLE_CONTACTS] = {};
  uint8_t discoveredNodeCount = 0;

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

  int getListCountForCurrentTab() const;

  /** Fills outIdx with raw indices of configured channels; returns count. */
  uint8_t collectVisibleChannels(uint8_t* outIdx) const;

  /** Trampoline for StatusMessageOverlay subtitle provider. */
  static void provideSubtitle(const void* ctx, char* buf, size_t bufSize);
};
