// MockSession implementation — JSON loading and mock lifecycle.
// Separate .cpp file so ArduinoJson includes only affect src/ compilation
// units, not lib/ code that includes NimBLEDevice.h.
#ifdef SIMULATOR

#include "MockSession.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

// Static member definitions
MockCompanion* MockSession::companions = nullptr;
uint8_t MockSession::companionCount = 0;
uint16_t MockSession::injectContactCounter = 0;
uint8_t MockSession::injectChannelIdx = 8;
// Free function (declared in NimBLEDevice.h) to handle CMD_ADD_UPDATE_CONTACT.
// Defined here to access MockSession without circular include.
void mockHandleAddUpdateContact(NimBLERemoteCharacteristic* txChar, const uint8_t* data, size_t len) {
  if (!txChar || len < 136) return;

  // Auto-cycle through responses on each call: OK → ERROR → TIMEOUT → DELAY_OK → …
  static uint8_t modeCounter = 0;
  uint8_t mode = modeCounter;
  modeCounter = (modeCounter + 1) % 4;

  char nameBuf[33] = {};
  size_t nameOff = 1 + 32 + 1 + 1 + 1 + 64;
  if (nameOff + 32 <= len) {
    memcpy(nameBuf, data + nameOff, 32);
    nameBuf[32] = '\0';
  }

  switch (mode) {
    case 0:  // OK
      LOG_INF("MOCK", "CMD_ADD_UPDATE_CONTACT \"%s\" → OK", nameBuf);
      {
        uint8_t ok = 0x00;
        txChar->injectRawPacket(&ok, 1);
      }
      break;
    case 1:  // ERROR
      LOG_INF("MOCK", "CMD_ADD_UPDATE_CONTACT \"%s\" → ERROR", nameBuf);
      {
        uint8_t err = 0x01;
        txChar->injectRawPacket(&err, 1);
      }
      break;
    case 2:  // TIMEOUT
      LOG_INF("MOCK", "CMD_ADD_UPDATE_CONTACT \"%s\" → (no response / timeout)", nameBuf);
      break;
    case 3:  // DELAY_OK
      sPendingEcho.type = PendingEchoType::ADD_CONTACT_OK;
      sPendingEcho.requestTimeMs = millis();
      LOG_INF("MOCK", "CMD_ADD_UPDATE_CONTACT \"%s\" → OK (delayed 1.5 s)", nameBuf);
      break;
  }
}

// Free function (declared in NimBLEDevice.h) to handle CMD_REMOVE_CONTACT.
// Has its own independent mode counter for testing remove ops in isolation.
void mockHandleRemoveContact(NimBLERemoteCharacteristic* txChar, const uint8_t* data, size_t len) {
  if (!txChar || len < 33) return;

  // Auto-cycle through responses on each call: OK → ERROR → TIMEOUT → DELAY_OK → …
  static uint8_t modeCounter = 0;
  uint8_t mode = modeCounter;
  modeCounter = (modeCounter + 1) % 4;

  // Log hex prefix of the pubkey for traceability
  char keyLabel[7] = {};
  snprintf(keyLabel, sizeof(keyLabel), "%02X%02X%02X", data[1], data[2], data[3]);

  switch (mode) {
    case 0:  // OK
      LOG_INF("MOCK", "CMD_REMOVE_CONTACT %s... → OK", keyLabel);
      {
        uint8_t ok = 0x00;
        txChar->injectRawPacket(&ok, 1);
      }
      break;
    case 1:  // ERROR
      LOG_INF("MOCK", "CMD_REMOVE_CONTACT %s... → ERROR", keyLabel);
      {
        // Companion error frame format: [PKT_ERROR(=1), err_code].
        // ERR_CODE_NOT_FOUND in companion is 2.
        uint8_t errFrame[2];
        errFrame[0] = 0x01;  // MeshProto::PKT_ERROR
        errFrame[1] = 0x02;  // ERR_CODE_NOT_FOUND
        txChar->injectRawPacket(errFrame, 2);
      }
      break;
    case 2:  // TIMEOUT
      LOG_INF("MOCK", "CMD_REMOVE_CONTACT %s... → (no response / timeout)", keyLabel);
      break;
    case 3:                                                 // DELAY_OK
      sPendingEcho.type = PendingEchoType::ADD_CONTACT_OK;  // reuse OK echo slot
      sPendingEcho.requestTimeMs = millis();
      LOG_INF("MOCK", "CMD_REMOVE_CONTACT %s... → OK (delayed 1.5 s)", keyLabel);
      break;
  }
}

