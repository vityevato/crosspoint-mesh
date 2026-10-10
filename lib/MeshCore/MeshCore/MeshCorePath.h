#pragma once

#include <cstdint>

// MeshCore routing-path metadata helpers.
//
// The "path length" byte carried in radio packets and in companion frames is
// NOT a raw hop count — it packs two fields (see MeshCore firmware
// src/Packet.h getPathHashSize()/getPathHashCount()):
//   bits 0-5: hop count (0-63)
//   bits 6-7: hash size minus one (1-4 bytes per hop)
// On receive, 0xFF means the packet arrived via a direct (non-flood) route
// and carries no path metadata. 0xFF is not a valid encoding (hash size 4 is
// reserved and rejected by Packet::isValidPathLen), so it is collision-free.
namespace MeshPath {

// 0xFF sentinel: no known path / packet arrived directly.
static constexpr uint8_t UNKNOWN = 0xFF;
// Bits 0-5 hold the hop count.
static constexpr uint8_t HOP_MASK = 0x3F;
// Bits 6-7 hold hash size - 1.
static constexpr uint8_t HASH_SIZE_SHIFT = 6;

// Number of hops encoded in a received path byte.
inline uint8_t hopCount(uint8_t encodedPathLen) { return encodedPathLen & HOP_MASK; }

// Routing hash size (bytes per hop) encoded in a received path byte.
inline uint8_t hashSize(uint8_t encodedPathLen) { return (encodedPathLen >> HASH_SIZE_SHIFT) + 1; }

// True when the byte is the "no path / direct route" sentinel.
inline bool isUnknown(uint8_t encodedPathLen) { return encodedPathLen == UNKNOWN; }

}  // namespace MeshPath
