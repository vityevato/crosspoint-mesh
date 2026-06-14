#include "MeshCoreHubActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MeshCoreBatteryPoller.h"
#include "MeshCoreChannelListView.h"
#include "MeshCoreContactListView.h"
#include "MeshCoreDiscoverActivity.h"
#include "MeshCoreDiscoveredListView.h"
#include "MeshCoreScanActivity.h"
#include "MeshCoreStatusView.h"
#include "MeshCoreSubtitle.h"
#include "MeshCoreThreadActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

#ifdef SIMULATOR
#include <MeshCoreMockHotkeys.h>
#include <MockSession.h>
#endif

// --- Callbacks ---

void MeshCoreHubActivity::onStateChanged(BleConnectionState state, void* ctx) {
  static_cast<MeshCoreHubActivity*>(ctx)->handleStateChange(state);
}

void MeshCoreHubActivity::onMessageReceived(const MeshCoreMessage& msg, void* ctx) {
  static_cast<MeshCoreHubActivity*>(ctx)->handleMessage(msg);
}

void MeshCoreHubActivity::onContactReceived(const MeshCoreContact& c, bool isEnd, void* ctx) {
  static_cast<MeshCoreHubActivity*>(ctx)->handleContact(c, isEnd);
}

void MeshCoreHubActivity::onAdvertReceived(const MeshCoreContact& node, void* ctx) {
  static_cast<MeshCoreHubActivity*>(ctx)->handleAdvert(node);
}

void MeshCoreHubActivity::onChannelReceived(const MeshCoreChannel& ch, void* ctx) {
  static_cast<MeshCoreHubActivity*>(ctx)->handleChannel(ch);
}

// --- Lifecycle ---

static constexpr const char* MESH_LOG_PATH = "/meshcore.log";

void MeshCoreHubActivity::onEnter() {
  Activity::onEnter();

#ifdef SIMULATOR
  MockSession::loadMockConfig("/meshcore_mock.json");
#endif

  // SD card debug logging — delete old log and start fresh
  if (Storage.exists(MESH_LOG_PATH)) {
    Storage.remove(MESH_LOG_PATH);
  }
  sdLogFile = Storage.open(MESH_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (sdLogFile) {
    setLogFileSink(&sdLogFile);
    LOG_INF("MESH", "SD log sink enabled: %s", MESH_LOG_PATH);
  }

  store.init();  // Create top-level directories only (no companion yet)

  // Load companion address first (root-level, always readable)
  char addr[18] = {};
  uint8_t addrType = 0;
  bool hasAddr = store.loadCompanionAddress(addr, sizeof(addr), &addrType);

  // Now scope the store to this companion's data directory
  if (hasAddr && addr[0] != '\0') {
    store.init(addr);
    savedContactCount = store.loadContacts(savedContacts, MAX_VISIBLE_CONTACTS);

    // Load unread counts
    uint16_t channelUnread[8] = {};
    store.loadUnreadCounts(channelUnread, 8, savedContacts, savedContactCount);
    for (int i = 0; i < 8; ++i) {
      channels[i].unreadCount = channelUnread[i];
    }
  }

  client.setStateCallback(onStateChanged, this);
  client.setMessageCallback(onMessageReceived, this);
  client.setContactCallback(onContactReceived, this);
  client.setAdvertCallback(onAdvertReceived, this);
  client.setChannelCallback(onChannelReceived, this);

  if (hasAddr && addr[0] != '\0') {
    client.setAutoReconnectAddress(addr, addrType);
    // Load stored PIN and set it on the client for auto-reconnect
    uint32_t storedPin = 123456;
    store.loadCompanionPin(&storedPin);
    client.setConnectPin(storedPin);
  }

  // Disconnect WiFi before BLE (ESP32-C3 shares the radio)
  if (WiFi.getMode() != WIFI_OFF) {
    LOG_INF("MESH", "Disconnecting WiFi for BLE");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // BLE is owned by the hub for its entire lifetime — init once here.
  if (!client.init()) {
    LOG_ERR("MESH", "BLE init failed");
    onGoHome();
    return;
  }

  // Auto-reconnect to known companion address
  if (addr[0] != '\0') {
    autoReconnecting = true;
    client.connectTo(addr, addrType);
    requestUpdate();
    return;
  }

  launchScanActivity();
}

void MeshCoreHubActivity::onExit() {
  // Disable SD log sink and close file
  clearLogFileSink();
  sdLogFile.close();

  // Save state
  store.saveContacts(savedContacts, savedContactCount);
  store.saveCompanionAddress(client.getAutoReconnectAddress(), client.getAutoReconnectAddressType());
  store.saveCompanionPin(client.getConnectPin());

  uint16_t channelUnread[8] = {};
  for (int i = 0; i < 8; ++i) {
    channelUnread[i] = channels[i].unreadCount;
  }
  store.saveUnreadCounts(channelUnread, 8, savedContacts, savedContactCount);

  client.deinit();

#ifdef SIMULATOR
  MockSession::unloadMockConfig();
#endif

  Activity::onExit();
}

void MeshCoreHubActivity::launchScanActivity() {
  startActivityForResult(std::make_unique<MeshCoreScanActivity>(renderer, mappedInput, client),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             onGoHome();
                             return;
                           }
                           // Connected — fetch channels
                           channelCount = client.getCompanion().maxChannels;
                           if (channelCount > 8) channelCount = 8;
                           requestUpdate();
                         });
}

