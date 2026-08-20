#include "MeshCoreMessageStore.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <cstdio>
#include <cstring>

#include "MeshCoreClock.h"

static constexpr char MESHCORE_DIR[] = "/.crosspoint/meshcore";
static constexpr char COMPANION_FILE[] = "/.crosspoint/meshcore/companion.json";
static constexpr uint8_t META_FILE_VERSION = 2;

void MeshCoreMessageStore::bleAddrToKey(const char* bleAddr, char* keyOut, size_t keySize) {
  if (!bleAddr || keySize < 13) {
    if (keySize > 0) keyOut[0] = '\0';
    return;
  }
  size_t j = 0;
  for (size_t i = 0; bleAddr[i] != '\0' && j < 12; ++i) {
    char c = bleAddr[i];
    if (c == ':') continue;
    // Lowercase and validate hex
    if (c >= 'A' && c <= 'F') c = static_cast<char>(c + 32);
    keyOut[j++] = c;
  }
  keyOut[j] = '\0';
}

bool MeshCoreMessageStore::init(const char* companionBleAddr) {
  if (!ensureDir("/.crosspoint")) return false;
  if (!ensureDir(MESHCORE_DIR)) return false;

  // Reset companion directory
  companionDir[0] = '\0';

  // Derive companion-scoped data directory from BLE address
  if (companionBleAddr && companionBleAddr[0] != '\0') {
    char key[13];
    bleAddrToKey(companionBleAddr, key, sizeof(key));
    if (key[0] != '\0') {
      snprintf(companionDir, sizeof(companionDir), "%s/%s", MESHCORE_DIR, key);
      if (!ensureDir(companionDir)) {
        companionDir[0] = '\0';
        return false;
      }
      // Ensure conv/ directory exists
      char convDir[64];
      snprintf(convDir, sizeof(convDir), "%s/conv", companionDir);
      if (!ensureDir(convDir)) {
        companionDir[0] = '\0';
        return false;
      }
      LOG_INF("MESH", "Store init for companion: %s", key);
      return true;
    }
  }

  LOG_INF("MESH", "Message store initialized (no companion)");
  return true;
}

void MeshCoreMessageStore::buildDataPath(const char* subPath, char* out, size_t maxLen) {
  snprintf(out, maxLen, "%s/%s", companionDir, subPath);
}

bool MeshCoreMessageStore::ensureDir(const char* path) {
  if (Storage.exists(path)) return true;
  if (!Storage.mkdir(path)) {
    LOG_ERR("MESH", "Failed to create dir: %s", path);
    return false;
  }
  return true;
}

void MeshCoreMessageStore::buildChannelPath(uint8_t idx, char* out, size_t maxLen) {
  snprintf(out, maxLen, "%s/ch_%d", companionDir, idx);
}

void MeshCoreMessageStore::buildContactPath(const uint8_t* pubkey32, char* out, size_t maxLen) {
  char hexPrefix[13];
  static constexpr char hex[] = "0123456789abcdef";
  for (int i = 0; i < 6; ++i) {
    hexPrefix[i * 2] = hex[pubkey32[i] >> 4];
    hexPrefix[i * 2 + 1] = hex[pubkey32[i] & 0x0F];
  }
  hexPrefix[12] = '\0';
  snprintf(out, maxLen, "%s/dm_%s", companionDir, hexPrefix);
}

// --- Conv path builders ---

void MeshCoreMessageStore::buildConvPath(uint8_t channelIdx, char* out, size_t maxLen) {
  snprintf(out, maxLen, "%s/conv/ch_%d", companionDir, channelIdx);
}

void MeshCoreMessageStore::buildConvPath(const uint8_t* pubkey32, char* out, size_t maxLen) {
  char hexPrefix[13];
  static constexpr char hex[] = "0123456789abcdef";
  for (int i = 0; i < 6; ++i) {
    hexPrefix[i * 2] = hex[pubkey32[i] >> 4];
    hexPrefix[i * 2 + 1] = hex[pubkey32[i] & 0x0F];
  }
  hexPrefix[12] = '\0';
  snprintf(out, maxLen, "%s/conv/dm_%s", companionDir, hexPrefix);
}

