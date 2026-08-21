#include "MeshCoreHubActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <time.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MeshCoreBatteryPoller.h"
#include "MeshCoreChannelListView.h"
#include "MeshCoreContactListView.h"
#include "MeshCoreDiscoverActivity.h"
#include "MeshCoreMenuView.h"
#include "MeshCoreScanActivity.h"
#include "MeshCoreStatusView.h"
#include "MeshCoreSubtitle.h"
#include "SilentRestart.h"
#include "activities/reader/QrDisplayActivity.h"
#include "components/UITheme.h"
#include "thread/MeshCoreThreadActivity.h"
#include "utils/MeshCoreContactUrlParser.h"
#include "utils/MeshCoreDisplayUtils.h"
#include "utils/MeshCoreHeapLog.h"
#include "utils/MeshCoreMessageHeight.h"
#include "utils/MeshCoreShareUrl.h"

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

void MeshCoreHubActivity::onChannelHeard(uint8_t channelIdx, uint8_t heardCount, const uint8_t* hashes, void* ctx) {
  static_cast<MeshCoreHubActivity*>(ctx)->handleChannelHeard(channelIdx, heardCount);
}

void MeshCoreHubActivity::onDeliveryStatic(uint32_t msgId, const uint8_t* pubkey32, DeliveryStatus status, void* ctx) {
  static_cast<MeshCoreHubActivity*>(ctx)->handleDelivery(msgId, pubkey32, status);
}

// --- Lifecycle ---

