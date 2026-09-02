#include "MeshCoreClient.h"

#include <Logging.h>
#include <NimBLEDevice.h>

#include <cstring>

#include "MeshCoreClock.h"
#include "MeshCoreProtocol.h"

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
      LOG_INF("MESH", "BLE pairing complete (encrypted=%d bonded=%d)", connInfo.isEncrypted(), connInfo.isBonded());
    } else {
      LOG_ERR("MESH", "BLE pairing failed — not encrypted (authenticated=%d keySize=%d)", connInfo.isAuthenticated(),
              connInfo.getSecKeySize());
    }
  }

  void onDisconnect(NimBLEClient* pClient, int reason) override {
    // reason=520 = BLE_HS_ECONNTERM(0x08) = HCI Connection Timeout (supervision timeout).
    // Most common cause: stale bond or pairing race on ESP32-C3 controller.
    LOG_INF("MESH", "BLE disconnected, reason=%d (0x%04X)", reason, reason);
  }
};

static MeshBleClientCallbacks sBleCallbacks;

namespace {

/// Marks a worker-side blocking operation (doConnect/doScan) as in flight so
/// disconnect() can abort and wait for it before tearing the client down.
/// Cleared automatically on every exit path.
struct InFlightScope {
  volatile bool& flag;
  explicit InFlightScope(volatile bool& f) : flag(f) { flag = true; }
  ~InFlightScope() { flag = false; }
};

}  // namespace

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

  // Clear any stale bonds from a previous session.
  // If the companion lost its bond (reset / fw update), NimBLE tries the old
  // LTK on reconnection, encryption fails, and the link drops with HCI timeout
  // (reason=520 / BLE_HS_ECONNTERM + BLE_ERR_CONN_TIMEOUT).  Starting fresh
  // forces a full pairing cycle (passkey entry), which is the safe default.
  NimBLEDevice::deleteAllBonds();

  // BLE security: companion firmware requires encrypted MITM-authenticated link
  // (ESP_GATT_PERM_READ_ENC_MITM / ESP_GATT_PERM_WRITE_ENC_MITM on NUS chars).
  // Without this, all GATT writes and notification subscriptions are silently rejected.
  NimBLEDevice::setSecurityAuth(true, true, true);          // bonding, MITM, SC
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
  heardCb = nullptr;
  // Clear all active trackers
  for (auto& t : _trackers) t.active = false;
  _pendingDm = {};

  // Interrupt any in-progress blocking connect/init so the worker can exit.
  // Only interrupt the link here — do NOT call NimBLEDevice::deleteClient()
  // yet, as the worker task may still be holding a reference to bleClient.
  if (bleClient && state == BleConnectionState::CONNECTING) {
    // A connect() attempt is in flight. bleClient->disconnect() (a plain
    // ble_gap_terminate) does NOT abort a pending connection attempt, so the
    // worker stays blocked inside connect() and gets force-killed 15 s later,
    // after which NimBLEDevice::deinit(true) races the host task still
    // processing the connect event and crashes (esp_nimble_disable vs
    // handleGapEvent/connect-cancelled). cancelConnect() sends
    // ble_gap_conn_cancel, which unblocks connect() and lets the worker exit
    // cleanly before the controller is torn down.
    bleClient->cancelConnect();
  } else if (bleClient && state == BleConnectionState::INITIALIZING) {
    // Link is established and the init handshake is in progress — terminating
    // the link makes runInitSequence() fail and the worker exit.
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
      // After force-kill, bleClient is in an indeterminate state — the
      // worker was blocked inside a NimBLE call (connect/secureConnection/
      // subscribe).  Null it out so disconnect() below is a no-op;
      // NimBLEDevice::deinit(true) will clean up the entire stack.
      bleClient = nullptr;
      rxChar = nullptr;
      txChar = nullptr;
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
  InFlightScope scanScope(scanInFlight);

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
    // NimBLE advertised device name is returned as a temporary object.
    // Keep it alive long enough for c_str() usage.
    std::string devNameStr = device->getName();
    const char* devName = devNameStr.c_str();
    snprintf(sr.name, sizeof(sr.name), "%s", (devName && devName[0]) ? devName : "Unknown");
    snprintf(sr.address, sizeof(sr.address), "%s", device->getAddress().toString().c_str());
    sr.addressType = device->getAddress().getType();
    sr.rssi = device->getRSSI();
    scanResultCount++;

    LOG_INF("MESH", "Found: %s [%s] RSSI=%d", sr.name, sr.address, sr.rssi);
  }

  if (scanResultCount == 0) {
    LOG_INF("MESH", "No MeshCore devices found");
  }

  // Release the NimBLE-layer advertisement objects now that the results are
  // copied into scanResults[]. NimBLEScan keeps every seen advertiser on the
  // heap until the next scan or BLE deinit; on the first-ever entry the scan
  // runs right before connect, so those retained frames (a few KB) would
  // otherwise stay allocated and push free heap below
  // MESHCORE_CONTACT_HEAP_RESERVE exactly when the contact list arrives,
  // dropping every contact (observed as an empty list on first connect; the
  // reconnect path skips the scan and therefore worked). Safe to clear here:
  // `results` must not be touched after this call (it aliases the same
  // devices), and no other code reads NimBLE scan results afterwards.
  scan->clearResults();

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
  // Mark the whole connect sequence (including init) as in flight so
  // disconnect()/deinit() abort and wait instead of freeing the client
  // underneath this worker task.
  InFlightScope connectScope(connectInFlight);

  // Skip stale CONNECT items that were queued before disconnect() ran:
  // the state machine already moved on, and running a full connect now
  // would clobber the new state (e.g. SCANNING set by startScan()).
  if (state != BleConnectionState::CONNECTING) {
    LOG_ERR("MESH", "Stale connect item skipped (state=%d)", static_cast<int>(state));
    return;
  }

  const uint32_t tStart = millis();
  LOG_INF("MESH", "Connecting to %s (type=%d)", bleAddress, addressType);
  LOG_DBG("MESH", "doConnect: heap=%d", (int)ESP.getFreeHeap());

  // Clean up any client left behind by a previous aborted attempt.
  if (bleClient) {
    NimBLEDevice::deleteClient(bleClient);
    bleClient = nullptr;
  }

  bleClient = NimBLEDevice::createClient();
  if (!bleClient) {
    LOG_ERR("MESH", "Failed to create BLE client");
    setState(BleConnectionState::DISCONNECTED);
    return;
  }

  // Abort when the main task cancels the attempt (disconnect/deinit) or the
  // link drops mid-sequence. When cancelConnect is set, failConnect() leaves
  // the client for the main task to tear down.
  const auto linkLost = [this]() { return cancelConnect || (bleClient && !bleClient->isConnected()); };

  // Attach security callbacks (handles passkey entry during pairing)
  bleClient->setClientCallbacks(&sBleCallbacks, false);