// --- Meta read/write ---

bool MeshCoreMessageStore::readMeta(const char* convPath, ConvMeta& out) {
  char filePath[80];
  snprintf(filePath, sizeof(filePath), "%s/meta.bin", convPath);

  HalFile file;
  if (!Storage.openFileForRead("MESH", filePath, file)) return false;

  uint8_t version;
  if (file.read(&version, 1) != 1 || version != META_FILE_VERSION) return false;

  if (file.read(reinterpret_cast<uint8_t*>(&out.count), 2) != 2) return false;
  if (file.read(reinterpret_cast<uint8_t*>(&out.startId), 4) != 4) return false;
  if (file.read(reinterpret_cast<uint8_t*>(&out.endId), 4) != 4) return false;
  if (file.read(reinterpret_cast<uint8_t*>(&out.positionId), 4) != 4) return false;
  if (file.read(reinterpret_cast<uint8_t*>(&out.totalPx), 4) != 4) return false;
  if (file.read(reinterpret_cast<uint8_t*>(&out.positionPx), 4) != 4) return false;
  int32_t fontIdRaw = 0;
  if (file.read(reinterpret_cast<uint8_t*>(&fontIdRaw), 4) != 4) return false;
  out.fontId = fontIdRaw;
  return true;
}

bool MeshCoreMessageStore::writeMeta(const char* convPath, const ConvMeta& meta) {
  char filePath[80];
  snprintf(filePath, sizeof(filePath), "%s/meta.bin", convPath);

  HalFile file;
  if (!Storage.openFileForWrite("MESH", filePath, file)) return false;

  uint8_t version = META_FILE_VERSION;
  if (file.write(&version, 1) != 1) return false;
  if (file.write(reinterpret_cast<const uint8_t*>(&meta.count), 2) != 2) return false;
  if (file.write(reinterpret_cast<const uint8_t*>(&meta.startId), 4) != 4) return false;
  if (file.write(reinterpret_cast<const uint8_t*>(&meta.endId), 4) != 4) return false;
  if (file.write(reinterpret_cast<const uint8_t*>(&meta.positionId), 4) != 4) return false;
  if (file.write(reinterpret_cast<const uint8_t*>(&meta.totalPx), 4) != 4) return false;
  if (file.write(reinterpret_cast<const uint8_t*>(&meta.positionPx), 4) != 4) return false;
  int32_t fontIdRaw = meta.fontId;
  if (file.write(reinterpret_cast<uint8_t*>(&fontIdRaw), 4) != 4) return false;
  return true;
}

// --- dropOldestMessage ---

bool MeshCoreMessageStore::dropOldestMessage(const char* convPath, ConvMeta& meta) {
  // Read the message to subtract its height from totalPx and adjust scroll offset
  MeshCoreMessage msg;
  if (readMessage(convPath, meta.startId, msg)) {
    meta.totalPx = (meta.totalPx > msg.heightPx) ? static_cast<uint32_t>(meta.totalPx - msg.heightPx) : 0;
    meta.positionPx = (meta.positionPx > msg.heightPx) ? static_cast<uint32_t>(meta.positionPx - msg.heightPx) : 0;
  }

  // Delete the oldest message file
  char msgPath[80];
  snprintf(msgPath, sizeof(msgPath), "%s/msgs/%lu", convPath, static_cast<unsigned long>(meta.startId));

  if (Storage.exists(msgPath)) {
    if (!Storage.remove(msgPath)) {
      LOG_ERR("MESH", "Failed to drop oldest msg: %s", msgPath);
      return false;
    }
  }

  meta.startId++;
  meta.count--;
  return true;
}

// --- Channel messages ---

