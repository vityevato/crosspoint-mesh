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
#include "Memory.h"
#include "MeshCoreHubActivity.h"
#include "MeshCoreMessageRenderer.h"
#include "MeshCoreSubtitle.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "utils/MeshCoreHeapLog.h"
#include "utils/MeshCoreMessageHeight.h"

/// Scroll state machine extracted from MeshCoreThreadActivity.
/// Holds references to all mutable scroll-related fields so the full
/// scroll logic (including savePosition) lives in one place.
/// The Activity wrapper methods only add requestUpdate() calls.
struct ThreadScroller {
  ConvMeta& meta;
  MeshCoreMessage* const msgs;
  uint8_t& count;
  MeshCoreMessage& filler;
  uint16_t& accH;
  uint32_t& firstId;
  uint32_t& lastId;
  const int ch;

  MeshCoreMessageStore& store;
  const bool isCh;
  const uint8_t chIdx;
  const uint8_t* const pubkey;

  bool loadBatch(uint32_t startId, bool up) {
    uint8_t loaded = 0;
    if (isCh) {
      store.loadChannelMessages(chIdx, startId, static_cast<uint16_t>(ch), up, msgs, loaded, filler);
    } else {
      store.loadDirectMessages(pubkey, startId, static_cast<uint16_t>(ch), up, msgs, loaded, filler);
    }
    count = loaded;
    if (loaded > 0) {
      firstId = msgs[0].id;
      lastId = msgs[loaded - 1].id;
      accH = 0;
      for (uint8_t i = 0; i < loaded; ++i) {
        accH += msgs[i].heightPx;
        if (accH > static_cast<uint16_t>(ch)) {
          accH -= msgs[i].heightPx;
          lastId = msgs[i - 1].id;
          break;
        }
      }
      return true;
    }
    return false;
  }

  void savePos() {
    if (isCh) {
      store.saveChannelMeta(chIdx, meta);
    } else {
      store.saveDirectMeta(pubkey, meta);
    }
  }

  void scrollDownPage() {
    LOG_DBG("MESH", "scrollDownPage: posPx=%u accH=%u totalPx=%u ch=%d lastId=%u endId=%u", meta.positionPx, accH,
            meta.totalPx, ch, lastId, meta.endId);
    if (meta.totalPx <= static_cast<uint16_t>(ch)) {
      LOG_DBG("MESH", "scrollDownPage: skip — fits in one page");
      return;
    }
    if (lastId >= meta.endId) {
      LOG_DBG("MESH", "scrollDownPage: skip — already at end");
      return;
    }

    meta.positionId = lastId + 1;
    meta.positionPx += accH;

    LOG_DBG("MESH", "scrollDownPage: advancing (posPx=%u posId=%u)", meta.positionPx, meta.positionId);
    loadBatch(meta.positionId > 0 ? meta.positionId : meta.startId, /*up=*/false);
    savePos();
  }

  void scrollUpPage() {
    LOG_DBG("MESH", "scrollUpPage: posPx=%u accH=%u posId=%u startId=%u", meta.positionPx, accH, meta.positionId,
            meta.startId);
    if (meta.positionPx == 0) {
      LOG_DBG("MESH", "scrollUpPage: skip — positionPx==0");
      return;
    }

    uint32_t newStartId = meta.positionId > 0 ? meta.positionId - 1 : meta.startId;
    loadBatch(newStartId, /*up=*/true);

    if (count == 0) {
      LOG_DBG("MESH", "scrollUpPage: no messages loaded from newStartId=%u", newStartId);
      return;
    }

    if (firstId <= meta.startId) {
      LOG_DBG("MESH", "scrollUpPage: hit top — reload from startId=%u down", meta.startId);
      meta.positionPx = 0;
      meta.positionId = meta.startId;
      loadBatch(meta.startId, /*up=*/false);
      savePos();
      return;
    }

    LOG_DBG("MESH", "scrollUpPage: loaded %d msgs [%u..%u] (posPx: %u - %u)", count, firstId, lastId, meta.positionPx,
            accH);

    meta.positionId = firstId;
    meta.positionPx = (meta.positionPx >= accH) ? meta.positionPx - accH : 0;

    LOG_DBG("MESH", "scrollUpPage: final posPx=%u posId=%u", meta.positionPx, meta.positionId);
    savePos();
  }

