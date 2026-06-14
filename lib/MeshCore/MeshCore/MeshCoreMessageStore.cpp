#include "MeshCoreMessageStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <cstdio>
#include <cstring>

static constexpr char MESHCORE_DIR[] = "/.crosspoint/meshcore";
static constexpr char COMPANION_FILE[] = "/.crosspoint/meshcore/companion.json";

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

// --- Generic message operations ---

bool MeshCoreMessageStore::appendMessage(const char* filePath, const MeshCoreMessage& msg) {
  HalFile file;

  if (Storage.exists(filePath)) {
    // Read existing file
    if (!Storage.openFileForRead("MESH", filePath, file)) {
      LOG_ERR("MESH", "Failed to open msg file for read: %s", filePath);
      return false;
    }

    uint8_t version = 0;
    file.read(&version, 1);
    uint16_t count = 0;
    file.read(reinterpret_cast<uint8_t*>(&count), 2);

    if (count >= MAX_MSGS_PER_THREAD) {
      file.close();
      if (!truncateOldMessages(filePath, count)) {
        LOG_ERR("MESH", "Failed to truncate old messages");
        return false;
      }
      // Re-read after truncation
      if (!Storage.openFileForRead("MESH", filePath, file)) {
        LOG_ERR("MESH", "Failed to reopen after truncation");
        return false;
      }
      file.read(&version, 1);
      file.read(reinterpret_cast<uint8_t*>(&count), 2);
    }

    // Read existing messages
    const size_t dataSize = count * sizeof(MeshCoreMessage);
    auto* buf = static_cast<uint8_t*>(malloc(dataSize));
    if (!buf) {
      LOG_ERR("MESH", "malloc failed for append: %zu bytes", dataSize);
      return false;
    }
    file.read(buf, dataSize);
    file.close();

    // Write back: header + existing messages + new message
    if (!Storage.openFileForWrite("MESH", filePath, file)) {
      free(buf);
      LOG_ERR("MESH", "Failed to open msg file for write: %s", filePath);
      return false;
    }
    file.write(&version, 1);
    uint16_t newCount = count + 1;
    file.write(reinterpret_cast<const uint8_t*>(&newCount), 2);
    file.write(buf, dataSize);
    free(buf);
    buf = nullptr;
    file.write(reinterpret_cast<const uint8_t*>(&msg), sizeof(MeshCoreMessage));
  } else {
    if (!Storage.openFileForWrite("MESH", filePath, file)) {
      LOG_ERR("MESH", "Failed to create msg file: %s", filePath);
      return false;
    }

    // Write header: version + count
    uint8_t version = MESHCORE_MSG_FILE_VERSION;
    file.write(&version, 1);
    uint16_t count = 1;
    file.write(reinterpret_cast<const uint8_t*>(&count), 2);

    // Write message
    file.write(reinterpret_cast<const uint8_t*>(&msg), sizeof(MeshCoreMessage));
  }

  return true;
}

bool MeshCoreMessageStore::truncateOldMessages(const char* filePath, uint16_t currentCount) {
  uint16_t keepCount = currentCount / 2;
  uint16_t skipCount = currentCount - keepCount;
  LOG_INF("MESH", "Truncating: drop %d oldest, keep %d newest", skipCount, keepCount);

  // Read the newest messages into a temporary buffer on heap
  // Each MeshCoreMessage is ~250 bytes, keepCount <= 100, so max ~25 KB
  auto* buf = static_cast<uint8_t*>(malloc(static_cast<size_t>(keepCount) * sizeof(MeshCoreMessage)));
  if (!buf) {
    LOG_ERR("MESH", "malloc failed for truncation buffer");
    return false;
  }

  HalFile src;
  if (!Storage.openFileForRead("MESH", filePath, src)) {
    free(buf);
    return false;
  }

  // Seek past header (3 bytes) + skipped messages
  uint32_t dataOffset = 3 + static_cast<uint32_t>(skipCount) * sizeof(MeshCoreMessage);
  src.seek(dataOffset);
  int bytesRead = src.read(buf, static_cast<size_t>(keepCount) * sizeof(MeshCoreMessage));
  src.close();

  if (bytesRead != static_cast<int>(static_cast<size_t>(keepCount) * sizeof(MeshCoreMessage))) {
    LOG_ERR("MESH", "Short read during truncation");
    free(buf);
    return false;
  }

  // Overwrite the file with header + kept messages
  HalFile dst;
  if (!Storage.openFileForWrite("MESH", filePath, dst)) {
    free(buf);
    return false;
  }

  uint8_t version = MESHCORE_MSG_FILE_VERSION;
  dst.write(&version, 1);
  dst.write(reinterpret_cast<const uint8_t*>(&keepCount), 2);
  dst.write(buf, static_cast<size_t>(keepCount) * sizeof(MeshCoreMessage));

  free(buf);
  buf = nullptr;
  return true;
}

