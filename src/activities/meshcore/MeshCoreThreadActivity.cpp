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
#include "components/UITheme.h"
#include "fontIds.h"
#include "utils/MeshCoreDisplayUtils.h"
#include "utils/MeshCoreTimeUtils.h"

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
  contentWidth = renderer.getScreenWidth();
  loadPage();

  // Restore saved scroll position from last session
  if (totalMessages > 0) {
    uint32_t savedId = isChannel ? store.loadThreadPosition(channelIdx) : store.loadDirectPosition(contactPubkey);
    if (savedId > 0) {
      // Find first message with globalId >= savedId
      int startIdx = 0;
      for (int i = 0; i < totalMessages; i++) {
        if (messages[i].globalId >= savedId) {
          startIdx = i;
          break;
        }
      }
      // Convert to pixel offset
      scrollOffsetPx = 0;
      for (int i = 0; i < startIdx; i++) scrollOffsetPx += msgHeights[i];
      int ch = contentHeight();
      if (scrollOffsetPx + ch > totalPixels) {
        scrollOffsetPx = (totalPixels > ch) ? totalPixels - ch : 0;
      }
    }
    // else: savedId == 0 or not found — stay at scrollToEnd from loadPage
  }
}

void MeshCoreThreadActivity::onExit() {
  uint32_t id = firstVisibleGlobalId();
  if (id > 0) {
    if (isChannel) {
      store.saveThreadPosition(channelIdx, id);
    } else {
      store.saveDirectPosition(contactPubkey, id);
    }
  }
  Activity::onExit();
}

void MeshCoreThreadActivity::loadPage() {
  if (isChannel) {
    totalMessages = store.getChannelMessageCount(channelIdx);
  } else {
    totalMessages = store.getDirectMessageCount(contactPubkey);
  }

  messages.resize(totalMessages);
  msgHeights.resize(totalMessages);

  if (totalMessages > 0) {
    uint8_t loaded = 0;
    if (isChannel) {
      store.loadChannelMessages(channelIdx, 0, messages.data(), static_cast<uint8_t>(totalMessages), loaded);
    } else {
      store.loadDirectMessages(contactPubkey, 0, messages.data(), static_cast<uint8_t>(totalMessages), loaded);
    }
    if (loaded < totalMessages) {
      messages.resize(loaded);
      totalMessages = loaded;
      msgHeights.resize(loaded);
    }
    recomputeHeights();
  } else {
    totalPixels = 0;
  }

  scrollToEnd();
  requestUpdate();
}

uint32_t MeshCoreThreadActivity::firstVisibleGlobalId() const {
  if (messages.empty()) return 0;
  uint16_t acc = 0;
  for (int i = 0; i < totalMessages; i++) {
    if (acc + msgHeights[i] > scrollOffsetPx) {
      return messages[i].globalId;
    }
    acc += msgHeights[i];
  }
  return messages.back().globalId;
}

void MeshCoreThreadActivity::recomputeHeights() {
  totalPixels = 0;
  Rect measRect(0, 0, contentWidth, 0);
  for (int i = 0; i < totalMessages; i++) {
    const auto& msg = messages[i];

    std::string senderStr;
    if (isChannel && msg.direction != MsgDirection::SENT) {
      senderStr = msg.senderName[0] ? msg.senderName : "Unknown";
    }

    char metaBuf[64] = {};
    if (msg.timestamp > 0) {
      char tsBuf[16];
      formatMeshCoreTimestamp(msg.timestamp, tsBuf, sizeof(tsBuf));
      char hopBuf[12];
      meshcore::formatMeshCoreHopCount(msg.pathLength, hopBuf, sizeof(hopBuf));
      snprintf(metaBuf, sizeof(metaBuf), "%s %s %s", tsBuf, meshcore::DotSeparator, hopBuf);
    }

    msgHeights[i] = GUI.measureMessageHeight(renderer, measRect, senderStr.c_str(), msg.text, metaBuf);
    totalPixels += msgHeights[i];
  }
}

int MeshCoreThreadActivity::contentHeight() const {
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  int tabBarTop = metrics.topPadding + metrics.headerHeight;
  int contentTop = tabBarTop + metrics.tabBarHeight + metrics.verticalSpacing;
  return pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - metrics.subtitleBottomMargin -
         metrics.bottomSubtitleHeight;
}

void MeshCoreThreadActivity::scrollDown() {
  int ch = contentHeight();
  if (totalPixels <= ch) return;
  uint16_t maxOffset = totalPixels - ch;
  if (scrollOffsetPx + ch < maxOffset) {
    scrollOffsetPx += ch;
  } else {
    scrollOffsetPx = maxOffset;
  }
  if (isChannel) {
    store.saveThreadPosition(channelIdx, firstVisibleGlobalId());
  } else {
    store.saveDirectPosition(contactPubkey, firstVisibleGlobalId());
  }
  requestUpdate();
}