  void scrollToEnd() {
    if (meta.count == 0) return;
    loadBatch(meta.endId, /*up=*/true);
    if (meta.totalPx > static_cast<uint16_t>(ch)) {
      meta.positionPx = meta.totalPx - static_cast<uint16_t>(ch);
    } else {
      meta.positionPx = 0;
    }
    meta.positionId = (count > 0) ? msgs[0].id : meta.endId;
    savePos();
  }

  void scrollDownByMessage() {
    LOG_DBG("MESH", "scrollDownByMsg: posPx=%u accH=%u lastId=%u endId=%u", meta.positionPx, accH, lastId, meta.endId);
    if (meta.count == 0) return;
    if (lastId >= meta.endId) return;

    meta.positionId = lastId + 1;
    meta.positionPx += accH;

    uint32_t newStartId = meta.positionId > 0 ? meta.positionId : meta.startId;
    loadBatch(newStartId, /*up=*/true);

    if (count == 0) {
      LOG_DBG("MESH", "scrollDownByMsg: no messages loaded from newStartId=%u", newStartId);
      return;
    }

    meta.positionId = firstId;
    meta.positionPx = (meta.positionPx >= accH) ? meta.positionPx - accH + msgs[count - 1].heightPx : 0;
    LOG_DBG("MESH", "scrollDownByMsg: final posPx=%u posId=%u", meta.positionPx, meta.positionId);

    savePos();

    if (meta.positionPx > meta.totalPx - static_cast<uint16_t>(ch)) {
      meta.positionPx = (meta.totalPx > static_cast<uint16_t>(ch)) ? meta.totalPx - static_cast<uint16_t>(ch) : 0;
    }

    loadBatch(meta.positionId > 0 ? meta.positionId : meta.startId, /*up=*/false);
    savePos();
  }

  void scrollUpByMessage() {
    LOG_DBG("MESH", "scrollUpByMsg: posPx=%u posId=%u startId=%u", meta.positionPx, meta.positionId, meta.startId);
    if (meta.positionPx == 0 && meta.positionId <= meta.startId) return;

    MeshCoreMessage prevMsg;
    uint8_t prevLoaded = 0;
    uint32_t searchId = meta.positionId > 0 ? meta.positionId - 1 : 0;
    if (isCh) {
      store.loadChannelMessages(chIdx, searchId, static_cast<uint8_t>(1), true, &prevMsg, prevLoaded);
    } else {
      store.loadDirectMessages(pubkey, searchId, static_cast<uint8_t>(1), true, &prevMsg, prevLoaded);
    }
    if (prevLoaded == 0) return;

    meta.positionId = prevMsg.id;
    loadBatch(meta.positionId, /*up=*/false);

    if (count == 0) return;

    meta.positionPx = (meta.positionPx >= prevMsg.heightPx) ? meta.positionPx - prevMsg.heightPx : 0;
    meta.positionId = firstId;

    savePos();
  }
};

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

  resolveBodyFont();
  const auto& metrics = UITheme::getInstance().getMetrics();
  _contentAreaWidth = renderer.getScreenWidth() - 2 * metrics.contentSidePadding;
  _contentAreaHeight = contentHeight();

  // Create scroll state machine (all fields are now set)
  _scroller = new (std::nothrow)
      ThreadScroller{_meta,          _visibleMsgs,       _visibleCount, _fillerMsg, _accHeight, _firstVisibleId,
                     _lastVisibleId, _contentAreaHeight, store,         isChannel,  channelIdx, contactPubkey};
  if (!_scroller) {
    LOG_ERR("MESH", "OOM: ThreadScroller");
    return;
  }

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
  _menuSettings.reset();
  delete _scroller;
  _scroller = nullptr;
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
  _scroller->scrollDownPage();
  requestUpdate();
}

void MeshCoreThreadActivity::scrollUpPage() {
  _scroller->scrollUpPage();
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
  _scroller->scrollToEnd();
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
  _scroller->scrollDownByMessage();
  requestUpdate();
}