uint16_t MeshCoreMessageStore::getMessageCount(const char* filePath) {
  HalFile file;
  if (!Storage.openFileForRead("MESH", filePath, file)) {
    return 0;
  }

  uint8_t version = 0;
  file.read(&version, 1);
  if (version != MESHCORE_MSG_FILE_VERSION) {
    LOG_ERR("MESH", "Bad msg file version: %d", version);
    return 0;
  }

  uint16_t count = 0;
  file.read(reinterpret_cast<uint8_t*>(&count), 2);
  return count;
}

bool MeshCoreMessageStore::loadMessages(const char* filePath, uint16_t offset, MeshCoreMessage* out, uint8_t count,
                                        uint8_t& loaded) {
  loaded = 0;
  HalFile file;
  if (!Storage.openFileForRead("MESH", filePath, file)) {
    return false;
  }

  uint8_t version = 0;
  file.read(&version, 1);
  if (version != MESHCORE_MSG_FILE_VERSION) {
    LOG_ERR("MESH", "Bad msg file version: %d", version);
    return false;
  }

  uint16_t totalCount = 0;
  file.read(reinterpret_cast<uint8_t*>(&totalCount), 2);

  if (offset >= totalCount) {
    return true;  // No messages at this offset
  }

  // Seek to offset position (header = 3 bytes)
  uint32_t seekPos = 3 + static_cast<uint32_t>(offset) * sizeof(MeshCoreMessage);
  file.seek(seekPos);

  uint8_t toRead = count;
  if (offset + toRead > totalCount) {
    toRead = static_cast<uint8_t>(totalCount - offset);
  }

  for (uint8_t i = 0; i < toRead; ++i) {
    int bytesRead = file.read(reinterpret_cast<uint8_t*>(&out[i]), sizeof(MeshCoreMessage));
    if (bytesRead != sizeof(MeshCoreMessage)) {
      LOG_ERR("MESH", "Short read at msg %d", i);
      break;
    }
    loaded++;
  }

  return loaded > 0;
}

// --- Channel messages ---

bool MeshCoreMessageStore::appendChannelMessage(uint8_t channelIdx, const MeshCoreMessage& msg) {
  char dirPath[64];
  buildChannelPath(channelIdx, dirPath, sizeof(dirPath));
  ensureDir(dirPath);

  char filePath[80];
  snprintf(filePath, sizeof(filePath), "%s/msgs.bin", dirPath);
  return appendMessage(filePath, msg);
}

uint16_t MeshCoreMessageStore::getChannelMessageCount(uint8_t channelIdx) {
  char dirPath[64];
  buildChannelPath(channelIdx, dirPath, sizeof(dirPath));
  char filePath[80];
  snprintf(filePath, sizeof(filePath), "%s/msgs.bin", dirPath);
  return getMessageCount(filePath);
}

bool MeshCoreMessageStore::loadChannelMessages(uint8_t channelIdx, uint16_t offset, MeshCoreMessage* out, uint8_t count,
                                               uint8_t& loaded) {
  char dirPath[64];
  buildChannelPath(channelIdx, dirPath, sizeof(dirPath));
  char filePath[80];
  snprintf(filePath, sizeof(filePath), "%s/msgs.bin", dirPath);
  return loadMessages(filePath, offset, out, count, loaded);
}

// --- Direct messages ---

bool MeshCoreMessageStore::appendDirectMessage(const uint8_t* pubkey32, const MeshCoreMessage& msg) {
  char dirPath[64];
  buildContactPath(pubkey32, dirPath, sizeof(dirPath));
  ensureDir(dirPath);

  char filePath[80];
  snprintf(filePath, sizeof(filePath), "%s/msgs.bin", dirPath);
  return appendMessage(filePath, msg);
}

uint16_t MeshCoreMessageStore::getDirectMessageCount(const uint8_t* pubkey32) {
  char dirPath[64];
  buildContactPath(pubkey32, dirPath, sizeof(dirPath));
  char filePath[80];
  snprintf(filePath, sizeof(filePath), "%s/msgs.bin", dirPath);
  return getMessageCount(filePath);
}

bool MeshCoreMessageStore::loadDirectMessages(const uint8_t* pubkey32, uint16_t offset, MeshCoreMessage* out,
                                              uint8_t count, uint8_t& loaded) {
  char dirPath[64];
  buildContactPath(pubkey32, dirPath, sizeof(dirPath));
  char filePath[80];
  snprintf(filePath, sizeof(filePath), "%s/msgs.bin", dirPath);
  return loadMessages(filePath, offset, out, count, loaded);
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