#ifdef SIMULATOR
  // Propagate the PIN to the mock layer so it can validate against companion's blePin
  NimBLEDevice::setConnectPin(connectPin);
#endif

  NimBLEAddress addr(std::string(bleAddress), addressType);

  LOG_DBG("MESH", "doConnect: calling bleClient->connect() (blocks; NimBLE default timeout)...");
  const bool connectOk = bleClient->connect(addr);
  LOG_DBG("MESH", "doConnect: connect() returned=%d after %lu ms", connectOk ? 1 : 0,
          (unsigned long)(millis() - tStart));
  if (!connectOk) {
    LOG_ERR("MESH", "BLE connect failed");
    failConnect(false);
    return;
  }

  LOG_INF("MESH", "BLE link established, waiting 150ms before pairing");

  // Small gap between HCI connect and pairing initiation.  Some ESP32-C3 BLE
  // controller revisions drop the link (reason=520 / HCI timeout) if pairing
  // starts too fast — the controller's LL state machine may still be settling.
  vTaskDelay(pdMS_TO_TICKS(150));
  if (linkLost()) {
    LOG_INF("MESH", "doConnect aborted: link lost or cancelled");
    failConnect(true);
    return;
  }

  // Establish encrypted link BEFORE any GATT operations.
  // Companion firmware requires MITM-authenticated encryption on all NUS
  // characteristics; without this, subscribe/write are silently rejected.
  LOG_INF("MESH", "Starting secure connection (pairing) with PIN %lu", (unsigned long)connectPin);
  const bool pairOk = bleClient->secureConnection();
  LOG_DBG("MESH", "secureConnection() returned=%d after %lu ms", pairOk ? 1 : 0, (unsigned long)(millis() - tStart));
  if (!pairOk) {
    LOG_ERR("MESH", "BLE pairing/encryption failed");
    failConnect(true);
    return;
  }
  if (linkLost()) {
    LOG_INF("MESH", "doConnect aborted: link lost or cancelled");
    failConnect(true);
    return;
  }
  NimBLEConnInfo connInfo = bleClient->getConnInfo();
  LOG_INF("MESH", "BLE link encrypted (bonded=%d timeout=%d)", connInfo.isBonded(), connInfo.getConnTimeout());

  // Discover NUS service
  LOG_DBG("MESH", "doConnect: discovering NUS service (elapsed %lu ms)", (unsigned long)(millis() - tStart));
  NimBLERemoteService* svc = bleClient->getService(NUS_SERVICE_UUID);
  if (!svc) {
    LOG_ERR("MESH", "NUS service not found");
    failConnect(true);
    return;
  }

  rxChar = svc->getCharacteristic(NUS_RX_UUID);
  txChar = svc->getCharacteristic(NUS_TX_UUID);
  LOG_DBG("MESH", "doConnect: NUS RX/TX characteristics found (elapsed %lu ms)", (unsigned long)(millis() - tStart));
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
  if (linkLost()) {
    LOG_INF("MESH", "doConnect aborted: link lost or cancelled");
    failConnect(true);
    return;
  }

  // Protocol handshake delay: allow device to process CCCD write
  // before sending commands (matches MeshMapper 500ms handshake delay)
  vTaskDelay(pdMS_TO_TICKS(500));
  LOG_DBG("MESH", "doConnect: handshake delay done (elapsed %lu ms)", (unsigned long)(millis() - tStart));
  if (linkLost()) {
    LOG_INF("MESH", "doConnect aborted: link lost or cancelled");
    failConnect(true);
    return;
  }

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
  // A connect/scan may be in flight on the worker task. Abort it and wait
  // for the worker to release bleClient before tearing the client down —
  // deleteClient() while doConnect() still holds it is a use-after-free
  // (observed on device as a null-rxChar panic in the APP_START write).
  const bool fromWorker = (xTaskGetCurrentTaskHandle() == workerTaskHandle);
  bool skipClientTeardown = false;

  if (connectInFlight && !fromWorker) {
    cancelConnect = true;
    if (bleClient) {
      if (state == BleConnectionState::CONNECTING) {
        // Unblock a pending connect attempt (ble_gap_conn_cancel).
        bleClient->cancelConnect();
      } else if (bleClient->isConnected()) {
        // Link is up (INITIALIZING stage) — dropping it makes the worker's
        // blocking writes/reads fail fast so doConnect can abort.
        bleClient->disconnect();
      }
    }
    uint32_t start = millis();
    while (connectInFlight && (millis() - start) < 15000) {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    cancelConnect = false;
    if (connectInFlight) {
      // Pathological: the worker is stuck inside a NimBLE call. Leave the
      // client alone — the worker aborts via its own guards when the call
      // returns and cleans up (failConnect deletes the client).
      LOG_ERR("MESH", "disconnect: connect still in flight after 15 s, skipping client teardown");
      skipClientTeardown = true;
    }
  }

  if (scanInFlight && !fromWorker) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan && scan->isScanning()) scan->stop();
    uint32_t start = millis();
    while (scanInFlight && (millis() - start) < 15000) {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (scanInFlight) {
      LOG_ERR("MESH", "disconnect: scan still in flight after 15 s");
    }
  }

  if (bleClient && !skipClientTeardown) {
    if (bleClient->isConnected()) {
      bleClient->disconnect();
    }
    NimBLEDevice::deleteClient(bleClient);
    bleClient = nullptr;

    // NimBLEDevice::deleteClient() defers deletion when the client is in
    // CONNECTED/DISCONNECTING state (sets deleteOnDisconnect flag, returns
    // without nulling the m_pClients slot).  Yield long enough for the
    // NimBLE host task to process the disconnect event and complete the
    // deferred delete, freeing the slot.  Without this, the next
    // deinit(true) stops the host before the slot is cleared, and
    // createClient() fails permanently (NIMBLE_MAX_CONNECTIONS=1).
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  // On the pathological timeout path the worker may still be inside
  // doConnect()/runInitSequence() using rxChar — leave the pointers for its
  // own cleanup (failConnect nulls them). The enqueueCmd state guard blocks
  // new commands once the state is reset below.
  if (!skipClientTeardown) {
    rxChar = nullptr;
    txChar = nullptr;
  }
  cmdCount = 0;
  cmdHead = 0;
  cmdTail = 0;
  cmdPending = false;
  cmdExpectedResponse = 0;
  rxHead = 0;
  rxTail = 0;
  _pendingDm = {};
  initialRequestsPending = false;
  msgDrainCount = 0;
  channelSyncActive = false;
  channelSyncNext = 0;
  channelSyncTotal = 0;

  if (state != BleConnectionState::DISCONNECTED) {
    setState(BleConnectionState::DISCONNECTED);
  }

  cmdInFlightCommandByte = 0;
}

