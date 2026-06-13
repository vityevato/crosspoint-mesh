#include "MeshCoreClient.h"

#include "MeshCoreProtocol.h"

#include <Logging.h>
#include <NimBLEDevice.h>

#include <cstring>

// Nordic UART Service UUIDs
static const NimBLEUUID NUS_SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static const NimBLEUUID NUS_RX_UUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
static const NimBLEUUID NUS_TX_UUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

MeshCoreClient* MeshCoreClient::sInstance = nullptr;

// BLE client callbacks for security/pairing and disconnect detection
class MeshBleClientCallbacks : public NimBLEClientCallbacks {
  void onPassKeyEntry(NimBLEConnInfo& connInfo) override {
    uint32_t pin = 0;
    if (MeshCoreClient::sInstance) {
      pin = MeshCoreClient::sInstance->connectPin;
    }
    LOG_INF("MESH", "BLE passkey entry: injecting PIN %lu", (unsigned long)pin);
    NimBLEDevice::injectPassKey(connInfo, pin);
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    if (connInfo.isEncrypted()) {
      LOG_INF("MESH", "BLE pairing complete (encrypted=%d bonded=%d)",
              connInfo.isEncrypted(), connInfo.isBonded());
    } else {
      LOG_ERR("MESH", "BLE pairing failed — not encrypted");
    }
  }

  void onDisconnect(NimBLEClient* pClient, int reason) override {
    LOG_INF("MESH", "BLE disconnected, reason=%d", reason);
  }
};

static MeshBleClientCallbacks sBleCallbacks;

MeshCoreClient::MeshCoreClient() = default;

MeshCoreClient::~MeshCoreClient() { deinit(); }

bool MeshCoreClient::init() {
  if (sInstance) {
    LOG_ERR("MESH", "MeshCoreClient already initialized");
    return false;
  }
  sInstance = this;

  NimBLEDevice::init("CrossPoint");
  NimBLEDevice::setMTU(512);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  // BLE security: companion firmware requires encrypted MITM-authenticated link
  // (ESP_GATT_PERM_READ_ENC_MITM / ESP_GATT_PERM_WRITE_ENC_MITM on NUS chars).
  // Without this, all GATT writes and notification subscriptions are silently rejected.
  NimBLEDevice::setSecurityAuth(true, true, true);   // bonding, MITM, SC
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);  // we input the passkey

  // Create worker task and queue for blocking BLE operations
  workQueue = xQueueCreate(2, sizeof(WorkItem));
  if (!workQueue) {
    LOG_ERR("MESH", "Failed to create BLE work queue");
    NimBLEDevice::deinit(true);
    sInstance = nullptr;
    return false;
  }
  if (xTaskCreate(workerTaskFunc, "BleWorker", 4096, this, 1, &workerTaskHandle) != pdPASS) {
    LOG_ERR("MESH", "Failed to create BLE worker task");
    vQueueDelete(workQueue);
    workQueue = nullptr;
    NimBLEDevice::deinit(true);
    sInstance = nullptr;
    return false;
  }

  LOG_INF("MESH", "BLE initialized");
  return true;
}

void MeshCoreClient::deinit() {
  // Abort any in-progress scan
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan && scan->isScanning()) scan->stop();

  // Signal doConnect() on the worker task to abandon bleClient cleanup.
  // bleClient ownership transfers back to deinit() once the worker exits.
  cancelConnect = true;

  // Clear callbacks to prevent firing during shutdown
  stateCb = nullptr;
  msgCb = nullptr;
  contactCb = nullptr;
  advertCb = nullptr;
  channelCb = nullptr;

  // Interrupt any in-progress blocking connect/init so the worker can exit.
  // Only disconnect the link here — do NOT call NimBLEDevice::deleteClient()
  // yet, as the worker task may still be holding a reference to bleClient.
  if (bleClient && (state == BleConnectionState::CONNECTING ||
                    state == BleConnectionState::INITIALIZING)) {
    bleClient->disconnect();
  }

  // Shut down worker task
  if (workQueue) {
    WorkItem item{};
    item.type = WorkType::SHUTDOWN;
    xQueueSend(workQueue, &item, pdMS_TO_TICKS(1000));
    // Wait for worker to finish current operation and exit
    uint32_t start = millis();
    while (workerRunning && (millis() - start) < 15000) {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (workerRunning) {
      LOG_ERR("MESH", "Worker task stuck, force killing");
      vTaskDelete(workerTaskHandle);
      workerRunning = false;
    }
    vQueueDelete(workQueue);
    workQueue = nullptr;
  }
  workerTaskHandle = nullptr;

  // Worker has exited — safe to take ownership of bleClient and clean up.
  cancelConnect = false;
  disconnect();
  if (sInstance == this) {
    NimBLEDevice::deinit(true);
    sInstance = nullptr;
    LOG_INF("MESH", "BLE deinitialized");
  }
}