void MeshCoreThreadActivity::scrollUp() {
  int ch = contentHeight();
  if (scrollOffsetPx >= ch) {
    scrollOffsetPx -= ch;
  } else {
    scrollOffsetPx = 0;
  }
  if (isChannel) {
    store.saveThreadPosition(channelIdx, firstVisibleGlobalId());
  } else {
    store.saveDirectPosition(contactPubkey, firstVisibleGlobalId());
  }
  requestUpdate();
}

auto MeshCoreThreadActivity::getVisibleState() const -> VisibleState {
  VisibleState vs = {};
  while (vs.startIdx < totalMessages && vs.acc + msgHeights[vs.startIdx] <= scrollOffsetPx) {
    vs.acc += msgHeights[vs.startIdx];
    vs.startIdx++;
  }
  if (vs.startIdx >= totalMessages) {
    vs.startIdx = totalMessages - 1;
    vs.acc = totalPixels - msgHeights[vs.startIdx];
  }
  vs.partialOffset = static_cast<int>(scrollOffsetPx - vs.acc);
  return vs;
}

void MeshCoreThreadActivity::saveScrollPosition() {
  if (isChannel) {
    store.saveThreadPosition(channelIdx, firstVisibleGlobalId());
  } else {
    store.saveDirectPosition(contactPubkey, firstVisibleGlobalId());
  }
}

void MeshCoreThreadActivity::scrollDownByMessage() {
  if (totalMessages == 0) return;
  int ch = contentHeight();
  if (totalPixels <= ch) return;

  VisibleState vs = getVisibleState();

  // Find the last (bottom-most) message with any part visible
  int yRel = 4 - vs.partialOffset;
  int lastIdx = vs.startIdx;
  for (int i = vs.startIdx; i < totalMessages; i++) {
    if (yRel + static_cast<int>(msgHeights[i]) <= ch) {
      lastIdx = i;
      yRel += msgHeights[i];
    } else if (yRel < ch) {
      lastIdx = i;  // partially visible at the bottom
      break;
    } else {
      break;
    }
  }

  // Sum of msgHeights from startIdx to lastIdx inclusive
  uint16_t sumSlice = 0;
  for (int i = vs.startIdx; i <= lastIdx; i++) sumSlice += msgHeights[i];
  int bottomRel = static_cast<int>(sumSlice) - vs.partialOffset + 4;

  if (bottomRel > ch) {
    // Step 1: bottom message is partially cut off — scroll to reveal it fully
    scrollOffsetPx += (bottomRel - ch);
  } else if (lastIdx + 1 < totalMessages) {
    // Step 2: bottom message fully visible — advance to next message
    scrollOffsetPx += msgHeights[lastIdx];
  } else {
    scrollToEnd();
    return;
  }

  // Clamp to valid range
  uint16_t maxOffset = totalPixels > ch ? totalPixels - ch : 0;
  if (scrollOffsetPx > maxOffset) scrollOffsetPx = maxOffset;

  saveScrollPosition();
  requestUpdate();
}

void MeshCoreThreadActivity::scrollUpByMessage() {
  if (totalMessages == 0) return;

  VisibleState vs = getVisibleState();

  if (vs.partialOffset > 0) {
    // Step 1: align partially-visible message to top
    scrollOffsetPx = vs.acc;
  } else {
    // Step 2: go to previous message
    if (vs.startIdx > 0) {
      scrollOffsetPx = vs.acc - msgHeights[vs.startIdx - 1];
    } else {
      scrollOffsetPx = 0;
    }
  }

  saveScrollPosition();
  requestUpdate();
}

