#include "MeshCoreThreadActivity.h"

#include <I18n.h>
#include <Logging.h>

#ifdef SIMULATOR
#include <MeshCoreMockHotkeys.h>
#endif

#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "FontCacheManager.h"
#include "MeshCoreHubActivity.h"
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
                                               const char* channelName, MeshCoreHubActivity* hub)
    : Activity("MeshCoreThread", renderer, mappedInput),
      client(client),
      store(store),
      _hub(hub),
      isChannel(true),
      channelIdx(channelIdx) {
  snprintf(threadName, sizeof(threadName), "%s", channelName);
}

// Direct message constructor
MeshCoreThreadActivity::MeshCoreThreadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               MeshCoreClient& client, MeshCoreMessageStore& store,
                                               const MeshCoreContact& contact, MeshCoreHubActivity* hub)
    : Activity("MeshCoreThread", renderer, mappedInput), client(client), store(store), _hub(hub), isChannel(false) {
  memcpy(contactPubkey, contact.publicKey, 32);
  if (contact.name[0] != '\0') {
    snprintf(threadName, sizeof(threadName), "%s", contact.name);
  } else {
    snprintf(threadName, sizeof(threadName), "%s", tr(STR_MESHCORE_UNKNOWN));
  }
}

void MeshCoreThreadActivity::onDeliveryUpdate(uint32_t msgId, const uint8_t* pubkey32, DeliveryStatus status) {
  // The Hub already wrote the new status to the store.
  // Reload meta and re-load visible messages so the UI reflects the change.
  ConvMeta currentMeta;
  if (store.getDirectMeta(contactPubkey, currentMeta)) {
    _meta = currentMeta;
    loadMessages(_meta.positionId > 0 ? _meta.positionId : _meta.startId, /*up=*/false);
  }
  requestUpdate();
  LOG_INF("MESH", "Thread DM delivery: msgId=%lu status=%d", (unsigned long)msgId, (int)status);
}

void MeshCoreThreadActivity::onEnter() {
  Activity::onEnter();
  MESHCORE_LOG_HEAP("Thread onEnter:start");

  // Register with Hub for delivery callbacks (Hub writes to store + forwards here)
  if (_hub) _hub->setActiveThread(this);

  // Explicitly reset to Messages tab on every entry
  currentTab = Tab::MESSAGES;
  selectedIndex = 0;

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
    requestUpdate();
    return;
  }

  // Check font match — if changed, recalculate all heights synchronously.
  // Mirrors the reader's pattern: block onEnter() with a popup while
  // rebuilding, so the user sees what's happening and the next render()
  // shows ready-to-use pages.
  if (_meta.fontId != _bodyFontId) {
    LOG_DBG("MESH", "Thread onEnter: font mismatch (meta=%d current=%d) — queuing rebuild", _meta.fontId, _bodyFontId);
    _needsRebuild = true;
  } else {
    loadMessages(_meta.positionId > 0 ? _meta.positionId : _meta.startId, /*up=*/false);
  }
  LOG_DBG("MESH", "Thread onEnter: loaded batch — posPx=%u posId=%u firstId=%u lastId=%u accHeight=%u totalPx=%u",
          _meta.positionPx, _meta.positionId, _firstVisibleId, _lastVisibleId, _accHeight, _meta.totalPx);
  MESHCORE_LOG_HEAP("Thread onEnter:end");

  requestUpdate();
}

void MeshCoreThreadActivity::onExit() {
  MESHCORE_LOG_HEAP("Thread onExit");
  _pendingOp = PendingOp::IDLE;
  // Unregister from Hub delivery forwarding
  if (_hub) _hub->clearActiveThread(this);
  savePosition();
  memset(_visibleMsgs, 0, sizeof(_visibleMsgs));
  _visibleCount = 0;
  Activity::onExit();
}

