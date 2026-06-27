#pragma once

#include <cstdint>

#include "MeshCoreTypes.h"

// Maximum messages stored per thread on SD card
static constexpr uint16_t MAX_MSGS_PER_THREAD = 200;

/// On-disk metadata for one conversation (channel or DM).
struct ConvMeta {
  uint16_t count = 0;
  uint32_t startId = 0;
  uint32_t endId = 0;
  uint32_t positionId = 0;
};

class MeshCoreMessageStore {
 public:
  // Initialize store for a specific companion (or nullptr for bare init).
  // companionBleAddr: BLE address (e.g. "c2:0e:d3:71:13:d9") used to
  // derive the companion-scoped storage directory. May be nullptr to
  // only create the top-level meshcore directory (no data loaded).
  bool init(const char* companionBleAddr = nullptr);

  // Returns true if a companion key has been set and data directories exist.
  bool hasCompanionKey() const { return companionDir[0] != '\0'; }

  // Derive a 12-char hex key from a BLE address (strips colons).
  // "AA:BB:CC:DD:EE:FF" → "aabbccddeeff"
  static void bleAddrToKey(const char* bleAddr, char* keyOut, size_t keySize);

  // Channel messages
  bool clearChannelMessages(uint8_t channelIdx);
  bool appendChannelMessage(uint8_t channelIdx, const MeshCoreMessage& msg);
  uint16_t getChannelMessageCount(uint8_t channelIdx);
  bool loadChannelMessages(uint8_t channelIdx, uint32_t startId, MeshCoreMessage* out, uint8_t maxCount,
                           uint8_t& loaded);

  // Update an existing channel message by id — called when a repeater
  // refloods the message (pathLength + snr change after send).
  bool updateChannelMessage(uint8_t channelIdx, uint32_t id, uint8_t newPathLength, int8_t newSnr);

  // Direct messages
  bool clearDirectMessages(const uint8_t* pubkey32);
  bool appendDirectMessage(const uint8_t* pubkey32, const MeshCoreMessage& msg);
  uint16_t getDirectMessageCount(const uint8_t* pubkey32);
  bool loadDirectMessages(const uint8_t* pubkey32, uint32_t startId, MeshCoreMessage* out, uint8_t maxCount,
                          uint8_t& loaded);

  // Update an existing direct message by id — called when delivery
  // status transitions (SENT → ACKED or SENT → FAILED).
  bool updateDirectMessage(const uint8_t* pubkey32, uint32_t id, DeliveryStatus newStatus);

  // Conversation metadata
  bool getChannelMeta(uint8_t channelIdx, ConvMeta& out);
  bool getDirectMeta(const uint8_t* pubkey32, ConvMeta& out);

  // Thread scroll position (id-based)
  bool saveChannelPosition(uint8_t channelIdx, uint32_t id);
  uint32_t loadChannelPosition(uint8_t channelIdx);
  bool saveDirectPosition(const uint8_t* pubkey32, uint32_t id);
  uint32_t loadDirectPosition(const uint8_t* pubkey32);

  // Saved contacts
  bool saveContacts(const MeshCoreContact* contacts, uint8_t count);
  uint8_t loadContacts(MeshCoreContact* out, uint8_t maxCount);

  // Companion auto-reconnect address
  bool saveCompanionAddress(const char* bleAddr, uint8_t addressType = 0);
  bool loadCompanionAddress(char* out, size_t maxLen, uint8_t* addressType = nullptr);

  // Companion BLE pairing PIN (instance methods use scoped companion)
  bool saveCompanionPin(uint32_t pin);
  bool loadCompanionPin(uint32_t* out);

  // Static: load PIN for any companion by BLE address (no instance needed)
  static bool loadCompanionPinForAddress(const char* bleAddr, uint32_t* out);

  // Unread counts
  bool saveUnreadCounts(const uint16_t* channelUnread, uint8_t channelCount, const MeshCoreContact* contacts,
                        uint8_t contactCount);
  bool loadUnreadCounts(uint16_t* channelUnread, uint8_t channelCount, MeshCoreContact* contacts, uint8_t contactCount);

 private:
  // Companion-scoped data directory: "/.crosspoint/meshcore/<12-hex-key>"
  // Empty if no companion has been set.
  char companionDir[50] = {};

  bool ensureDir(const char* path);
  void buildDataPath(const char* subPath, char* out, size_t maxLen);
  void buildChannelPath(uint8_t idx, char* out, size_t maxLen);
  void buildContactPath(const uint8_t* pubkey32, char* out, size_t maxLen);

  // Build path to conversation directory: "conv/ch_<idx>" or "conv/dm_<hex>"
  void buildConvPath(uint8_t channelIdx, char* out, size_t maxLen);
  void buildConvPath(const uint8_t* pubkey32, char* out, size_t maxLen);

  // Read/write meta.bin for a conversation
  bool readMeta(const char* convPath, ConvMeta& out);
  bool writeMeta(const char* convPath, const ConvMeta& meta);

  // Delete oldest message file, update meta accordingly
  // Caller holds meta with count >= MAX_MSGS_PER_THREAD
  bool dropOldestMessage(const char* convPath, ConvMeta& meta);
};