void MeshCoreThreadActivity::scrollUpByMessage() {
  _scroller->scrollUpByMessage();
  requestUpdate();
}

void MeshCoreThreadActivity::loop() {
  client.poll();

  // Auto-clear expired toast messages
  if (_toast.poll()) {
    requestUpdate();
  }

  _loopBleStateMachine();

#ifdef SIMULATOR
  if (handleMockKey("Thread", client.getBleClient())) {
    requestUpdate();
    return;
  }
  pollMock(client.getBleClient(), millis());
#endif

  _loopDetectNewMessages();

  if (_loopConfirmPopup()) return;  // popup active — consumed all input

  _loopInput();
}

void MeshCoreThreadActivity::_loopBleStateMachine() {
  if (_pendingOp != PendingOp::IDLE && !client.isCommandPending()) {
    completeUnlistOp(client.getLastCommandResult());
  }
  if (_pendingOp != PendingOp::IDLE && (millis() - _pendingStartMs) > 10000) {
    LOG_ERR("MESH", "Unlist BLE timeout (no response after 10 s)");
    completeUnlistOp(false);
  }
}

void MeshCoreThreadActivity::_loopDetectNewMessages() {
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
}

bool MeshCoreThreadActivity::_loopConfirmPopup() {
  if (_confirmAction == ConfirmAction::NONE) return false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    _confirmAction = ConfirmAction::NONE;
    requestUpdate();
    return true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    auto action = _confirmAction;
    _confirmAction = ConfirmAction::NONE;
    switch (action) {
      case ConfirmAction::CLEAR_CONVERSATION:
        clearConversation();
        break;
      case ConfirmAction::REMOVE_CONTACT: {
        if (!client.removeContact(contactPubkey)) {
          LOG_ERR("MESH", "Failed to queue contact delete");
          _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
          requestUpdate();
          return true;
        }
        LOG_DBG("MESH", "Queued contact unlist — waiting for BLE response");
        _pendingOp = PendingOp::DELETING_CONTACT;
        _pendingStartMs = millis();
        _toast.show(tr(STR_MESHCORE_REMOVING), 0);
        selectedIndex = 0;
        break;
      }
      case ConfirmAction::DELETE_CHANNEL: {
        if (!client.deleteChannel(channelIdx)) {
          LOG_ERR("MESH", "Failed to queue channel delete");
          _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
          requestUpdate();
          return true;
        }
        LOG_DBG("MESH", "Queued channel %d delete — waiting for BLE response", channelIdx);
        _pendingOp = PendingOp::DELETING_CHANNEL;
        _pendingStartMs = millis();
        _toast.show(tr(STR_MESHCORE_REMOVING), 0);
        selectedIndex = 0;
        break;
      }
      default:
        break;
    }
    requestUpdate();
    return true;
  }

  return true;  // Consume all other input while popup is active
}

