#include "MeshCoreThreadActivity.h"

#include <I18n.h>
#include <Logging.h>

#ifdef SIMULATOR
#include <MeshCoreMockHotkeys.h>
#endif

#include <cstring>
#include <string>

#include "../MeshCoreHubActivity.h"
#include "../MeshCoreSubtitle.h"
#include "../utils/MeshCoreHeapLog.h"
#include "../utils/MeshCoreMessageHeight.h"
#include "CrossPointSettings.h"
#include "FontCacheManager.h"
#include "Memory.h"
#include "MeshCoreMessageRenderer.h"
#include "ThreadMenuRenderer.h"
#include "ThreadMessenger.h"
#include "ThreadScroller.h"
#include "activities/util/T4EntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

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
  if (_loopInputBack()) return;
  if (_loopInputConfirm()) return;
  _loopInputNav();
}

bool MeshCoreThreadActivity::_loopInputBack() {
  if (!mappedInput.wasPressed(MappedInputManager::Button::Back)) return false;
  if (selectedIndex > 0) {
    selectedIndex = 0;
    requestUpdate();
  } else {
    finish();
  }
  return true;
}

bool MeshCoreThreadActivity::_loopInputConfirm() {
  if (!mappedInput.wasPressed(MappedInputManager::Button::Confirm)) return false;

  // Block all Confirm actions while a BLE operation is pending
  if (_pendingOp != PendingOp::IDLE) return true;

  if (selectedIndex == 0) {
    // Tab bar — cycle to next tab
    int tab = static_cast<int>(currentTab);
    tab = (tab < static_cast<int>(Tab::TAB_COUNT) - 1) ? tab + 1 : 0;
    switchTab(static_cast<Tab>(tab));
    return true;
  }

  int itemIdx = selectedIndex - 1;
  if (currentTab == Tab::MESSAGES) {
    // Messages tab: Confirm opens keyboard to send a message
    sendMessage();
    return true;
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
      return true;
    }

    if (isChannel) {
      // Channel menu: 0=Scroll to End, 1=Clear, 2=Delete Channel
      bool connected = (client.getState() == BleConnectionState::CONNECTED);
      switch (itemIdx) {
        case 0:  // Scroll to End
          scrollToEnd();
          return true;
        case 1:  // Clear Conversation
          _confirmAction = ConfirmAction::CLEAR_CONVERSATION;
          requestUpdate();
          return true;
        case 2: {  // Delete Channel (async, waits for BLE)
          if (!connected) {
            _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
            requestUpdate();
            break;
          }
          _confirmAction = ConfirmAction::DELETE_CHANNEL;
          requestUpdate();
          return true;
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
          return true;
        }
        case 1:  // Scroll to End
          scrollToEnd();
          return true;
        case 2:  // Clear Conversation
          _confirmAction = ConfirmAction::CLEAR_CONVERSATION;
          requestUpdate();
          return true;
        case 3: {  // Unlist Contact (async, waits for BLE)
          if (!connected) {
            _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
            requestUpdate();
            break;
          }
          _confirmAction = ConfirmAction::REMOVE_CONTACT;
          requestUpdate();
          return true;
        }
        default:
          break;
      }
    }
  }
  return true;
}

void MeshCoreThreadActivity::_loopInputNav() {
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
  ThreadMessenger messenger{client, store, isChannel, channelIdx, contactPubkey, threadName, _bodyFontId};
  startActivityForResult(
      std::make_unique<T4EntryActivity>(renderer, mappedInput, tr(STR_MESHCORE_SEND), "", MESHCORE_SEND_CHAR_LIMIT,
                                        InputType::Text),
      [this, messenger](const ActivityResult& result) mutable { messenger.onSendComplete(*this, result); });
}

// --- Rendering ---

void MeshCoreThreadActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (_renderFontRebuildPopup()) return;
  if (_renderConfirmPopup()) return;
  _renderNormal();
}

bool MeshCoreThreadActivity::_renderFontRebuildPopup() { return ThreadMenuRenderer::renderFontRebuildPopup(*this); }

bool MeshCoreThreadActivity::_renderConfirmPopup() { return ThreadMenuRenderer::renderConfirmPopup(*this); }

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

void MeshCoreThreadActivity::renderMenu(const Rect& contentRect) { ThreadMenuRenderer::renderMenu(*this, contentRect); }

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
