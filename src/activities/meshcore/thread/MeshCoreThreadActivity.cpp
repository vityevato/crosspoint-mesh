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
#include "../utils/MeshCoreDisplayUtils.h"
#include "../utils/MeshCoreHeapLog.h"
#include "../utils/MeshCoreMessageHeight.h"
#include "../utils/MeshCoreShareUrl.h"
#include "CrossPointSettings.h"
#include "FontCacheManager.h"
#include "Memory.h"
#include "MeshCoreMessageRenderer.h"
#include "ThreadMenuRenderer.h"
#include "ThreadMessenger.h"
#include "ThreadScroller.h"
#include "activities/reader/QrDisplayActivity.h"
#include "activities/util/TextEntryHelpers.h"
#include "components/UITheme.h"

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
  _isFavourite = (contact.flags & MeshCoreContact::FLAG_FAVOURITE) != 0;
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

void MeshCoreThreadActivity::onChannelHeardUpdate(uint8_t chIdx, uint8_t heardCount) {
  // The Hub already wrote the new heard count (pathLength) to the store.
  // endId/startId do not change on a metadata-only update, so
  // _loopDetectNewMessages() would not repaint — reload and repaint here.
  ConvMeta currentMeta;
  if (store.getChannelMeta(channelIdx, currentMeta)) {
    _meta = currentMeta;
    loadMessages(_meta.positionId > 0 ? _meta.positionId : _meta.startId, /*up=*/false);
  }
  requestUpdate();
  LOG_INF("MESH", "Thread channel heard: ch=%d count=%d", (int)chIdx, (int)heardCount);
}

void MeshCoreThreadActivity::onEnter() {
  Activity::onEnter();
  MESHCORE_LOG_HEAP("Thread onEnter:start");

  // Register with Hub for delivery callbacks (Hub writes to store + forwards here)
  if (_hub) _hub->setActiveThread(this);

  // Explicitly reset to Messages tab on every entry, landing focus directly
  // in the conversation (not on the tab bar).
  currentTab = Tab::MESSAGES;
  selectedIndex = 1;

  _toast.setClock(&millis);
  _toast.setSubtitleProvider(provideSubtitle, this);

  _dcPopup.setClock(&millis);
  _dcPopup.arm(client.getState() == BleConnectionState::CONNECTED);

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
  // shows ready-to-use pages. metaFontId mismatches cover both the
  // reader-font toggle and the one-time v2→v3 cache migration (meta lines
  // are now measured with the system font's line height).
  if (_meta.fontId != _bodyFontId || _meta.metaFontId != meshcore::MESHCORE_META_FONT_ID) {
    LOG_DBG("MESH", "Thread onEnter: font mismatch (meta=%d/%d current=%d/%d) — queuing rebuild", _meta.fontId,
            _meta.metaFontId, _bodyFontId, meshcore::MESHCORE_META_FONT_ID);
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
      if (_accHeight > static_cast<uint32_t>(_contentAreaHeight)) {
        if (i == 0) {
          // Single message taller than viewport: keep full height for
          // correct scrollbar maths (positionPx += real height).
          _lastVisibleId = _visibleMsgs[0].id;
        } else {
          _accHeight -= _visibleMsgs[i].heightPx;
          _lastVisibleId = _visibleMsgs[i - 1].id;
        }
        break;
      }
    }
    return true;
  }
  return false;
}

int MeshCoreThreadActivity::contentHeight() const {
  return meshCoreThreadContentHeight(renderer, UITheme::getInstance().getMetrics());
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

void MeshCoreThreadActivity::shareContactQr() {
  // Pull the full contact record from the store to get the node name and type.
  MeshCoreContact foundContact;
  const bool haveContact = store.findContactByPubkey(contactPubkey, foundContact);

  MeshNodeType nodeType = MeshNodeType::COMPANION;
  const char* name = threadName;  // already resolved (contact name or "Unknown")
  if (haveContact) {
    nodeType = foundContact.type;
    if (foundContact.name[0] != '\0') name = foundContact.name;
  }

  char url[384] = {};
  if (meshcore::buildMeshCoreContactShareUrl(name, contactPubkey, nodeType, url, sizeof(url)) == 0) {
    _toast.show(tr(STR_MESHCORE_SHARE_FAILED), 3000);
    requestUpdate();
    return;
  }
  LOG_DBG("MESH", "Thread share QR URL: %s", url);
  startActivityForResult(
      std::make_unique<QrDisplayActivity>(renderer, mappedInput, std::string(url), tr(STR_MESHCORE_SHARE_CONTACT)),
      [this](const ActivityResult&) { requestUpdate(); });
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

  // Disconnect popup is modal: consume all input and return to the hub
  // (finish()) on Back or after the auto-return timeout.
  if (_dcPopup.isActive()) {
    if (_dcPopup.handleInput(mappedInput)) {
      finish();
    }
    return;
  }

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
    const bool success = client.getLastCommandResult();
    if (_pendingOp == PendingOp::DELETING_CONTACT) {
      completeUnlistOp(success);
    } else {
      completeFavouriteOp(success);
    }
  }
  if (_pendingOp != PendingOp::IDLE && (millis() - _pendingStartMs) > 10000) {
    LOG_ERR("MESH", "Async BLE timeout (no response after 10 s)");
    if (_pendingOp == PendingOp::DELETING_CONTACT) {
      completeUnlistOp(false);
    } else {
      completeFavouriteOp(false);
    }
  }

  // Unexpected disconnect while this thread is on screen: pop up a notice
  // and return to the hub, where the endless auto-reconnect takes over.
  // Note: completeUnlistOp() above may already have finished the activity,
  // so guard the popup activation accordingly.
  if (_dcPopup.update(client)) {
    requestUpdate();
  }
}

