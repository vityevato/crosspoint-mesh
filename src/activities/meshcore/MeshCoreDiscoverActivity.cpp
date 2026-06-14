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
#include "fontIds.h"

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

void MeshCoreDiscoverActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void MeshCoreDiscoverActivity::onExit() { Activity::onExit(); }

void MeshCoreDiscoverActivity::loop() {
  client.poll();

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
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      addSelectedToContacts();
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
    return;
  }

  savedContacts[savedContactCount] = node;
  savedContacts[savedContactCount].isSaved = true;
  savedContactCount++;
  store.saveContacts(savedContacts, savedContactCount);
  LOG_INF("MESH", "Added contact: %s", node.name);
  requestUpdate();
}

bool MeshCoreDiscoverActivity::isAlreadySaved(const MeshCoreContact& node) const {
  for (uint8_t i = 0; i < savedContactCount; ++i) {
    if (memcmp(savedContacts[i].publicKey, node.publicKey, 32) == 0) return true;
  }
  return false;
}

void MeshCoreDiscoverActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  char headerSubtitle[64];
  formatMeshCoreSubtitle(client, headerSubtitle, sizeof(headerSubtitle));
  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), tr(STR_MESHCORE_DISCOVERED),
                 headerSubtitle);

  int contentTop = metrics.topPadding + metrics.headerHeight;
  int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.topPadding;
  Rect contentRect(0, contentTop, pageWidth, contentHeight);

  if (discoveredNodeCount == 0) {
    GUI.drawHelpText(renderer, contentRect, tr(STR_MESHCORE_NO_DEVICES));
  } else {
    const auto* nodes = discoveredNodes;
    const auto* saved = savedContacts;
    const auto savedCount = savedContactCount;

    GUI.drawList(
        renderer, contentRect, discoveredNodeCount, selectedIndex,
        [nodes](int index) { return std::string(nodes[index].name); },
        [nodes](int index) {
          char buf[48];
          char prefix[13];
          nodes[index].getPublicKeyPrefix(prefix);
          snprintf(buf, sizeof(buf), "%s  %dhop  SNR:%d", prefix, nodes[index].pathLength, nodes[index].snr);
          return std::string(buf);
        },
        nullptr, nullptr, false,
        [nodes, saved, savedCount](int index) {
          // Dim already-saved contacts
          for (uint8_t i = 0; i < savedCount; ++i) {
            if (memcmp(saved[i].publicKey, nodes[index].publicKey, 32) == 0) return true;
          }
          return false;
        });
  }

  const char* btn2 =
      (discoveredNodeCount > 0 && !isAlreadySaved(discoveredNodes[selectedIndex])) ? tr(STR_MESHCORE_ADD_CONTACT) : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), btn2, "", tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
