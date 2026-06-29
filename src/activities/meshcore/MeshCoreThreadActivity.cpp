#include "MeshCoreThreadActivity.h"

#include <I18n.h>
#include <Logging.h>

#ifdef SIMULATOR
#include <MeshCoreMockHotkeys.h>
#endif

#include <cstring>
#include <iterator>
#include <string>

#include "CrossPointSettings.h"
#include "FontCacheManager.h"
#include "MeshCoreBatteryPoller.h"
#include "MeshCoreStatusView.h"
#include "MeshCoreSubtitle.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "utils/MeshCoreDisplayUtils.h"
#include "utils/MeshCoreHeapLog.h"
#include "utils/MeshCoreMessageHeight.h"
#include "utils/MeshCoreTimeUtils.h"

namespace {

/// Pre-computed font/layout metrics for a thread view.  Factors out the
/// repeated (renderer, rect, fontSetting) → (bodyFontId, lineHeights,
/// maxTextWidth) derivation so it lives in one place.
struct ThreadRenderCtx {
  const ThemeMetrics& metrics;
  int bodyFontId;
  int bodyLineH;
  int metaFontId;  // Font ID for meta information
  int metaLineH;   // Height of a line for meta information
  int maxTextWidth;

  ThreadRenderCtx(const GfxRenderer& renderer, const Rect& rect, bool useReaderFontSettings)
      : metrics(UITheme::getInstance().getMetrics()),
        bodyFontId(useReaderFontSettings ? SETTINGS.getReaderFontId() : SMALL_FONT_ID),
        bodyLineH(renderer.getLineHeight(bodyFontId)),
        metaFontId(bodyFontId),
        metaLineH(renderer.getLineHeight(metaFontId)),
        // Removed duplicate metaLineH initialization
        maxTextWidth(rect.width - 2 * metrics.contentSidePadding) {}
};

}  // namespace

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
  MESHCORE_LOG_HEAP("Thread onEnter:start");
  _toast.setClock(&millis);
  _toast.setSubtitleProvider(provideSubtitle, this);

  _bodyFontId = SETTINGS.getReaderFontId();
  const auto& metrics = UITheme::getInstance().getMetrics();
  _contentAreaWidth = renderer.getScreenWidth() - 2 * metrics.contentSidePadding;
  _contentAreaHeight = contentHeight();

  // Load ConvMeta from store
  bool hasMeta = isChannel ? store.getChannelMeta(channelIdx, _meta) : store.getDirectMeta(contactPubkey, _meta);

  if (!hasMeta || _meta.count == 0) {
    _visibleCount = 0;
    MESHCORE_LOG_HEAP("Thread onEnter:empty thread");
    return;
  }

  // Check font match — if changed, recalculate all heights asynchronously
  if (_meta.fontId != _bodyFontId) {
    LOG_DBG("MESH", "Thread onEnter: font mismatch (meta=%d current=%d), starting recalc", _meta.fontId, _bodyFontId);
    _recalcState = RecalcState::RUNNING;
    _recalcGid = _meta.startId;
    _recalcEndId = _meta.endId;
    _recalcNewTotalPx = 0;
    _recalcMeta = _meta;
    _recalcFontId = _bodyFontId;
    _recalcContentWidth = _contentAreaWidth;
    _recalcIsChannel = isChannel;
    _toast.show(tr(STR_MESHCORE_RECALC_LAYOUT), 0);  // persistent toast
    _visibleCount = 0;                               // nothing to show until recalc completes
    LOG_DBG("MESH", "Thread onEnter: recalculation started from id=%u to %u", _recalcGid, _recalcEndId);
  } else {
    loadVisibleBatch();
    LOG_DBG("MESH", "Thread onEnter: loaded batch — posPx=%u posId=%u firstId=%u lastId=%u accHeight=%u totalPx=%u",
            _meta.positionPx, _meta.positionId, _firstVisibleId, _lastVisibleId, _accHeight, _meta.totalPx);
  }
  MESHCORE_LOG_HEAP("Thread onEnter:end");
}