bool MeshCoreThreadActivity::loadMessages(uint32_t startId, bool up) {
  uint8_t loaded = 0;
  if (isChannel) {
    store.loadChannelMessages(channelIdx, startId, static_cast<uint16_t>(_contentAreaHeight), up, _visibleMsgs, loaded,
                              _fillerMsg);
  } else {
    store.loadDirectMessages(contactPubkey, startId, static_cast<uint16_t>(_contentAreaHeight), up, _visibleMsgs,
                             loaded, _fillerMsg);
  }
  _visibleCount = loaded;
  if (loaded > 0) {
    _firstVisibleId = _visibleMsgs[0].id;
    _lastVisibleId = _visibleMsgs[loaded - 1].id;
    _accHeight = 0;
    for (uint8_t i = 0; i < loaded; ++i) {
      _accHeight += _visibleMsgs[i].heightPx;
      if (_accHeight > static_cast<uint16_t>(_contentAreaHeight)) {
        _accHeight -= _visibleMsgs[i].heightPx;
        _lastVisibleId = _visibleMsgs[i - 1].id;
        break;
      }
    }
    return true;
  }
  return false;
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
  // if (_meta.positionPx > _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight)) {
  //   _meta.positionPx = (_meta.totalPx > static_cast<uint16_t>(_contentAreaHeight))
  //                          ? _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight)
  //                          : 0;
  //   _meta.positionId = _meta.endId;
  //   LOG_DBG("MESH", "scrollDownPage: clamped to end, loading Up (posPx=%u posId=%u)", _meta.positionPx,
  //           _meta.positionId);
  //   loadVisibleBatchUp();
  // } else {
  LOG_DBG("MESH", "scrollDownPage: advancing (posPx=%u posId=%u)", _meta.positionPx, _meta.positionId);
  loadMessages(_meta.positionId > 0 ? _meta.positionId : _meta.startId, /*up=*/false);
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
  loadMessages(newStartId, /*up=*/true);

  if (_visibleCount == 0) {
    LOG_DBG("MESH", "scrollUpPage: no messages loaded from newStartId=%u", newStartId);
    return;
  }

  LOG_DBG("MESH", "scrollUpPage: loaded %d msgs [%u..%u] (new posPx calculation: %u - %u)", _visibleCount,
          _firstVisibleId, _lastVisibleId, _meta.positionPx, _accHeight);

  _meta.positionId = _firstVisibleId;

  // Subtract _accHeight (total visible content height from the last render before
  // this scroll) — symmetric with scrollDownPage which adds _accHeight.
  // Using "all-but-last" here was asymmetric and caused positionPx drift,
  // leaving the scrollbar stuck mid-way after scrolling all the way up.
  _meta.positionPx = (_meta.positionPx >= _accHeight) ? _meta.positionPx - _accHeight : 0;

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

void MeshCoreThreadActivity::scrollToEnd() {
  if (_meta.count == 0) return;
  loadMessages(_meta.endId, /*up=*/true);
  if (_meta.totalPx > static_cast<uint16_t>(_contentAreaHeight)) {
    _meta.positionPx = _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight);
  } else {
    _meta.positionPx = 0;
  }
  _meta.positionId = (_visibleCount > 0) ? _visibleMsgs[0].id : _meta.endId;
  savePosition();
  currentTab = Tab::MESSAGES;
  selectedIndex = 1;
  requestUpdate();
}

void MeshCoreThreadActivity::clearConversation() {
  if (isChannel) {
    store.clearChannelMessages(channelIdx);
  } else {
    store.clearDirectMessages(contactPubkey);
  }
  _meta = {};
  _visibleCount = 0;
  _fillerMsg = {};
  savePosition();
  _toast.show(tr(STR_MESHCORE_CONVERSATION_CLEARED), 3000);
  currentTab = Tab::MESSAGES;
  selectedIndex = 0;
  requestUpdate();
}