// --- Input handling ---

void MeshCoreHubActivity::loop() {
  client.poll();

  if (pollMeshCoreBattery(client, lastBatteryRequestMs, lastBatteryMv)) {
    requestUpdate();
  }

  // Handle async auto-reconnect result
  if (autoReconnecting) {
    auto bleState = client.getState();
    if (bleState == BleConnectionState::CONNECTED) {
      autoReconnecting = false;
      channelCount = client.getCompanion().maxChannels;
      if (channelCount > 8) channelCount = 8;
      requestUpdate();
    } else if (bleState == BleConnectionState::DISCONNECTED) {
      autoReconnecting = false;
      launchScanActivity();
      return;
    }
    // Still CONNECTING/INITIALIZING — allow Back to cancel
    if (autoReconnecting) {
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        autoReconnecting = false;
        onGoHome();
      }
      return;
    }
  }

  // Handle unexpected BLE disconnect — auto-scan for companions
  if (pendingAutoScan) {
    pendingAutoScan = false;
    reconnectOnDisconnect = false;
    launchScanActivity();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectedIndex > 0) {
      selectedIndex = 0;
      requestUpdate();
    } else {
      reconnectOnDisconnect = false;
      onGoHome();
    }
    return;
  }

#ifdef SIMULATOR
  if (handleMockKey("Hub", client.getBleClient())) {
    requestUpdate();
    return;
  }
  pollMock(client.getBleClient(), millis());
#endif

  int listCount = getListCountForCurrentTab();

  // Channel management actions (side buttons on channel tab)
  if (currentTab == Tab::CHANNELS) {
    if (mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
      addChannel();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::PageBack) && listCount > 0 && selectedIndex > 0) {
      deleteChannel(static_cast<uint8_t>(selectedIndex - 1));
      if (selectedIndex > getListCountForCurrentTab()) {
        selectedIndex = getListCountForCurrentTab();
        if (selectedIndex < 0) selectedIndex = 0;
      }
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == 0) {
      // Cycle to next tab (Settings-style navigation)
      int tab = static_cast<int>(currentTab);
      tab = (tab < static_cast<int>(Tab::TAB_COUNT) - 1) ? tab + 1 : 0;
      switchTab(static_cast<Tab>(tab));
      return;
    }
    if (listCount > 0) {
      int itemIdx = selectedIndex - 1;
      switch (currentTab) {
        case Tab::CHANNELS:
          if (itemIdx < channelCount && channels[itemIdx].configured) {
            openChannelThread(itemIdx);
          }
          break;
        case Tab::CONTACTS:
          if (itemIdx < savedContactCount) {
            openContactThread(savedContacts[itemIdx]);
          }
          break;
        case Tab::DISCOVERED:
          openDiscover();
          break;
        case Tab::STATUS:
          client.disconnect();
          // State change will be handled by handleStateChange on next poll()
          break;
        default:
          break;
      }
    }
    return;
  }

  int navCount = listCount + 1;  // +1 for tab bar row
  buttonNavigator.onNextRelease([this, navCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, navCount);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, navCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, navCount);
    requestUpdate();
  });
}