void MeshCoreThreadActivity::onExit() {
  MESHCORE_LOG_HEAP("Thread onExit");
  savePosition();
  memset(_visibleMsgs, 0, sizeof(_visibleMsgs));
  _visibleCount = 0;
  Activity::onExit();
}

void MeshCoreThreadActivity::loadVisibleBatch() {
  _visibleCount = 0;
  _firstVisibleId = 0;
  _lastVisibleId = 0;

  if (_meta.count == 0) return;

  uint32_t startId = _meta.positionId > 0 ? _meta.positionId : _meta.startId;
  LOG_DBG("MESH", "loadVisibleBatch: startId=%u height=%d count=%u", startId, _contentAreaHeight, _meta.count);

  if (isChannel) {
    store.loadChannelMessages(channelIdx, startId, static_cast<uint16_t>(_contentAreaHeight), /*up=*/false,
                              _visibleMsgs, _visibleCount);
  } else {
    store.loadDirectMessages(contactPubkey, startId, static_cast<uint16_t>(_contentAreaHeight), /*up=*/false,
                             _visibleMsgs, _visibleCount);
  }

  if (_visibleCount > 0) {
    _firstVisibleId = _visibleMsgs[0].id;
    _lastVisibleId = _visibleMsgs[_visibleCount - 1].id;
    LOG_DBG("MESH", "loadVisibleBatch: loaded %d msgs [%u..%u]", _visibleCount, _firstVisibleId, _lastVisibleId);
  } else {
    LOG_DBG("MESH", "loadVisibleBatch: no messages loaded");
  }
}

void MeshCoreThreadActivity::loadVisibleBatchUp() {
  uint8_t loaded = 0;
  if (isChannel) {
    store.loadChannelMessages(channelIdx, _meta.endId, static_cast<uint16_t>(_contentAreaHeight), /*up=*/true,
                              _visibleMsgs, loaded);
  } else {
    store.loadDirectMessages(contactPubkey, _meta.endId, static_cast<uint16_t>(_contentAreaHeight), /*up=*/true,
                             _visibleMsgs, loaded);
  }
  _visibleCount = loaded;
  if (loaded > 0) {
    _firstVisibleId = _visibleMsgs[0].id;
    _lastVisibleId = _visibleMsgs[loaded - 1].id;
    LOG_DBG("MESH", "loadVisibleBatchUp: loaded %d msgs [%u..%u]", loaded, _firstVisibleId, _lastVisibleId);
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

void MeshCoreThreadActivity::scrollDownPage() {
  LOG_DBG("MESH", "scrollDownPage: posPx=%u accH=%u totalPx=%u ch=%d lastId=%u endId=%u", _meta.positionPx, _accHeight,
          _meta.totalPx, _contentAreaHeight, _lastVisibleId, _meta.endId);
  if (_meta.totalPx <= static_cast<uint16_t>(_contentAreaHeight)) {
    LOG_DBG("MESH", "scrollDownPage: skip — fits in one page");
    return;
  }
  if (_lastVisibleId >= _meta.endId) {
    LOG_DBG("MESH", "scrollDownPage: skip — already at end");
    return;
  }

  _meta.positionId = _lastVisibleId + 1;
  _meta.positionPx += _accHeight;
  if (_meta.positionPx > _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight)) {
    _meta.positionPx = (_meta.totalPx > static_cast<uint16_t>(_contentAreaHeight))
                           ? _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight)
                           : 0;
    _meta.positionId = _meta.endId;
    LOG_DBG("MESH", "scrollDownPage: clamped to end, loading Up (posPx=%u posId=%u)", _meta.positionPx,
            _meta.positionId);
    loadVisibleBatchUp();
  } else {
    LOG_DBG("MESH", "scrollDownPage: advancing (posPx=%u posId=%u)", _meta.positionPx, _meta.positionId);
    loadVisibleBatch();
  }
  savePosition();
  requestUpdate();
}