bool MeshCoreMessageStore::clearChannelMessages(uint8_t channelIdx) {
  char convPath[64];
  buildConvPath(channelIdx, convPath, sizeof(convPath));

  if (!Storage.exists(convPath)) return true;  // Nothing to clear

  if (!Storage.removeDir(convPath)) {
    LOG_ERR("MESH", "Failed to remove channel %d conversation dir", channelIdx);
    return false;
  }
  LOG_INF("MESH", "Cleared channel %d messages", channelIdx);
  return true;
}

bool MeshCoreMessageStore::appendChannelMessage(uint8_t channelIdx, const MeshCoreMessage& msg) {
  char convPath[64];
  buildConvPath(channelIdx, convPath, sizeof(convPath));

  // Ensure convPath/msgs/ exists
  char msgsPath[80];
  snprintf(msgsPath, sizeof(msgsPath), "%s/msgs", convPath);
  if (!ensureDir(convPath)) return false;
  if (!ensureDir(msgsPath)) return false;

  // Read or initialize metadata
  ConvMeta meta;
  if (!readMeta(convPath, meta)) {
    meta = {};
  }

  // Truncate if at capacity
  if (meta.count >= MAX_MSGS_PER_THREAD) {
    if (!dropOldestMessage(convPath, meta)) return false;
  }

  // Assign id
  uint32_t newId;
  if (meta.count == 0) {
    newId = 1;
    meta.startId = 1;
  } else {
    newId = meta.endId + 1;
  }

  // Write message as individual file
  char msgPath[80];
  snprintf(msgPath, sizeof(msgPath), "%s/%lu", msgsPath, static_cast<unsigned long>(newId));

  MeshCoreMessage msgWithId = msg;
  msgWithId.id = newId;

  HalFile file;
  if (!Storage.openFileForWrite("MESH", msgPath, file)) return false;
  file.write(reinterpret_cast<const uint8_t*>(&msgWithId), sizeof(MeshCoreMessage));
  // DESTRUCTOR_CLOSES_FILE=1

  // Update metadata
  meta.count++;
  meta.endId = newId;
  meta.totalPx += msgWithId.heightPx;
  return writeMeta(convPath, meta);
}

bool MeshCoreMessageStore::loadChannelMessages(uint8_t channelIdx, uint32_t startId, uint8_t maxCount, bool up,
                                               MeshCoreMessage* out, uint8_t& loaded) {
  char convPath[64];
  buildConvPath(channelIdx, convPath, sizeof(convPath));
  MeshCoreMessage filler;
  memset(&filler, 0, sizeof(filler));
  return loadMessages(convPath, startId, maxCount, 0, up, out, loaded, filler);
}

bool MeshCoreMessageStore::loadChannelMessages(uint8_t channelIdx, uint32_t startId, uint16_t maxHeightPx, bool up,
                                               MeshCoreMessage* out, uint8_t& loaded, MeshCoreMessage& filler) {
  char convPath[64];
  buildConvPath(channelIdx, convPath, sizeof(convPath));
  return loadMessages(convPath, startId, 0, maxHeightPx, up, out, loaded, filler);
}

bool MeshCoreMessageStore::updateChannelMessage(uint8_t channelIdx, uint32_t id, uint8_t newPathLength, int8_t newSnr) {
  char convPath[64];
  buildConvPath(channelIdx, convPath, sizeof(convPath));

  MeshCoreMessage msg;
  if (!readMessage(convPath, id, msg)) return false;
  msg.pathLength = newPathLength;
  msg.snr = newSnr;
  return writeMessage(convPath, id, msg);
}

bool MeshCoreMessageStore::updateChannelMessage(uint8_t channelIdx, uint32_t id, MeshCoreMessage& msg) {
  char convPath[64];
  buildConvPath(channelIdx, convPath, sizeof(convPath));
  return writeMessage(convPath, id, msg);
}

// --- Direct messages ---

bool MeshCoreMessageStore::clearDirectMessages(const uint8_t* pubkey32) {
  char convPath[64];
  buildConvPath(pubkey32, convPath, sizeof(convPath));

  if (!Storage.exists(convPath)) return true;  // Nothing to clear

  if (!Storage.removeDir(convPath)) {
    LOG_ERR("MESH", "Failed to remove DM conv dir");
    return false;
  }
  LOG_INF("MESH", "Cleared DM messages");
  return true;
}

