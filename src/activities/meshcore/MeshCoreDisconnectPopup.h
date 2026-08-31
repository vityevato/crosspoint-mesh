#pragma once

#include <cstdint>
#include <cstdio>

#include "MappedInputManager.h"

class GfxRenderer;
class MeshCoreClient;

/**
 * Disconnect popup with auto-return for MeshCore inner activities
 * (thread, discover). Detects the CONNECTED -> DISCONNECTED transition,
 * shows a blocking popup explaining the loss, and signals the activity
 * to finish() back to the hub — either on Back or after AUTO_RETURN_MS.
 * The hub's pendingAutoReconnect then takes over and reconnects.
 *
 * Header-only state, zero heap. The clock is injected via setClock()
 * for consistency with StatusMessageOverlay.
 */
class MeshCoreDisconnectPopup {
 public:
  /// How long the popup stays on screen before auto-returning to the hub.
  static constexpr uint32_t AUTO_RETURN_MS = 3000;

  /** Monotonic millisecond clock — typically &millis. */
  using ClockFn = unsigned long (*)();

  /** Wire up the monotonic clock. Call once during onEnter(). */
  void setClock(ClockFn fn) { _clockFn = fn; }

  /**
   * Seed the "was connected" baseline so a disconnect that already
   * happened before the first loop() is detected too.
   * Call once in onEnter() after setClock().
   */
  void arm(bool connectedNow) { _wasConnected = connectedNow; }

  /**
   * Must be called every frame (after client.poll()).
   * Activates the popup on the first observed CONNECTED -> DISCONNECTED
   * transition. Never re-activates (or overwrites a custom show() message)
   * while the popup is already up.
   * @return true when the popup was newly activated — caller should requestUpdate().
   */
  bool update(const MeshCoreClient& client);

  /** Activate the popup with a custom message (e.g. send-failure text). */
  void show(const char* msg);

  /** @return true while the popup is being displayed. */
  bool isActive() const { return _active; }

  /**
   * Must be called every frame while isActive().
   * @return true when the activity should finish() now (Back pressed or
   *         the AUTO_RETURN_MS timeout expired).
   */
  bool handleInput(MappedInputManager& input);

  /** Full-screen popup render: header, message, button hint, flush. */
  void render(GfxRenderer& renderer, MappedInputManager& input, const char* title, const char* subtitle);

 private:
  char _message[64] = {};
  bool _wasConnected = false;
  bool _active = false;
  uint32_t _sinceMs = 0;
  ClockFn _clockFn = nullptr;
};
