#pragma once

#include <cstdint>

#include "MeshCoreTypes.h"

// Maximum messages stored per thread on SD card
static constexpr uint16_t MAX_MSGS_PER_THREAD = 200;

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
  bool appendChannelMessage(uint8_t channelIdx, const MeshCoreMessage& msg);
  uint16_t getChannelMessageCount(uint8_t channelIdx);
  bool loadChannelMessages(uint8_t channelIdx, uint16_t offset, MeshCoreMessage* out, uint8_t count, uint8_t& loaded);

  // Direct messages
  bool appendDirectMessage(const uint8_t* pubkey32, const MeshCoreMessage& msg);
  uint16_t getDirectMessageCount(const uint8_t* pubkey32);
  bool loadDirectMessages(const uint8_t* pubkey32, uint16_t offset, MeshCoreMessage* out, uint8_t count,
                          uint8_t& loaded);

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

  // Thread scroll position (lastSeenGlobalId)
  bool saveThreadPosition(uint8_t channelIdx, uint32_t globalId);
  uint32_t loadThreadPosition(uint8_t channelIdx);
  bool saveDirectPosition(const uint8_t* pubkey32, uint32_t globalId);
  uint32_t loadDirectPosition(const uint8_t* pubkey32);

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

  // Generic message append/load for both channel and DM files
  bool appendMessage(const char* filePath, const MeshCoreMessage& msg);
  bool truncateOldMessages(const char* filePath, uint16_t currentCount, uint32_t nextGlobalId);
  uint16_t getMessageCount(const char* filePath);
  bool loadMessages(const char* filePath, uint16_t offset, MeshCoreMessage* out, uint8_t count, uint8_t& loaded);
};