bool MeshCoreMessageStore::appendDirectMessage(const uint8_t* pubkey32, const MeshCoreMessage& msg, uint32_t* outId) {
  char convPath[64];
  buildConvPath(pubkey32, convPath, sizeof(convPath));

  // Ensure convPath/msgs/ exists
  char msgsPath[80];
  snprintf(msgsPath, sizeof(msgsPath), "%s/msgs", convPath);
  if (!ensureDir(convPath)) return false;
  if (!ensureDir(msgsPath)) return false;

  // Read or initialize metadata
  ConvMeta meta;
  if (!readMeta(convPath, meta)) {
    meta = {};
  }

  // Truncate if at capacity
  if (meta.count >= MAX_MSGS_PER_THREAD) {
    if (!dropOldestMessage(convPath, meta)) return false;
  }

  // Assign id
  uint32_t newId;
  if (meta.count == 0) {
    newId = 1;
    meta.startId = 1;
  } else {
    newId = meta.endId + 1;
  }

  // Write message as individual file
  char msgPath[80];
  snprintf(msgPath, sizeof(msgPath), "%s/%lu", msgsPath, static_cast<unsigned long>(newId));

  MeshCoreMessage msgWithId = msg;
  msgWithId.id = newId;

  HalFile file;
  if (!Storage.openFileForWrite("MESH", msgPath, file)) return false;
  file.write(reinterpret_cast<const uint8_t*>(&msgWithId), sizeof(MeshCoreMessage));
  // DESTRUCTOR_CLOSES_FILE=1

  // Update metadata
  meta.count++;
  meta.endId = newId;
  meta.totalPx += msgWithId.heightPx;

  if (outId) *outId = newId;

  return writeMeta(convPath, meta);
}

bool MeshCoreMessageStore::loadDirectMessages(const uint8_t* pubkey32, uint32_t startId, uint8_t maxCount, bool up,
                                              MeshCoreMessage* out, uint8_t& loaded) {
  char convPath[64];
  buildConvPath(pubkey32, convPath, sizeof(convPath));
  MeshCoreMessage filler;
  memset(&filler, 0, sizeof(filler));
  return loadMessages(convPath, startId, maxCount, 0, up, out, loaded, filler);
}

bool MeshCoreMessageStore::loadDirectMessages(const uint8_t* pubkey32, uint32_t startId, uint16_t maxHeightPx, bool up,
                                              MeshCoreMessage* out, uint8_t& loaded, MeshCoreMessage& filler) {
  char convPath[64];
  buildConvPath(pubkey32, convPath, sizeof(convPath));
  return loadMessages(convPath, startId, 0, maxHeightPx, up, out, loaded, filler);
}

bool MeshCoreMessageStore::updateDirectMessage(const uint8_t* pubkey32, uint32_t id, DeliveryStatus newStatus) {
  char convPath[64];
  buildConvPath(pubkey32, convPath, sizeof(convPath));

  MeshCoreMessage msg;
  if (!readMessage(convPath, id, msg)) return false;
  if (msg.deliveryStatus != newStatus) {
    // Update timestamp to reflect the status-change time
    msg.timestamp = meshcoreNowUtc();
  }
  msg.deliveryStatus = newStatus;
  return writeMessage(convPath, id, msg);
}

bool MeshCoreMessageStore::updateDirectMessage(const uint8_t* pubkey32, uint32_t id, MeshCoreMessage& msg) {
  char convPath[64];
  buildConvPath(pubkey32, convPath, sizeof(convPath));
  return writeMessage(convPath, id, msg);
}

// --- Unified message loader (private) ---