bool MeshCoreClient::requestContacts(uint32_t since) {
  lastContactListFull = (since == 0);
  uint8_t buf[5];
  size_t len = MeshProto::buildGetContacts(buf, sizeof(buf), since);
  // Expect PKT_CONTACT_END (0x04), NOT PKT_CONTACT_START (0x02): the command must
  // stay in-flight until the whole list has streamed, so the queue does not advance
  // (and start firing channel/message commands) while the contact list is still
  // open. Keeping the companion idle during the stream also stops it deferring
  // contacts — the companion prioritises incoming command frames over streaming.
  bool ok = len > 0 && enqueueCmd(buf, len, MeshProto::PKT_CONTACT_END);
  LOG_DBG("MESH", "requestContacts(since=%lu): %s (queue=%d/%d)", (unsigned long)since, ok ? "queued" : "FAILED",
          cmdCount, CMD_QUEUE_SIZE);
  return ok;
}

bool MeshCoreClient::requestNewContacts() { return requestContacts(contactsMostRecentLastmod); }

bool MeshCoreClient::addUpdateContact(const MeshCoreContact& contact) {
  uint8_t buf[CMD_BUF_SIZE];
  size_t len = MeshProto::buildAddUpdateContact(buf, sizeof(buf), contact);
  bool ok = len > 0 && enqueueCmd(buf, len, MeshProto::PKT_OK);
  LOG_DBG("MESH", "addUpdateContact(%s): %s (queue=%d/%d)", contact.name, ok ? "queued" : "FAILED", cmdCount,
          CMD_QUEUE_SIZE);
  return ok;
}

bool MeshCoreClient::removeContact(const uint8_t* pubkey32) {
  uint8_t buf[CMD_BUF_SIZE];
  size_t len = MeshProto::buildRemoveContact(buf, sizeof(buf), pubkey32);
  bool ok = len > 0 && enqueueCmd(buf, len, MeshProto::PKT_OK);
  LOG_DBG("MESH", "removeContact: %s (queue=%d/%d)", ok ? "queued" : "FAILED", cmdCount, CMD_QUEUE_SIZE);
  return ok;
}

bool MeshCoreClient::requestChannel(uint8_t idx) {
  uint8_t buf[2];
  size_t len = MeshProto::buildGetChannel(buf, sizeof(buf), idx);
  return len > 0 && enqueueCmd(buf, len, MeshProto::PKT_CHANNEL_INFO);
}

bool MeshCoreClient::requestBattery() {
  uint8_t buf[1];
  size_t len = MeshProto::buildGetBattery(buf, sizeof(buf));
  bool ok = len > 0 && enqueueCmd(buf, len, MeshProto::PKT_BATTERY);
  LOG_DBG("MESH", "requestBattery: %s (queue=%d/%d)", ok ? "queued" : "FAILED", cmdCount, CMD_QUEUE_SIZE);
  return ok;
}

bool MeshCoreClient::requestMessages() {
  lastMessagePollTime = millis();  // reset periodic poll timer
  uint8_t buf[1];
  size_t len = MeshProto::buildGetMessage(buf, sizeof(buf));
  return len > 0 && enqueueCmd(buf, len, 0);  // Accept any response type
}

void MeshCoreClient::drainNextMessage() {
  if (msgDrainCount >= MAX_MSG_DRAIN) {
    LOG_DBG("MESH", "Msg drain cap %d reached; deferring remainder to periodic poll", (int)MAX_MSG_DRAIN);
    msgDrainCount = 0;  // let the periodic poll open a fresh drain window
    return;
  }
  ++msgDrainCount;
  requestMessages();
}