void MeshCoreThreadActivity::_loopDetectNewMessages() {
  ConvMeta currentMeta;
  bool hasMeta =
      isChannel ? store.getChannelMeta(channelIdx, currentMeta) : store.getDirectMeta(contactPubkey, currentMeta);
  if (!hasMeta) return;

  // Compare endId (monotonically increasing), NOT count: in a conversation at
  // MAX_MSGS_PER_THREAD capacity, appending drops the oldest message first so
  // count stays constant and a count-based comparison would miss new messages.
  if (currentMeta.endId > _meta.endId) {
    // New messages arrived
    bool wasAtEnd =
        (_meta.positionPx + static_cast<uint32_t>(_contentAreaHeight) >= _meta.totalPx || _meta.totalPx == 0);
    LOG_DBG("MESH", "New msgs detected: old.endId=%u new.endId=%u wasAtEnd=%d", _meta.endId, currentMeta.endId,
            wasAtEnd);
    _meta = currentMeta;
    if (wasAtEnd) {
      loadMessages(_meta.endId, /*up=*/true);
      _meta.positionPx = (_meta.totalPx > static_cast<uint32_t>(_contentAreaHeight))
                             ? _meta.totalPx - static_cast<uint32_t>(_contentAreaHeight)
                             : 0;
      _meta.positionId = (_visibleCount > 0) ? _visibleMsgs[0].id : _meta.endId;
      // The user is at the end of the conversation and will see the new
      // message — it must not be counted as unread.
      if (_hub) {
        if (isChannel) {
          _hub->markChannelRead(channelIdx);
        } else {
          _hub->markContactRead(contactPubkey);
        }
      }
    } else {
      loadMessages(_meta.positionId > 0 ? _meta.positionId : _meta.startId, /*up=*/false);
    }
    requestUpdate();
  } else if (currentMeta.startId > _meta.startId) {
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
    int actionCount = isChannel ? 2 : 6;
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
      // Channel menu: 0=Scroll to End, 1=Clear
      switch (itemIdx) {
        case 0:  // Scroll to End
          scrollToEnd();
          return true;
        case 1:  // Clear Conversation
          _confirmAction = ConfirmAction::CLEAR_CONVERSATION;
          requestUpdate();
          return true;
        default:
          break;
      }
    } else {
      // DM menu: 0=Toggle Favourite, 1=Reset Path, 2=Scroll to End, 3=Clear, 4=Share QR, 5=Unlist
      bool connected = (client.getState() == BleConnectionState::CONNECTED);
      switch (itemIdx) {
        case 0: {  // Toggle Favourite (async — waits for companion PKT_OK)
          if (!connected) {
            _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
            requestUpdate();
            break;
          }
          // Compute the target from the hub's RAM state (source of truth —
          // the disk copy lags behind until the hub flushes), never XOR a
          // possibly-stale flags byte read from the store.
          const bool targetFavourite = !contactIsFavourite();
          MeshCoreContact found = {};
          if (!store.findContactByPubkey(contactPubkey, found)) {
            _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
            requestUpdate();
            break;
          }
          found.flags = (found.flags & ~MeshCoreContact::FLAG_FAVOURITE) |
                        (targetFavourite ? MeshCoreContact::FLAG_FAVOURITE : 0);
          if (!client.addUpdateContact(found)) {
            LOG_ERR("MESH", "Failed to queue favourite update");
            _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
            requestUpdate();
            break;
          }
          _pendingOp = PendingOp::SETTING_FAVOURITE;
          _pendingStartMs = millis();
          _pendingFavouriteTarget = targetFavourite;
          _toast.show(tr(STR_MESHCORE_SAVING), 0);  // persistent until the node replies
          requestUpdate();
          return true;
        }
        case 1: {  // Reset Path
          if (!connected) {
            _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
            requestUpdate();
            break;
          }
          // Load the contact from the store to get current pathLength
          MeshCoreContact found = {};
          const bool haveContact = store.findContactByPubkey(contactPubkey, found);
          bool hasPath = haveContact && (found.pathLength != 0xFF);
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
        case 2:  // Scroll to End
          scrollToEnd();
          return true;
        case 3:  // Clear Conversation
          _confirmAction = ConfirmAction::CLEAR_CONVERSATION;
          requestUpdate();
          return true;
        case 4:  // Share Contact (QR)
          shareContactQr();
          return true;
        case 5: {  // Unlist Contact (async, waits for BLE)
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

  // Long-press (hold) on NavNext/NavPrevious cycles the tabs, like the
  // MeshCore hub (Settings-style). Continuous callbacks fire only on held
  // buttons, so they never race the short-press navigation below.
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
      if (selectedIndex == 0) {
        selectedIndex = 1;
        requestUpdate();
      } else {
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
      if (selectedIndex == 0) {
        selectedIndex = 1;
        requestUpdate();
      } else {
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
    _bodyFontId = settings->useReaderFont ? SETTINGS.getReaderFontId() : meshcore::MESHCORE_META_FONT_ID;
  } else {
    _bodyFontId = meshcore::MESHCORE_META_FONT_ID;  // fallback
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
      int count = isChannel ? 2 : 6;  // Channel: 2 actions; DM: 6 (Favourite, Reset Path, ..., Unlist)
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

void MeshCoreThreadActivity::notifyDisconnect(bool sendFailed) {
  _dcPopup.show(sendFailed ? tr(STR_MESHCORE_MSG_NOT_SENT) : tr(STR_MESHCORE_CONNECTION_LOST));
  requestUpdate();
}

// ── Async unlist completion handler ──
// Called from loop() when the BLE command completes or times out.
// Mirrors Discovery's completeContactSave() exactly — also updates the
// saved contact list in the store so the Hub picks up the change.

void MeshCoreThreadActivity::completeUnlistOp(bool success) {
  _pendingOp = PendingOp::IDLE;
  if (success) {
    LOG_DBG("MESH", "Contact unlist succeeded");

    // Remove the contact from the persisted list so the Hub picks up the change.
    if (store.removeContactByPubkey(contactPubkey)) {
      LOG_INF("MESH", "Removed contact from saved list");
    }

    setResult(ActivityResult(MeshCoreUnlistResult{}));
    finish();
  } else {
    LOG_ERR("MESH", "Contact unlist failed");
    _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
    requestUpdate();
  }
}

// ── Async favourite toggle completion handler ──
// Called from loop() when the companion ACKs (PKT_OK) or the op times out.
// On success the flag is committed in place and the Thread stays open on the
// same conversation and menu (the favourite toggle must not bounce the user
// back to the hub). The Hub reconciles the in-RAM contact, persists, re-sorts
// and re-selects the row, so the Contacts list is correct the moment the user
// returns. Local state is committed ONLY after the node ACKed, so the menu
// label never disagrees with the companion's stored state.

void MeshCoreThreadActivity::completeFavouriteOp(bool success) {
  _pendingOp = PendingOp::IDLE;
  if (success) {
    _isFavourite = _pendingFavouriteTarget;
    if (_hub) _hub->handleContactFavouriteResult(contactPubkey, _pendingFavouriteTarget);
    _toast.show(_pendingFavouriteTarget ? tr(STR_MESHCORE_FAVOURITE_ADDED) : tr(STR_MESHCORE_FAVOURITE_REMOVED), 3000);
    requestUpdate();
  } else {
    LOG_ERR("MESH", "Favourite update failed");
    _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
    requestUpdate();
  }
}

// --- Message sending ---

void MeshCoreThreadActivity::sendMessage() {
  ThreadMessenger messenger{client, store, isChannel, channelIdx, contactPubkey, threadName, _bodyFontId};
  char title[96];
  snprintf(title, sizeof(title), tr(STR_MESHCORE_SEND_TO), threadName);
  startActivityForResult(
      textentry::makeEntryActivity(renderer, mappedInput, title, "", MESHCORE_SEND_CHAR_LIMIT, InputType::Text),
      [this, messenger](const ActivityResult& result) mutable { messenger.onSendComplete(*this, result); });
}

// --- Rendering ---

void MeshCoreThreadActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (_renderFontRebuildPopup()) return;
  if (_dcPopup.isActive()) {
    char headerSubtitle[64];
    _toast.getSubtitle(headerSubtitle, sizeof(headerSubtitle));
    _dcPopup.render(renderer, mappedInput, threadName, headerSubtitle);
    return;
  }
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
    int actionCount = isChannel ? 2 : 6;
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
  uint32_t newTotalPx = 0;
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

    msg.heightPx = measureMeshCoreMessageHeight(renderer, _bodyFontId, _contentAreaWidth, isChannel, msg, tmetrics,
                                                _contentAreaHeight);

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
    _meta.positionPx = static_cast<uint32_t>((static_cast<uint64_t>(_meta.positionPx) * newTotalPx) / _meta.totalPx);
  }
  _meta.totalPx = newTotalPx;
  _meta.fontId = _bodyFontId;
  _meta.metaFontId = meshcore::MESHCORE_META_FONT_ID;

  if (isChannel) {
    store.saveChannelMeta(channelIdx, _meta);
  } else {
    store.saveDirectMeta(contactPubkey, _meta);
  }

  LOG_DBG("MESH", "Heights rebuilt: %u msgs, %u ms, totalPx=%u fontId=%d (old=%d)", _meta.count, millis() - t0,
          _meta.totalPx, _bodyFontId, oldFontId);
}