bool MeshCoreMessageStore::loadMessages(const char* convPath, uint32_t startId, uint8_t maxCount, uint16_t maxHeightPx,
                                        bool up, MeshCoreMessage* out, uint8_t& loaded, MeshCoreMessage& filler) {
  loaded = 0;
  memset(&filler, 0, sizeof(filler));

  ConvMeta meta;
  if (!readMeta(convPath, meta)) return false;
  if (meta.count == 0) return true;

  char msgsPath[80];
  snprintf(msgsPath, sizeof(msgsPath), "%s/msgs", convPath);

  bool byCount = (maxCount > 0);
  bool byHeight = (maxHeightPx > 0);

  if (byCount) {
    if (up) {
      // Load backwards from startId, then reverse to ascending order
      uint32_t gid = startId;
      while (gid >= meta.startId && loaded < maxCount) {
        char msgPath[80];
        snprintf(msgPath, sizeof(msgPath), "%s/%lu", msgsPath, static_cast<unsigned long>(gid));

        HalFile file;
        if (!Storage.openFileForRead("MESH", msgPath, file)) {
          if (gid == 0) break;
          --gid;
          continue;
        }

        if (file.read(reinterpret_cast<uint8_t*>(&out[loaded]), sizeof(MeshCoreMessage)) == sizeof(MeshCoreMessage)) {
          loaded++;
        }
        if (gid == 0) break;
        --gid;
      }

      // Reverse to ascending order
      for (uint8_t i = 0; i < loaded / 2; ++i) {
        MeshCoreMessage tmp = out[i];
        out[i] = out[loaded - 1 - i];
        out[loaded - 1 - i] = tmp;
      }
    } else {
      if (startId > meta.endId) return loaded > 0;

      for (uint32_t gid = startId; gid <= meta.endId && loaded < maxCount; ++gid) {
        char msgPath[80];
        snprintf(msgPath, sizeof(msgPath), "%s/%lu", msgsPath, static_cast<unsigned long>(gid));

        HalFile file;
        if (!Storage.openFileForRead("MESH", msgPath, file)) continue;

        if (file.read(reinterpret_cast<uint8_t*>(&out[loaded]), sizeof(MeshCoreMessage)) == sizeof(MeshCoreMessage)) {
          loaded++;
        }
      }
    }
  } else if (byHeight) {
    uint16_t accumulated = 0;

    if (up) {
      uint32_t gid = startId;
      while (gid >= meta.startId) {
        char msgPath[80];
        snprintf(msgPath, sizeof(msgPath), "%s/%lu", msgsPath, static_cast<unsigned long>(gid));

        HalFile file;
        if (!Storage.openFileForRead("MESH", msgPath, file)) {
          if (gid == 0) break;
          --gid;
          continue;
        }

        if (file.read(reinterpret_cast<uint8_t*>(&out[loaded]), sizeof(MeshCoreMessage)) == sizeof(MeshCoreMessage)) {
          if (accumulated + out[loaded].heightPx > maxHeightPx) {
            if (loaded > 0) break;
            // First message taller than viewport: accept it anyway.
            // The renderer clips it with "..." and scroll can advance.
          }
          accumulated += out[loaded].heightPx;
          loaded++;
        }
        if (gid == 0) break;
        --gid;
      }

      // Reverse to ascending order
      for (uint8_t i = 0; i < loaded / 2; ++i) {
        MeshCoreMessage tmp = out[i];
        out[i] = out[loaded - 1 - i];
        out[loaded - 1 - i] = tmp;
      }

      // Filler for up=true: next message after startId in forward direction.
      // This is the message that would appear "below" startId in the viewport.
      if (startId < UINT32_MAX && startId < meta.endId) {
        for (uint32_t gid = startId + 1; gid <= meta.endId; ++gid) {
          char msgPath[80];
          snprintf(msgPath, sizeof(msgPath), "%s/%lu", msgsPath, static_cast<unsigned long>(gid));

          HalFile file;
          if (!Storage.openFileForRead("MESH", msgPath, file)) continue;

          if (file.read(reinterpret_cast<uint8_t*>(&filler), sizeof(MeshCoreMessage)) == sizeof(MeshCoreMessage)) {
            break;
          }
        }
      }
    } else {
      bool overflowed = false;

      for (uint32_t gid = startId; gid <= meta.endId; ++gid) {
        char msgPath[80];
        snprintf(msgPath, sizeof(msgPath), "%s/%lu", msgsPath, static_cast<unsigned long>(gid));

        HalFile file;
        if (!Storage.openFileForRead("MESH", msgPath, file)) continue;

        if (file.read(reinterpret_cast<uint8_t*>(&out[loaded]), sizeof(MeshCoreMessage)) == sizeof(MeshCoreMessage)) {
          if (accumulated + out[loaded].heightPx > maxHeightPx) {
            if (loaded > 0) {
              overflowed = true;
              break;
            }
            // First message taller than viewport: accept it anyway.
            // The renderer clips it with "..." and scroll can advance.
            accumulated += out[loaded].heightPx;
            loaded++;
            break;
          }
          accumulated += out[loaded].heightPx;
          loaded++;
        }
      }

      // Filler for up=false: the message that was read but didn't fit
      // (already in out[loaded] from the last read before break).
      if (overflowed) {
        filler = out[loaded];
      }
    }
  }

  return loaded > 0;
}

