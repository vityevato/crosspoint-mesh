#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>

/**
 * Periodic battery polling helper for MeshCore activities.
 *
 * Call pollMeshCoreBattery() from the activity's loop() after client.poll().
 * It handles the 5-minute poll interval and detects value changes.
 *
 * Usage:
 *   if (pollMeshCoreBattery(client, lastBatteryRequestMs, lastBatteryMv)) {
 *     requestUpdate();
 *   }
 *
 * For immediate refresh (onEnter, Confirm button):
 *   client.requestBattery();
 *   lastBatteryRequestMs = millis();
 *
 * @param client     Reference to the MeshCoreClient
 * @param lastReqMs  Timestamp (in/out) of last successful battery request
 * @param lastMv     Last known battery mV (in/out) — used to detect changes
 * @return true if battery value changed and UI should redraw
 */
inline bool pollMeshCoreBattery(MeshCoreClient& client, uint32_t& lastReqMs, uint16_t& lastMv) {
  static constexpr uint32_t POLL_INTERVAL_MS = 300000;  // 5 minutes

  if (client.getState() != BleConnectionState::CONNECTED) {
    return false;
  }

  uint32_t now = millis();
  if (now - lastReqMs >= POLL_INTERVAL_MS) {
    LOG_DBG("MESH", "Battery poll: %.1f min elapsed", (now - lastReqMs) / 60000.0f);
    if (client.requestBattery()) {
      lastReqMs = now;
    } else {
      LOG_ERR("MESH", "Battery poll: requestBattery FAILED (queue full or disconnected)");
    }
  }

  uint16_t batteryMv = client.getCompanion().batteryMv;
  if (batteryMv != lastMv) {
    LOG_DBG("MESH", "Battery changed: %d -> %d mV", lastMv, batteryMv);
    lastMv = batteryMv;
    return true;
  }
  return false;
}
