#pragma once

#include <cstdint>

#include "MeshCoreTypes.h"

// Maximum messages stored per thread on SD card
static constexpr uint16_t MAX_MSGS_PER_THREAD = 200;

/// On-disk metadata for one conversation (channel or DM).
struct ConvMeta {
  uint16_t count = 0;       ///< Number of messages in this conversation
  uint32_t startId = 0;     ///< Id of the oldest message (monotonically increasing)
  uint32_t endId = 0;       ///< Id of the newest message
  uint32_t positionId = 0;  ///< Id of the viewed message (scroll restore) which
                            ///< corresponds to positionPx below
  uint32_t totalPx = 0;     ///< Total pixel height of all messages in this thread
  uint32_t positionPx = 0;  ///< Pixel offset of the messages list (scroll restore),
                            ///< measured from the top of the first message and
                            ///< corresponds to the top of the viewport on screen.
  int fontId = 0;           ///< Font ID used to render this thread (for scroll restore)
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

  /// Append a message to the channel.
  /// Side effect: updates ConvMeta (count, endId, totalPx).
  /// If the thread is at capacity, drops the oldest message first
  /// (which also adjusts positionPx).
  bool appendChannelMessage(uint8_t channelIdx, const MeshCoreMessage& msg);
  bool loadChannelMessages(uint8_t channelIdx, uint32_t startId, uint8_t maxCount, bool up, MeshCoreMessage* out,
                           uint8_t& loaded);
  /// Overload that loads messages by pixel height rather than count.
  /// Starts at startId and loads messages filling up to maxHeightPx pixels.
  /// If up is true — loads backwards (ids <= startId), otherwise forwards (ids >= startId).
  /// On return, messages in out are always ordered by id ascending.
  /// filler receives the next message after the main batch if one exists:
  ///   up=false → message after the last loaded message (the one that didn't fit);
  ///   up=true  → message after startId in the forward direction.
  /// filler.id == 0 means no filler is available.
  bool loadChannelMessages(uint8_t channelIdx, uint32_t startId, uint16_t maxHeightPx, bool up, MeshCoreMessage* out,
                           uint8_t& loaded, MeshCoreMessage& filler);

  /// Overload that replaces the entire message on disk by id.
  /// Reads the existing file, applies all fields from msg, then writes it back.
  bool updateChannelMessage(uint8_t channelIdx, uint32_t id, MeshCoreMessage& msg);

  // Update an existing channel message by id — called when a repeater
  // refloods the message (pathLength + snr change after send).
  bool updateChannelMessage(uint8_t channelIdx, uint32_t id, uint8_t newPathLength, int8_t newSnr);

  // Direct messages
  bool clearDirectMessages(const uint8_t* pubkey32);

  /// Append a message to a direct-message conversation.
  /// Side effect: updates ConvMeta (count, endId, totalPx).
  /// If the thread is at capacity, drops the oldest message first
  /// (which also adjusts positionPx).
  /// On success, writes the assigned message id to *outId if non-null.
  bool appendDirectMessage(const uint8_t* pubkey32, const MeshCoreMessage& msg, uint32_t* outId = nullptr);
  bool loadDirectMessages(const uint8_t* pubkey32, uint32_t startId, uint8_t maxCount, bool up, MeshCoreMessage* out,
                          uint8_t& loaded);
  /// Overload that loads messages by pixel height rather than count.
  /// Starts at startId and loads messages filling up to maxHeightPx pixels.
  /// If up is true — loads backwards (ids <= startId), otherwise forwards (ids >= startId).
  /// On return, messages in out are always ordered by id ascending.
  /// filler receives the next message after the main batch if one exists:
  ///   up=false → message after the last loaded message (the one that didn't fit);
  ///   up=true  → message after startId in the forward direction.
  /// filler.id == 0 means no filler is available.
  bool loadDirectMessages(const uint8_t* pubkey32, uint32_t startId, uint16_t maxHeightPx, bool up,
                          MeshCoreMessage* out, uint8_t& loaded, MeshCoreMessage& filler);

  /// Overload that replaces the entire message on disk by id.
  /// Reads the existing file, applies all fields from msg, then writes it back.
  bool updateDirectMessage(const uint8_t* pubkey32, uint32_t id, MeshCoreMessage& msg);

  // Update an existing direct message by id — called when delivery
  // status transitions (SENT → ACKED or SENT → FAILED).
  bool updateDirectMessage(const uint8_t* pubkey32, uint32_t id, DeliveryStatus newStatus);

  /// Loads the most recent message (highest id) in a channel conversation,
  /// regardless of direction. Returns false when the conversation has no
  /// messages; @p out is left untouched on failure.
  bool loadNewestChannelMessage(uint8_t channelIdx, MeshCoreMessage& out);

