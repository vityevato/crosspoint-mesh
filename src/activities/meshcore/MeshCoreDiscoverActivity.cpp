#include "MeshCoreDiscoverActivity.h"

#include <I18n.h>
#include <Logging.h>

#ifdef SIMULATOR
#include <MeshCoreMockHotkeys.h>
#endif

#include <cstring>
#include <string>

#include "MeshCoreSubtitle.h"
#include "components/UITheme.h"
#include "utils/MeshCoreDisplayUtils.h"
#include "utils/MeshCoreHeapLog.h"
#include "utils/MeshCoreTimeUtils.h"

MeshCoreDiscoverActivity::MeshCoreDiscoverActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   MeshCoreClient& client, MeshCoreMessageStore& store,
                                                   MeshCoreContact* discoveredNodes, uint8_t& discoveredNodeCount,
                                                   MeshCoreContact* savedContacts, uint8_t& savedContactCount)
    : Activity("MeshCoreDiscover", renderer, mappedInput),
      client(client),
      store(store),
      discoveredNodes(discoveredNodes),
      discoveredNodeCount(discoveredNodeCount),
      savedContacts(savedContacts),
      savedContactCount(savedContactCount) {}

void MeshCoreDiscoverActivity::provideSubtitle(const void* ctx, char* buf, size_t bufSize) {
  formatMeshCoreSubtitle(*static_cast<const MeshCoreClient*>(ctx), buf, bufSize);
}

void MeshCoreDiscoverActivity::onEnter() {
  Activity::onEnter();
  MESHCORE_LOG_HEAP("Discover onEnter");
  _toast.setClock(&millis);
  _toast.setSubtitleProvider(provideSubtitle, &client);
  requestUpdate();
}

void MeshCoreDiscoverActivity::onExit() {
  MESHCORE_LOG_HEAP("Discover onExit");
  Activity::onExit();
}

void MeshCoreDiscoverActivity::loop() {
  client.poll();

  if (_toast.poll()) requestUpdate();

  // Async BLE command state machine: check if the command completed.
  if (_pendingOp != PendingOp::IDLE && !client.isCommandPending()) {
    completeContactSave(client.getLastCommandResult());
  }

  // Secondary timeout guard: if command somehow got stuck (cmdPending
  // never cleared), abandon after 10 s to avoid indefinite toast.
  if (_pendingOp != PendingOp::IDLE && (millis() - _pendingStartMs) > 10000) {
    LOG_ERR("MESH", "Contact BLE timeout (no response after 10 s)");
    completeContactSave(false);
  }

#ifdef SIMULATOR
  if (handleMockKey("Discover", client.getBleClient())) {
    requestUpdate();
    return;
  }
  pollMock(client.getBleClient(), millis());
#endif

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (discoveredNodeCount > 0) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && _pendingOp == PendingOp::IDLE) {
      if (isAlreadySaved(discoveredNodes[selectedIndex])) {
        removeSelectedFromContacts();
      } else {
        addSelectedToContacts();
      }
      return;
    }

    buttonNavigator.onNextRelease([this] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, discoveredNodeCount);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, discoveredNodeCount);
      requestUpdate();
    });
  }
}

void MeshCoreDiscoverActivity::addSelectedToContacts() {
  if (selectedIndex >= discoveredNodeCount) return;

  const auto& node = discoveredNodes[selectedIndex];
  if (isAlreadySaved(node)) return;

  if (savedContactCount >= 20) {
    LOG_ERR("MESH", "Contact list full");
    _toast.show(tr(STR_MESHCORE_CONTACT_LIST_FULL), 3000);
    requestUpdate();
    return;
  }

  // Fire async BLE command. UI stays responsive; loop() polls for result.
  if (!client.addUpdateContact(node)) {
    LOG_ERR("MESH", "Failed to queue contact sync: %s", node.name);
    _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
    requestUpdate();
    return;
  }

  _pendingContact = node;
  _pendingOp = PendingOp::SAVING;
  _pendingStartMs = millis();
  _toast.show(tr(STR_MESHCORE_SAVING), 0);  // persistent until result arrives
  requestUpdate();
}

void MeshCoreDiscoverActivity::removeSelectedFromContacts() {
  if (selectedIndex >= discoveredNodeCount) return;

  const auto& node = discoveredNodes[selectedIndex];
  // Find the index in savedContacts
  for (uint8_t i = 0; i < savedContactCount; ++i) {
    if (memcmp(savedContacts[i].publicKey, node.publicKey, 32) == 0) {
      if (!client.removeContact(node.publicKey)) {
        LOG_ERR("MESH", "Failed to queue contact delete: %s", node.name);
        _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
        requestUpdate();
        return;
      }

      _pendingContact = node;
      _pendingOp = PendingOp::DELETING;
      _pendingStartMs = millis();
      _pendingDeleteIndex = i;
      _toast.show(tr(STR_MESHCORE_REMOVING), 0);  // persistent until result
      LOG_INF("MESH", "Deleting contact from companion: %s", node.name);
      requestUpdate();
      return;
    }
  }
}