void MeshCoreHubActivity::switchTab(Tab tab) {
  currentTab = tab;
  selectedIndex = 0;
  requestUpdate();
}

int MeshCoreHubActivity::getListCountForCurrentTab() const {
  switch (currentTab) {
    case Tab::CHANNELS: {
      int count = 0;
      for (uint8_t i = 0; i < channelCount; ++i) {
        if (channels[i].configured) count++;
      }
      return count > 0 ? channelCount : 0;
    }
    case Tab::CONTACTS:
      return savedContactCount;
    case Tab::DISCOVERED:
      return discoveredNodeCount;
    case Tab::STATUS:
      return (client.getState() == BleConnectionState::CONNECTED) ? 1 : 0;
    default:
      return 0;
  }
}

// --- Rendering ---

void MeshCoreHubActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  char headerSubtitle[64];
  formatMeshCoreSubtitle(client, headerSubtitle, sizeof(headerSubtitle));
  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), tr(STR_MESHCORE),
                 headerSubtitle);

  // Tab bar — Settings-style: highlight tab bar when selectedIndex == 0
  int tabBarTop = metrics.topPadding + metrics.headerHeight;
  constexpr int tabCount = static_cast<int>(Tab::TAB_COUNT);
  const char* tabNames[tabCount] = {
      tr(STR_MESHCORE_CHANNEL_LIST), tr(STR_MESHCORE_CONTACTS), tr(STR_MESHCORE_DISCOVERED),
      tr(STR_MESHCORE_STATUS)};
  std::vector<TabInfo> tabs;
  tabs.reserve(tabCount);
  for (int i = 0; i < tabCount; ++i) {
    tabs.push_back({tabNames[i], currentTab == static_cast<Tab>(i)});
  }
  GUI.drawTabBar(renderer, Rect(0, tabBarTop, pageWidth, metrics.tabBarHeight), tabs, selectedIndex == 0);

  // Content area
  int contentTop = tabBarTop + metrics.tabBarHeight;
  int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.topPadding;
  Rect contentRect(0, contentTop, pageWidth, contentHeight);

  switch (currentTab) {
    case Tab::CHANNELS:
      renderChannelList(contentRect);
      break;
    case Tab::CONTACTS:
      renderContactList(contentRect);
      break;
    case Tab::DISCOVERED:
      renderDiscoveredList(contentRect);
      break;
    case Tab::STATUS:
      renderStatus(contentRect);
      break;
    default:
      break;
  }

  // Button hints — Settings-style: show next tab name when on tab row
  const char* btn2 = "";
  if (selectedIndex == 0) {
    int nextTab = (static_cast<int>(currentTab) + 1) % tabCount;
    btn2 = tabNames[nextTab];
  } else if (currentTab == Tab::CHANNELS || currentTab == Tab::CONTACTS) {
    btn2 = tr(STR_OPEN);
  } else if (currentTab == Tab::STATUS) {
    btn2 = tr(STR_MESHCORE_DISCONNECT);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), btn2, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void MeshCoreHubActivity::renderChannelList(const Rect& contentRect) {
  MeshCoreChannelListView::render(renderer, contentRect, channels, channelCount, selectedIndex);
}