// --- Single message read/write (private) ---

bool MeshCoreMessageStore::readMessage(const char* convPath, uint32_t id, MeshCoreMessage& msg) {
  char msgPath[80];
  snprintf(msgPath, sizeof(msgPath), "%s/msgs/%lu", convPath, static_cast<unsigned long>(id));

  HalFile file;
  if (!Storage.openFileForRead("MESH", msgPath, file)) return false;

  if (file.read(reinterpret_cast<uint8_t*>(&msg), sizeof(MeshCoreMessage)) != sizeof(MeshCoreMessage)) {
    return false;
  }
  return true;
}

bool MeshCoreMessageStore::writeMessage(const char* convPath, uint32_t id, const MeshCoreMessage& msg) {
  char msgPath[80];
  snprintf(msgPath, sizeof(msgPath), "%s/msgs/%lu", convPath, static_cast<unsigned long>(id));

  HalFile file;
  if (!Storage.openFileForWrite("MESH", msgPath, file)) return false;
  file.write(reinterpret_cast<const uint8_t*>(&msg), sizeof(MeshCoreMessage));
  return true;
}

// --- Conversation metadata ---

bool MeshCoreMessageStore::getChannelMeta(uint8_t channelIdx, ConvMeta& out) {
  char convPath[64];
  buildConvPath(channelIdx, convPath, sizeof(convPath));
  return readMeta(convPath, out);
}

bool MeshCoreMessageStore::getDirectMeta(const uint8_t* pubkey32, ConvMeta& out) {
  char convPath[64];
  buildConvPath(pubkey32, convPath, sizeof(convPath));
  return readMeta(convPath, out);
}

bool MeshCoreMessageStore::saveChannelMeta(uint8_t channelIdx, const ConvMeta& meta) {
  char convPath[64];
  buildConvPath(channelIdx, convPath, sizeof(convPath));
  return writeMeta(convPath, meta);
}

bool MeshCoreMessageStore::saveDirectMeta(const uint8_t* pubkey32, const ConvMeta& meta) {
  char convPath[64];
  buildConvPath(pubkey32, convPath, sizeof(convPath));
  return writeMeta(convPath, meta);
}

// --- Contacts ---

bool MeshCoreMessageStore::saveContacts(const MeshCoreContact* contacts, uint8_t count) {
  if (companionDir[0] == '\0') return false;
  char filePath[64];
  buildDataPath("contacts.bin", filePath, sizeof(filePath));
  HalFile file;
  if (!Storage.openFileForWrite("MESH", filePath, file)) {
    LOG_ERR("MESH", "Failed to open contacts file for write");
    return false;
  }

  uint8_t version = MESHCORE_CONTACT_FILE_VERSION;
  file.write(&version, 1);
  file.write(&count, 1);

  for (uint8_t i = 0; i < count; ++i) {
    file.write(reinterpret_cast<const uint8_t*>(&contacts[i]), sizeof(MeshCoreContact));
  }

  LOG_INF("MESH", "Saved %d contacts", count);
  return true;
}