bool MeshCoreClient::sendSelfAdvert(bool flood) {
  uint8_t buf[2];
  size_t len = MeshProto::buildSendSelfAdvert(buf, sizeof(buf), flood);
  bool ok = len > 0 && enqueueCmd(buf, len, MeshProto::PKT_OK);
  LOG_DBG("MESH", "sendSelfAdvert(flood=%d): %s (queue=%d/%d)", (int)flood, ok ? "queued" : "FAILED", cmdCount,
          CMD_QUEUE_SIZE);
  return ok;
}

bool MeshCoreClient::sendChannelMessage(uint8_t channelIdx, const char* text) {
  uint8_t buf[CMD_BUF_SIZE];
  uint32_t ts = meshcoreNowUtc();
  size_t len = MeshProto::buildSendChannelMsg(buf, sizeof(buf), channelIdx, ts, text);
  // Firmware replies to CMD_SEND_CHAN_MSG with a bare PKT_OK (0x00), not
  // PKT_MSG_SENT (0x06): channel messages are flood broadcasts with no
  // per-message ACK. (Direct messages below DO get PKT_MSG_SENT with an ack
  // tag.) Expecting PKT_MSG_SENT here left the command pending until the 5 s
  // timeout fired, producing a spurious "Command timeout" after every send.
  bool ok = len > 0 && enqueueCmd(buf, len, MeshProto::PKT_OK);
  if (ok) {
    // Register a pending tracker for echo detection.
    // The tracker stays in pending state (payloadHash == 0) until the first
    // matching GRP_TXT re-flood arrives on our channel (identified by the
    // channel routing hash — the message origin's hash is NOT in the flood
    // path, only forwarding repeaters append theirs).
    bool registered = false;
    for (auto& t : _trackers) {
      if (t.active) continue;  // skip active trackers
      if (static_cast<size_t>(std::strlen(text)) >= sizeof(t.text)) break;
      std::strncpy(t.text, text, sizeof(t.text) - 1);
      t.text[sizeof(t.text) - 1] = '\0';
      t.channelIdx = channelIdx;
      t.channelHash =
          (channelIdx < MESHCORE_MAX_CHANNELS && _channelHashValid[channelIdx]) ? _channelHash[channelIdx] : 0;
      t.sentTimeMs = millis();
      t.payloadHash = 0;  // pending
      t.echoCount = 0;
      t.active = true;
      registered = true;
      if (t.channelHash == 0) {
        LOG_DBG("MESH", "ECHO tracker ch=%d \"%.30s\": no channel hash yet, will not lock", (int)channelIdx, text);
      } else {
        LOG_DBG("MESH", "ECHO tracker registered: ch=%d text=\"%.30s\" channelHash=0x%02X", (int)channelIdx, text,
                (int)t.channelHash);
      }
      break;
    }
    if (!registered) {
      LOG_DBG("MESH", "ECHO tracker FULL: could not register ch=%d \"%.30s\"", (int)channelIdx, text);
    }
  }
  return ok;
}

bool MeshCoreClient::sendDirectMessage(const MeshCoreContact& contact, const char* text, uint32_t msgId) {
  uint8_t buf[CMD_BUF_SIZE];
  uint32_t ts = meshcoreNowUtc();
  size_t len = MeshProto::buildSendDirectMsg(buf, sizeof(buf), contact.publicKey, ts, text, /*attempt=*/0);
  if (len == 0) return false;

  // Fill the in-flight tracker slot
  _pendingDm = {};
  _pendingDm.active = true;
  _pendingDm.msgId = msgId;
  memcpy(_pendingDm.pubkey, contact.publicKey, 32);
  snprintf(_pendingDm.name, sizeof(_pendingDm.name), "%s", contact.name);
  _pendingDm.type = contact.type;
  snprintf(_pendingDm.text, sizeof(_pendingDm.text), "%s", text);
  _pendingDm.awaitingSent = true;
  _pendingDm.stage = 0;

  // If contact has no known path (0xFF), skip direct stages entirely.
  // Start with the first flood attempt (stage=2 → attempt=2).
  if (contact.pathLength == 0xFF) {
    _pendingDm.stage = 2;
    // Rebuild the send buffer with the flood attempt byte
    len = MeshProto::buildSendDirectMsg(buf, sizeof(buf), contact.publicKey, ts, text, /*attempt=*/2);
  }

  const bool queued = enqueueCmd(buf, len, MeshProto::PKT_MSG_SENT);
  if (!queued) {
    // Never leave a live tracker behind for a message that was not queued.
    _pendingDm = {};
  }
  return queued;
}

bool MeshCoreClient::resetPath(const MeshCoreContact& contact) {
  // Build CMD_ADD_UPDATE_CONTACT with pathLength=0xFF and zeroed 64-byte path.
  // This tells the companion to clear its stored route and re-learn it
  // on the next round-trip. We pass the real name/type to avoid clobbering
  // the contact's display name on the companion.
  MeshCoreContact resetContact = contact;
  resetContact.pathLength = 0xFF;
  // buildAddUpdateContact zeros the 64-byte path — exactly what we want.
  return addUpdateContact(resetContact);
}