void MeshCoreHubActivity::onEnter() {
  Activity::onEnter();
  MESHCORE_LOG_HEAP("Hub onEnter:start");

#ifdef SIMULATOR
  MockSession::loadMockConfig("/meshcore_mock.json");
#endif

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
  MESHCORE_LOG_HEAP("Hub onEnter:after store");

  client.setStateCallback(onStateChanged, this);
  client.setMessageCallback(onMessageReceived, this);
  client.setContactCallback(onContactReceived, this);
  client.setAdvertCallback(onAdvertReceived, this);
  client.setChannelCallback(onChannelReceived, this);
  client.setChannelHeardCallback(onChannelHeard, this);
  client.setDeliveryCallback(onDeliveryStatic, this);  // persistent — never cleared

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
  MESHCORE_LOG_HEAP("Hub onEnter:before BLE init");
  if (!client.init()) {
    LOG_ERR("MESH", "BLE init failed");
    onGoHome();
    return;
  }
  MESHCORE_LOG_HEAP("Hub onEnter:after BLE init");

  // Wire up the toast overlay: status messages override the standard subtitle
  _toast.setClock(&millis);
  _toast.setSubtitleProvider(provideSubtitle, this);

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
  MESHCORE_LOG_HEAP("Hub onExit:start");

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
  MESHCORE_LOG_HEAP("Hub onExit:after BLE deinit");

#ifdef SIMULATOR
  MockSession::unloadMockConfig();
#endif

  Activity::onExit();

  // Defrag heap after BLE teardown (radio + buffers + message store).
  // WiFi activities do the same to release fragmented radio memory.
  silentRestart();
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

  // Poll advert completion
  if (_advertInFlight) {
    if (!client.isCommandPending()) {
      // Command completed — success
      _advertInFlight = false;
      _toast.show(_advertIsFlood ? tr(STR_MESHCORE_FLOOD_ADVERT_SENT) : tr(STR_MESHCORE_ADVERT_SENT), 5000);
      requestUpdate();
    } else if (millis() - _advertSentTime > 6000) {
      // Timeout (longer than client's 5s CMD_TIMEOUT_MS to avoid race)
      _advertInFlight = false;
      _toast.show(_advertIsFlood ? tr(STR_MESHCORE_FLOOD_ADVERT_FAILED) : tr(STR_MESHCORE_ADVERT_FAILED), 5000);
      requestUpdate();
    }
  }

  // Poll batch contact loading from file (async via BLE, like DiscoverActivity)
  if (_contactsFileLoadPending) {
    if (!client.isCommandPending()) {
      advanceFileContactLoad(client.getLastCommandResult());
    } else if (millis() - _contactsFileLoadStartMs > 30000) {
      // 30 s timeout for the whole batch
      _contactsFileLoadPending = false;
      if (_pendingFileContactCount > 0) {
        store.saveContacts(savedContacts, savedContactCount);
        _toast.show(tr(STR_MESHCORE_CONTACTS_IMPORTED), 5000);
        switchTab(Tab::CONTACTS);
        selectedIndex = 0;
      }
      requestUpdate();
    }
  }

  // Auto-clear expired toast messages
  if (_toast.poll()) {
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
        // Tear down BLE first so the worker task can unblock cleanly.
        // Without this, deinit() may need to force-kill the worker while
        // it's blocked inside a NimBLE call, leaving the stack in an
        // indeterminate state that causes a panic on cleanup.
        client.disconnect();
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

  // Handle Back from status subscreen or disconnect popup
  if (showingStatus) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      showingStatus = false;
      requestUpdate();
    }
    return;
  }
  if (showingDisconnectPopup) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      showingDisconnectPopup = false;
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      showingDisconnectPopup = false;
      client.disconnect();
      requestUpdate();
    }
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
        case Tab::CHANNELS: {
          uint8_t visibleIdx[8];
          uint8_t visibleCount = collectVisibleChannels(visibleIdx);
          if (itemIdx >= 0 && itemIdx < visibleCount) {
            openChannelThread(visibleIdx[itemIdx]);
          }
          break;
        }
        case Tab::CONTACTS:
          if (itemIdx < savedContactCount) {
            openContactThread(savedContacts[itemIdx]);
          }
          break;
        case Tab::MENU: {
          if (itemIdx >= 0 && itemIdx < 8) {
            bool connected = (client.getState() == BleConnectionState::CONNECTED);
            switch (itemIdx) {
              case 0:  // Discovery Nodes
                openDiscover();
                return;
              case 1:  // Send Advert
                if (connected) {
                  if (client.sendSelfAdvert(false)) {
                    _advertInFlight = true;
                    _advertIsFlood = false;
                    _advertSentTime = millis();
                    _toast.show(tr(STR_MESHCORE_SENDING), 0);
                  }
                } else {
                  _toast.show(tr(STR_MESHCORE_NOT_CONNECTED), 5000);
                }
                requestUpdate();
                return;
              case 2:  // Send Flood Advert
                if (connected) {
                  if (client.sendSelfAdvert(true)) {
                    _advertInFlight = true;
                    _advertIsFlood = true;
                    _advertSentTime = millis();
                    _toast.show(tr(STR_MESHCORE_SENDING), 0);
                  }
                } else {
                  _toast.show(tr(STR_MESHCORE_NOT_CONNECTED), 5000);
                }
                requestUpdate();
                return;
              case 3:  // Save Advert to File
                saveAdvertToFile();
                return;
              case 4:  // Share Contact (QR)
                shareContactQr();
                return;
              case 5:  // Load Contacts from File
                loadContactsFromFile();
                return;
              case 6:  // Status
                if (client.getState() == BleConnectionState::CONNECTED) {
                  lastCompanion = client.getCompanion();
                }
                showingStatus = true;
                requestUpdate();
                return;
              case 7:  // Disconnect
                if (connected) {
                  showingDisconnectPopup = true;
                  requestUpdate();
                }
                // Dimmed when disconnected — no-op
                return;
              default:
                break;
            }
          }
          break;
        }
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

  // Settings-style tab switching: a long hold on Right/Down or Left/Up cycles
  // the tabs regardless of cursor position. The continuous callbacks only fire
  // on held buttons, so they never race the short-press list navigation above.
  buttonNavigator.onNextContinuous([this] {
    int tab = static_cast<int>(currentTab);
    tab = (tab < static_cast<int>(Tab::TAB_COUNT) - 1) ? tab + 1 : 0;
    switchTab(static_cast<Tab>(tab));
  });
  buttonNavigator.onPreviousContinuous([this] {
    int tab = static_cast<int>(currentTab);
    tab = (tab > 0) ? tab - 1 : static_cast<int>(Tab::TAB_COUNT) - 1;
    switchTab(static_cast<Tab>(tab));
  });
}

uint8_t MeshCoreHubActivity::collectVisibleChannels(uint8_t* outIdx) const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < channelCount; ++i) {
    if (channels[i].configured) outIdx[n++] = i;
  }
  return n;
}