void MeshCoreThreadActivity::scrollDownByMessage() {
  LOG_DBG("MESH", "scrollDownByMsg: posPx=%u accH=%u lastId=%u endId=%u", _meta.positionPx, _accHeight, _lastVisibleId,
          _meta.endId);
  if (_meta.count == 0) return;
  if (_lastVisibleId >= _meta.endId) return;

  _meta.positionId = _lastVisibleId + 1;
  _meta.positionPx += _accHeight;

  uint32_t newStartId = _meta.positionId > 0 ? _meta.positionId : _meta.startId;
  loadMessages(newStartId, /*up=*/true);

  if (_visibleCount == 0) {
    LOG_DBG("MESH", "scrollDownByMsg: no messages loaded from newStartId=%u", newStartId);
    return;
  }

  _meta.positionId = _firstVisibleId;
  _meta.positionPx =
      (_meta.positionPx >= _accHeight) ? _meta.positionPx - _accHeight + _visibleMsgs[_visibleCount - 1].heightPx : 0;
  LOG_DBG("MESH", "scrollDownByMsg: final posPx=%u posId=%u", _meta.positionPx, _meta.positionId);

  savePosition();
  requestUpdate();

  if (_meta.positionPx > _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight)) {
    _meta.positionPx = (_meta.totalPx > static_cast<uint16_t>(_contentAreaHeight))
                           ? _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight)
                           : 0;
  }

  loadMessages(_meta.positionId > 0 ? _meta.positionId : _meta.startId, /*up=*/false);
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
  loadMessages(_meta.positionId, /*up=*/false);

  if (_visibleCount == 0) return;

  _meta.positionPx = (_meta.positionPx >= prevMsg.heightPx) ? _meta.positionPx - prevMsg.heightPx : 0;
  _meta.positionId = _firstVisibleId;

  savePosition();
  requestUpdate();
}

