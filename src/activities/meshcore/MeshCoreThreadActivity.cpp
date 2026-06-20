#include "MeshCoreThreadActivity.h"

#include <I18n.h>
#include <Logging.h>

#ifdef SIMULATOR
#include <MeshCoreMockHotkeys.h>
#endif

#include <cstring>
#include <string>

#include "MeshCoreBatteryPoller.h"
#include "MeshCoreStatusView.h"
#include "MeshCoreSubtitle.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "utils/MeshCoreDisplayUtils.h"
#include "utils/MeshCoreTimeUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Channel thread constructor
MeshCoreThreadActivity::MeshCoreThreadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               MeshCoreClient& client, MeshCoreMessageStore& store, uint8_t channelIdx,
                                               const char* channelName)
    : Activity("MeshCoreThread", renderer, mappedInput),
      client(client),
      store(store),
      isChannel(true),
      channelIdx(channelIdx) {
  snprintf(threadName, sizeof(threadName), "%s", channelName);
}

// Direct message constructor
MeshCoreThreadActivity::MeshCoreThreadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               MeshCoreClient& client, MeshCoreMessageStore& store,
                                               const MeshCoreContact& contact)
    : Activity("MeshCoreThread", renderer, mappedInput), client(client), store(store), isChannel(false) {
  memcpy(contactPubkey, contact.publicKey, 32);
  snprintf(threadName, sizeof(threadName), "%s", contact.name);
}

void MeshCoreThreadActivity::onEnter() {
  Activity::onEnter();
  _toast.setClock(&millis);
  _toast.setSubtitleProvider(provideSubtitle, this);
  loadPage();
}

void MeshCoreThreadActivity::onExit() { Activity::onExit(); }

void MeshCoreThreadActivity::loadPage() {
  if (isChannel) {
    totalMessages = store.getChannelMessageCount(channelIdx);
  } else {
    totalMessages = store.getDirectMessageCount(contactPubkey);
  }

  // Start at most recent page
  if (pageOffset == 0 && totalMessages > MSGS_PER_PAGE) {
    pageOffset = totalMessages - MSGS_PER_PAGE;
  }

  if (isChannel) {
    store.loadChannelMessages(channelIdx, pageOffset, messages, MSGS_PER_PAGE, msgCount);
  } else {
    store.loadDirectMessages(contactPubkey, pageOffset, messages, MSGS_PER_PAGE, msgCount);
  }
  requestUpdate();
}

