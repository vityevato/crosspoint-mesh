#pragma once

#include <cstdint>

#include "MeshCoreTypes.h"

// Maximum messages stored per thread on SD card
static constexpr uint16_t MAX_MSGS_PER_THREAD = 200;
// Messages per visible page (RAM)
static constexpr uint8_t MSGS_PER_PAGE = 10;

class MeshCoreMessageStore {
 public:
  // Initialize store (creates directories if needed)
  bool init();

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

  // Unread counts
  bool saveUnreadCounts(const uint16_t* channelUnread, uint8_t channelCount, const MeshCoreContact* contacts,
                        uint8_t contactCount);
  bool loadUnreadCounts(uint16_t* channelUnread, uint8_t channelCount, MeshCoreContact* contacts, uint8_t contactCount);

 private:
  bool ensureDir(const char* path);
  void buildChannelPath(uint8_t idx, char* out, size_t maxLen);
  void buildContactPath(const uint8_t* pubkey32, char* out, size_t maxLen);

  // Generic message append/load for both channel and DM files
  bool appendMessage(const char* filePath, const MeshCoreMessage& msg);
  bool truncateOldMessages(const char* filePath, uint16_t currentCount);
  uint16_t getMessageCount(const char* filePath);
  bool loadMessages(const char* filePath, uint16_t offset, MeshCoreMessage* out, uint8_t count, uint8_t& loaded);
};
