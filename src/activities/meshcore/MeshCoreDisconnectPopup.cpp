#include "MeshCoreDisconnectPopup.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <MeshCore/MeshCoreClient.h>

#include "I18n.h"
#include "components/UITheme.h"

bool MeshCoreDisconnectPopup::update(const MeshCoreClient& client) {
  const auto state = client.getState();
  if (state == BleConnectionState::CONNECTED) {
    _wasConnected = true;
    return false;
  }
  if (state == BleConnectionState::DISCONNECTED && _wasConnected && !_active) {
    _active = true;
    _sinceMs = _clockFn ? _clockFn() : 0;
    snprintf(_message, sizeof(_message), "%s", tr(STR_MESHCORE_CONNECTION_LOST));
    LOG_INF("MESH", "Connection lost — showing disconnect popup");
    return true;
  }
  return false;
}

void MeshCoreDisconnectPopup::show(const char* msg) {
  _active = true;
  _sinceMs = _clockFn ? _clockFn() : 0;
  snprintf(_message, sizeof(_message), "%s", msg);
}

bool MeshCoreDisconnectPopup::handleInput(MappedInputManager& input) {
  if (!_active) return false;
  // Back closes immediately; otherwise auto-return after AUTO_RETURN_MS.
  if (input.wasPressed(MappedInputManager::Button::Back)) {
    LOG_INF("MESH", "Disconnect popup: Back pressed, closing");
    return true;
  }
  if (_clockFn && static_cast<uint32_t>(_clockFn() - _sinceMs) >= AUTO_RETURN_MS) return true;
  return false;
}

void MeshCoreDisconnectPopup::render(GfxRenderer& renderer, MappedInputManager& input, const char* title,
                                     const char* subtitle) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), title, subtitle);
  GUI.drawPopup(renderer, _message);
  const auto labels = input.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
