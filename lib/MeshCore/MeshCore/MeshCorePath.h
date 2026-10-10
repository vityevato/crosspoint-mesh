#pragma once

#include <cstdint>
#include <cstring>

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
// Largest supported per-hop routing hash (defensive; hash size 4 is reserved).
static constexpr uint8_t MAX_HASH_SIZE = 4;
// Capacity of HashSet — caps how many distinct repeaters one channel message
// can report as having re-flooded it.
static constexpr uint8_t MAX_HASHES = 16;

// Number of hops encoded in a received path byte.
inline uint8_t hopCount(uint8_t encodedPathLen) { return encodedPathLen & HOP_MASK; }

// Routing hash size (bytes per hop) encoded in a received path byte.
inline uint8_t hashSize(uint8_t encodedPathLen) { return (encodedPathLen >> HASH_SIZE_SHIFT) + 1; }

// True when the byte is the "no path / direct route" sentinel.
inline bool isUnknown(uint8_t encodedPathLen) { return encodedPathLen == UNKNOWN; }

// Fixed-capacity set of relay routing hashes (prefixes of repeater public
// keys) seen in re-floods of one channel message. Deduplicates by the full
// hash width: with 2/3/4-byte hash modes, comparing only the first byte would
// collapse distinct repeaters that happen to share it.
struct HashSet {
  static constexpr uint8_t CAPACITY = MAX_HASHES;

  uint8_t count = 0;     // number of entries stored
  uint8_t hashSize = 0;  // bytes per hash, fixed by the first add
  uint8_t entries[CAPACITY][MAX_HASH_SIZE] = {};

  // Adds a hash; returns true when it was not present yet. False when the
  // hash is already stored, the size is invalid, or the set is full.
  bool add(const uint8_t* hash, uint8_t size) {
    if (size == 0 || size > MAX_HASH_SIZE) return false;
    if (count == 0) hashSize = size;
    const uint8_t cmpSize = (size < hashSize) ? size : hashSize;
    for (uint8_t i = 0; i < count; ++i) {
      if (memcmp(entries[i], hash, cmpSize) == 0) return false;
    }
    if (count >= CAPACITY) return false;
    memcpy(entries[count], hash, size);
    count++;
    return true;
  }
};

}  // namespace MeshPath