uint8_t MeshCoreMessageStore::loadContacts(MeshCoreContact* out, uint8_t maxCount) {
  if (companionDir[0] == '\0') return 0;
  char filePath[64];
  buildDataPath("contacts.bin", filePath, sizeof(filePath));
  HalFile file;
  if (!Storage.openFileForRead("MESH", filePath, file)) {
    return 0;
  }

  uint8_t version = 0;
  file.read(&version, 1);
  if (version != MESHCORE_CONTACT_FILE_VERSION) {
    LOG_ERR("MESH", "Bad contacts file version: %d", version);
    return 0;
  }

  uint8_t count = 0;
  file.read(&count, 1);
  if (count > maxCount) count = maxCount;

  for (uint8_t i = 0; i < count; ++i) {
    int bytesRead = file.read(reinterpret_cast<uint8_t*>(&out[i]), sizeof(MeshCoreContact));
    if (bytesRead != sizeof(MeshCoreContact)) {
      LOG_ERR("MESH", "Short read at contact %d", i);
      return i;
    }
  }

  LOG_INF("MESH", "Loaded %d contacts", count);
  return count;
}

// --- Companion address ---

bool MeshCoreMessageStore::saveCompanionAddress(const char* bleAddr, uint8_t addressType) {
  if (!bleAddr || bleAddr[0] == '\0') return false;

  HalFile file;
  if (!Storage.openFileForWrite("MESH", COMPANION_FILE, file)) {
    LOG_ERR("MESH", "Failed to save companion address");
    return false;
  }

  // Format: "<address>:<type>" e.g. "c2:0e:d3:71:13:d9:1"
  char buf[21];
  snprintf(buf, sizeof(buf), "%s:%u", bleAddr, (unsigned)addressType);
  file.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
  return true;
}

bool MeshCoreMessageStore::loadCompanionAddress(char* out, size_t maxLen, uint8_t* addressType) {
  HalFile file;
  if (!Storage.openFileForRead("MESH", COMPANION_FILE, file)) {
    return false;
  }

  size_t fileSize = file.size();
  if (fileSize == 0 || fileSize >= maxLen + 3) {
    return false;
  }

  char buf[21] = {};
  file.read(reinterpret_cast<uint8_t*>(buf), std::min(fileSize, sizeof(buf) - 1));
  buf[fileSize] = '\0';

  // Parse "<address>:<type>" — last colon separates address from type byte
  char* lastColon = strrchr(buf, ':');
  if (lastColon && (lastColon - buf) == 17) {
    // 17 chars before last colon = valid BLE address
    if (addressType) {
      *addressType = (uint8_t)atoi(lastColon + 1);
    }
    *lastColon = '\0';
    snprintf(out, maxLen, "%s", buf);
  } else {
    // Legacy format — address only, assume public (0)
    if (addressType) *addressType = 0;
    snprintf(out, maxLen, "%s", buf);
  }
  return out[0] != '\0';
}

// --- Companion PIN (per-companion, stored in scoped directory) ---

bool MeshCoreMessageStore::saveCompanionPin(uint32_t pin) {
  if (companionDir[0] == '\0') {
    LOG_ERR("MESH", "No companion scoped — cannot save PIN");
    return false;
  }
  char filePath[64];
  buildDataPath("pin.bin", filePath, sizeof(filePath));
  HalFile file;
  if (!Storage.openFileForWrite("MESH", filePath, file)) {
    LOG_ERR("MESH", "Failed to save companion PIN");
    return false;
  }
  file.write(reinterpret_cast<const uint8_t*>(&pin), sizeof(pin));
  LOG_INF("MESH", "Saved companion PIN: %lu", (unsigned long)pin);
  return true;
}