bool MeshCoreClient::startScan(uint32_t durationSec) {
  if (state != BleConnectionState::DISCONNECTED) {
    LOG_ERR("MESH", "Cannot scan: not disconnected");
    return false;
  }
  if (!workQueue) {
    LOG_ERR("MESH", "Worker not initialized");
    return false;
  }

  scanResultCount = 0;
  memset(scanResults, 0, sizeof(scanResults));
  setState(BleConnectionState::SCANNING);

  WorkItem item{};
  item.type = WorkType::SCAN;
  item.scanDurationSec = durationSec;
  if (xQueueSend(workQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
    LOG_ERR("MESH", "Failed to enqueue scan");
    setState(BleConnectionState::DISCONNECTED);
    return false;
  }
  return true;
}

void MeshCoreClient::doScan(uint32_t durationSec) {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);

  LOG_INF("MESH", "Starting BLE scan for %lu s", (unsigned long)durationSec);

  NimBLEScanResults results = scan->getResults(durationSec * 1000, false);

  for (int i = 0; i < results.getCount() && scanResultCount < MAX_SCAN_RESULTS; ++i) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    if (!device) continue;

    // Filter for devices advertising NUS service
    if (!device->isAdvertisingService(NUS_SERVICE_UUID)) continue;

    ScanResult& sr = scanResults[scanResultCount];
    const char* devName = device->getName().c_str();
    snprintf(sr.name, sizeof(sr.name), "%s", devName[0] ? devName : "Unknown");
    snprintf(sr.address, sizeof(sr.address), "%s", device->getAddress().toString().c_str());
    sr.addressType = device->getAddress().getType();
    sr.rssi = device->getRSSI();
    scanResultCount++;

    LOG_INF("MESH", "Found: %s [%s] RSSI=%d", sr.name, sr.address, sr.rssi);
  }

  if (scanResultCount == 0) {
    LOG_INF("MESH", "No MeshCore devices found");
  }

  setState(BleConnectionState::DISCONNECTED);
}

void MeshCoreClient::stopScan() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) {
    scan->stop();
  }
}