void MeshCoreThreadActivity::scrollUpPage() {
  LOG_DBG("MESH", "scrollUpPage: posPx=%u accH=%u posId=%u startId=%u", _meta.positionPx, _accHeight, _meta.positionId,
          _meta.startId);
  if (_meta.positionPx == 0) {
    LOG_DBG("MESH", "scrollUpPage: skip — positionPx==0");
    return;
  }

  uint32_t newStartId = _meta.positionId > 0 ? _meta.positionId - 1 : _meta.startId;
  uint8_t loaded = 0;
  if (isChannel) {
    store.loadChannelMessages(channelIdx, newStartId, static_cast<uint16_t>(_contentAreaHeight), /*up=*/true,
                              _visibleMsgs, loaded);
  } else {
    store.loadDirectMessages(contactPubkey, newStartId, static_cast<uint16_t>(_contentAreaHeight), /*up=*/true,
                             _visibleMsgs, loaded);
  }

  if (loaded == 0) {
    LOG_DBG("MESH", "scrollUpPage: no messages loaded from newStartId=%u", newStartId);
    return;
  }

  _visibleCount = loaded;
  _firstVisibleId = _visibleMsgs[0].id;
  _lastVisibleId = _visibleMsgs[loaded - 1].id;
  LOG_DBG("MESH", "scrollUpPage: loaded %d msgs [%u..%u] (new posPx calculation: %u - %u)", loaded, _firstVisibleId,
          _lastVisibleId, _meta.positionPx, _accHeight);

  _meta.positionId = _firstVisibleId;

  // Subtract heights of all visible messages except the last one
  {
    uint16_t exceptLastHeight = 0;
    for (uint8_t i = 0; i < loaded - 1; ++i) {
      exceptLastHeight += _visibleMsgs[i].heightPx;
    }
    _meta.positionPx = (_meta.positionPx >= exceptLastHeight) ? _meta.positionPx - exceptLastHeight : 0;
  }

  LOG_DBG("MESH", "scrollUpPage: final posPx=%u posId=%u", _meta.positionPx, _meta.positionId);

  savePosition();
  requestUpdate();
}

void MeshCoreThreadActivity::savePosition() {
  if (isChannel) {
    store.saveChannelMeta(channelIdx, _meta);
  } else {
    store.saveDirectMeta(contactPubkey, _meta);
  }
}

void MeshCoreThreadActivity::scrollDownByMessage() {
  LOG_DBG("MESH", "scrollDownByMsg: posPx=%u accH=%u lastId=%u endId=%u", _meta.positionPx, _accHeight, _lastVisibleId,
          _meta.endId);
  if (_meta.count == 0) return;
  if (_lastVisibleId >= _meta.endId) return;

  _meta.positionId = _lastVisibleId + 1;
  _meta.positionPx += _accHeight;
  
  uint32_t newStartId = _meta.positionId > 0 ? _meta.positionId : _meta.startId;
  uint8_t loaded = 0;
  if (isChannel) {
    store.loadChannelMessages(channelIdx, newStartId, static_cast<uint16_t>(_contentAreaHeight), /*up=*/true,
                              _visibleMsgs, loaded);
  } else {
    store.loadDirectMessages(contactPubkey, newStartId, static_cast<uint16_t>(_contentAreaHeight), /*up=*/true,
                             _visibleMsgs, loaded);
  }

  if (loaded == 0) {
    LOG_DBG("MESH", "scrollDownByMsg: no messages loaded from newStartId=%u", newStartId);
    return;
  }

  _visibleCount = loaded;
  _firstVisibleId = _visibleMsgs[0].id;
  _lastVisibleId = _visibleMsgs[loaded - 1].id;
  LOG_DBG("MESH", "scrollDownByMsg: loaded %d msgs [%u..%u] (new posPx calculation: %u - %u)", loaded, _firstVisibleId,
          _lastVisibleId, _meta.positionPx, _accHeight);

  _meta.positionId = _firstVisibleId;
  // _meta.positionPx = (_meta.positionPx >= _accHeight) ? _meta.positionPx - _accHeight : 0;
  LOG_DBG("MESH", "scrollDownByMsg: final posPx=%u posId=%u", _meta.positionPx, _meta.positionId);

  savePosition();
  requestUpdate();

  
  if (_meta.positionPx > _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight)) {
    _meta.positionPx = (_meta.totalPx > static_cast<uint16_t>(_contentAreaHeight))
                           ? _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight)
                           : 0;
  }

  loadVisibleBatch();
  savePosition();
  requestUpdate();
}