void MeshCoreHubActivity::switchTab(Tab tab) {
  currentTab = tab;
  selectedIndex = 0;
  requestUpdate();
}

int MeshCoreHubActivity::getListCountForCurrentTab() const {
  switch (currentTab) {
    case Tab::CHANNELS: {
      uint8_t visibleIdx[8];
      return collectVisibleChannels(visibleIdx);
    }
    case Tab::CONTACTS:
      return savedContactCount;
    case Tab::MENU:
      return 8;  // Always 8 menu items
    default:
      return 0;
  }
}

void MeshCoreHubActivity::provideSubtitle(const void* ctx, char* buf, size_t bufSize) {
  const auto* self = static_cast<const MeshCoreHubActivity*>(ctx);
  formatMeshCoreSubtitle(self->client, buf, bufSize);
}

// --- Rendering ---

void MeshCoreHubActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Status info as a popup overlay (delegates to MeshCoreStatusView)
  if (showingStatus) {
    char headerSubtitle[64];
    _toast.getSubtitle(headerSubtitle, sizeof(headerSubtitle));
    GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), tr(STR_MESHCORE),
                   headerSubtitle);

    const auto& comp = (client.getState() == BleConnectionState::CONNECTED) ? client.getCompanion() : lastCompanion;
    MeshCoreStatusView::renderAsPopup(renderer, comp);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer();
    return;
  }

  // Disconnect confirmation popup (full-screen overlay)
  if (showingDisconnectPopup) {
    char headerSubtitle[64];
    _toast.getSubtitle(headerSubtitle, sizeof(headerSubtitle));
    GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), tr(STR_MESHCORE),
                   headerSubtitle);

    char popupMsg[128];
    const char* name = client.getCompanion().name;
    if (name[0] == '\0') name = lastCompanion.name;
    snprintf(popupMsg, sizeof(popupMsg), "%s %s?", tr(STR_MESHCORE_DISCONNECT_CONFIRM), name);
    GUI.drawPopup(renderer, popupMsg);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer();
    return;
  }

  // Auto-reconnect in progress — full-screen popup so the user knows the hub
  // is busy (and can cancel with Back) instead of a stale-looking tab UI.
  if (autoReconnecting) {
    GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), tr(STR_MESHCORE), nullptr);

    GUI.drawPopup(renderer, tr(STR_CONNECTING));

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer();
    return;
  }

  char headerSubtitle[64];
  _toast.getSubtitle(headerSubtitle, sizeof(headerSubtitle));
  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), tr(STR_MESHCORE),
                 headerSubtitle);

  // Tab bar — Settings-style: highlight tab bar when selectedIndex == 0
  // Prefix the Contacts/Channels tab label with a dot when any dialog in
  // that list has unread messages.
  constexpr int tabCount = static_cast<int>(Tab::TAB_COUNT);
  const char* plainTabNames[tabCount] = {tr(STR_MESHCORE_CONTACTS), tr(STR_MESHCORE_CHANNEL_LIST),
                                         tr(STR_MESHCORE_MENU)};

  bool contactsUnread = false;
  for (uint8_t i = 0; i < savedContactCount; ++i) {
    if (savedContacts[i].unreadCount > 0) {
      contactsUnread = true;
      break;
    }
  }

  bool channelsUnread = false;
  uint8_t unreadChIdx[8];
  const uint8_t unreadChCount = collectVisibleChannels(unreadChIdx);
  for (uint8_t i = 0; i < unreadChCount; ++i) {
    if (channels[unreadChIdx[i]].unreadCount > 0) {
      channelsUnread = true;
      break;
    }
  }

  const bool tabUnread[tabCount] = {contactsUnread, channelsUnread, false};
  char tabNames[tabCount][32] = {};
  std::vector<TabInfo> tabs;
  tabs.reserve(tabCount);
  for (int i = 0; i < tabCount; ++i) {
    if (tabUnread[i]) {
      snprintf(tabNames[i], sizeof(tabNames[i]), "%s %s", meshcore::DotSeparator, plainTabNames[i]);
    } else {
      snprintf(tabNames[i], sizeof(tabNames[i]), "%s", plainTabNames[i]);
    }
    tabs.push_back({tabNames[i], currentTab == static_cast<Tab>(i)});
  }
  int tabBarTop = metrics.topPadding + metrics.headerHeight;
  GUI.drawTabBar(renderer, Rect(0, tabBarTop, pageWidth, metrics.tabBarHeight), tabs, selectedIndex == 0);

  // Content area
  int contentTop = tabBarTop + metrics.tabBarHeight + metrics.verticalSpacing;
  int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.topPadding - metrics.bottomSubtitleHeight;
  Rect contentRect(0, contentTop, pageWidth, contentHeight);

  switch (currentTab) {
    case Tab::CONTACTS:
      renderContactList(contentRect);
      break;
    case Tab::CHANNELS:
      renderChannelList(contentRect);
      break;
    case Tab::MENU:
      renderMenu(contentRect);
      break;
    default:
      break;
  }

  // Button hints — Settings-style: show next tab name when on tab row
  const char* btn2 = "";
  if (selectedIndex == 0) {
    int nextTab = (static_cast<int>(currentTab) + 1) % tabCount;
    btn2 = plainTabNames[nextTab];
  } else if (currentTab == Tab::CHANNELS || currentTab == Tab::CONTACTS) {
    btn2 = tr(STR_OPEN);
  } else if (currentTab == Tab::MENU) {
    btn2 = tr(STR_SELECT);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), btn2, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void MeshCoreHubActivity::renderChannelList(const Rect& contentRect) {
  uint8_t visibleIdx[8];
  uint8_t visibleCount = collectVisibleChannels(visibleIdx);
  MeshCoreChannelListView::render(renderer, contentRect, channels, visibleIdx, visibleCount, selectedIndex, store);
}