void MeshCoreThreadActivity::loop() {
  client.poll();

  // Battery polling
  if (pollMeshCoreBattery(client, lastBatteryRequestMs, lastBatteryMv)) {
    requestUpdate();
  }

  // Poll advert completion
  if (_advertInFlight) {
    if (!client.isCommandPending()) {
      _advertInFlight = false;
      _toast.show(_advertIsFlood ? tr(STR_MESHCORE_FLOOD_ADVERT_SENT) : tr(STR_MESHCORE_ADVERT_SENT), 5000);
      requestUpdate();
    } else if (millis() - _advertSentTime > 6000) {
      _advertInFlight = false;
      _toast.show(_advertIsFlood ? tr(STR_MESHCORE_FLOOD_ADVERT_FAILED) : tr(STR_MESHCORE_ADVERT_FAILED), 5000);
      requestUpdate();
    }
  }

  // Auto-clear expired toast messages
  if (_toast.poll()) {
    requestUpdate();
  }

#ifdef SIMULATOR
  if (handleMockKey("Thread", client.getBleClient())) {
    requestUpdate();
    return;
  }
  pollMock(client.getBleClient(), millis());
#endif

  // Detect new messages and auto-scroll if on last page
  uint16_t currentTotal;
  if (isChannel) {
    currentTotal = store.getChannelMessageCount(channelIdx);
  } else {
    currentTotal = store.getDirectMessageCount(contactPubkey);
  }
  if (currentTotal > totalMessages) {
    bool onLastPage = (pageOffset + MSGS_PER_PAGE >= totalMessages);
    totalMessages = currentTotal;
    if (onLastPage) {
      pageOffset = (totalMessages > MSGS_PER_PAGE) ? totalMessages - MSGS_PER_PAGE : 0;
      if (isChannel) {
        store.loadChannelMessages(channelIdx, pageOffset, messages, MSGS_PER_PAGE, msgCount);
      } else {
        store.loadDirectMessages(contactPubkey, pageOffset, messages, MSGS_PER_PAGE, msgCount);
      }
      requestUpdate();
    }
  }

  // --- Status subscreen ---
  if (showingStatus) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      showingStatus = false;
      requestUpdate();
    }
    return;
  }

  // --- Back ---
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectedIndex > 0) {
      selectedIndex = 0;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  // --- Confirm ---
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == 0) {
      // Tab bar — cycle to next tab
      int tab = static_cast<int>(currentTab);
      tab = (tab < static_cast<int>(Tab::TAB_COUNT) - 1) ? tab + 1 : 0;
      switchTab(static_cast<Tab>(tab));
      return;
    }

    int itemIdx = selectedIndex - 1;
    if (currentTab == Tab::MESSAGES) {
      // Messages tab: Confirm opens keyboard to send a message
      sendMessage();
      return;
    }

    if (currentTab == Tab::MENU) {
      if (isChannel) {
        // Channel menu: 4 items (Send Advert, Send Flood Advert, Status, Disconnect)
        bool connected = (client.getState() == BleConnectionState::CONNECTED);
        switch (itemIdx) {
          case 0:  // Send Advert
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
          case 1:  // Send Flood Advert
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
          case 2:  // Status
            if (client.getState() == BleConnectionState::CONNECTED) {
              lastCompanion = client.getCompanion();
            }
            showingStatus = true;
            requestUpdate();
            return;
          case 3:  // Disconnect
            if (connected) {
              client.disconnect();
            }
            finish();
            return;
          default:
            break;
        }
      } else {
        // DM menu: 2 items (Status, Disconnect)
        bool connected = (client.getState() == BleConnectionState::CONNECTED);
        switch (itemIdx) {
          case 0:  // Status
            if (client.getState() == BleConnectionState::CONNECTED) {
              lastCompanion = client.getCompanion();
            }
            showingStatus = true;
            requestUpdate();
            return;
          case 1:  // Disconnect
            if (connected) {
              client.disconnect();
            }
            finish();
            return;
          default:
            break;
        }
      }
    }
    return;
  }

  // --- Up/Down navigation ---
  int listCount = getListCountForCurrentTab();
  int navCount = listCount + 1;  // +1 for tab bar row

  if (currentTab == Tab::MESSAGES) {
    // Messages tab: on tab bar, Down/Up enters content; inside, pages
    buttonNavigator.onNextRelease([this] {
      if (selectedIndex == 0) {
        selectedIndex = 1;  // Enter content area
        requestUpdate();
      } else {
        nextPage();
      }
    });
    buttonNavigator.onPreviousRelease([this] {
      if (selectedIndex == 1) {
        prevPage();
      }
      // On tab bar (index 0), Up does nothing — Back exits activity
    });
  } else {
    // Menu tab: Up/Down navigates the list
    buttonNavigator.onNextRelease([this, navCount] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, navCount);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, navCount] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, navCount);
      requestUpdate();
    });
  }
}

void MeshCoreThreadActivity::switchTab(Tab tab) {
  currentTab = tab;
  selectedIndex = 0;
  requestUpdate();
}

int MeshCoreThreadActivity::getListCountForCurrentTab() const {
  switch (currentTab) {
    case Tab::MESSAGES:
      return 0;  // Messages tab has no list navigation — uses page nav instead
    case Tab::MENU:
      return isChannel ? 4 : 2;  // Channel: 4 items, DM: 2 items
    default:
      return 0;
  }
}