void MeshCoreDiscoverActivity::completeContactSave(bool success) {
  PendingOp op = _pendingOp;
  _pendingOp = PendingOp::IDLE;

  if (op == PendingOp::SAVING) {
    if (success) {
      savedContacts[savedContactCount] = _pendingContact;
      savedContacts[savedContactCount].isSaved = true;
      savedContactCount++;
      store.saveContacts(savedContacts, savedContactCount);
      _toast.show(tr(STR_MESHCORE_CONTACT_ADDED), 3000);
      LOG_INF("MESH", "Added contact: %s", _pendingContact.name);
    } else {
      _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
    }
  } else if (op == PendingOp::DELETING) {
    if (success) {
      // Shift remaining contacts down
      for (uint8_t j = _pendingDeleteIndex; j < savedContactCount - 1; ++j) {
        savedContacts[j] = savedContacts[j + 1];
      }
      savedContactCount--;
      store.saveContacts(savedContacts, savedContactCount);
      _toast.show(tr(STR_MESHCORE_CONTACT_REMOVED), 3000);
      LOG_INF("MESH", "Removed contact: %s", _pendingContact.name);
    } else {
      _toast.show(tr(STR_MESHCORE_SYNC_FAILED), 3000);
    }
  }

  requestUpdate();
}

bool MeshCoreDiscoverActivity::isAlreadySaved(const MeshCoreContact& node) const {
  for (uint8_t i = 0; i < savedContactCount; ++i) {
    if (memcmp(savedContacts[i].publicKey, node.publicKey, 32) == 0) return true;
  }
  return false;
}

bool MeshCoreDiscoverActivity::isSavingInProgress(const MeshCoreContact& node) const {
  return _pendingOp != PendingOp::IDLE && memcmp(_pendingContact.publicKey, node.publicKey, 32) == 0;
}

void MeshCoreDiscoverActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  char headerSubtitle[64];
  _toast.getSubtitle(headerSubtitle, sizeof(headerSubtitle));
  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), tr(STR_MESHCORE_DISCOVERED),
                 headerSubtitle);

  int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.topPadding - metrics.bottomSubtitleHeight;
  Rect contentRect(0, contentTop, pageWidth, contentHeight);

  if (discoveredNodeCount == 0) {
    GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_DEVICES));
  } else {
    const auto* nodes = discoveredNodes;
    const auto* saved = savedContacts;
    const auto savedCount = savedContactCount;

    GUI.drawList(
        renderer, contentRect, discoveredNodeCount, selectedIndex,
        [nodes](int index) {
          const char* n = nodes[index].name;
          if (n[0] == '\0') n = tr(STR_MESHCORE_UNKNOWN);
          return std::string(n);
        },
        [nodes](int index) {
          char buf[48];
          char keyLabel[MeshCoreContact::PUBLIC_KEY_DISPLAY_LEN];
          char ts[24] = "---";
          nodes[index].getPublicKeyLabel(keyLabel);
          if (nodes[index].lastSeen != 0) {
            formatMeshCoreTimestamp(nodes[index].lastSeen, ts, sizeof(ts));
          }
          char hopBuf[12];
          meshcore::formatMeshCoreHopCount(nodes[index].pathLength, hopBuf, sizeof(hopBuf));
          snprintf(buf, sizeof(buf), "%s %s %s %s %s", keyLabel, meshcore::DotSeparator, ts, meshcore::DotSeparator,
                   hopBuf);
          return std::string(buf);
        },
        nullptr, nullptr, false,
        [this, nodes, saved, savedCount](int index) {
          // Dim already-saved contacts
          for (uint8_t i = 0; i < savedCount; ++i) {
            if (memcmp(saved[i].publicKey, nodes[index].publicKey, 32) == 0) return true;
          }
          // Dim contact currently being saved (immediate feedback)
          if (isSavingInProgress(nodes[index])) return true;
          return false;
        });
  }

  const char* btn2 = "";
  if (discoveredNodeCount > 0 && _pendingOp == PendingOp::IDLE) {
    if (isAlreadySaved(discoveredNodes[selectedIndex])) {
      btn2 = tr(STR_MESHCORE_UNLIST);
    } else {
      btn2 = tr(STR_MESHCORE_ADD);
    }
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), btn2, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