void MeshCoreHubActivity::renderContactList(const Rect& contentRect) {
  MeshCoreContactListView::render(renderer, contentRect, savedContacts, savedContactCount, selectedIndex, store);
}

void MeshCoreHubActivity::renderMenu(const Rect& contentRect) {
  bool isConnected = (client.getState() == BleConnectionState::CONNECTED);
  MeshCoreMenuView::render(renderer, contentRect, selectedIndex, isConnected);
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
    // Cache companion data for disconnected status view
    lastCompanion = client.getCompanion();

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
  // Compute rendered height before storing (needed for batch-load scrolling)
  const auto& metrics = UITheme::getInstance().getMetrics();
  int contentWidth = renderer.getScreenWidth() - 2 * metrics.contentSidePadding;
  MeshCoreSettings settings;
  int fontId =
      (meshcore_settings::load(settings) && settings.useReaderFont) ? SETTINGS.getReaderFontId() : SMALL_FONT_ID;

  MeshCoreMessage msgWithHeight = msg;
  msgWithHeight.heightPx = measureMeshCoreMessageHeight(renderer, fontId, contentWidth, msg.type == MsgType::CHANNEL,
                                                        msgWithHeight, metrics);
  LOG_DBG("MESH", "Computed heightPx=%d for msg id=%u", msgWithHeight.heightPx, msgWithHeight.id);

  if (msg.type == MsgType::CHANNEL) {
    bool ok = store.appendChannelMessage(msg.channelIdx, msgWithHeight);
    if (ok) {
      LOG_INF("MESH", "Stored ch%d msg: %.40s", msg.channelIdx, msgWithHeight.text);
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
        store.appendDirectMessage(savedContacts[i].publicKey, msgWithHeight);
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
    LOG_DBG("MESH", "Contact list end (%d total)", savedContactCount);
    requestUpdate();
    return;
  }
  // PKT_CONTACT_START sends an empty contact — skip it
  if (c.name[0] == '\0' && c.publicKey[0] == 0) {
    LOG_DBG("MESH", "Contact list start (sentinel)");
    return;
  }

  LOG_DBG("MESH", "Contact: %s type=%d saved=%d", c.name, (int)c.type, c.isSaved);

  // Only show MeshCore clients (companion apps), not repeaters or room servers
  if (c.type != MeshNodeType::COMPANION) return;

  if (c.isSaved) {
    // Update or add saved contact with full data from companion
    for (uint8_t i = 0; i < savedContactCount; ++i) {
      if (memcmp(savedContacts[i].publicKey, c.publicKey, 32) == 0) {
        uint16_t prevUnread = savedContacts[i].unreadCount;
        savedContacts[i] = c;
        savedContacts[i].isSaved = true;            // Ensure flag
        savedContacts[i].unreadCount = prevUnread;  // Preserve local state
        store.saveContacts(savedContacts, savedContactCount);
        return;
      }
    }
    // New contact from companion — add as saved
    if (savedContactCount < MAX_VISIBLE_CONTACTS) {
      savedContacts[savedContactCount] = c;
      savedContacts[savedContactCount].isSaved = true;
      savedContactCount++;
      store.saveContacts(savedContacts, savedContactCount);
    }
  } else {
    // Unsolicited new node discovery (PKT_NEW_ADVERT) — add to discovered nodes
    for (uint8_t i = 0; i < discoveredNodeCount; ++i) {
      if (memcmp(discoveredNodes[i].publicKey, c.publicKey, 32) == 0) {
        discoveredNodes[i] = c;
        requestUpdate();
        return;
      }
    }
    if (discoveredNodeCount < MAX_VISIBLE_CONTACTS) {
      discoveredNodes[discoveredNodeCount] = c;
      discoveredNodeCount++;
      requestUpdate();
    }
  }
}

void MeshCoreHubActivity::handleAdvert(const MeshCoreContact& node) {
  // PKT_ADVERTISEMENT (0x80) — pubkey-only "seen again" beacon.
  // Only publicKey is populated; all other fields are zeroed.
  // Never overwrite existing name/type/pathLength with empty data.
  char keyLabel[MeshCoreContact::PUBLIC_KEY_DISPLAY_LEN];
  node.getPublicKeyLabel(keyLabel);
  LOG_DBG("MESH", "Advert: key=%s name='%s' type=%d saved=%d pathLen=%d snr=%d", keyLabel, node.name, (int)node.type,
          node.isSaved, node.pathLength, node.snr);

  uint32_t now = static_cast<uint32_t>(time(nullptr));

  for (uint8_t i = 0; i < discoveredNodeCount; ++i) {
    if (memcmp(discoveredNodes[i].publicKey, node.publicKey, 32) == 0) {
      // Update lastSeen only — preserve name/type/pathLength from previous
      // PKT_NEW_ADVERT (0x8A) which carried full data.
      discoveredNodes[i].lastSeen = now;
      requestUpdate();
      return;
    }
  }
  // // New unseen pubkey — add weak entry (name will be empty until PKT_NEW_ADVERT).
  // // Before adding, check savedContacts for a matching pubkey and copy name/type
  // // from there so previously saved contacts display their names immediately.
  // if (discoveredNodeCount < MAX_VISIBLE_CONTACTS) {
  //   discoveredNodes[discoveredNodeCount] = node;
  //   discoveredNodes[discoveredNodeCount].lastSeen = now;
  //   // Look for a matching saved contact to fill in name/type/path
  //   for (uint8_t i = 0; i < savedContactCount; ++i) {
  //     if (memcmp(savedContacts[i].publicKey, node.publicKey, 32) == 0) {
  //       memcpy(discoveredNodes[discoveredNodeCount].name, savedContacts[i].name, sizeof(MeshCoreContact::name));
  //       discoveredNodes[discoveredNodeCount].type = savedContacts[i].type;
  //       discoveredNodes[discoveredNodeCount].pathLength = savedContacts[i].pathLength;
  //       discoveredNodes[discoveredNodeCount].snr = savedContacts[i].snr;
  //       foundSaved = true;
  //       break;
  //     }
  //   }
  //   discoveredNodeCount++;
  //   requestUpdate();

  // First sighting of a pubkey we don't have in the address book. The companion
  // sends 0x80 (pubkey-only) instead of 0x8A precisely because it just auto-added
  // the contact to its own contacts[] — from its perspective the contact is now
  // "known". Pull the freshly added full record with an incremental GET_CONTACTS
  // so the name/type arrive; the companion never pushes them on its own.
  if (client.getState() == BleConnectionState::CONNECTED) {
    client.requestNewContacts();
  }
  // }
}

void MeshCoreHubActivity::handleChannel(const MeshCoreChannel& ch) {
  if (ch.index < 8) {
    uint16_t prevUnread = channels[ch.index].unreadCount;
    channels[ch.index] = ch;
    // Preserve the unread counter: PKT_CHANNEL_INFO carries no unread count
    // (parseChannelInfo leaves it 0), so copying the whole struct would wipe
    // the persisted/accumulated value on every (re)connect.
    channels[ch.index].unreadCount = prevUnread;
    requestUpdate();
  }
}

void MeshCoreHubActivity::handleChannelHeard(uint8_t channelIdx, uint8_t heardCount) {
  // Update pathLength on the last SENT message in this channel
  ConvMeta meta;
  if (!store.getChannelMeta(channelIdx, meta) || meta.count == 0) return;

  uint8_t loaded = 0;
  MeshCoreMessage lastMsg;
  store.loadChannelMessages(channelIdx, meta.endId, static_cast<uint8_t>(1), true, &lastMsg, loaded);
  if (loaded == 1 && lastMsg.direction == MsgDirection::SENT) {
    store.updateChannelMessage(channelIdx, lastMsg.id, heardCount, lastMsg.snr);
  }
}

void MeshCoreHubActivity::handleDelivery(uint32_t msgId, const uint8_t* pubkey32, DeliveryStatus status) {
  // Always persist delivery status to the store — works regardless of
  // whether a Thread activity is currently open.
  store.updateDirectMessage(pubkey32, msgId, status);
  LOG_INF("MESH", "Delivery: msgId=%lu status=%d", (unsigned long)msgId, (int)status);

  // If a Thread activity is open and this delivery is for its conversation,
  // forward the update so it can reload messages and repaint.
  if (_activeThread && memcmp(pubkey32, _activeThread->contactPubkeyForDelivery(), 32) == 0) {
    _activeThread->onDeliveryUpdate(msgId, pubkey32, status);
  }
}

// --- Navigation ---

void MeshCoreHubActivity::markChannelRead(uint8_t channelIdx) {
  if (channelIdx < 8 && channels[channelIdx].unreadCount > 0) {
    channels[channelIdx].unreadCount = 0;
    requestUpdate();
  }
}

void MeshCoreHubActivity::markContactRead(const uint8_t* pubkey32) {
  for (uint8_t i = 0; i < savedContactCount; ++i) {
    if (memcmp(savedContacts[i].publicKey, pubkey32, 32) == 0) {
      if (savedContacts[i].unreadCount > 0) {
        savedContacts[i].unreadCount = 0;
        requestUpdate();
      }
      return;
    }
  }
}

void MeshCoreHubActivity::openChannelThread(uint8_t channelIdx) {
  channels[channelIdx].unreadCount = 0;
  startActivityForResult(std::make_unique<MeshCoreThreadActivity>(renderer, mappedInput, client, store, channelIdx,
                                                                  channels[channelIdx].name, this),
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
  startActivityForResult(std::make_unique<MeshCoreThreadActivity>(renderer, mappedInput, client, store, contact, this),
                         [this](const ActivityResult& result) {
                           if (std::get_if<MeshCoreUnlistResult>(&result.data)) {
                             LOG_DBG("MESH", "Hub: contact deleted — reloading from store");
                             savedContactCount = store.loadContacts(savedContacts, MAX_VISIBLE_CONTACTS);
                           }
                           requestUpdate();
                         });
}

void MeshCoreHubActivity::openDiscover() {
  startActivityForResult(
      std::make_unique<MeshCoreDiscoverActivity>(renderer, mappedInput, client, store, discoveredNodes,
                                                 discoveredNodeCount, savedContacts, savedContactCount),
      [this](const ActivityResult&) { requestUpdate(); });
}

void MeshCoreHubActivity::saveAdvertToFile() {
  if (client.getState() != BleConnectionState::CONNECTED) {
    _toast.show(tr(STR_MESHCORE_NOT_CONNECTED), 5000);
    requestUpdate();
    return;
  }
  const auto& comp = client.getCompanion();
  // Build meshcore://contact/add?name=<name>&public_key=<64 hex>&type=1
  char url[384] = {};
  if (meshcore::buildMeshCoreContactShareUrl(comp.name, comp.publicKey, MeshNodeType::COMPANION, url, sizeof(url)) ==
      0) {
    _toast.show(tr(STR_MESHCORE_ADVERT_SAVE_FAILED), 5000);
    requestUpdate();
    return;
  }
  // Write to SD card root
  HalFile file;
  if (Storage.openFileForWrite("MESH", MESHCORE_CONTACTS_FILE, file)) {
    file.print(url);
    file.print("\n");
    _toast.show(tr(STR_MESHCORE_ADVERT_SAVED_TO_FILE), 5000);
  } else {
    _toast.show(tr(STR_MESHCORE_ADVERT_SAVE_FAILED), 5000);
  }
  requestUpdate();
}

void MeshCoreHubActivity::shareContactQr() {
  if (client.getState() != BleConnectionState::CONNECTED) {
    _toast.show(tr(STR_MESHCORE_NOT_CONNECTED), 5000);
    requestUpdate();
    return;
  }
  const auto& comp = client.getCompanion();
  char url[384] = {};
  if (meshcore::buildMeshCoreContactShareUrl(comp.name, comp.publicKey, MeshNodeType::COMPANION, url, sizeof(url)) ==
      0) {
    _toast.show(tr(STR_MESHCORE_SHARE_FAILED), 5000);
    requestUpdate();
    return;
  }
  LOG_DBG("MESH", "Share QR URL: %s", url);
  startActivityForResult(
      std::make_unique<QrDisplayActivity>(renderer, mappedInput, std::string(url), tr(STR_MESHCORE_SHARE_CONTACT)),
      [this](const ActivityResult&) { requestUpdate(); });
}

void MeshCoreHubActivity::loadContactsFromFile() {
  LOG_INF("MESH", "loadContactsFromFile: opening %s", MESHCORE_CONTACTS_FILE);
  HalFile file;
  if (!Storage.openFileForRead("MESH", MESHCORE_CONTACTS_FILE, file)) {
    LOG_ERR("MESH", "loadContactsFromFile: file not found");
    _toast.show(tr(STR_MESHCORE_CONTACTS_FILE_NOT_FOUND), 5000);
    requestUpdate();
    return;
  }

  // Our own public key (companion's key)
  const uint8_t* ownPubkey = client.getCompanion().publicKey;
  bool hasOwnPubkey = (client.getState() == BleConnectionState::CONNECTED);
  LOG_DBG("MESH", "loadContactsFromFile: hasOwnPubkey=%d", hasOwnPubkey);

  _pendingFileContactCount = 0;
  _pendingFileContactIndex = 0;
  _pendingFileContactSuccessCount = 0;

  char line[512];
  size_t linePos = 0;
  uint16_t totalLines = 0;

  // Read file byte by byte, building lines.
  // When the file doesn't end with \n, lastLine triggers one final parse.
  bool lastLine = false;
  while (true) {
    int c = file.read();
    if (c < 0) {
      if (linePos == 0) break;
      lastLine = true;
      c = '\n';
    }
    if (c == '\n' || c == '\r') {
      if (linePos == 0) {
        if (lastLine) break;
        continue;
      }
      line[linePos] = '\0';
      totalLines++;

      // Parse the URL using the shared parser utility
      MeshCoreContact contact;
      if (parseMeshCoreContactUrl(line, contact)) {
        char keyLabel[MeshCoreContact::PUBLIC_KEY_DISPLAY_LEN];
        contact.getPublicKeyLabel(keyLabel);
        LOG_DBG("MESH", "loadContactsFromFile: parsed '%s' key=%s type=%d", contact.name, keyLabel, (int)contact.type);

        if (contact.type == MeshNodeType::COMPANION) {
          // Skip our own pubkey
          bool skip = (hasOwnPubkey && memcmp(contact.publicKey, ownPubkey, 32) == 0);
          if (skip) {
            LOG_DBG("MESH", "loadContactsFromFile: skip own pubkey %s", keyLabel);
          }

          // Skip duplicates (check both savedContacts and already-pending)
          if (!skip) {
            for (uint8_t i = 0; i < savedContactCount; ++i) {
              if (memcmp(savedContacts[i].publicKey, contact.publicKey, 32) == 0) {
                LOG_DBG("MESH", "loadContactsFromFile: duplicate in savedContacts[%d]", i);
                skip = true;
                break;
              }
            }
          }
          if (!skip) {
            for (uint8_t i = 0; i < _pendingFileContactCount; ++i) {
              if (memcmp(_pendingFileContacts[i].publicKey, contact.publicKey, 32) == 0) {
                LOG_DBG("MESH", "loadContactsFromFile: duplicate in pending[%d]", i);
                skip = true;
                break;
              }
            }
          }

          if (!skip && _pendingFileContactCount < MAX_VISIBLE_CONTACTS) {
            _pendingFileContacts[_pendingFileContactCount++] = contact;
            LOG_DBG("MESH", "loadContactsFromFile: accepted [%d/%d]", _pendingFileContactCount, MAX_VISIBLE_CONTACTS);
          }
        } else {
          LOG_DBG("MESH", "loadContactsFromFile: skip non-chat type=%d", (int)contact.type);
        }
      } else {
        LOG_DBG("MESH", "loadContactsFromFile: failed to parse URL '%.60s'", line);
      }

      linePos = 0;
      if (lastLine) break;
    } else if (linePos < sizeof(line) - 1) {
      line[linePos++] = static_cast<char>(c);
    }
  }

  LOG_INF("MESH", "loadContactsFromFile: %d total lines, %d accepted contacts", totalLines, _pendingFileContactCount);

  if (_pendingFileContactCount == 0) {
    LOG_ERR("MESH", "loadContactsFromFile: no valid contacts found");
    _toast.show(tr(STR_MESHCORE_CONTACTS_NO_NEW), 5000);
    requestUpdate();
    return;
  }

  // Start async batch: queue first contact with the companion
  LOG_INF("MESH", "loadContactsFromFile: starting async batch, queueing contact 0/%d", _pendingFileContactCount);
  _contactsFileLoadPending = true;
  _contactsFileLoadStartMs = millis();
  _toast.show(tr(STR_MESHCORE_SAVING), 0);  // persistent during batch
  client.addUpdateContact(_pendingFileContacts[0]);
  requestUpdate();
}

void MeshCoreHubActivity::advanceFileContactLoad(bool success) {
  LOG_DBG("MESH", "advanceFileContactLoad: idx=%d/%d success=%d", _pendingFileContactIndex, _pendingFileContactCount,
          success);

  if (success) {
    _pendingFileContactSuccessCount++;
    // Insert at the TOP of savedContacts
    const auto& contact = _pendingFileContacts[_pendingFileContactIndex];
    char keyLabel[MeshCoreContact::PUBLIC_KEY_DISPLAY_LEN];
    contact.getPublicKeyLabel(keyLabel);
    LOG_INF("MESH", "advanceFileContactLoad: added '%s' key=%s at top", contact.name, keyLabel);

    if (savedContactCount < MAX_VISIBLE_CONTACTS) {
      for (int i = static_cast<int>(savedContactCount); i > 0; --i) {
        savedContacts[i] = savedContacts[i - 1];
      }
      savedContacts[0] = contact;
      savedContactCount++;
    }
  } else {
    LOG_ERR("MESH", "advanceFileContactLoad: BLE command failed for contact idx=%d", _pendingFileContactIndex);
  }

  _pendingFileContactIndex++;

  if (_pendingFileContactIndex < _pendingFileContactCount) {
    LOG_DBG("MESH", "advanceFileContactLoad: queueing next contact %d/%d", _pendingFileContactIndex,
            _pendingFileContactCount);
    client.addUpdateContact(_pendingFileContacts[_pendingFileContactIndex]);
  } else {
    LOG_INF("MESH", "advanceFileContactLoad: batch complete, success=%d/%d", _pendingFileContactSuccessCount,
            _pendingFileContactCount);
    _contactsFileLoadPending = false;
    store.saveContacts(savedContacts, savedContactCount);

    if (_pendingFileContactSuccessCount > 0) {
      _toast.show(tr(STR_MESHCORE_CONTACTS_IMPORTED), 5000);
      switchTab(Tab::CONTACTS);
      selectedIndex = 0;
    } else if (_pendingFileContactCount > 0) {
      _toast.show(tr(STR_MESHCORE_CONTACTS_SAVE_FAILED), 5000);
    } else {
      _toast.show(tr(STR_MESHCORE_CONTACTS_NO_NEW), 5000);
    }
  }
  requestUpdate();
}