bool MeshCoreClient::connectTo(const char* bleAddress, uint8_t addressType) {
  if (state == BleConnectionState::CONNECTED || state == BleConnectionState::CONNECTING) {
    LOG_ERR("MESH", "Already connected or connecting");
    return false;
  }
  if (!workQueue) {
    LOG_ERR("MESH", "Worker not initialized");
    return false;
  }

  setState(BleConnectionState::CONNECTING);

  WorkItem item{};
  item.type = WorkType::CONNECT;
  snprintf(item.address, sizeof(item.address), "%s", bleAddress);
  item.addressType = addressType;
  if (xQueueSend(workQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
    LOG_ERR("MESH", "Failed to enqueue connect");
    setState(BleConnectionState::DISCONNECTED);
    return false;
  }
  return true;
}

void MeshCoreClient::failConnect(bool disconnectFirst) {
  if (!cancelConnect) {
    if (disconnectFirst && bleClient) bleClient->disconnect();
    if (bleClient) {
      NimBLEDevice::deleteClient(bleClient);
      bleClient = nullptr;
    }
  }
  rxChar = nullptr;
  txChar = nullptr;
  setState(BleConnectionState::DISCONNECTED);
}

void MeshCoreClient::doConnect(const char* bleAddress, uint8_t addressType) {
  LOG_INF("MESH", "Connecting to %s (type=%d)", bleAddress, addressType);

  bleClient = NimBLEDevice::createClient();
  if (!bleClient) {
    LOG_ERR("MESH", "Failed to create BLE client");
    setState(BleConnectionState::DISCONNECTED);
    return;
  }

  // Attach security callbacks (handles passkey entry during pairing)
  bleClient->setClientCallbacks(&sBleCallbacks, false);

  NimBLEAddress addr(std::string(bleAddress), addressType);
  if (!bleClient->connect(addr)) {
    LOG_ERR("MESH", "BLE connect failed");
    failConnect(false);
    return;
  }

  // Establish encrypted link BEFORE any GATT operations.
  // Companion firmware requires MITM-authenticated encryption on all NUS
  // characteristics; without this, subscribe/write are silently rejected.
  if (!bleClient->secureConnection()) {
    LOG_ERR("MESH", "BLE pairing/encryption failed");
    failConnect(true);
    return;
  }
  LOG_INF("MESH", "BLE link encrypted");

  // Discover NUS service
  NimBLERemoteService* svc = bleClient->getService(NUS_SERVICE_UUID);
  if (!svc) {
    LOG_ERR("MESH", "NUS service not found");
    failConnect(true);
    return;
  }

  rxChar = svc->getCharacteristic(NUS_RX_UUID);
  txChar = svc->getCharacteristic(NUS_TX_UUID);
  if (!rxChar || !txChar) {
    LOG_ERR("MESH", "NUS characteristics not found");
    failConnect(true);
    return;
  }

  // Subscribe to TX notifications (device -> app)
  // CCCD descriptor requires ATT Write Request (response=true) per BLE spec
  if (!txChar->subscribe(true, notifyCallback, true)) {
    LOG_ERR("MESH", "Failed to subscribe to TX notifications");
    failConnect(true);
    return;
  }
  LOG_DBG("MESH", "Subscribed to TX notifications");

  // Protocol handshake delay: allow device to process CCCD write
  // before sending commands (matches MeshMapper 500ms handshake delay)
  vTaskDelay(pdMS_TO_TICKS(500));

  // Save address for auto-reconnect
  snprintf(autoReconnectAddr, sizeof(autoReconnectAddr), "%s", bleAddress);
  autoReconnectAddrType = addressType;

  LOG_INF("MESH", "Connected, starting init sequence");
  setState(BleConnectionState::INITIALIZING);

  if (!runInitSequence()) {
    LOG_ERR("MESH", "Init sequence failed");
    disconnect();
    return;
  }

  setState(BleConnectionState::CONNECTED);
  LOG_INF("MESH", "Init complete: %s", companion.name);
}

void MeshCoreClient::disconnect() {
  if (bleClient) {
    if (bleClient->isConnected()) {
      bleClient->disconnect();
    }
    NimBLEDevice::deleteClient(bleClient);
    bleClient = nullptr;
  }
  rxChar = nullptr;
  txChar = nullptr;
  cmdCount = 0;
  cmdHead = 0;
  cmdTail = 0;
  cmdPending = false;
  rxHead = 0;
  rxTail = 0;

  if (state != BleConnectionState::DISCONNECTED) {
    setState(BleConnectionState::DISCONNECTED);
  }
}

bool MeshCoreClient::requestContacts() {
  uint8_t buf[1];
  size_t len = MeshProto::buildGetContacts(buf, sizeof(buf));
  return len > 0 && enqueueCmd(buf, len, MeshProto::PKT_CONTACT_START);
}

bool MeshCoreClient::requestChannel(uint8_t idx) {
  uint8_t buf[2];
  size_t len = MeshProto::buildGetChannel(buf, sizeof(buf), idx);
  return len > 0 && enqueueCmd(buf, len, MeshProto::PKT_CHANNEL_INFO);
}

bool MeshCoreClient::requestBattery() {
  uint8_t buf[1];
  size_t len = MeshProto::buildGetBattery(buf, sizeof(buf));
  return len > 0 && enqueueCmd(buf, len, MeshProto::PKT_BATTERY);
}

bool MeshCoreClient::requestMessages() {
  lastMessagePollTime = millis();  // reset periodic poll timer
  uint8_t buf[1];
  size_t len = MeshProto::buildGetMessage(buf, sizeof(buf));
  return len > 0 && enqueueCmd(buf, len, 0);  // Accept any response type
}

bool MeshCoreClient::sendChannelMessage(uint8_t channelIdx, const char* text) {
  uint8_t buf[CMD_BUF_SIZE];
  uint32_t ts = static_cast<uint32_t>(millis() / 1000);
  size_t len = MeshProto::buildSendChannelMsg(buf, sizeof(buf), channelIdx, ts, text);
  return len > 0 && enqueueCmd(buf, len, MeshProto::PKT_MSG_SENT);
}

bool MeshCoreClient::sendDirectMessage(const uint8_t* pubkey32, const char* text) {
  uint8_t buf[CMD_BUF_SIZE];
  uint32_t ts = static_cast<uint32_t>(millis() / 1000);
  size_t len = MeshProto::buildSendDirectMsg(buf, sizeof(buf), pubkey32, ts, text);
  return len > 0 && enqueueCmd(buf, len, MeshProto::PKT_MSG_SENT);
}

bool MeshCoreClient::setChannel(uint8_t idx, const char* name, const uint8_t* secret16) {
  uint8_t buf[50];
  size_t len = MeshProto::buildSetChannel(buf, sizeof(buf), idx, name, secret16);
  return len > 0 && enqueueCmd(buf, len, MeshProto::PKT_OK);
}

bool MeshCoreClient::deleteChannel(uint8_t idx) {
  const uint8_t emptySecret[16] = {};
  return setChannel(idx, "", emptySecret);
}

void MeshCoreClient::setStateCallback(StateCallback cb, void* ctx) {
  stateCb = cb;
  stateCbCtx = ctx;
}

void MeshCoreClient::setMessageCallback(MessageCallback cb, void* ctx) {
  msgCb = cb;
  msgCbCtx = ctx;
}

void MeshCoreClient::setContactCallback(ContactCallback cb, void* ctx) {
  contactCb = cb;
  contactCbCtx = ctx;
}

void MeshCoreClient::setAdvertCallback(AdvertCallback cb, void* ctx) {
  advertCb = cb;
  advertCbCtx = ctx;
}

void MeshCoreClient::setChannelCallback(ChannelCallback cb, void* ctx) {
  channelCb = cb;
  channelCbCtx = ctx;
}

void MeshCoreClient::setPinCallback(PinCallback cb, void* ctx) {
  pinCb = cb;
  pinCbCtx = ctx;
}

void MeshCoreClient::setAutoReconnectAddress(const char* addr, uint8_t addressType) {
  if (addr) {
    snprintf(autoReconnectAddr, sizeof(autoReconnectAddr), "%s", addr);
    autoReconnectAddrType = addressType;
  } else {
    autoReconnectAddr[0] = '\0';
    autoReconnectAddrType = 0;
  }
}

const char* MeshCoreClient::getAutoReconnectAddress() const { return autoReconnectAddr; }

uint8_t MeshCoreClient::getAutoReconnectAddressType() const { return autoReconnectAddrType; }

void MeshCoreClient::poll() {
  // runInitSequence() drains the ring-buffer from the worker task;
  // don't race with it by consuming entries here simultaneously.
  if (inInitSequence) return;

  // Check for connection loss
  if (state == BleConnectionState::CONNECTED && bleClient && !bleClient->isConnected()) {
    LOG_INF("MESH", "Connection lost");
    rxChar = nullptr;
    txChar = nullptr;
    NimBLEDevice::deleteClient(bleClient);
    bleClient = nullptr;
    cmdPending = false;
    cmdCount = 0;
    setState(BleConnectionState::DISCONNECTED);
    return;
  }

  // Process all received notifications (ring-buffer drain).
  // Processing in a loop ensures a full multi-packet burst (e.g. contact list)
  // is handled within a single poll() call rather than being split across frames.
  while (rxHead != rxTail) {
    uint8_t localBuf[RX_BUF_SIZE];
    size_t localLen = rxQueue[rxHead].len;
    memcpy(localBuf, rxQueue[rxHead].data, localLen);
    rxHead = (rxHead + 1) % RX_QUEUE_SIZE;  // Consume entry

    processResponse(localBuf, localLen);
  }

  // Check command timeout
  if (cmdPending && (millis() - cmdSentTime) > MeshProto::CMD_TIMEOUT_MS) {
    LOG_ERR("MESH", "Command timeout");
    cmdPending = false;
    sendNextCmd();
  }

  // Send next queued command
  if (!cmdPending && cmdCount > 0) {
    sendNextCmd();
  }

  // Periodic message poll: guard against PKT_MSGS_WAITING lost to rxBuf overwrite
  // during e-ink refresh (1-2 s). Only poll when idle (no pending/queued cmds).
  if (state == BleConnectionState::CONNECTED && !cmdPending && cmdCount == 0) {
    if (millis() - lastMessagePollTime > MESSAGE_POLL_INTERVAL_MS) {
      LOG_DBG("MESH", "Periodic message poll");
      requestMessages();
    }
  }
}

bool MeshCoreClient::enqueueCmd(const uint8_t* data, size_t len, uint8_t expectedResp) {
  if (cmdCount >= CMD_QUEUE_SIZE) {
    LOG_ERR("MESH", "Command queue full");
    return false;
  }
  if (len > CMD_BUF_SIZE) {
    LOG_ERR("MESH", "Command too large: %d", (int)len);
    return false;
  }

  CmdEntry& entry = cmdQueue[cmdTail];
  memcpy(entry.data, data, len);
  entry.len = len;
  entry.expectedResponse = expectedResp;
  cmdTail = (cmdTail + 1) % CMD_QUEUE_SIZE;
  cmdCount++;

  // If nothing pending, send immediately
  if (!cmdPending) {
    sendNextCmd();
  }
  return true;
}

bool MeshCoreClient::sendNextCmd() {
  if (cmdCount == 0) return false;
  if (!rxChar) {
    LOG_ERR("MESH", "Cannot send: not connected");
    return false;
  }

  CmdEntry& entry = cmdQueue[cmdHead];
  if (!rxChar->writeValue(static_cast<const uint8_t*>(entry.data), entry.len, true)) {
    LOG_ERR("MESH", "BLE write failed");
    return false;
  }

  cmdPending = true;
  cmdSentTime = millis();
  cmdHead = (cmdHead + 1) % CMD_QUEUE_SIZE;
  cmdCount--;
  return true;
}

void MeshCoreClient::processResponse(const uint8_t* data, size_t len) {
  if (len == 0) return;
  uint8_t pktType = data[0];

  // Clear pending flag for expected responses
  cmdPending = false;

  switch (pktType) {
    case MeshProto::PKT_SELF_INFO:
      MeshProto::parseSelfInfo(data, len, companion);
      LOG_INF("MESH", "Self info: %s", companion.name);
      break;

    case MeshProto::PKT_DEVICE_INFO:
      MeshProto::parseDeviceInfo(data, len, companion);
      LOG_INF("MESH", "Device: %s %s", companion.model, companion.version);
      break;

    case MeshProto::PKT_CHANNEL_INFO: {
      MeshCoreChannel ch = {};
      if (MeshProto::parseChannelInfo(data, len, ch)) {
        LOG_DBG("MESH", "Channel %d: %s", ch.index, ch.name);
        if (channelCb) channelCb(ch, channelCbCtx);
      }
      break;
    }

    case MeshProto::PKT_BATTERY:
      MeshProto::parseBattery(data, len, companion);
      LOG_DBG("MESH", "Battery: %d mV", companion.batteryMv);
      break;

    case MeshProto::PKT_CONTACT_START:
      LOG_DBG("MESH", "Contact list start");
      if (contactCb) contactCb(MeshCoreContact{}, false, contactCbCtx);
      break;

    case MeshProto::PKT_CONTACT: {
      MeshCoreContact contact = {};
      if (MeshProto::parseContact(data, len, contact)) {
        contact.isSaved = true;
        if (contactCb) contactCb(contact, false, contactCbCtx);
      }
      break;
    }

    case MeshProto::PKT_CONTACT_END:
      LOG_DBG("MESH", "Contact list end");
      if (contactCb) contactCb(MeshCoreContact{}, true, contactCbCtx);
      break;

    case MeshProto::PKT_MSGS_WAITING:
      LOG_DBG("MESH", "Messages waiting, polling");
      requestMessages();
      break;

    case MeshProto::PKT_CHANNEL_MSG:
    case MeshProto::PKT_CHANNEL_MSG_V3: {
      MeshCoreMessage msg = {};
      if (MeshProto::parseChannelMessage(data, len, msg)) {
        LOG_DBG("MESH", "Channel msg ch=%d: %.40s", msg.channelIdx, msg.text);
        msg.direction = MsgDirection::RECEIVED;
        msg.type = MsgType::CHANNEL;
        if (msgCb) msgCb(msg, msgCbCtx);
      } else {
        LOG_ERR("MESH", "Failed to parse channel msg (len=%d)", (int)len);
      }
      break;
    }

    case MeshProto::PKT_CONTACT_MSG:
    case MeshProto::PKT_CONTACT_MSG_V3: {
      MeshCoreMessage msg = {};
      if (MeshProto::parseContactMessage(data, len, msg)) {
        LOG_DBG("MESH", "Contact msg from %02X%02X%02X: %.40s",
                msg.pubkeyPrefix[0], msg.pubkeyPrefix[1], msg.pubkeyPrefix[2], msg.text);
        msg.direction = MsgDirection::RECEIVED;
        msg.type = MsgType::DIRECT;
        if (msgCb) msgCb(msg, msgCbCtx);
      } else {
        LOG_ERR("MESH", "Failed to parse contact msg (len=%d)", (int)len);
      }
      break;
    }

    case MeshProto::PKT_ADVERTISEMENT: {
      // PUSH_CODE_ADVERT: [0x80][pub_key:32] = 33 bytes.
      // This is a "contact seen again" notification, NOT a full contact dump.
      // Extract pubkey and notify caller; do NOT call parseContact here.
      if (len < 33) {
        LOG_DBG("MESH", "ADVERT too short (%d)", (int)len);
        break;
      }
      if (advertCb) {
        MeshCoreContact node = {};
        memcpy(node.publicKey, data + 1, 32);
        advertCb(node, advertCbCtx);
      }
      break;
    }

    case MeshProto::PKT_NEW_ADVERT: {
      // PUSH_CODE_NEW_ADVERT: writeContactRespFrame format (148 bytes).
      MeshCoreContact contact = {};
      if (MeshProto::parseContact(data, len, contact)) {
        LOG_DBG("MESH", "New advert: %s", contact.name);
        contact.isSaved = false;
        if (contactCb) contactCb(contact, false, contactCbCtx);
      }
      break;
    }

    case 0x81:  // PUSH_CODE_PATH_UPDATED
    case 0x84:  // PUSH_CODE_RAW_DATA
    case 0x85:  // PUSH_CODE_LOGIN_SUCCESS
    case 0x86:  // PUSH_CODE_LOGIN_FAIL
    case 0x87:  // PUSH_CODE_STATUS_RESPONSE
    case 0x88:  // PUSH_CODE_LOG_RX_DATA (raw LoRa packet log, verbose)
    case 0x89:  // PUSH_CODE_TRACE_DATA
      LOG_DBG("MESH", "Push 0x%02X len=%d (ignored)", pktType, (int)len);
      break;

    case MeshProto::PKT_ACK: {
      uint8_t ackHash[4];
      if (MeshProto::parseAck(data, len, ackHash)) {
        LOG_DBG("MESH", "ACK received");
      }
      break;
    }

    case MeshProto::PKT_MSG_SENT: {
      uint32_t ackTag, timeout;
      if (MeshProto::parseMsgSent(data, len, ackTag, timeout)) {
        LOG_DBG("MESH", "Msg sent, ack tag=%lu timeout=%lu", (unsigned long)ackTag,
                (unsigned long)timeout);
      }
      break;
    }

    case MeshProto::PKT_NO_MORE_MSGS:
      LOG_DBG("MESH", "No more messages");
      break;

    case MeshProto::PKT_OK:
      LOG_DBG("MESH", "OK response");
      break;

    case MeshProto::PKT_ERROR:
      LOG_ERR("MESH", "Error response from companion");
      break;

    default:
      LOG_DBG("MESH", "Unknown packet type: 0x%02X len=%d", pktType, (int)len);
      break;
  }
}

bool MeshCoreClient::runInitSequence() {
  inInitSequence = true;

  uint8_t buf[64];
  size_t len;

  // Per MeshCore protocol spec, APP_START should be the first command.
  // Send it with Write With Response (response=true) for reliable delivery —
  // the companion's RX characteristic advertises PROPERTY_WRITE only.
  len = MeshProto::buildAppStart(buf, sizeof(buf));
  LOG_DBG("MESH", "Sending APP_START (%d bytes)", (int)len);
  if (len == 0 || !rxChar->writeValue(static_cast<const uint8_t*>(buf), len, true)) {
    LOG_ERR("MESH", "Failed to send APP_START");
    inInitSequence = false;
    return false;
  }

  // Gap between writes: companion processes one command per loop iteration
  // and enforces 60ms minimum interval between BLE notifications
  vTaskDelay(pdMS_TO_TICKS(200));

  // Send DEVICE_QUERY second (also Write With Response for reliability)
  len = MeshProto::buildDeviceQuery(buf, sizeof(buf));
  LOG_DBG("MESH", "Sending DEVICE_QUERY (%d bytes)", (int)len);
  if (len == 0 || !rxChar->writeValue(static_cast<const uint8_t*>(buf), len, true)) {
    LOG_ERR("MESH", "Failed to send DEVICE_QUERY");
    inInitSequence = false;
    return false;
  }

  // Wait for both responses (SELF_INFO and DEVICE_INFO, may arrive in any order).
  // Drain the ring-buffer instead of the old single-slot rxBuf to avoid losing
  // any packets that arrive while we wait.
  bool gotDeviceInfo = false;
  bool gotSelfInfo = false;
  uint32_t start = millis();
  while ((!gotDeviceInfo || !gotSelfInfo) &&
         (millis() - start) < MeshProto::CMD_TIMEOUT_MS) {
    while (rxHead != rxTail) {
      uint8_t localBuf[RX_BUF_SIZE];
      size_t localLen = rxQueue[rxHead].len;
      memcpy(localBuf, rxQueue[rxHead].data, localLen);
      uint8_t pktType = localLen > 0 ? localBuf[0] : 0;
      rxHead = (rxHead + 1) % RX_QUEUE_SIZE;

      processResponse(localBuf, localLen);
      if (pktType == MeshProto::PKT_DEVICE_INFO) gotDeviceInfo = true;
      if (pktType == MeshProto::PKT_SELF_INFO) gotSelfInfo = true;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (!gotDeviceInfo) {
    LOG_ERR("MESH", "Timeout waiting for DEVICE_INFO");
    inInitSequence = false;
    return false;
  }
  if (!gotSelfInfo) {
    LOG_ERR("MESH", "Timeout waiting for SELF_INFO");
    inInitSequence = false;
    return false;
  }

  LOG_INF("MESH", "Device: %s %s pin=%lu", companion.model, companion.version,
          (unsigned long)companion.blePin);

  if (companion.blePin != 0 && pinCb) {
    pinCb(companion.blePin, pinCbCtx);
  }

  // Queue GET_CONTACTS and GET_CHANNEL x maxChannels
  requestContacts();
  for (uint8_t i = 0; i < companion.maxChannels && i < 8; ++i) {
    requestChannel(i);
  }

  // Also request pending messages
  requestMessages();

  inInitSequence = false;
  return true;
}

void MeshCoreClient::workerTaskFunc(void* param) {
  auto* self = static_cast<MeshCoreClient*>(param);
  self->workerRunning = true;

  WorkItem item{};
  while (xQueueReceive(self->workQueue, &item, portMAX_DELAY) == pdTRUE) {
    if (item.type == WorkType::SHUTDOWN) break;
    switch (item.type) {
      case WorkType::SCAN:
        self->doScan(item.scanDurationSec);
        break;
      case WorkType::CONNECT:
        self->doConnect(item.address, item.addressType);
        break;
      default:
        break;
    }
  }

  self->workerRunning = false;
  vTaskDelete(nullptr);
}

void MeshCoreClient::notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData,
                                     size_t length, bool isNotify) {
  (void)pChar;
  (void)isNotify;
  if (!sInstance || length == 0 || length > RX_BUF_SIZE) {
    if (sInstance && length > RX_BUF_SIZE) {
      LOG_ERR("MESH", "Notify too large: %d > %d", (int)length, (int)RX_BUF_SIZE);
    }
    return;
  }
  LOG_DBG("MESH", "Notify rx: %d bytes, first=0x%02X", (int)length, pData[0]);

  // Push into ring-buffer. Drop on overflow (producer is faster than consumer).
  uint8_t nextTail = (sInstance->rxTail + 1) % RX_QUEUE_SIZE;
  if (nextTail == sInstance->rxHead) {
    LOG_ERR("MESH", "RX queue full, dropping notification (0x%02X)", pData[0]);
    return;
  }
  RxEntry& entry = sInstance->rxQueue[sInstance->rxTail];
  memcpy(entry.data, pData, length);
  entry.len = static_cast<uint16_t>(length);
  sInstance->rxTail = nextTail;  // Publish entry to consumer
}

void MeshCoreClient::setState(BleConnectionState newState) {
  if (state == newState) return;
  state = newState;
  if (stateCb) stateCb(state, stateCbCtx);
}