  /// Loads the most recent inbound (RECEIVED) direct message from a contact,
  /// scanning backwards from the newest id. Returns false when the contact
  /// has no received messages; @p out is left untouched on failure.
  bool loadNewestReceivedDirectMessage(const uint8_t* pubkey32, MeshCoreMessage& out);

  // Conversation metadata
  bool getChannelMeta(uint8_t channelIdx, ConvMeta& out);
  bool getDirectMeta(const uint8_t* pubkey32, ConvMeta& out);
  bool saveChannelMeta(uint8_t channelIdx, const ConvMeta& meta);
  bool saveDirectMeta(const uint8_t* pubkey32, const ConvMeta& meta);

  // Saved contacts
  bool saveContacts(const MeshCoreContact* contacts, uint16_t count);
  /// Read the stored contact count without loading any records, so callers can
  /// size their destination buffer first. Returns 0 when absent or version
  /// mismatched (no migration — pre-production format bumps are dropped).
  uint16_t peekContactsCount() const;
  uint16_t loadContacts(MeshCoreContact* out, uint16_t maxCount);
  /// Load a single saved contact by full public key. Streams records straight
  /// from the file — no heap buffer needed. Returns false when absent.
  bool findContactByPubkey(const uint8_t* pubkey32, MeshCoreContact& out);
  /// Remove a saved contact by public key, persisting the updated list.
  /// Returns true when it existed and was removed.
  bool removeContactByPubkey(const uint8_t* pubkey32);

  // Companion auto-reconnect address
  bool saveCompanionAddress(const char* bleAddr, uint8_t addressType = 0);
  bool loadCompanionAddress(char* out, size_t maxLen, uint8_t* addressType = nullptr);
  /// Forgets the auto-reconnect target (removes the stored companion address
  /// file) so the next Hub entry no longer auto-connects to it. The companion's
  /// own data directory (contacts, messages, PIN) is left untouched.
  bool removeCompanionAddress();

  // Companion BLE pairing PIN (instance methods use scoped companion)
  bool saveCompanionPin(uint32_t pin);
  bool loadCompanionPin(uint32_t* out);

  // Static: load PIN for any companion by BLE address (no instance needed)
  static bool loadCompanionPinForAddress(const char* bleAddr, uint32_t* out);

  // Unread counts
  bool saveUnreadCounts(const uint16_t* channelUnread, uint16_t channelCount, const MeshCoreContact* contacts,
                        uint16_t contactCount);
  bool loadUnreadCounts(uint16_t* channelUnread, uint16_t channelCount, MeshCoreContact* contacts,
                        uint16_t contactCount);

 private:
  // Companion-scoped data directory: "/.crosspoint/meshcore/<12-hex-key>"
  // Empty if no companion has been set.
  char companionDir[50] = {};

  bool ensureDir(const char* path);
  void buildDataPath(const char* subPath, char* out, size_t maxLen) const;
  void buildChannelPath(uint8_t idx, char* out, size_t maxLen);
  void buildContactPath(const uint8_t* pubkey32, char* out, size_t maxLen);

  // Build path to conversation directory: "conv/ch_<idx>" or "conv/dm_<hex>"
  void buildConvPath(uint8_t channelIdx, char* out, size_t maxLen);
  void buildConvPath(const uint8_t* pubkey32, char* out, size_t maxLen);

  // Read/write meta.bin for a conversation
  bool readMeta(const char* convPath, ConvMeta& out);
  bool writeMeta(const char* convPath, const ConvMeta& meta);

  /// Delete oldest message file and update meta accordingly:
  /// startId++, count--, totalPx -= heightPx, positionPx -= heightPx.
  /// Caller must hold meta with count >= MAX_MSGS_PER_THREAD.
  bool dropOldestMessage(const char* convPath, ConvMeta& meta);

  /// Unified message loader — all four public load*Messages overloads
  /// delegate here.
  /// maxCount > 0 → count mode: loads up to maxCount messages from
  ///                startId forward (ascending).
  /// maxHeightPx > 0 → height mode: loads per 'up' direction, stops
  ///                    when accumulated heightPx ≥ maxHeightPx.
  /// Messages in out are always ordered by id ascending.
  /// filler: see public overload docs.
  bool loadMessages(const char* convPath, uint32_t startId, uint8_t maxCount, uint16_t maxHeightPx, bool up,
                    MeshCoreMessage* out, uint8_t& loaded, MeshCoreMessage& filler);

  /// Read a single message by id from a conversation directory.
  bool readMessage(const char* convPath, uint32_t id, MeshCoreMessage& msg);

  /// Write (overwrite) a single message by id to a conversation directory.
  bool writeMessage(const char* convPath, uint32_t id, const MeshCoreMessage& msg);
};