void MeshCoreThreadActivity::provideSubtitle(const void* ctx, char* buf, size_t bufSize) {
  const auto* self = static_cast<const MeshCoreThreadActivity*>(ctx);
  formatMeshCoreSubtitle(self->client, buf, bufSize);
}

// --- Message sending ---

void MeshCoreThreadActivity::sendMessage() {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_MESHCORE_SEND), "",
                                                                 MESHCORE_SEND_CHAR_LIMIT, InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             requestUpdate();
                             return;
                           }
                           const auto& text = std::get<KeyboardResult>(result.data).text;
                           if (text.empty()) {
                             requestUpdate();
                             return;
                           }

                           bool sent = false;
                           if (isChannel) {
                             sent = client.sendChannelMessage(channelIdx, text.c_str());
                           } else {
                             sent = client.sendDirectMessage(contactPubkey, text.c_str());
                           }

                           if (sent) {
                             LOG_INF("MESH", "Message queued");
                             // Build a local copy for display
                             MeshCoreMessage msg = {};
                             msg.direction = MsgDirection::SENT;
                             msg.type = isChannel ? MsgType::CHANNEL : MsgType::DIRECT;
                             msg.channelIdx = channelIdx;
                             msg.timestamp = static_cast<uint32_t>(millis() / 1000);
                             msg.deliveryStatus = DeliveryStatus::SENT;
                             snprintf(msg.text, sizeof(msg.text), "%s", text.c_str());

                             if (isChannel) {
                               store.appendChannelMessage(channelIdx, msg);
                             } else {
                               store.appendDirectMessage(contactPubkey, msg);
                             }
                             pageOffset = 0;  // Jump to latest
                             loadPage();
                           } else {
                             LOG_ERR("MESH", "Failed to queue message");
                             requestUpdate();
                           }
                         });
}

// --- Page navigation ---

void MeshCoreThreadActivity::nextPage() {
  if (pageOffset + MSGS_PER_PAGE < totalMessages) {
    pageOffset += MSGS_PER_PAGE;
    loadPage();
  }
}

void MeshCoreThreadActivity::prevPage() {
  if (pageOffset >= MSGS_PER_PAGE) {
    pageOffset -= MSGS_PER_PAGE;
  } else {
    pageOffset = 0;
  }
  loadPage();
}

// --- Rendering ---

void MeshCoreThreadActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // --- Status popup overlay ---
  if (showingStatus) {
    char headerSubtitle[64];
    _toast.getSubtitle(headerSubtitle, sizeof(headerSubtitle));
    GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), threadName, headerSubtitle);

    const auto& comp = (client.getState() == BleConnectionState::CONNECTED) ? client.getCompanion() : lastCompanion;
    MeshCoreStatusView::renderAsPopup(renderer, comp);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer();
    return;
  }

  // --- Normal tabbed layout ---
  char headerSubtitle[64];
  _toast.getSubtitle(headerSubtitle, sizeof(headerSubtitle));
  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), threadName, headerSubtitle);

  // Tab bar
  int tabBarTop = metrics.topPadding + metrics.headerHeight;
  constexpr int tabCount = static_cast<int>(Tab::TAB_COUNT);
  const char* tabNames[tabCount] = {tr(STR_MESHCORE_DIRECT_MESSAGES), tr(STR_MESHCORE_MENU)};
  std::vector<TabInfo> tabs;
  tabs.reserve(tabCount);
  for (int i = 0; i < tabCount; ++i) {
    tabs.push_back({tabNames[i], currentTab == static_cast<Tab>(i)});
  }
  GUI.drawTabBar(renderer, Rect(0, tabBarTop, pageWidth, metrics.tabBarHeight), tabs, selectedIndex == 0);

  // Content area
  int contentTop = tabBarTop + metrics.tabBarHeight + metrics.verticalSpacing;
  int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.topPadding;
  Rect contentRect(0, contentTop, pageWidth, contentHeight);

  switch (currentTab) {
    case Tab::MESSAGES:
      if (msgCount == 0) {
        GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_MESSAGES));
      } else {
        GUI.drawMessages(
            renderer, contentRect, msgCount, totalMessages, pageOffset,
            /*sender*/
            [this](int i) -> std::string {
              if (messages[i].direction == MsgDirection::SENT || !isChannel) {
                return "";
              }
              char buf[64];
              snprintf(buf, sizeof(buf), "%s", messages[i].senderName[0] ? messages[i].senderName : "Unknown");
              return buf;
            },
            /*text*/
            [this](int i) -> std::string { return messages[i].text; },
            /*meta*/
            [this](int i) -> std::string {
              char buf[64];
              uint32_t ts = messages[i].timestamp;
              if (ts > 0) {
                char tsBuf[16];
                formatMeshCoreTimestamp(ts, tsBuf, sizeof(tsBuf));
                char hopBuf[12];
                meshcore::formatMeshCoreHopCount(messages[i].pathLength, hopBuf, sizeof(hopBuf));
                snprintf(buf, sizeof(buf), "%s %s %s", tsBuf, meshcore::DotSeparator, hopBuf);
              } else {
                buf[0] = '\0';
              }
              return buf;
            },
            /*isOutgoing*/
            [this](int i) -> bool { return messages[i].direction == MsgDirection::SENT; });
      }
      break;
    case Tab::MENU:
      renderMenu(contentRect);
      break;
    default:
      break;
  }

  // Button hints
  const char* btn2 = "";
  if (selectedIndex == 0) {
    // Tab row — show next tab name
    int nextTab = (static_cast<int>(currentTab) + 1) % tabCount;
    btn2 = tabNames[nextTab];
  } else if (currentTab == Tab::MESSAGES) {
    btn2 = tr(STR_MESHCORE_SEND);
  } else if (currentTab == Tab::MENU) {
    btn2 = tr(STR_SELECT);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), btn2, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void MeshCoreThreadActivity::renderMenu(const Rect& contentRect) {
  bool connected = (client.getState() == BleConnectionState::CONNECTED);

  if (isChannel) {
    // Channel menu: 4 items
    constexpr int kItemCount = 4;
    GUI.drawList(
        renderer, contentRect, kItemCount, selectedIndex - 1,
        /*rowTitle*/
        [](int index) -> std::string {
          switch (index) {
            case 0:
              return tr(STR_MESHCORE_SEND_ADVERT);
            case 1:
              return tr(STR_MESHCORE_SEND_FLOOD_ADVERT);
            case 2:
              return tr(STR_MESHCORE_STATUS);
            case 3:
              return tr(STR_MESHCORE_DISCONNECT);
            default:
              return {};
          }
        },
        /*rowSubtitle*/ nullptr,
        /*rowIcon*/ nullptr,
        /*rowValue*/ nullptr,
        /*highlightValue*/ false,
        /*rowDimmed*/
        [connected](int index) -> bool {
          // Items 0, 1, 3 require a connected companion
          if (connected) return false;
          return (index == 0 || index == 1 || index == 3);
        });
  } else {
    // DM menu: 2 items
    constexpr int kItemCount = 2;
    GUI.drawList(
        renderer, contentRect, kItemCount, selectedIndex - 1,
        /*rowTitle*/
        [](int index) -> std::string {
          switch (index) {
            case 0:
              return tr(STR_MESHCORE_STATUS);
            case 1:
              return tr(STR_MESHCORE_DISCONNECT);
            default:
              return {};
          }
        },
        /*rowSubtitle*/ nullptr,
        /*rowIcon*/ nullptr,
        /*rowValue*/ nullptr,
        /*highlightValue*/ false,
        /*rowDimmed*/
        [connected](int index) -> bool {
          // Item 1 requires connected companion
          if (connected) return false;
          return (index == 1);
        });
  }
}
