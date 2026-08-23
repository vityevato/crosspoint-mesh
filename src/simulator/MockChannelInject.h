#pragma once

#include <cstdint>

/**
 * Computes the next slot index for mock channel injection (MeshCore
 * hotkey 3). Simulates a channel appearing on the device side in the
 * first free slot after the JSON-defined channels, cycling through
 * [base, maxChannels).
 *
 * Returns false when no free slot exists (base >= maxChannels).
 * On success advances seq and writes the slot index to outIdx.
 */
inline bool nextMockChannelSlot(uint8_t base, uint8_t maxChannels, uint8_t& seq, uint8_t& outIdx) {
  if (base >= maxChannels) return false;
  outIdx = static_cast<uint8_t>(base + (seq % static_cast<uint8_t>(maxChannels - base)));
  ++seq;
  return true;
}
