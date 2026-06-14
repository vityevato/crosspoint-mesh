#pragma once

#include <HalStorage.h>
#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

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
 * After connection, it presents a tabbed UI:
 *  - CHANNELS — list of LoRa channels from the companion.
 *  - CONTACTS — saved peer contacts.
 *  - DISCOVERED — advertised nodes not yet saved.
 *  - STATUS — companion device info (battery, firmware, radio, etc.).
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

 private:
  enum class Tab : uint8_t { CHANNELS = 0, CONTACTS, DISCOVERED, STATUS, TAB_COUNT };

  MeshCoreClient client;
  MeshCoreMessageStore store;
  ButtonNavigator buttonNavigator;
  HalFile sdLogFile;

  Tab currentTab = Tab::CHANNELS;
  int selectedIndex = 0;
  bool autoReconnecting = false;
  bool reconnectOnDisconnect = false;
  bool pendingAutoScan = false;
  uint32_t lastBatteryRequestMs = 0;
  uint16_t lastBatteryMv = 0;

  // Data loaded from client/store
  MeshCoreChannel channels[8] = {};
  uint8_t channelCount = 0;

  static constexpr uint8_t MAX_VISIBLE_CONTACTS = 20;
  MeshCoreContact savedContacts[MAX_VISIBLE_CONTACTS] = {};
  uint8_t savedContactCount = 0;

  MeshCoreContact discoveredNodes[MAX_VISIBLE_CONTACTS] = {};
  uint8_t discoveredNodeCount = 0;

  // Callbacks (static -> instance via context pointer)
  static void onStateChanged(BleConnectionState state, void* ctx);
  static void onMessageReceived(const MeshCoreMessage& msg, void* ctx);
  static void onContactReceived(const MeshCoreContact& c, bool isEnd, void* ctx);
  static void onAdvertReceived(const MeshCoreContact& node, void* ctx);
  static void onChannelReceived(const MeshCoreChannel& ch, void* ctx);

  void handleStateChange(BleConnectionState state);
  void handleMessage(const MeshCoreMessage& msg);
  void handleContact(const MeshCoreContact& c, bool isEnd);
  void handleAdvert(const MeshCoreContact& node);
  void handleChannel(const MeshCoreChannel& ch);

  void renderChannelList(const Rect& contentRect);
  void renderContactList(const Rect& contentRect);
  void renderDiscoveredList(const Rect& contentRect);
  void renderStatus(const Rect& contentRect);

  void openChannelThread(uint8_t channelIdx);
  void openContactThread(const MeshCoreContact& contact);
  void openDiscover();
  void launchScanActivity();
  void switchTab(Tab tab);

  void addChannel();
  void deleteChannel(uint8_t idx);

  int getListCountForCurrentTab() const;
};