void MeshCoreHubActivity::renderContactList(const Rect& contentRect) {
  MeshCoreContactListView::render(renderer, contentRect, savedContacts, savedContactCount, selectedIndex);
}

void MeshCoreHubActivity::renderDiscoveredList(const Rect& contentRect) {
  MeshCoreDiscoveredListView::render(renderer, contentRect, discoveredNodes, discoveredNodeCount, savedContacts,
                                     savedContactCount, selectedIndex);
}

void MeshCoreHubActivity::renderStatus(const Rect& contentRect) {
  MeshCoreStatusView::render(renderer, contentRect, client, selectedIndex);
}

// --- Event handlers ---

void MeshCoreHubActivity::handleStateChange(BleConnectionState state) {
  if (state == BleConnectionState::CONNECTED) {
    // Save the PIN that was used for this successful connection
    store.saveCompanionPin(client.getConnectPin());

    // Scope the store to the newly connected companion
    const char* addr = client.getAutoReconnectAddress();
    if (addr[0] != '\0') {
      store.init(addr);
      savedContactCount = store.loadContacts(savedContacts, MAX_VISIBLE_CONTACTS);
      uint16_t channelUnread[8] = {};
      store.loadUnreadCounts(channelUnread, 8, savedContacts, savedContactCount);
      for (int i = 0; i < 8; ++i) {
        channels[i].unreadCount = channelUnread[i];
      }
    }
    reconnectOnDisconnect = true;
  } else if (state == BleConnectionState::DISCONNECTED) {
    // Unexpected disconnect while previously connected — auto-scan
    if (reconnectOnDisconnect && !autoReconnecting) {
      pendingAutoScan = true;
    }
  }
  LOG_INF("MESH", "Hub state: %d", static_cast<int>(state));
  requestUpdate();
}

void MeshCoreHubActivity::handleMessage(const MeshCoreMessage& msg) {
  if (msg.type == MsgType::CHANNEL) {
    bool ok = store.appendChannelMessage(msg.channelIdx, msg);
    if (ok) {
      LOG_INF("MESH", "Stored ch%d msg: %.40s", msg.channelIdx, msg.text);
    } else {
      LOG_ERR("MESH", "Failed to store ch%d msg", msg.channelIdx);
    }
    if (msg.channelIdx < 8) {
      channels[msg.channelIdx].unreadCount++;
    }
  } else {
    // Check if sender is in saved contacts
    for (uint8_t i = 0; i < savedContactCount; ++i) {
      if (memcmp(savedContacts[i].publicKey, msg.pubkeyPrefix, 6) == 0) {
        store.appendDirectMessage(savedContacts[i].publicKey, msg);
        savedContacts[i].unreadCount++;
        requestUpdate();
        return;
      }
    }
    // FR-011: Discard DMs from non-contacts
    LOG_DBG("MESH", "Discarding DM from non-contact");
    return;
  }
  requestUpdate();
}

void MeshCoreHubActivity::handleContact(const MeshCoreContact& c, bool isEnd) {
  if (isEnd) {
    LOG_DBG("MESH", "Contact list complete");
    requestUpdate();
    return;
  }
  // PKT_CONTACT_START sends an empty contact — skip it
  if (c.name[0] == '\0' && c.publicKey[0] == 0) return;

  // Update saved contacts with server data
  for (uint8_t i = 0; i < savedContactCount; ++i) {
    if (memcmp(savedContacts[i].publicKey, c.publicKey, 32) == 0) {
      savedContacts[i].lastSeen = c.lastSeen;
      savedContacts[i].pathLength = c.pathLength;
      savedContacts[i].snr = c.snr;
      savedContacts[i].type = c.type;
      if (c.name[0] != '\0') {
        snprintf(savedContacts[i].name, sizeof(savedContacts[i].name), "%s", c.name);
      }
      return;
    }
  }
  // New contact from companion — add as saved
  if (savedContactCount < MAX_VISIBLE_CONTACTS) {
    savedContacts[savedContactCount] = c;
    savedContacts[savedContactCount].isSaved = true;
    savedContactCount++;
  }
}