void MeshCoreClient::setDeliveryCallback(DeliveryCallback cb, void* ctx) {
  deliveryCb = cb;
  deliveryCbCtx = ctx;
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

void MeshCoreClient::setChannelHeardCallback(ChannelHeardCallback cb, void* ctx) {
  heardCb = cb;
  heardCbCtx = ctx;
}

void MeshCoreClient::handleRxLog(const uint8_t* data, size_t len) {
  uint8_t hashes[MeshProto::MESH_MAX_PATH_HASHES];
  uint8_t hashCount = 0;
  uint8_t channelHash = 0;
  uint32_t payloadHash = 0;
  if (!MeshProto::parseChannelReflood(data, len, hashes, sizeof(hashes), hashCount, payloadHash, channelHash)) {
    return;  // not a GRP_TXT packet
  }

  LOG_DBG("MESH", "ECHO RX_LOG: GRP_TXT ch=0x%02X payloadHash=0x%08lX hashCount=%d", (int)channelHash,
          (unsigned long)payloadHash, (int)hashCount);

  uint32_t now = millis();

  // Expire stale trackers
  for (auto& t : _trackers) {
    if (t.active && now - t.sentTimeMs > TRACKER_TTL_MS) {
      LOG_DBG("MESH", "ECHO EXPIRE: tracker ch=%d \"%.20s\" age=%lums", (int)t.channelIdx, t.text,
              (unsigned long)(now - t.sentTimeMs));
      t.active = false;
    }
  }

  // Find a matching tracker
  SentChannelTracker* found = nullptr;
  for (auto& t : _trackers) {
    if (!t.active) continue;

    if (t.payloadHash == 0) {
      // Pending tracker: lock onto the first re-flood of our channel within
      // the lock window. Trackers are scanned in send order, so when several
      // pending trackers share a channel the oldest message locks first.
      // hashCount == 0 means the packet still has an empty flood path, i.e.
      // it is someone's origin transmission, never a re-flood of ours.
      if (hashCount == 0) continue;
      if (now - t.sentTimeMs > TRACKER_LOCK_WINDOW_MS) {
        LOG_DBG("MESH", "ECHO EXPIRE pending: ch=%d \"%.20s\" age=%lums", (int)t.channelIdx, t.text,
                (unsigned long)(now - t.sentTimeMs));
        t.active = false;  // expired
        continue;
      }
      if (t.channelHash == 0 || t.channelHash != channelHash) continue;
      found = &t;
      LOG_DBG("MESH", "ECHO LOCK: pending tracker ch=%d \"%.20s\" → payloadHash=0x%08lX", (int)t.channelIdx, t.text,
              (unsigned long)payloadHash);
      break;
    } else if (t.payloadHash == payloadHash) {
      // Already-locked tracker: match by payload hash
      found = &t;
      LOG_DBG("MESH", "ECHO MATCH: locked tracker ch=%d echoCount=%d payloadHash=0x%08lX", (int)t.channelIdx,
              (int)t.echoCount, (unsigned long)payloadHash);
      break;
    }
  }

  if (!found) {
#if LOG_LEVEL >= 2
    uint8_t activeCount = 0;
    for (auto& t : _trackers) {
      if (t.active) activeCount++;
    }
    LOG_DBG("MESH", "ECHO SKIP: no matching tracker (active=%d ch=0x%02X payloadHash=0x%08lX)", (int)activeCount,
            (int)channelHash, (unsigned long)payloadHash);
#endif
    return;
  }

  // Count distinct repeaters: append every relay hash not seen in previous
  // re-floods of this message.
  bool changed = false;
  for (uint8_t i = 0; i < hashCount; ++i) {
    uint8_t h = hashes[i];
    bool known = false;
    for (uint8_t j = 0; j < found->echoCount; ++j) {
      if (found->seenHashes[j] == h) {
        known = true;
        break;
      }
    }
    if (!known && found->echoCount < MeshProto::MESH_MAX_PATH_HASHES) {
      found->seenHashes[found->echoCount] = h;
      found->echoCount++;
      changed = true;
    }
  }

  if (found->payloadHash == 0) {
    // Transition from pending → locked on the first matching re-flood.
    found->payloadHash = payloadHash;
    LOG_DBG("MESH", "ECHO FIRST: ch=%d echoCount=%d", (int)found->channelIdx, (int)found->echoCount);
    if (heardCb) {
      heardCb(found->channelIdx, found->echoCount, hashes, heardCbCtx);
    }
  } else if (changed && heardCb) {
    LOG_DBG("MESH", "ECHO UPDATE: ch=%d echoCount=%d", (int)found->channelIdx, (int)found->echoCount);
    heardCb(found->channelIdx, found->echoCount, hashes, heardCbCtx);
  } else {
    LOG_DBG("MESH", "ECHO NODUP: ch=%d echoCount=%d (no new repeaters)", (int)found->channelIdx, (int)found->echoCount);
  }
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
    lastCmdSuccess = false;
    cmdCount = 0;
    setState(BleConnectionState::DISCONNECTED);
    return;
  }

  // Fire the deferred post-init request burst on the main-loop thread.
  // runInitSequence() (worker task) sets initialRequestsPending; running the
  // enqueues here guarantees every command-queue mutation happens on this
  // single thread. Enqueuing from the worker task while poll() runs on the
  // main loop races on cmdHead/cmdTail/cmdCount/cmdPending — observed as
  // GET_CONTACTS being dropped and GET_BATTERY sent twice.
  if (initialRequestsPending && rxChar) {
    initialRequestsPending = false;
    if (!requestBattery()) {
      LOG_ERR("MESH", "Failed to queue battery request");
    }
    requestContacts();
    // Channel info is fetched over the whole companion channel range (up to
    // MESHCORE_MAX_CHANNELS) in small batches as the command queue drains —
    // see the refill loop below. The initial message drain (requestMessages)
    // is deferred until the channel list has been fully requested.
    channelSyncTotal = (companion.maxChannels > MESHCORE_MAX_CHANNELS) ? MESHCORE_MAX_CHANNELS : companion.maxChannels;
    channelSyncNext = 0;
    channelSyncActive = true;
  }

  // Channel-list sync: refill GET_CHANNEL requests in batches until the whole
  // companion channel range has been queried. Bounded by the command queue so
  // the burst never overflows it, and leaving CHANNEL_SYNC_QUEUE_RESERVE slots
  // free for periodic commands (battery poll, mark-read, sends). Once complete,
  // fire the deferred initial message drain.
  if (channelSyncActive && rxChar) {
    while (channelSyncNext < channelSyncTotal && cmdCount < CMD_QUEUE_SIZE - CHANNEL_SYNC_QUEUE_RESERVE) {
      if (!requestChannel(channelSyncNext)) break;
      ++channelSyncNext;
    }
    if (channelSyncNext >= channelSyncTotal) {
      channelSyncActive = false;
      requestMessages();
    }
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
    // If the timed-out command was GET_CONTACTS, the companion never streamed
    // the list to its end (PKT_CONTACT_END dropped by RX overflow). Re-fetch the
    // full list so no contact is silently lost. Bounded by retryContactsCount,
    // which resets whenever a list does reach PKT_CONTACT_END.
    const bool retryContacts = (cmdInFlightCommandByte == MeshProto::CMD_GET_CONTACTS &&  //
                                retryContactsCount < MAX_CONTACT_RETRIES);
    lastCmdSuccess = false;
    cmdPending = false;
    sendNextCmd();
    if (retryContacts) {
      retryContactsCount++;
      LOG_DBG("MESH", "GET_CONTACTS timed out, re-fetching full list (%d/%d)", (int)retryContactsCount,
              (int)MAX_CONTACT_RETRIES);
      requestContacts(0);
    }
  }

  // Send next queued command
  if (!cmdPending && cmdCount > 0) {
    sendNextCmd();
  }

  // Check DM delivery escalation timeout (lazy, ~once per poll())
  if (_pendingDm.active && !_pendingDm.awaitingSent && state == BleConnectionState::CONNECTED) {
    uint32_t elapsed = millis() - _pendingDm.sentAtMs;
    if (elapsed > _pendingDm.timeoutMs) {
      startDmEscalation();
    }
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
  // Fail fast when the link is gone: a queued command would silently sit
  // in the queue (sendNextCmd() cannot write without rxChar) until the next
  // poll() flushes it, and callers would report success to the user.
  if (state != BleConnectionState::CONNECTED) {
    LOG_ERR("MESH", "Cannot enqueue cmd=0x%02X: not connected", (unsigned)data[0]);
    return false;
  }
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

  LOG_DBG("MESH", "enqueueCmd: cmd=0x%02X len=%d expect=0x%02X (count=%d pending=%d)", (unsigned)data[0], (int)len,
          (unsigned)expectedResp, (int)cmdCount, (int)cmdPending);

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
    LOG_ERR("MESH", "BLE write failed (cmd=0x%02X len=%d)", (unsigned)entry.data[0], (int)entry.len);
    return false;
  }

  LOG_DBG("MESH", "sendNextCmd: wrote cmd=0x%02X len=%d expect=0x%02X (remaining=%d)", (unsigned)entry.data[0],
          (int)entry.len, (unsigned)entry.expectedResponse, (int)(cmdCount - 1));

  cmdPending = true;
  cmdExpectedResponse = entry.expectedResponse;
  // Store app->companion command byte so PKT_ERROR can be decoded.
  cmdInFlightCommandByte = entry.data[0];
  cmdSentTime = millis();
  cmdHead = (cmdHead + 1) % CMD_QUEUE_SIZE;
  cmdCount--;
  return true;
}