void MeshCoreThreadActivity::scrollUpByMessage() {
  LOG_DBG("MESH", "scrollUpByMsg: posPx=%u posId=%u startId=%u", _meta.positionPx, _meta.positionId, _meta.startId);
  if (_meta.positionPx == 0 && _meta.positionId <= _meta.startId) return;

  // Find the real previous message id (1 message up, skips holes)
  MeshCoreMessage prevMsg;
  uint8_t prevLoaded = 0;
  uint32_t searchId = _meta.positionId > 0 ? _meta.positionId - 1 : 0;
  if (isChannel) {
    store.loadChannelMessages(channelIdx, searchId, static_cast<uint8_t>(1), true, &prevMsg, prevLoaded);
  } else {
    store.loadDirectMessages(contactPubkey, searchId, static_cast<uint8_t>(1), true, &prevMsg, prevLoaded);
  }
  if (prevLoaded == 0) return;

  _meta.positionId = prevMsg.id;
  uint8_t loaded = 0;
  if (isChannel) {
    store.loadChannelMessages(channelIdx, _meta.positionId, static_cast<uint16_t>(_contentAreaHeight), /*up=*/false,
                              _visibleMsgs, loaded);
  } else {
    store.loadDirectMessages(contactPubkey, _meta.positionId, static_cast<uint16_t>(_contentAreaHeight), /*up=*/false,
                             _visibleMsgs, loaded);
  }

  if (loaded == 0) return;

  _visibleCount = loaded;
  _firstVisibleId = _visibleMsgs[0].id;
  _lastVisibleId = _visibleMsgs[loaded - 1].id;

  _meta.positionPx = (_meta.positionPx >= prevMsg.heightPx) ? _meta.positionPx - prevMsg.heightPx : 0;
  _meta.positionId = _firstVisibleId;

  savePosition();
  requestUpdate();
}