bool MockSession::loadMockConfig(const char* jsonPath) {
  // Clean up any previously loaded config
  unloadMockConfig();

  String json = Storage.readFile(jsonPath);
  if (json.isEmpty()) {
    LOG_INF("MOCK", "no config file at %s, mock inactive", jsonPath);
    return false;
  }

  JsonDocument doc;
  // Use c_str() to pass a const char* to ArduinoJson, bypassing the
  // simulator's custom String::read() which returns signed char.
  // Without this, bytes >127 (Cyrillic UTF-8 etc.) become negative ints
  // and are misinterpreted as end-of-input (= IncompleteInput error).
  auto error = deserializeJson(doc, json.c_str());
  if (error) {
    LOG_ERR("MOCK", "JSON parse error at %s: %s", jsonPath, error.c_str());
    return false;
  }

  JsonArrayConst compArr = doc["companions"];
  if (compArr.isNull() || compArr.size() == 0) {
    LOG_ERR("MOCK", "no companions array in config");
    return false;
  }

  uint8_t count = std::min(static_cast<uint8_t>(compArr.size()), MOCK_MAX_COMPANIONS);
  auto* comps = new (std::nothrow) MockCompanion[count];
  if (!comps) {
    LOG_ERR("MOCK", "OOM allocating %d companions", count);
    return false;
  }

  uint8_t validCount = 0;
  for (uint8_t i = 0; i < count; ++i) {
    MockCompanion tmp;
    if (parseCompanion(compArr[i], tmp) && tmp.isValid()) {
      comps[validCount] = tmp;
      ++validCount;
    } else {
      LOG_ERR("MOCK",
              "companion[%d] invalid (missing name or ble_address), "
              "skipping",
              i);
    }
  }

  if (validCount == 0) {
    LOG_ERR("MOCK", "no valid companions in config");
    delete[] comps;
    return false;
  }

  companions = comps;
  companionCount = validCount;
  resetInjectCounters();
  LOG_INF("MOCK", "config loaded, %d companion(s)", validCount);
  return true;
}

void MockSession::unloadMockConfig() {
  if (companions) {
    delete[] companions;
    companions = nullptr;
  }
  companionCount = 0;
}