void MeshCoreClient::handleCompanionErrorResponse(const uint8_t* data, size_t len) {
  // Companion error payload format: [RESP_CODE_ERR, err_code].
  // For CMD_REMOVE_CONTACT the companion returns ERR_CODE_NOT_FOUND (2)
  // when the contact is already missing — treat it as success.
  uint8_t err = (len >= 2) ? data[1] : 0;
  constexpr uint8_t ERR_CODE_NOT_FOUND = 2;

  const uint8_t inFlightCmd = cmdInFlightCommandByte;
  cmdPending = false;

  const bool removeContactOk = (inFlightCmd == MeshProto::CMD_REMOVE_CONTACT && err == ERR_CODE_NOT_FOUND);
  lastCmdSuccess = removeContactOk;

  // Direct-message send failures must update persisted delivery status.
  // Without this, the Thread activity stays at SENT and the UI shows
  // "sent" even though the companion rejected the command.
  if (inFlightCmd == MeshProto::CMD_SEND_DM && _pendingDm.active) {
    LOG_ERR("MESH", "DM send rejected by companion: msgId=%lu err=%u", (unsigned long)_pendingDm.msgId, (unsigned)err);
    _pendingDm.active = false;
    _pendingDm.awaitingSent = false;
    fireDelivery(DeliveryStatus::FAILED);
  }

  LOG_ERR("MESH", "Error response from companion: cmd=0x%02X err=%u", (unsigned)inFlightCmd, (unsigned)err);
}