void MeshCoreThreadActivity::_loopInput() {
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
      // Action indices: 0..(actionCount-1) = menu actions, actionCount = settings toggle
      int actionCount = isChannel ? 3 : 4;
      if (itemIdx >= actionCount) {
        // Settings toggle
        if (_menuSettings) {
          _menuSettings->useReaderFont = !_menuSettings->useReaderFont;
          meshcore_settings::save(*_menuSettings);
          resolveBodyFont();
          _needsRebuild = true;
        }
        requestUpdate();
        return;
      }

      if (isChannel) {
        // Channel menu: 0=Scroll to End, 1=Clear, 2=Delete Channel
        bool connected = (client.getState() == BleConnectionState::CONNECTED);
        switch (itemIdx) {
          case 0:  // Scroll to End
            scrollToEnd();
            return;
          case 1:  // Clear Conversation
            _confirmAction = ConfirmAction::CLEAR_CONVERSATION;
            requestUpdate();
            return;
          case 2: {  // Delete Channel (async, waits for BLE)
            if (!connected) {
              _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
              requestUpdate();
              break;
            }
            _confirmAction = ConfirmAction::DELETE_CHANNEL;
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
            _confirmAction = ConfirmAction::CLEAR_CONVERSATION;
            requestUpdate();
            return;
          case 3: {  // Unlist Contact (async, waits for BLE)
            if (!connected) {
              _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
              requestUpdate();
              break;
            }
            _confirmAction = ConfirmAction::REMOVE_CONTACT;
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

void MeshCoreThreadActivity::resolveBodyFont() {
  auto settings = makeUniqueNoThrow<MeshCoreSettings>();
  if (settings && meshcore_settings::load(*settings)) {
    _bodyFontId = settings->useReaderFont ? SETTINGS.getReaderFontId() : SMALL_FONT_ID;
  } else {
    _bodyFontId = SMALL_FONT_ID;  // fallback
  }
}

void MeshCoreThreadActivity::switchTab(Tab tab) {
  // Free settings when leaving MENU
  if (currentTab == Tab::MENU && tab != Tab::MENU) {
    _menuSettings.reset();
  }
  // Load settings when entering MENU
  if (tab == Tab::MENU && !_menuSettings) {
    _menuSettings = makeUniqueNoThrow<MeshCoreSettings>();
    if (_menuSettings) {
      meshcore_settings::load(*_menuSettings);
    }
  }
  currentTab = tab;
  selectedIndex = 0;
  requestUpdate();
}

int MeshCoreThreadActivity::getListCountForCurrentTab() const {
  switch (currentTab) {
    case Tab::MESSAGES:
      return 0;  // Messages tab has no list navigation — uses page nav instead
    case Tab::MENU: {
      int count = isChannel ? 3 : 4;  // Channel: 3 actions; DM: 4 actions (+Reset Path)
      if (_menuSettings) count += 1;  // +1 for the settings toggle
      return count;
    }
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
                           msg.heightPx = measureMeshCoreMessageHeight(renderer, _bodyFontId, tcontentWidth, isChannel,
                                                                       msg, tmetrics);

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
  if (_renderFontRebuildPopup()) return;
  if (_renderConfirmPopup()) return;
  _renderNormal();
}

bool MeshCoreThreadActivity::_renderFontRebuildPopup() {
  if (!_needsRebuild) return false;
  _needsRebuild = false;
  renderer.clearScreen();
  GUI.drawPopup(renderer, tr(STR_MESHCORE_RECALC_LAYOUT));
  renderer.displayBuffer();
  _rebuildMessageHeights();
  loadMessages(_meta.positionId > 0 ? _meta.positionId : _meta.startId, /*up=*/false);
  requestUpdate();
  return true;
}

bool MeshCoreThreadActivity::_renderConfirmPopup() {
  if (_confirmAction == ConfirmAction::NONE) return false;

  const char* confirmMsg = "";
  const char* confirmLabel = "";
  switch (_confirmAction) {
    case ConfirmAction::CLEAR_CONVERSATION:
      confirmMsg = tr(STR_MESHCORE_CLEAR_CONFIRM);
      confirmLabel = tr(STR_MESHCORE_CLEAR_CONVERSATION);
      break;
    case ConfirmAction::REMOVE_CONTACT:
      confirmMsg = tr(STR_MESHCORE_REMOVE_CONTACT_CONFIRM);
      confirmLabel = tr(STR_MESHCORE_REMOVE_CONTACT);
      break;
    case ConfirmAction::DELETE_CHANNEL:
      confirmMsg = tr(STR_MESHCORE_DELETE_CHANNEL_CONFIRM);
      confirmLabel = tr(STR_MESHCORE_DELETE_CHANNEL);
      break;
    default:
      break;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  char headerSubtitle[64];
  _toast.getSubtitle(headerSubtitle, sizeof(headerSubtitle));
  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), threadName, headerSubtitle);
  GUI.drawPopup(renderer, confirmMsg);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  return true;
}

void MeshCoreThreadActivity::_renderNormal() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

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
        if (fcm) {
          MESHCORE_LOG_HEAP("Thread prewarm:before");
          auto scope = fcm->createPrewarmScope();
          drawVisibleMessages(renderer, contentRect, _bodyFontId, /*scanOnly=*/true);
          scope.endScanAndPrewarm();
          MESHCORE_LOG_HEAP("Thread prewarm:after");
          drawVisibleMessages(renderer, contentRect, _bodyFontId);
        } else {
          drawVisibleMessages(renderer, contentRect, _bodyFontId);
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
    int actionCount = isChannel ? 3 : 4;
    btn2 = (selectedIndex > 0 && selectedIndex - 1 >= actionCount) ? tr(STR_TOGGLE) : tr(STR_SELECT);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), btn2, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void MeshCoreThreadActivity::renderMenu(const Rect& contentRect) {
  bool connected = (client.getState() == BleConnectionState::CONNECTED);

  constexpr int kChannelActionCount = 3;
  constexpr int kDmActionCount = 4;
  int kActionCount = isChannel ? kChannelActionCount : kDmActionCount;
  bool hasSettings = _menuSettings != nullptr;

  const auto& m = UITheme::getInstance().getMetrics();
  const int sepGap = m.verticalSpacing;  // theme-aware gap above and below the separator

  // Menu item labels — table-driven
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

  // ── Action list ──
  int listSel = selectedIndex - 1;  // 0-based
  int actionSel = (listSel >= 0 && listSel < kActionCount) ? listSel : -1;

  Rect actionRect = contentRect;
  GUI.drawList(
      renderer, actionRect, kActionCount, actionSel,
      /*rowTitle*/
      [&](int index) -> std::string {
        if (index < 0 || index >= kActionCount) return {};
        return I18n::getInstance().get(titles[index]);
      },
      /*rowSubtitle*/ nullptr,
      /*rowIcon*/ nullptr,
      /*rowValue*/ nullptr,
      /*highlightValue*/ false,
      /*rowDimmed*/
      [this, connected](int index) -> bool {
        if (isChannel) {
          if (!connected) return (index == 2);
          return false;
        } else {
          if (index == 0) {
            if (!connected) return true;
            constexpr uint8_t kMaxDim = 20;
            MeshCoreContact contacts[kMaxDim] = {};
            uint8_t count = store.loadContacts(contacts, kMaxDim);
            for (uint8_t i = 0; i < count; ++i) {
              if (memcmp(contacts[i].publicKey, contactPubkey, 32) == 0) {
                return (contacts[i].pathLength == 0xFF);
              }
            }
            return true;
          }
          if (!connected) return (index == 3);
          return false;
        }
      });

  // ── Separator + settings ──
  if (!hasSettings) return;

  int sepY = contentRect.y + kActionCount * m.listRowHeight + sepGap;
  renderer.drawLine(contentRect.x + m.contentSidePadding, sepY,
                    contentRect.x + contentRect.width - m.contentSidePadding - 1, sepY, true);

  int settingSel = (listSel >= kActionCount) ? (listSel - kActionCount) : -1;
  int settingsTop = sepY + 1 + sepGap;  // +1 for the drawn line
  Rect settingsRect(contentRect.x, settingsTop, contentRect.width, contentRect.y + contentRect.height - settingsTop);

  GUI.drawList(
      renderer, settingsRect, 1, settingSel,
      /*rowTitle*/
      [](int) -> std::string { return I18n::getInstance().get(StrId::STR_MESHCORE_USE_READER_FONT); },
      /*rowSubtitle*/ nullptr,
      /*rowIcon*/ nullptr,
      /*rowValue*/
      [this](int) -> std::string { return _menuSettings->useReaderFont ? tr(STR_STATE_ON) : tr(STR_STATE_OFF); },
      /*highlightValue*/ true,
      /*rowDimmed*/ nullptr);
}

// --- Message rendering (reads from _visibleMsgs array) ---

void MeshCoreThreadActivity::drawVisibleMessages(const GfxRenderer& renderer, Rect rect, int bodyFontId,
                                                 bool scanOnly) {
  ThreadRenderCtx ctx(renderer, rect, bodyFontId);
  renderMessageBatch(renderer, rect, _visibleMsgs, _visibleCount, _fillerMsg, ctx, isChannel, _meta.totalPx,
                     _meta.positionPx, scanOnly);
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