bool MeshCoreMessageStore::loadCompanionPin(uint32_t* out) {
  if (!out) return false;
  *out = 123456;  // MeshCore default PIN
  if (companionDir[0] == '\0') return false;

  char filePath[64];
  buildDataPath("pin.bin", filePath, sizeof(filePath));
  HalFile file;
  if (!Storage.openFileForRead("MESH", filePath, file)) {
    return false;  // No saved PIN file — return default
  }

  uint32_t pin = 0;
  int bytesRead = file.read(reinterpret_cast<uint8_t*>(&pin), sizeof(pin));
  if (bytesRead == sizeof(pin) && pin > 0) {
    *out = pin;
  }
  return true;
}

// --- Static PIN lookup by address ---

bool MeshCoreMessageStore::loadCompanionPinForAddress(const char* bleAddr, uint32_t* out) {
  if (!out || !bleAddr || bleAddr[0] == '\0') return false;
  *out = 123456;  // MeshCore default

  char key[13];
  bleAddrToKey(bleAddr, key, sizeof(key));
  if (key[0] == '\0') return false;

  char filePath[64];
  snprintf(filePath, sizeof(filePath), "%s/%s/pin.bin", MESHCORE_DIR, key);

  HalFile file;
  if (!Storage.openFileForRead("MESH", filePath, file)) {
    return false;  // No stored PIN for this companion
  }

  uint32_t pin = 0;
  int bytesRead = file.read(reinterpret_cast<uint8_t*>(&pin), sizeof(pin));
  if (bytesRead == sizeof(pin) && pin > 0) {
    *out = pin;
  }
  return true;
}

// --- Unread counts ---

bool MeshCoreMessageStore::saveUnreadCounts(const uint16_t* channelUnread, uint8_t channelCount,
                                            const MeshCoreContact* contacts, uint8_t contactCount) {
  if (companionDir[0] == '\0') return false;
  char filePath[64];
  buildDataPath("unread.bin", filePath, sizeof(filePath));
  HalFile file;
  if (!Storage.openFileForWrite("MESH", filePath, file)) {
    LOG_ERR("MESH", "Failed to save unread counts");
    return false;
  }

  uint8_t version = 1;
  file.write(&version, 1);

  // Channel unread counts
  file.write(&channelCount, 1);
  for (uint8_t i = 0; i < channelCount; ++i) {
    file.write(reinterpret_cast<const uint8_t*>(&channelUnread[i]), 2);
  }

  // Contact unread counts (pubkey prefix + count)
  file.write(&contactCount, 1);
  for (uint8_t i = 0; i < contactCount; ++i) {
    file.write(contacts[i].publicKey, 6);  // 6-byte prefix
    file.write(reinterpret_cast<const uint8_t*>(&contacts[i].unreadCount), 2);
  }

  return true;
}

bool MeshCoreMessageStore::loadUnreadCounts(uint16_t* channelUnread, uint8_t channelCount, MeshCoreContact* contacts,
                                            uint8_t contactCount) {
  if (companionDir[0] == '\0') return false;
  char filePath[64];
  buildDataPath("unread.bin", filePath, sizeof(filePath));
  HalFile file;
  if (!Storage.openFileForRead("MESH", filePath, file)) {
    return false;
  }

  uint8_t version = 0;
  file.read(&version, 1);
  if (version != 1) return false;

  // Channel unread counts
  uint8_t savedChannelCount = 0;
  file.read(&savedChannelCount, 1);
  for (uint8_t i = 0; i < savedChannelCount; ++i) {
    uint16_t val = 0;
    file.read(reinterpret_cast<uint8_t*>(&val), 2);
    if (i < channelCount) {
      channelUnread[i] = val;
    }
  }

  // Contact unread counts
  uint8_t savedContactCount = 0;
  file.read(&savedContactCount, 1);
  for (uint8_t i = 0; i < savedContactCount; ++i) {
    uint8_t prefix[6];
    uint16_t val = 0;
    file.read(prefix, 6);
    file.read(reinterpret_cast<uint8_t*>(&val), 2);

    // Match to loaded contacts by pubkey prefix
    for (uint8_t j = 0; j < contactCount; ++j) {
      if (memcmp(contacts[j].publicKey, prefix, 6) == 0) {
        contacts[j].unreadCount = val;
        break;
      }
    }
  }

  return true;
}