void MeshCoreClient::processResponse(const uint8_t* data, size_t len) {
  if (len == 0) return;
  uint8_t pktType = data[0];

  if (pktType == MeshProto::PKT_ERROR) {
    handleCompanionErrorResponse(data, len);
  } else {
    // Only clear pending flag if this packet matches the expected response.
    // Push notifications (PKT_MSGS_WAITING, PKT_NEW_ADVERT, etc.) must NOT
    // advance the command queue — they are processed but keep cmdPending intact.
    if (cmdExpectedResponse == 0 || pktType == cmdExpectedResponse) {
      cmdPending = false;
      lastCmdSuccess = true;
    }
  }

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
        if (ch.index < MESHCORE_MAX_CHANNELS) {
          _channelHash[ch.index] = MeshProto::channelHashFromSecret(ch.secret);
          _channelHashValid[ch.index] = true;
          LOG_DBG("MESH", "Channel %d hash=0x%02X", (int)ch.index, (int)_channelHash[ch.index]);
        }
        if (channelCb) channelCb(ch, channelCbCtx);
      }
      break;
    }

    case MeshProto::PKT_BATTERY:
      MeshProto::parseBattery(data, len, companion);
      LOG_DBG("MESH", "Battery: %d mV", companion.batteryMv);
      break;

    case MeshProto::PKT_CONTACT_START: {
      // The companion appends getNumContacts() (total, unfiltered) as 4 LE bytes.
      uint32_t total = 0;
      if (len >= 5) memcpy(&total, data + 1, sizeof(total));  // memcpy: data+1 may be unaligned
      LOG_DBG("MESH", "Contact list start (companion reports %lu contacts)", (unsigned long)total);
      lastContactListTotal = total;
      if (contactCb) contactCb(MeshCoreContact{}, false, contactCbCtx);
      break;
    }

    case MeshProto::PKT_CONTACT: {
      MeshCoreContact contact = {};
      if (MeshProto::parseContact(data, len, contact)) {
        contact.isSaved = true;  // CMD_GET_CONTACTS returns all known contacts
        LOG_DBG("MESH", "PKT_CONTACT: %s type=%d pathLen=%d (len=%d)", contact.name, (int)contact.type,
                (int)contact.pathLength, (int)len);
        if (contactCb) contactCb(contact, false, contactCbCtx);
      } else {
        LOG_ERR("MESH", "PKT_CONTACT parse failed (len=%d)", (int)len);
      }
      break;
    }

    case MeshProto::PKT_CONTACT_END:
      // Trailing 4 bytes (when present) are the most-recent contact lastmod.
      // Store it so the next incremental sync can filter with 'since'.
      if (len >= 5) {
        uint32_t lastmod = 0;
        memcpy(&lastmod, data + 1, sizeof(lastmod));  // memcpy: data+1 may be unaligned
        if (lastmod > contactsMostRecentLastmod) contactsMostRecentLastmod = lastmod;
      }
      LOG_DBG("MESH", "Contact list end (mostRecentLastmod=%lu)", (unsigned long)contactsMostRecentLastmod);
      retryContactsCount = 0;  // list delivered — reset GET_CONTACTS retry budget
      if (contactCb) contactCb(MeshCoreContact{}, true, contactCbCtx);
      break;

    case MeshProto::PKT_MSGS_WAITING:
      LOG_DBG("MESH", "Messages waiting, polling");
      msgDrainCount = 0;  // fresh message(s) — open a new drain window
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
        // Drain the companion's offline queue: CMD_GET_MESSAGE pops one message
        // per request, and the companion only sends PUSH_CODE_MSG_WAITING for
        // messages received *while connected*. Messages queued while we were
        // disconnected produce no tickle, so re-request until PKT_NO_MORE_MSGS.
        drainNextMessage();
      } else {
        LOG_ERR("MESH", "Failed to parse channel msg (len=%d)", (int)len);
      }
      break;
    }

    case MeshProto::PKT_CONTACT_MSG:
    case MeshProto::PKT_CONTACT_MSG_V3: {
      MeshCoreMessage msg = {};
      if (MeshProto::parseContactMessage(data, len, msg)) {
        LOG_DBG("MESH", "Contact msg from %02X%02X%02X: %.40s", msg.pubkeyPrefix[0], msg.pubkeyPrefix[1],
                msg.pubkeyPrefix[2], msg.text);
        msg.direction = MsgDirection::RECEIVED;
        msg.type = MsgType::DIRECT;
        if (msgCb) msgCb(msg, msgCbCtx);
        // Drain the companion's offline queue (see channel-msg comment above).
        drainNextMessage();
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
        // Newly discovered node — always goes to discovered list
        contact.isSaved = false;
        LOG_DBG("MESH", "New advert: %s flags=0x%02X", contact.name, contact.flags);
        if (contactCb) contactCb(contact, false, contactCbCtx);
      }
      break;
    }

    case 0x81:  // PUSH_CODE_PATH_UPDATED
    case 0x84:  // PUSH_CODE_RAW_DATA
    case 0x85:  // PUSH_CODE_LOGIN_SUCCESS
    case 0x86:  // PUSH_CODE_LOGIN_FAIL
    case 0x87:  // PUSH_CODE_STATUS_RESPONSE
    case 0x89:  // PUSH_CODE_TRACE_DATA
      LOG_DBG("MESH", "Push 0x%02X len=%d (ignored)", pktType, (int)len);
      break;

    case MeshProto::PUSH_LOG_RX_DATA:  // 0x88: raw LoRa RX log — used to count channel re-floods
      handleRxLog(data, len);
      break;

    case MeshProto::PKT_ACK: {
      uint8_t ackHash[4];
      if (MeshProto::parseAck(data, len, ackHash)) {
        uint32_t hash32;
        memcpy(&hash32, ackHash, 4);  // RISC-V unaligned-load safe
        LOG_DBG("MESH", "ACK received: hash=0x%08lX", (unsigned long)hash32);
        if (_pendingDm.active && !_pendingDm.awaitingSent && _pendingDm.ackTag == hash32) {
          LOG_INF("MESH", "DM ACK matched: msgId=%lu pubkey=%02X%02X", (unsigned long)_pendingDm.msgId,
                  _pendingDm.pubkey[0], _pendingDm.pubkey[1]);
          _pendingDm.active = false;
          fireDelivery(DeliveryStatus::ACKED);
        } else {
          LOG_DBG("MESH", "ACK hash 0x%08lX: no matching in-flight DM (active=%d awaiting=%d ackTag=0x%08lX)",
                  (unsigned long)hash32, (int)_pendingDm.active, (int)_pendingDm.awaitingSent,
                  (unsigned long)_pendingDm.ackTag);
        }
      }
      break;
    }

    case MeshProto::PKT_MSG_SENT: {
      uint32_t ackTag, timeout;
      bool isSentFlood = false;
      if (MeshProto::parseMsgSent(data, len, ackTag, timeout, isSentFlood)) {
        LOG_DBG("MESH", "Msg sent, ack tag=%lu timeout=%lu flood=%d", (unsigned long)ackTag, (unsigned long)timeout,
                (int)isSentFlood);
        if (_pendingDm.active && _pendingDm.awaitingSent) {
          _pendingDm.ackTag = ackTag;
          _pendingDm.sentFlood = isSentFlood;
          _pendingDm.awaitingSent = false;
          _pendingDm.sentAtMs = millis();
          // Honour companion timeout if sensible, else use stage table fallback.
          if (timeout > 0 && timeout < 60000) {
            uint32_t stageT = DM_STAGE_TIMEOUT_MS[_pendingDm.stage];
            _pendingDm.timeoutMs = (timeout > stageT) ? timeout : stageT;
          } else {
            _pendingDm.timeoutMs = DM_STAGE_TIMEOUT_MS[_pendingDm.stage];
          }
          LOG_DBG("MESH", "DM tracker: ackTag=0x%08lX stage=%d flood=%d", (unsigned long)ackTag, (int)_pendingDm.stage,
                  (int)isSentFlood);
        }
      }
      break;
    }

    case MeshProto::PKT_NO_MORE_MSGS:
      LOG_DBG("MESH", "No more messages");
      msgDrainCount = 0;  // queue drained — reset the drain window
      break;

    case MeshProto::PKT_OK:
      LOG_DBG("MESH", "OK response");
      break;

    case MeshProto::PKT_ERROR:
      // `lastCmdSuccess` is updated above when this error matches the expected
      // response. Here we only avoid extra logging/side-effects.
      break;

    default:
      LOG_DBG("MESH", "Unknown packet type: 0x%02X len=%d", pktType, (int)len);
      break;
  }
}