void MeshCoreThreadActivity::scrollToEnd() {
  if (totalPixels == 0) {
    scrollOffsetPx = 0;
    return;
  }
  int ch = contentHeight();
  if (totalPixels > ch) {
    scrollOffsetPx = totalPixels - ch;
  } else {
    scrollOffsetPx = 0;
  }
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

  // Detect new messages and auto-scroll if at end
  uint16_t currentTotal;
  if (isChannel) {
    currentTotal = store.getChannelMessageCount(channelIdx);
  } else {
    currentTotal = store.getDirectMessageCount(contactPubkey);
  }
  if (currentTotal > totalMessages) {
    // A — new messages arrived
    bool wasAtEnd = (scrollOffsetPx + contentHeight() >= totalPixels || totalPixels == 0);
    uint16_t oldCount = totalMessages;
    uint16_t newCount = currentTotal - oldCount;
    totalMessages = currentTotal;
    messages.resize(totalMessages);
    msgHeights.resize(totalMessages);

    uint8_t loaded = 0;
    if (isChannel) {
      store.loadChannelMessages(channelIdx, oldCount, messages.data() + oldCount, static_cast<uint8_t>(newCount),
                                loaded);
    } else {
      store.loadDirectMessages(contactPubkey, oldCount, messages.data() + oldCount, static_cast<uint8_t>(newCount),
                               loaded);
    }

    // Compute heights for new messages only
    Rect measRect(0, 0, contentWidth, 0);
    for (int i = oldCount; i < oldCount + loaded; i++) {
      const auto& msg = messages[i];
      std::string senderStr;
      if (isChannel && msg.direction != MsgDirection::SENT) {
        senderStr = msg.senderName[0] ? msg.senderName : "Unknown";
      }
      char metaBuf[64] = {};
      if (msg.timestamp > 0) {
        char tsBuf[16];
        formatMeshCoreTimestamp(msg.timestamp, tsBuf, sizeof(tsBuf));
        char hopBuf[12];
        meshcore::formatMeshCoreHopCount(msg.pathLength, hopBuf, sizeof(hopBuf));
        snprintf(metaBuf, sizeof(metaBuf), "%s %s %s", tsBuf, meshcore::DotSeparator, hopBuf);
      }
      msgHeights[i] = GUI.measureMessageHeight(renderer, measRect, senderStr.c_str(), msg.text, metaBuf);
      totalPixels += msgHeights[i];
    }

    // Only auto-scroll if user was already at the end
    if (wasAtEnd) {
      scrollToEnd();
    }
    requestUpdate();

  } else if (currentTotal < messages.size()) {
    // B — truncate happened, reload everything from store
    loadPage();
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
    // Side buttons (Up/Down): message-by-message scrolling
    //   Down on tab bar → enter content; in content → scroll to next message
    //   Up in content → scroll to previous message; on tab bar → no-op
    buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this] {
      if (selectedIndex == 0) {
        selectedIndex = 1;
        requestUpdate();
      } else {
        scrollDownByMessage();
      }
    });
    buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this] {
      if (selectedIndex == 1) {
        scrollUpByMessage();
      }
    });

    // Front buttons (Right/Left): page-by-page (screenful) scrolling
    //   Right on tab bar → enter content; in content → page down
    //   Left in content → page up; on tab bar → no-op
    buttonNavigator.onRelease({MappedInputManager::Button::Right}, [this] {
      if (selectedIndex == 0) {
        selectedIndex = 1;
        requestUpdate();
      } else {
        scrollDown();
      }
    });
    buttonNavigator.onRelease({MappedInputManager::Button::Left}, [this] {
      if (selectedIndex == 1) {
        scrollUp();
      }
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
                             // Reload all messages, scroll to end, save position
                             loadPage();
                             if (isChannel) {
                               store.saveThreadPosition(channelIdx, firstVisibleGlobalId());
                             } else {
                               store.saveDirectPosition(contactPubkey, firstVisibleGlobalId());
                             }
                           } else {
                             LOG_ERR("MESH", "Failed to queue message");
                             requestUpdate();
                           }
                         });
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

  int tabBarTop = metrics.topPadding + metrics.headerHeight;
  constexpr int tabCount = static_cast<int>(Tab::TAB_COUNT);
  const char* tabNames[tabCount] = {tr(STR_MESHCORE_DIRECT_MESSAGES), tr(STR_MESHCORE_MENU)};

  // Content area (drawn FIRST so header/tab-bar can overwrite any overflow)
  int contentTop = tabBarTop + metrics.tabBarHeight + metrics.verticalSpacing;
  int ch = contentHeight();
  Rect contentRect(0, contentTop, pageWidth, ch);

  switch (currentTab) {
    case Tab::MESSAGES:
      if (messages.empty()) {
        GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_MESSAGES));
      } else {
        GUI.drawMessages(
            renderer, contentRect, totalMessages, msgHeights.data(), totalPixels, scrollOffsetPx,
            /*sender*/
            [this](int i) -> std::string {
              if (i < 0 || i >= totalMessages) return {};
              if (messages[i].direction == MsgDirection::SENT || !isChannel) {
                return "";
              }
              char buf[64];
              snprintf(buf, sizeof(buf), "%s", messages[i].senderName[0] ? messages[i].senderName : "Unknown");
              return buf;
            },
            /*text*/
            [this](int i) -> std::string {
              if (i < 0 || i >= totalMessages) return {};
              return messages[i].text;
            },
            /*meta*/
            [this](int i) -> std::string {
              if (i < 0 || i >= totalMessages) return {};
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
            [this](int i) -> bool {
              if (i < 0 || i >= totalMessages) return false;
              return messages[i].direction == MsgDirection::SENT;
            });
      }
      break;
    case Tab::MENU:
      renderMenu(contentRect);
      break;
    default:
      break;
  }

  // Header (drawn after content to clean up any overflow)
  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), threadName, headerSubtitle);

  // Tab bar
  std::vector<TabInfo> tabs;
  tabs.reserve(tabCount);
  for (int i = 0; i < tabCount; ++i) {
    tabs.push_back({tabNames[i], currentTab == static_cast<Tab>(i)});
  }
  GUI.drawTabBar(renderer, Rect(0, tabBarTop, pageWidth, metrics.tabBarHeight), tabs, selectedIndex == 0);

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