void MeshCoreThreadActivity::loop() {
  client.poll();

  // Auto-clear expired toast messages
  if (_toast.poll()) {
    requestUpdate();
  }

  // ── Async BLE unlist operation state machine ──
  if (_pendingOp != PendingOp::IDLE && !client.isCommandPending()) {
    completeUnlistOp(client.getLastCommandResult());
  }
  if (_pendingOp != PendingOp::IDLE && (millis() - _pendingStartMs) > 10000) {
    LOG_ERR("MESH", "Unlist BLE timeout (no response after 10 s)");
    completeUnlistOp(false);
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
    LOG_DBG("MESH", "New msgs detected: old.count=%u new.count=%u old.fontId=%d new.fontId=%d wasAtEnd=%d", _meta.count,
            currentMeta.count, _meta.fontId, currentMeta.fontId, wasAtEnd);
    _meta = currentMeta;
    if (wasAtEnd) {
      loadMessages(_meta.endId, /*up=*/true);
      _meta.positionPx = (_meta.totalPx > static_cast<uint16_t>(_contentAreaHeight))
                             ? _meta.totalPx - static_cast<uint16_t>(_contentAreaHeight)
                             : 0;
      _meta.positionId = (_visibleCount > 0) ? _visibleMsgs[0].id : _meta.endId;
    } else {
      loadMessages(_meta.positionId > 0 ? _meta.positionId : _meta.startId, /*up=*/false);
    }
    requestUpdate();
  } else if (hasMeta && currentMeta.count < _meta.count) {
    // Truncation happened (oldest messages dropped)
    _meta = currentMeta;
    loadMessages(_meta.positionId > 0 ? _meta.positionId : _meta.startId, /*up=*/false);
    requestUpdate();
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
    // Block all Confirm actions while a BLE operation is pending
    if (_pendingOp != PendingOp::IDLE) return;

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
        // Channel menu: 0=Scroll to End, 1=Clear, 2=Delete Channel
        bool connected = (client.getState() == BleConnectionState::CONNECTED);
        switch (itemIdx) {
          case 0:  // Scroll to End
            scrollToEnd();
            return;
          case 1:  // Clear Conversation
            clearConversation();
            return;
          case 2: {  // Delete Channel (async, waits for BLE)
            if (!connected) {
              _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
              requestUpdate();
              break;
            }
            if (!client.deleteChannel(channelIdx)) {
              LOG_ERR("MESH", "Failed to queue channel delete");
              _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
              requestUpdate();
              break;
            }
            LOG_DBG("MESH", "Queued channel %d delete — waiting for BLE response", channelIdx);
            _pendingOp = PendingOp::DELETING_CHANNEL;
            _pendingStartMs = millis();
            _toast.show(tr(STR_MESHCORE_REMOVING), 0);  // persistent until result
            selectedIndex = 0;
            requestUpdate();
            return;
          }
          default:
            break;
        }
      } else {
        // DM menu: 0=Reset Path, 1=Scroll to End, 2=Clear, 3=Unlist
        bool connected = (client.getState() == BleConnectionState::CONNECTED);
        switch (itemIdx) {
          case 0: {  // Reset Path
            if (!connected) {
              _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
              requestUpdate();
              break;
            }
            // Load the contact from the store to get current pathLength
            constexpr uint8_t kMaxContacts = 20;
            MeshCoreContact contacts[kMaxContacts] = {};
            uint8_t count = store.loadContacts(contacts, kMaxContacts);
            bool hasPath = false;
            MeshCoreContact found = {};
            for (uint8_t i = 0; i < count; ++i) {
              if (memcmp(contacts[i].publicKey, contactPubkey, 32) == 0) {
                found = contacts[i];
                hasPath = (found.pathLength != 0xFF);
                break;
              }
            }
            if (!hasPath) {
              _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
              requestUpdate();
              break;
            }
            client.resetPath(found);
            _toast.show(tr(STR_PATH_RESET), 3000);
            currentTab = Tab::MESSAGES;
            selectedIndex = 0;
            requestUpdate();
            return;
          }
          case 1:  // Scroll to End
            scrollToEnd();
            return;
          case 2:  // Clear Conversation
            clearConversation();
            return;
          case 3: {  // Unlist Contact (async, waits for BLE)
            if (!connected) {
              _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
              requestUpdate();
              break;
            }
            if (!client.removeContact(contactPubkey)) {
              LOG_ERR("MESH", "Failed to queue contact delete");
              _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
              requestUpdate();
              break;
            }
            LOG_DBG("MESH", "Queued contact unlist — waiting for BLE response");
            _pendingOp = PendingOp::DELETING_CONTACT;
            _pendingStartMs = millis();
            _toast.show(tr(STR_MESHCORE_REMOVING), 0);  // persistent until result
            selectedIndex = 0;
            requestUpdate();
            return;
          }
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
      return isChannel ? 3 : 4;  // Channel: 3 items; DM: 4 items (+Reset Path)
    default:
      return 0;
  }
}

void MeshCoreThreadActivity::provideSubtitle(const void* ctx, char* buf, size_t bufSize) {
  const auto* self = static_cast<const MeshCoreThreadActivity*>(ctx);
  formatMeshCoreSubtitle(self->client, buf, bufSize);
}

// ── Async unlist completion handler ──
// Called from loop() when the BLE command completes or times out.
// Mirrors Discovery's completeContactSave() exactly — also updates the
// saved contact list in the store so the Hub picks up the change.

void MeshCoreThreadActivity::completeUnlistOp(bool success) {
  _pendingOp = PendingOp::IDLE;
  if (success) {
    LOG_DBG("MESH", "Unlist %s succeeded", isChannel ? "channel" : "contact");

    if (!isChannel) {
      // Load current contacts, remove this one, save back — same pattern
      // as Discovery's completeContactSave(DELETING).
      constexpr uint8_t kMaxContacts = 20;
      MeshCoreContact contacts[kMaxContacts] = {};
      uint8_t count = store.loadContacts(contacts, kMaxContacts);
      for (uint8_t i = 0; i < count; ++i) {
        if (memcmp(contacts[i].publicKey, contactPubkey, 32) == 0) {
          for (uint8_t j = i; j + 1 < count; ++j) {
            contacts[j] = contacts[j + 1];
          }
          count--;
          store.saveContacts(contacts, count);
          LOG_INF("MESH", "Removed contact from saved list");
          break;
        }
      }
    }

    setResult(ActivityResult(MeshCoreUnlistResult{isChannel}));
    finish();
  } else {
    LOG_ERR("MESH", "Unlist %s failed", isChannel ? "channel" : "contact");
    _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
    requestUpdate();
  }
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

                           // For DMs: append to store FIRST to obtain the assigned id,
                           // then send with that id for delivery tracking.
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

                           bool sent = false;
                           if (isChannel) {
                             sent = client.sendChannelMessage(channelIdx, text.c_str());
                             if (sent) store.appendChannelMessage(channelIdx, msg);
                           } else {
                             // Append first to obtain the assigned id
                             uint32_t msgId = 0;
                             if (store.appendDirectMessage(contactPubkey, msg, &msgId)) {
                               // Now send with the store-assigned id for delivery tracking.
                               // Load the real contact from the store to get pathLength
                               // (0xFF = no path → start at flood; otherwise → direct).
                               MeshCoreContact contact = {};
                               memcpy(contact.publicKey, contactPubkey, 32);
                               snprintf(contact.name, sizeof(contact.name), "%s", threadName);
                               // Try to load saved contact for pathLength
                               constexpr uint8_t kMaxSend = 20;
                               MeshCoreContact saved[kMaxSend] = {};
                               uint8_t savedCount = store.loadContacts(saved, kMaxSend);
                               for (uint8_t i = 0; i < savedCount; ++i) {
                                 if (memcmp(saved[i].publicKey, contactPubkey, 32) == 0) {
                                   contact.pathLength = saved[i].pathLength;
                                   contact.type = saved[i].type;
                                   break;
                                 }
                               }
                               sent = client.sendDirectMessage(contact, text.c_str(), msgId);
                               if (!sent) {
                                 // Send failed — update the persisted record to FAILED
                                 store.updateDirectMessage(contactPubkey, msgId, DeliveryStatus::FAILED);
                               }
                             }
                           }

                           if (sent) {
                             LOG_INF("MESH", "Message queued");
                             // Reload meta and batch-load from end
                             if (isChannel) {
                               store.getChannelMeta(channelIdx, _meta);
                             } else {
                               store.getDirectMeta(contactPubkey, _meta);
                             }
                             loadMessages(_meta.endId, /*up=*/true);
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

  // --- Font rebuild popup (like reader's "Indexing…") ---
  if (_needsRebuild) {
    _needsRebuild = false;
    renderer.clearScreen();
    GUI.drawPopup(renderer, tr(STR_MESHCORE_RECALC_LAYOUT));
    renderer.displayBuffer();
    _rebuildMessageHeights();
    loadMessages(_meta.positionId > 0 ? _meta.positionId : _meta.startId, /*up=*/false);
    requestUpdate();
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

  constexpr int kChannelItemCount = 3;
  constexpr int kDmItemCount = 4;
  int kItemCount = isChannel ? kChannelItemCount : kDmItemCount;

  // Menu item labels — table-driven to avoid duplicating the shared
  // entries (Scroll to End, Clear Conversation) across two switches.
  static constexpr StrId kChannelTitles[] = {
      StrId::STR_MESHCORE_SCROLL_TO_END,
      StrId::STR_MESHCORE_CLEAR_CONVERSATION,
      StrId::STR_MESHCORE_DELETE_CHANNEL,
  };
  static constexpr StrId kDmTitles[] = {
      StrId::STR_PATH_RESET,
      StrId::STR_MESHCORE_SCROLL_TO_END,
      StrId::STR_MESHCORE_CLEAR_CONVERSATION,
      StrId::STR_MESHCORE_REMOVE_CONTACT,
  };
  const auto& titles = isChannel ? kChannelTitles : kDmTitles;

  GUI.drawList(
      renderer, contentRect, kItemCount, selectedIndex - 1,
      /*rowTitle*/
      [&titles, kItemCount](int index) -> std::string {
        if (index < 0 || index >= kItemCount) return {};
        return I18n::getInstance().get(titles[index]);
      },
      /*rowSubtitle*/ nullptr,
      /*rowIcon*/ nullptr,
      /*rowValue*/ nullptr,
      /*highlightValue*/ false,
      /*rowDimmed*/
      [this, connected](int index) -> bool {
        if (isChannel) {
          // Channel: item 2 (unlist/delete) requires connected companion
          if (!connected) return (index == 2);
          return false;
        } else {
          // DM: item 0 (Reset Path) requires connected + has path;
          // item 3 (unlist) requires connected
          if (index == 0) {
            if (!connected) return true;
            // Check if contact has a path by loading from store
            constexpr uint8_t kMaxDim = 20;
            MeshCoreContact contacts[kMaxDim] = {};
            uint8_t count = store.loadContacts(contacts, kMaxDim);
            for (uint8_t i = 0; i < count; ++i) {
              if (memcmp(contacts[i].publicKey, contactPubkey, 32) == 0) {
                return (contacts[i].pathLength == 0xFF);  // dim if no path
              }
            }
            return true;  // contact not found → dim
          }
          if (!connected) return (index == 3);
          return false;
        }
      });
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
    // Also prewarm the filler message if present
    if (_fillerMsg.id != 0) {
      bool showSender = (isChannel && _fillerMsg.direction != MsgDirection::SENT && _fillerMsg.senderName[0]);
      if (showSender) {
        renderer.drawText(ctx.bodyFontId, 0, 0, _fillerMsg.senderName, true);
      }
      if (_fillerMsg.text[0]) {
        renderer.drawText(ctx.bodyFontId, 0, 0, _fillerMsg.text, true);
      }
      if (_fillerMsg.timestamp > 0) {
        char tsBuf[16];
        formatMeshCoreTimestamp(_fillerMsg.timestamp, tsBuf, sizeof(tsBuf));
        renderer.drawText(ctx.metaFontId, 0, 0, tsBuf, true);
      }
    }
    return;
  }

  int y = rect.y;
  bool rendered = false;

  // ── Helper: render one message (sender → body → meta), advancing y ──
  // clipToFit=true  → only lines that fit fully (y + lineH <= bottom) are drawn.
  // clipToFit=false → draws lines normally, caller stops when y > bottom.
  auto renderOneMsg = [&](const MeshCoreMessage& msg, int& yPos, bool clipToFit) {
    const int bottom = rect.y + rect.height;
    const bool outgoing = (msg.direction == MsgDirection::SENT);
    const bool showSender = (isChannel && !outgoing && msg.senderName[0]);

    auto fits = [&](int lineH) { return clipToFit ? (yPos + lineH <= bottom) : (yPos <= bottom); };

    // Sender line
    if (showSender && fits(ctx.bodyLineH)) {
      int senderX;
      if (outgoing) {
        int senderW = renderer.getTextWidth(ctx.bodyFontId, msg.senderName);
        senderX = rect.x + rect.width - ctx.metrics.contentSidePadding - senderW;
      } else {
        senderX = rect.x + ctx.metrics.contentSidePadding;
      }
      renderer.drawText(ctx.bodyFontId, senderX, yPos, msg.senderName, true);
      if (!outgoing) {
        int sw = renderer.getTextWidth(ctx.bodyFontId, msg.senderName);
        for (int py = yPos; py < yPos + ctx.bodyLineH; py++)
          for (int px = senderX; px < senderX + sw; px++)
            if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
      }
      yPos += ctx.bodyLineH;
    }

    // Body text — line by line
    if (msg.text[0] && fits(0)) {
      auto lines = wrapMessageBody(renderer, ctx.bodyFontId, msg.text, ctx.maxTextWidth, maxLines);
      for (const auto& line : lines) {
        if (!fits(ctx.bodyLineH)) break;
        if (!line.empty()) {
          if (outgoing) {
            int textW = renderer.getTextWidth(ctx.bodyFontId, line.c_str());
            renderer.drawText(ctx.bodyFontId, rect.x + rect.width - ctx.metrics.contentSidePadding - textW, yPos,
                              line.c_str(), true);
          } else {
            renderer.drawText(ctx.bodyFontId, rect.x + ctx.metrics.contentSidePadding, yPos, line.c_str(), true);
          }
        }
        yPos += ctx.bodyLineH;
      }
    }

    // Meta line (timestamp + delivery status or hops)
    if (msg.timestamp > 0 && fits(ctx.metaLineH)) {
      char metaBuf[64];
      char tsBuf[16];
      formatMeshCoreTimestamp(msg.timestamp, tsBuf, sizeof(tsBuf));
      char hopBuf[20];
      if (isChannel && outgoing) {
        meshcore::formatMeshCoreHeardRepeats(msg.pathLength, hopBuf, sizeof(hopBuf));
      } else if (!isChannel && outgoing) {
        // Show delivery status instead of hop count for outgoing DMs
        switch (msg.deliveryStatus) {
          case DeliveryStatus::ACKED:
            snprintf(hopBuf, sizeof(hopBuf), "%s", tr(STR_MESHCORE_MSG_ACKED));
            break;
          case DeliveryStatus::FAILED:
            snprintf(hopBuf, sizeof(hopBuf), "%s", tr(STR_MESHCORE_MSG_FAILED));
            break;
          case DeliveryStatus::SENT:
          default:
            snprintf(hopBuf, sizeof(hopBuf), "%s", tr(STR_MESHCORE_MSG_SENT));
            break;
        }
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
      renderer.drawText(ctx.metaFontId, metaX, yPos, metaBuf, true);
      int mw = renderer.getTextWidth(ctx.metaFontId, metaBuf);
      for (int py = yPos; py < yPos + ctx.metaLineH; py++)
        for (int px = metaX; px < metaX + mw; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
      yPos += ctx.metaLineH;
    }
  };

  // ── Main message batch ──
  for (uint8_t i = 0; i < _visibleCount; ++i) {
    if (y > rect.y + rect.height) break;
    rendered = true;
    renderOneMsg(_visibleMsgs[i], y, /*clipToFit=*/false);
    // Vertical gap between messages (proportional to font size)
    if (y < rect.y + rect.height) {
      y += meshcoreMessageGapPx(ctx.bodyLineH);
    }
  }

  // ── Filler message (partial render in remaining space at bottom) ──
  if (_fillerMsg.id != 0 && y < rect.y + rect.height) {
    renderOneMsg(_fillerMsg, y, /*clipToFit=*/true);
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

// ── Font recalculation (synchronous, called from onEnter) ──
// Mirrors the reader's createSectionFile pattern: blocks until done,
// with a popup on screen so the user knows the device is busy.

void MeshCoreThreadActivity::_rebuildMessageHeights() {
  const auto& tmetrics = UITheme::getInstance().getMetrics();
  uint16_t newTotalPx = 0;
  const int oldFontId = _meta.fontId;
  const uint32_t t0 = millis();

  for (uint32_t gid = _meta.startId; gid <= _meta.endId; ++gid) {
    MeshCoreMessage msg;
    uint8_t loaded = 0;
    if (isChannel) {
      store.loadChannelMessages(channelIdx, gid, static_cast<uint8_t>(1), false, &msg, loaded);
    } else {
      store.loadDirectMessages(contactPubkey, gid, static_cast<uint8_t>(1), false, &msg, loaded);
    }
    if (loaded == 0) continue;

    // Warm the SD card font's advance table in one batched, glyph-index-ordered SD
    // read before measuring. Without this, measureMeshCoreMessageHeight() -> wrappedText()
    // -> getTextAdvanceX() would fall back to a per-character glyph load (random SD I/O)
    // for every uncached codepoint. Builtin fonts are not in sdCardFonts_, so this is a
    // no-op for them. Body text is drawn REGULAR, so only style 0x01 is needed.
    if (msg.text[0]) {
      renderer.ensureSdCardFontReady(_bodyFontId, msg.text, /*styleMask=*/0x01);
    }

    msg.heightPx = measureMeshCoreMessageHeight(renderer, _bodyFontId, _contentAreaWidth, isChannel, msg, tmetrics);

    if (isChannel) {
      store.updateChannelMessage(channelIdx, gid, msg);
    } else {
      store.updateDirectMessage(contactPubkey, gid, msg);
    }

    newTotalPx += msg.heightPx;

    if ((gid - _meta.startId) % 10 == 0) {
      LOG_DBG("MESH", "Recalc: %u/%u msgs, %u ms", gid - _meta.startId + 1, _meta.endId - _meta.startId + 1,
              millis() - t0);
    }
  }

  // Proportionally scale scroll position
  if (_meta.totalPx > 0) {
    _meta.positionPx = static_cast<uint16_t>((static_cast<uint32_t>(_meta.positionPx) * newTotalPx) / _meta.totalPx);
  }
  _meta.totalPx = newTotalPx;
  _meta.fontId = _bodyFontId;

  if (isChannel) {
    store.saveChannelMeta(channelIdx, _meta);
  } else {
    store.saveDirectMeta(contactPubkey, _meta);
  }

  LOG_DBG("MESH", "Heights rebuilt: %u msgs, %u ms, totalPx=%u fontId=%d (old=%d)", _meta.count, millis() - t0,
          _meta.totalPx, _bodyFontId, oldFontId);
}