bool MockSession::parseCompanion(JsonObjectConst obj, MockCompanion& out) {
  auto name = obj["name"] | "";
  auto addr = obj["ble_address"] | "";
  strncpy(out.name, name, sizeof(out.name) - 1);
  strncpy(out.bleAddress, addr, sizeof(out.bleAddress) - 1);

  out.addressType = obj["address_type"] | 0;
  out.rssi = obj["rssi"] | 0;
  out.blePin = obj["ble_pin"] | 0U;

  auto pk = obj["public_key"] | "";
  strncpy(out.publicKey, pk, sizeof(out.publicKey) - 1);

  auto fw = obj["firmware_build"] | "";
  strncpy(out.firmwareBuild, fw, sizeof(out.firmwareBuild) - 1);

  auto model = obj["model"] | "";
  strncpy(out.model, model, sizeof(out.model) - 1);

  auto ver = obj["version"] | "";
  strncpy(out.version, ver, sizeof(out.version) - 1);

  out.batteryMv = obj["battery_mv"] | 0;
  out.storageUsedKb = obj["storage_used_kb"] | 0;
  out.storageTotalKb = obj["storage_total_kb"] | 0;
  out.radioFreq = obj["radio_freq"] | 0.0f;
  out.radioBw = obj["radio_bw"] | 0.0f;
  out.radioSf = obj["radio_sf"] | 0;
  out.radioCr = obj["radio_cr"] | 0;
  out.maxContacts = obj["max_contacts"] | 0;
  out.maxChannels = obj["max_channels"] | 8;

  // Parse contacts array
  JsonArrayConst contactsArr = obj["contacts"];
  if (!contactsArr.isNull()) {
    out.contactCount = std::min(static_cast<uint16_t>(contactsArr.size()), MOCK_MAX_CONTACTS);
    for (uint16_t i = 0; i < out.contactCount; ++i) {
      parseContact(contactsArr[i], out.contacts[i]);
    }
  }

  // Parse channels array
  JsonArrayConst channelsArr = obj["channels"];
  if (!channelsArr.isNull()) {
    out.channelCount = std::min(static_cast<uint8_t>(channelsArr.size()), MOCK_MAX_CHANNELS);
    for (uint8_t i = 0; i < out.channelCount; ++i) {
      parseChannel(channelsArr[i], out.channels[i]);
    }
  }

  // Parse discovered_nodes array
  JsonArrayConst discoveredArr = obj["discovered_nodes"];
  if (!discoveredArr.isNull()) {
    out.discoveredNodeCount = std::min(static_cast<uint8_t>(discoveredArr.size()), MOCK_MAX_DISCOVERED_NODES);
    for (uint8_t i = 0; i < out.discoveredNodeCount; ++i) {
      parseDiscoveredNode(discoveredArr[i], out.discoveredNodes[i]);
    }
  }

  return true;  // validation done by caller via isValid()
}

void MockSession::parseContact(JsonObjectConst obj, MockContact& out) {
  auto name = obj["name"] | "";
  strncpy(out.name, name, sizeof(out.name) - 1);

  auto pk = obj["public_key"] | "";
  strncpy(out.publicKey, pk, sizeof(out.publicKey) - 1);

  out.type = obj["type"] | 0;
  out.flags = obj["flags"] | 0;
  out.lastSeen = obj["last_seen"] | 0U;
  out.pathLength = obj["path_length"] | 0;
  out.snr = obj["snr"] | 0;
}

void MockSession::parseChannel(JsonObjectConst obj, MockChannel& out) {
  auto name = obj["name"] | "";
  strncpy(out.name, name, sizeof(out.name) - 1);

  out.type = obj["type"] | 0;
  out.unreadCount = obj["unread_count"] | 0;

  // Parse messages array
  JsonArrayConst msgsArr = obj["messages"];
  if (!msgsArr.isNull()) {
    out.messageCount = std::min(static_cast<uint8_t>(msgsArr.size()), MOCK_MAX_MESSAGES);
    for (uint8_t i = 0; i < out.messageCount; ++i) {
      parseMessage(msgsArr[i], out.messages[i]);
    }
  }
}

void MockSession::parseMessage(JsonObjectConst obj, MockMessage& out) {
  auto text = obj["text"] | "";
  strncpy(out.text, text, sizeof(out.text) - 1);

  out.direction = obj["direction"] | 0;
  out.timestamp = obj["timestamp"] | 0U;
}

void MockSession::parseDiscoveredNode(JsonObjectConst obj, MockDiscoveredNode& out) {
  auto pk = obj["public_key"] | "";
  strncpy(out.publicKey, pk, sizeof(out.publicKey) - 1);

  auto name = obj["name"] | "";
  strncpy(out.name, name, sizeof(out.name) - 1);

  out.type = obj["type"] | 0;
  out.flags = obj["flags"] | 0;
  out.lastSeen = obj["last_seen"] | 0U;
  out.pathLength = obj["path_length"] | 0;
  out.snr = obj["snr"] | 0;
}

#endif  // SIMULATOR