void MeshCoreHubActivity::handleAdvert(const MeshCoreContact& node) {
  // Update existing or add new discovered node
  for (uint8_t i = 0; i < discoveredNodeCount; ++i) {
    if (memcmp(discoveredNodes[i].publicKey, node.publicKey, 32) == 0) {
      discoveredNodes[i] = node;
      requestUpdate();
      return;
    }
  }
  if (discoveredNodeCount < MAX_VISIBLE_CONTACTS) {
    discoveredNodes[discoveredNodeCount] = node;
    discoveredNodeCount++;
    requestUpdate();
  }
}

void MeshCoreHubActivity::handleChannel(const MeshCoreChannel& ch) {
  if (ch.index < 8) {
    channels[ch.index] = ch;
    // Preserve unread count from store (not in protocol response)
    requestUpdate();
  }
}

// --- Navigation ---

void MeshCoreHubActivity::openChannelThread(uint8_t channelIdx) {
  channels[channelIdx].unreadCount = 0;
  startActivityForResult(std::make_unique<MeshCoreThreadActivity>(renderer, mappedInput, client, store, channelIdx,
                                                                  channels[channelIdx].name),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void MeshCoreHubActivity::openContactThread(const MeshCoreContact& contact) {
  // Clear unread count for this contact
  for (uint8_t i = 0; i < savedContactCount; ++i) {
    if (memcmp(savedContacts[i].publicKey, contact.publicKey, 32) == 0) {
      savedContacts[i].unreadCount = 0;
      break;
    }
  }
  startActivityForResult(std::make_unique<MeshCoreThreadActivity>(renderer, mappedInput, client, store, contact),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void MeshCoreHubActivity::openDiscover() {
  startActivityForResult(
      std::make_unique<MeshCoreDiscoverActivity>(renderer, mappedInput, client, store, discoveredNodes,
                                                 discoveredNodeCount, savedContacts, savedContactCount),
      [this](const ActivityResult&) { requestUpdate(); });
}

// --- Channel management (Task 13) ---

void MeshCoreHubActivity::addChannel() {
  // Find first empty slot (skip index 0 = public)
  int emptyIdx = -1;
  for (int i = 1; i < channelCount; ++i) {
    if (channels[i].isEmpty()) {
      emptyIdx = i;
      break;
    }
  }
  if (emptyIdx < 0) return;

  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_MESHCORE_CHANNEL_NAME),
                                                                 "", 32, InputType::Text),
                         [this, emptyIdx](const ActivityResult& result) {
                           if (result.isCancelled) {
                             requestUpdate();
                             return;
                           }
                           auto name = std::get<KeyboardResult>(result.data).text;

                           startActivityForResult(
                               std::make_unique<KeyboardEntryActivity>(
                                   renderer, mappedInput, tr(STR_MESHCORE_CHANNEL_SECRET), "", 32, InputType::Text),
                               [this, emptyIdx, name = std::move(name)](const ActivityResult& r2) {
                                 if (r2.isCancelled) {
                                   requestUpdate();
                                   return;
                                 }
                                 auto secretHex = std::get<KeyboardResult>(r2.data).text;
                                 uint8_t secret[16] = {};
                                 for (size_t i = 0; i < 16 && i * 2 + 1 < secretHex.size(); ++i) {
                                   char byte[3] = {secretHex[i * 2], secretHex[i * 2 + 1], '\0'};
                                   secret[i] = static_cast<uint8_t>(strtoul(byte, nullptr, 16));
                                 }
                                 client.setChannel(static_cast<uint8_t>(emptyIdx), name.c_str(), secret);
                                 client.requestChannel(static_cast<uint8_t>(emptyIdx));
                                 requestUpdate();
                               });
                         });
}

void MeshCoreHubActivity::deleteChannel(uint8_t idx) {
  client.deleteChannel(idx);
  channels[idx] = {};
  requestUpdate();
}