bool MeshCoreClient::runInitSequence() {
  inInitSequence = true;

  // Defensive: rxChar/txChar are nulled by disconnect()/failConnect() during
  // teardown. Without this, a write on a torn-down link dereferences null.
  if (!rxChar || !txChar) {
    LOG_ERR("MESH", "Init sequence aborted: link torn down");
    inInitSequence = false;
    return false;
  }

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
  while ((!gotDeviceInfo || !gotSelfInfo) && (millis() - start) < MeshProto::CMD_TIMEOUT_MS) {
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

  LOG_INF("MESH", "Device: %s %s pin=%lu", companion.model, companion.version, (unsigned long)companion.blePin);

  if (companion.blePin != 0 && pinCb) {
    pinCb(companion.blePin, pinCbCtx);
  }

  // Release the ring-buffer to poll() BEFORE queuing commands that will
  // generate bursts of responses (contacts, channels, messages).
  // Otherwise notifyCallback fills the ring-buffer while poll() is blocked,
  // and packets are dropped before they can be consumed.
  inInitSequence = false;

  // Defer the post-init request burst (battery, contacts, channels, messages)
  // to poll() on the main-loop thread. Enqueuing here (worker task) while the
  // main loop concurrently runs poll() races on the command ring buffer
  // (cmdHead/cmdTail/cmdCount/cmdPending) — observed on device as GET_CONTACTS
  // being dropped and GET_BATTERY sent twice. Routing every enqueue through
  // the single main-loop thread removes the race without a mutex.
  initialRequestsPending = true;

  return true;
}

void MeshCoreClient::startDmEscalation() {
  if (!_pendingDm.active || _pendingDm.awaitingSent) return;

  _pendingDm.stage++;
  if (_pendingDm.stage >= 4) {
    LOG_INF("MESH", "DM escalation exhausted: msgId=%lu marking FAILED", (unsigned long)_pendingDm.msgId);
    _pendingDm.active = false;
    fireDelivery(DeliveryStatus::FAILED);
    return;
  }

  // Before the first flood stage (stage 2), reset the companion's route
  // to clear any stale direct route.
  if (_pendingDm.stage == 2) {
    LOG_INF("MESH", "DM escalation: resetting path before flood stage for %s", _pendingDm.name);
    MeshCoreContact resetContact = {};
    memcpy(resetContact.publicKey, _pendingDm.pubkey, 32);
    snprintf(resetContact.name, sizeof(resetContact.name), "%s", _pendingDm.name);
    resetContact.type = _pendingDm.type;
    resetContact.pathLength = 0xFF;
    resetPath(resetContact);
  }

  LOG_INF("MESH", "DM escalation: resending msgId=%lu stage=%d attempt=%d", (unsigned long)_pendingDm.msgId,
          (int)_pendingDm.stage, (int)_pendingDm.stage);
  resendDm(_pendingDm.stage);
}

void MeshCoreClient::resendDm(uint8_t attempt) {
  if (!_pendingDm.active) return;

  uint8_t buf[CMD_BUF_SIZE];
  uint32_t ts = meshcoreNowUtc();
  size_t len = MeshProto::buildSendDirectMsg(buf, sizeof(buf), _pendingDm.pubkey, ts, _pendingDm.text, attempt);
  if (len == 0) return;

  _pendingDm.awaitingSent = true;
  _pendingDm.sentAtMs = millis();
  _pendingDm.timeoutMs = DM_STAGE_TIMEOUT_MS[_pendingDm.stage];
  enqueueCmd(buf, len, MeshProto::PKT_MSG_SENT);
}

void MeshCoreClient::fireDelivery(DeliveryStatus status) {
  if (deliveryCb) {
    deliveryCb(_pendingDm.msgId, _pendingDm.pubkey, status, deliveryCbCtx);
  }
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

void MeshCoreClient::notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
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