void MeshCoreThreadActivity::loop() {
  client.poll();

  // ── Font recalculation (non-blocking, one msg per iteration) ──
  if (_recalcState == RecalcState::RUNNING) {
    _recalcStep();
    if (_toast.poll()) requestUpdate();
    // Don't process input during recalc (except Back handled below)
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

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

  // ── Detect new messages (store updated by hub callback) ──
  ConvMeta currentMeta;
  bool hasMeta =
      isChannel ? store.getChannelMeta(channelIdx, currentMeta) : store.getDirectMeta(contactPubkey, currentMeta);

  if (hasMeta && currentMeta.count > _meta.count) {
    // New messages arrived
    bool wasAtEnd =
        (_meta.positionPx + static_cast<uint16_t>(_contentAreaHeight) >= _meta.totalPx || _meta.totalPx == 0);
    _meta = currentMeta;
    if (wasAtEnd) {
      loadVisibleBatchUp();
      _meta.positionPx = (_meta.totalPx > static_cast<uint16_t>(_contentAreaHeight))
                             ? _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight)
                             : 0;
      _meta.positionId = (_visibleCount > 0) ? _visibleMsgs[0].id : _meta.endId;
    } else {
      loadVisibleBatch();
    }
    requestUpdate();
  } else if (hasMeta && currentMeta.count < _meta.count) {
    // Truncation happened (oldest messages dropped)
    _meta = currentMeta;
    loadVisibleBatch();
    requestUpdate();
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
    buttonNavigator.onRelease({MappedInputManager::Button::Right}, [this] {
      if (selectedIndex == 0) {
        selectedIndex = 1;
        requestUpdate();
      } else {
        scrollDownPage();
      }
    });
    buttonNavigator.onRelease({MappedInputManager::Button::Left}, [this] {
      if (selectedIndex == 1) {
        scrollUpPage();
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

                             // Compute rendered height for batch-load scrolling
                             const auto& tmetrics = UITheme::getInstance().getMetrics();
                             int tcontentWidth = renderer.getScreenWidth() - 2 * tmetrics.contentSidePadding;
                             msg.heightPx = measureMeshCoreMessageHeight(renderer, SETTINGS.getReaderFontId(),
                                                                         tcontentWidth, isChannel, msg, tmetrics);

                             if (isChannel) {
                               store.appendChannelMessage(channelIdx, msg);
                             } else {
                               store.appendDirectMessage(contactPubkey, msg);
                             }
                             // Reload meta and batch-load from end
                             if (isChannel) {
                               store.getChannelMeta(channelIdx, _meta);
                             } else {
                               store.getDirectMeta(contactPubkey, _meta);
                             }
                             loadVisibleBatchUp();
                             _meta.positionPx = (_meta.totalPx > static_cast<uint16_t>(_contentAreaHeight))
                                                    ? _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight)
                                                    : 0;
                             _meta.positionId = (_visibleCount > 0) ? _visibleMsgs[0].id : _meta.endId;
                             savePosition();
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
      if (_visibleCount == 0 && _meta.count == 0) {
        GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_MESSAGES));
      } else if (_visibleCount > 0) {
        // Two-pass font prewarming: scan → prewarm → render
        auto* fcm = renderer.getFontCacheManager();
        bool useReaderFontSettings = true;
        if (fcm) {
          MESHCORE_LOG_HEAP("Thread prewarm:before");
          auto scope = fcm->createPrewarmScope();
          drawVisibleMessages(renderer, contentRect, useReaderFontSettings, /*scanOnly=*/true);
          scope.endScanAndPrewarm();
          MESHCORE_LOG_HEAP("Thread prewarm:after");
          drawVisibleMessages(renderer, contentRect, useReaderFontSettings);
        } else {
          drawVisibleMessages(renderer, contentRect, useReaderFontSettings);
        }
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

// --- Message rendering (reads from _visibleMsgs array) ---

void MeshCoreThreadActivity::drawVisibleMessages(const GfxRenderer& renderer, Rect rect, bool useReaderFontSettings,
                                                 bool scanOnly) {
  if (_visibleCount == 0) return;

  constexpr int maxLines = 100;
  const ThreadRenderCtx ctx(renderer, rect, useReaderFontSettings);

  // ── Scan-only pass for font prewarming ──
  if (scanOnly) {
    for (uint8_t i = 0; i < _visibleCount; ++i) {
      const auto& msg = _visibleMsgs[i];
      bool showSender = (isChannel && msg.direction != MsgDirection::SENT && msg.senderName[0]);
      if (showSender) {
        renderer.drawText(ctx.bodyFontId, 0, 0, msg.senderName, true);
      }
      if (msg.text[0]) {
        renderer.drawText(ctx.bodyFontId, 0, 0, msg.text, true);
      }
      if (msg.timestamp > 0) {
        char tsBuf[16];
        formatMeshCoreTimestamp(msg.timestamp, tsBuf, sizeof(tsBuf));
        renderer.drawText(ctx.metaFontId, 0, 0, tsBuf, true);
      }
    }
    return;
  }

  // Accumulate height of messages before the visible batch to compute partial offset
  _accHeight = 0;
  for (uint8_t i = 0; i < _visibleCount; ++i) {
    _accHeight += _visibleMsgs[i].heightPx;
    if (_accHeight > static_cast<uint16_t>(_contentAreaHeight)) {
      _accHeight -= _visibleMsgs[i].heightPx;
      _lastVisibleId = _visibleMsgs[i - 1].id;
      break;
    }
  }

  int y = rect.y;
  bool rendered = false;

  for (uint8_t i = 0; i < _visibleCount; ++i) {
    if (y > rect.y + rect.height) break;
    rendered = true;

    const auto& msg = _visibleMsgs[i];
    const bool outgoing = (msg.direction == MsgDirection::SENT);
    const bool showSender = (isChannel && !outgoing && msg.senderName[0]);

    // ── Sender line ──
    if (showSender) {
      int senderX;
      if (outgoing) {
        int senderW = renderer.getTextWidth(ctx.bodyFontId, msg.senderName);
        senderX = rect.x + rect.width - ctx.metrics.contentSidePadding - senderW;
      } else {
        senderX = rect.x + ctx.metrics.contentSidePadding;
      }
      renderer.drawText(ctx.bodyFontId, senderX, y, msg.senderName, true);
      // Checkerboard dither for received messages
      if (!outgoing) {
        int sw = renderer.getTextWidth(ctx.bodyFontId, msg.senderName);
        for (int py = y; py < y + ctx.bodyLineH; py++)
          for (int px = senderX; px < senderX + sw; px++)
            if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
      }
      y += ctx.bodyLineH;
    }

    // ── Body text ──
    if (msg.text[0]) {
      auto lines = wrapMessageBody(renderer, ctx.bodyFontId, msg.text, ctx.maxTextWidth, maxLines);
      for (const auto& line : lines) {
        if (y > rect.y + rect.height) break;
        if (line.empty()) {
          y += ctx.bodyLineH;
          continue;
        }
        if (outgoing) {
          int textW = renderer.getTextWidth(ctx.bodyFontId, line.c_str());
          renderer.drawText(ctx.bodyFontId, rect.x + rect.width - ctx.metrics.contentSidePadding - textW, y,
                            line.c_str(), true);
        } else {
          renderer.drawText(ctx.bodyFontId, rect.x + ctx.metrics.contentSidePadding, y, line.c_str(), true);
        }
        y += ctx.bodyLineH;
      }
    }

    // ── Meta line (timestamp + hops) ──
    if (msg.timestamp > 0) {
      if (y > rect.y + rect.height) break;
      char metaBuf[64];
      char tsBuf[16];
      formatMeshCoreTimestamp(msg.timestamp, tsBuf, sizeof(tsBuf));
      char hopBuf[12];
      if (isChannel && outgoing) {
        meshcore::formatMeshCoreHeardRepeats(msg.pathLength, hopBuf, sizeof(hopBuf));
      } else {
        meshcore::formatMeshCoreHopCount(msg.pathLength, hopBuf, sizeof(hopBuf));
      }
      snprintf(metaBuf, sizeof(metaBuf), "%s %s %s", tsBuf, meshcore::DotSeparator, hopBuf);
      int metaX;
      if (outgoing) {
        int metaW = renderer.getTextWidth(ctx.metaFontId, metaBuf);
        metaX = rect.x + rect.width - ctx.metrics.contentSidePadding - metaW;
      } else {
        metaX = rect.x + ctx.metrics.contentSidePadding;
      }
      renderer.drawText(ctx.metaFontId, metaX, y, metaBuf, true);
      // Checkerboard dither
      int mw = renderer.getTextWidth(ctx.metaFontId, metaBuf);
      for (int py = y; py < y + ctx.metaLineH; py++)
        for (int px = metaX; px < metaX + mw; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
      y += ctx.metaLineH;
    }

    // Vertical spacing between messages
    if (y < rect.y + rect.height) {
      y += ctx.metrics.verticalSpacing;
    }
  }

  // ── Render pass ──
  GUI.drawScrollBar(renderer, rect, _meta.totalPx, _meta.positionPx);

  // Clear overflow areas
  if (rendered) {
    const int screenW = renderer.getScreenWidth();
    const int screenH = renderer.getScreenHeight();
    if (rect.y > 0) renderer.fillRect(0, 0, screenW, rect.y, false);
    const int belowY = rect.y + rect.height;
    if (belowY < screenH) renderer.fillRect(0, belowY, screenW, screenH - belowY, false);
  }
}

// --- Font recalculation (async, non-blocking) ---

void MeshCoreThreadActivity::_recalcStep() {
  if (_recalcState != RecalcState::RUNNING) return;

  // Process one message per call
  MeshCoreMessage msg;
  uint8_t loaded = 0;
  if (isChannel) {
    store.loadChannelMessages(channelIdx, _recalcGid, static_cast<uint8_t>(1), false, &msg, loaded);
  } else {
    store.loadDirectMessages(contactPubkey, _recalcGid, static_cast<uint8_t>(1), false, &msg, loaded);
  }
  if (loaded == 0) {
    // Skip missing message, advance to next
    _recalcGid++;
    if (_recalcGid > _recalcEndId) _finishRecalc();
    return;
  }

  // Recompute height with current font settings
  msg.heightPx = measureMeshCoreMessageHeight(renderer, _recalcFontId, _recalcContentWidth, _recalcIsChannel, msg,
                                              UITheme::getInstance().getMetrics());

  // Write back
  if (isChannel) {
    store.updateChannelMessage(channelIdx, _recalcGid, msg);
  } else {
    store.updateDirectMessage(contactPubkey, _recalcGid, msg);
  }

  _recalcNewTotalPx += msg.heightPx;
  _recalcGid++;

  if (_recalcGid > _recalcEndId) {
    _finishRecalc();
  }
}

void MeshCoreThreadActivity::_finishRecalc() {
  // Update and save ConvMeta
  if (_recalcMeta.totalPx > 0) {
    _recalcMeta.positionPx = static_cast<uint16_t>((static_cast<uint32_t>(_recalcMeta.positionPx) * _recalcNewTotalPx) /
                                                   _recalcMeta.totalPx);
  }
  _recalcMeta.totalPx = _recalcNewTotalPx;
  _recalcMeta.fontId = _recalcFontId;

  if (isChannel) {
    store.saveChannelMeta(channelIdx, _recalcMeta);
  } else {
    store.saveDirectMeta(contactPubkey, _recalcMeta);
  }

  _meta = _recalcMeta;
  _recalcState = RecalcState::DONE;

  // Clear toast, show loaded confirmation
  _toast.show(tr(STR_MESHCORE_LOADED), 3000);

  // Load visible batch with new heights
  loadVisibleBatch();
  requestUpdate();
  LOG_DBG("MESH", "Recalculation complete: totalPx=%d fontId=%d", _meta.totalPx, _recalcFontId);
}

// --- Word-wrap helper (moved from BaseTheme) ---

std::vector<std::string> MeshCoreThreadActivity::wrapMessageBody(const GfxRenderer& renderer, int fontId,
                                                                 const char* text, int maxWidth, int maxLines) {
  std::vector<std::string> result;
  if (!text || !*text) return result;

  // Fast path: no newlines → delegate directly to wrappedText (no extra copy)
  bool hasNewline = false;
  for (const char* p = text; *p; ++p) {
    if (*p == '\n') {
      hasNewline = true;
      break;
    }
  }
  if (!hasNewline) {
    return renderer.wrappedText(fontId, text, maxWidth, maxLines);
  }

  // Slow path: split on \n and word-wrap each segment
  const char* segStart = text;
  const char* p = text;
  while (*p && static_cast<int>(result.size()) < maxLines) {
    if (*p == '\n') {
      // Handle \r\n: adjust end to exclude preceding \r
      const char* segEnd = (p > segStart && *(p - 1) == '\r') ? p - 1 : p;
      size_t segLen = segEnd - segStart;
      if (segLen == 0) {
        result.emplace_back();  // empty line marker
      } else {
        std::string segment(segStart, segLen);
        auto wrapped =
            renderer.wrappedText(fontId, segment.c_str(), maxWidth, maxLines - static_cast<int>(result.size()));
        auto n = std::min(wrapped.size(), static_cast<size_t>(std::max(0, maxLines - static_cast<int>(result.size()))));
        result.insert(result.end(), std::make_move_iterator(wrapped.begin()),
                      std::make_move_iterator(wrapped.begin() + n));
      }
      segStart = p + 1;  // skip past \n
    }
    ++p;
  }

  // Trailing segment after the last \n (or whole text if no \n ended the loop)
  if (segStart < p && static_cast<int>(result.size()) < maxLines) {
    size_t segLen = p - segStart;
    std::string segment(segStart, segLen);
    auto wrapped = renderer.wrappedText(fontId, segment.c_str(), maxWidth, maxLines - static_cast<int>(result.size()));
    result.insert(result.end(), std::make_move_iterator(wrapped.begin()), std::make_move_iterator(wrapped.end()));
  }

  return result;
}
