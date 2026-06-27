#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstddef>
#include <cstdint>

#include "MeshCoreProtocol.h"
#include "MeshCoreTypes.h"

class NimBLEClient;
class NimBLERemoteCharacteristic;
class MeshBleClientCallbacks;

class MeshCoreClient {
  friend class MeshBleClientCallbacks;

 public:
  // Callback types (raw function pointers, no std::function)
  using StateCallback = void (*)(BleConnectionState state, void* ctx);
  using MessageCallback = void (*)(const MeshCoreMessage& msg, void* ctx);
  using ContactCallback = void (*)(const MeshCoreContact& c, bool isEnd, void* ctx);
  using AdvertCallback = void (*)(const MeshCoreContact& node, void* ctx);
  using ChannelCallback = void (*)(const MeshCoreChannel& ch, void* ctx);
  // Called during init if companion has a PIN (non-zero). Show PIN to user.
  using PinCallback = void (*)(uint32_t pin, void* ctx);
  // Fired when the count of distinct repeaters that re-flooded our most recent
  // outgoing channel message increases. hashes points to heardCount routing
  // hashes (first byte of each repeater public key).
  using ChannelHeardCallback = void (*)(uint8_t channelIdx, uint8_t heardCount, const uint8_t* hashes, void* ctx);

  MeshCoreClient();
  ~MeshCoreClient();

  // Lifecycle
  bool init();
  void deinit();

  // Scanning
  bool startScan(uint32_t durationSec = 10);
  void stopScan();

  // Connection
  bool connectTo(const char* bleAddress, uint8_t addressType = 0);
  void disconnect();
  BleConnectionState getState() const { return state; }

  // BLE pairing PIN (set before connectTo; default 123456)
  void setConnectPin(uint32_t pin) { connectPin = pin; }
  uint32_t getConnectPin() const { return connectPin; }

  // Companion info (valid after CONNECTED)
  const MeshCoreCompanion& getCompanion() const { return companion; }

  // Commands (queued, async)
  bool requestContacts();
  bool requestChannel(uint8_t idx);
  bool requestBattery();
  bool requestMessages();
  bool sendChannelMessage(uint8_t channelIdx, const char* text);
  bool sendDirectMessage(const uint8_t* pubkey32, const char* text);
  bool setChannel(uint8_t idx, const char* name, const uint8_t* secret16);
  bool deleteChannel(uint8_t idx);
  bool addUpdateContact(const MeshCoreContact& contact);
  bool sendSelfAdvert(bool flood);

  bool isCommandPending() const { return cmdPending; }
  /// Result of the last completed command. Reset on disconnect.
  bool getLastCommandResult() const { return lastCmdSuccess; }

  // Callbacks (set before connect)

  /// Called when BLE connection state changes (DISCONNECTED → SCANNING →
  /// CONNECTING → INITIALIZING → CONNECTED, or any → DISCONNECTED on
  /// disconnect/timeout). Use this to update UI indicators.
  void setStateCallback(StateCallback cb, void* ctx);

  /// Called when a channel or direct message arrives from the companion.
  /// The MeshCoreMessage is fully parsed — direction, type, sender info,
  /// timestamp, pathLength, and text are populated. Store it via
  /// MeshCoreMessageStore and update unread counts.
  void setMessageCallback(MessageCallback cb, void* ctx);

  /// Called during contact enumeration (companion sends contact list
  /// split into multiple packets). isEnd signals the last packet.
  /// Build your contact list incrementally from these calls.
  void setContactCallback(ContactCallback cb, void* ctx);

  /// Called when the companion relays a self-advert from a mesh node
  /// (received during scanning or passively). Use to discover nodes
  /// reachable through the companion.
  void setAdvertCallback(AdvertCallback cb, void* ctx);

  /// Called when channel configuration data arrives from the companion
  /// (name, secret, type for a given index). Populate the channel list
  /// from these calls.
  void setChannelCallback(ChannelCallback cb, void* ctx);

  /// Called during init if the companion requires a BLE pairing PIN
  /// (pin is non-zero). Show the PIN on screen so the user can enter
  /// it on the companion device.
  void setPinCallback(PinCallback cb, void* ctx);

  /// Called when repeaters re-flood our outgoing channel message.
  /// heardCount is the number of distinct repeaters heard so far, hashes
  /// is an array of first-byte-of-public-key routing hashes. Use to
  /// update the message's pathLength in the store for live UI feedback.
  void setChannelHeardCallback(ChannelHeardCallback cb, void* ctx);

  // Must be called from activity loop() to process responses and timeouts
  void poll();

  // Auto-reconnect target
  void setAutoReconnectAddress(const char* addr, uint8_t addressType = 0);
  const char* getAutoReconnectAddress() const;
  uint8_t getAutoReconnectAddressType() const;

  // Scan results access
  struct ScanResult {
    char name[64];
    char address[18];
    uint8_t addressType;  // 0=public, 1=random
    int rssi;
  };
  static constexpr uint8_t MAX_SCAN_RESULTS = 8;
  const ScanResult* getScanResults() const { return scanResults; }
  uint8_t getScanResultCount() const { return scanResultCount; }
  bool isScanning() const { return state == BleConnectionState::SCANNING; }

#ifdef SIMULATOR
  NimBLEClient* getBleClient() const { return bleClient; }
#endif

 private:
  BleConnectionState state = BleConnectionState::DISCONNECTED;
  MeshCoreCompanion companion = {};
  NimBLEClient* bleClient = nullptr;
  NimBLERemoteCharacteristic* rxChar = nullptr;
  NimBLERemoteCharacteristic* txChar = nullptr;

  // Scan results
  ScanResult scanResults[MAX_SCAN_RESULTS] = {};
  uint8_t scanResultCount = 0;

  // Command queue (simple ring buffer).
  // 12 slots: the hub init/connect burst (APP_START, DEVICE_QUERY, battery, channel
  // queries, message poll) enqueues commands faster than responses return, so a smaller
  // ring (e.g. 8) overflows with "Command queue full" before the in-flight command
  // completes. 12 × 256 bytes = 3 KB.
  static constexpr size_t CMD_QUEUE_SIZE = 12;
  static constexpr size_t CMD_BUF_SIZE = 256;
  struct CmdEntry {
    uint8_t data[CMD_BUF_SIZE];
    size_t len;
    uint8_t expectedResponse;
  };
  CmdEntry cmdQueue[CMD_QUEUE_SIZE] = {};
  uint8_t cmdHead = 0;
  uint8_t cmdTail = 0;
  uint8_t cmdCount = 0;
  bool cmdPending = false;
  uint8_t cmdExpectedResponse = 0;
  uint32_t cmdSentTime = 0;
  bool lastCmdSuccess = false;

  // True while runInitSequence() owns rxBuf; poll() must not consume responses.
  volatile bool inInitSequence = false;

  // Set by deinit() to signal doConnect() to skip bleClient cleanup.
  // Ownership of bleClient transfers back to deinit() when this is true.
  volatile bool cancelConnect = false;

  // Callbacks
  StateCallback stateCb = nullptr;
  void* stateCbCtx = nullptr;
  MessageCallback msgCb = nullptr;
  void* msgCbCtx = nullptr;
  ContactCallback contactCb = nullptr;
  void* contactCbCtx = nullptr;
  AdvertCallback advertCb = nullptr;
  void* advertCbCtx = nullptr;
  ChannelCallback channelCb = nullptr;
  void* channelCbCtx = nullptr;
  PinCallback pinCb = nullptr;
  void* pinCbCtx = nullptr;
  ChannelHeardCallback heardCb = nullptr;
  void* heardCbCtx = nullptr;

  // Tracker for "heard by N repeaters" on outgoing channel messages.
  // Each call to sendChannelMessage() registers a pending tracker keyed by the
  // message text + channel.  When a matching GRP_TXT re-flood arrives via
  // PUSH_LOG_RX_DATA (0x88) and contains our node hash in its path, the tracker
  // transitions from pending (payloadHash == 0) to locked (payloadHash != 0).
  // Subsequent re-floods with the same payload hash increment echoCount.
  struct SentChannelTracker {
    char text[184];  // MAX_MSG_TEXT_LEN from MeshCoreTypes.h
    uint8_t channelIdx;
    uint32_t sentTimeMs;
    uint32_t payloadHash;  // 0 = pending (not yet locked)
    uint8_t echoCount;
    bool active;
  };
  static constexpr uint8_t MAX_TRACKERS = 4;
  static constexpr uint32_t TRACKER_TTL_MS = 45000;          // 45 s expiry
  static constexpr uint32_t TRACKER_LOCK_WINDOW_MS = 15000;  // 15 s to lock pending tracker
  SentChannelTracker _trackers[MAX_TRACKERS] = {};
  uint8_t _ourNodeHash = 0;  // first byte of companion public key, set on SELF_INFO
  void handleRxLog(const uint8_t* data, size_t len);

  char autoReconnectAddr[18] = {};
  uint8_t autoReconnectAddrType = 0;
  uint32_t connectPin = 123456;  // BLE pairing passkey (MeshCore default)

  // Notification receive ring-buffer.
  // Multiple BLE notifications can arrive during e-ink refresh (100-200 ms);
  // a single-slot buffer would silently overwrite earlier entries.
  // After init, the device can burst out a full contact list (one PKT_CONTACT
  // per contact) plus channels and messages before poll() drains them.
  // 24 slots × 256 bytes = 6 KB — keeps comfortable headroom over the realistic
  // contact-list burst (tested device sent 15+ packets before the next poll() call)
  // while trimming static RAM vs the previous 32-slot ring.
  static constexpr size_t RX_BUF_SIZE = 256;
  static constexpr uint8_t RX_QUEUE_SIZE = 24;
  struct RxEntry {
    uint8_t data[RX_BUF_SIZE];
    uint16_t len;
  };
  RxEntry rxQueue[RX_QUEUE_SIZE] = {};
  volatile uint8_t rxHead = 0;  // consumer index (poll / runInitSequence)
  volatile uint8_t rxTail = 0;  // producer index (notifyCallback)

  // Periodic message poll: re-fetch queued messages in case PKT_MSGS_WAITING was
  // overwritten in rxBuf during e-ink refresh (1-2s) before poll() could run.
  static constexpr uint32_t MESSAGE_POLL_INTERVAL_MS = 30000;  // 30s
  uint32_t lastMessagePollTime = 0;

  // Worker task for blocking BLE operations (scan, connect)
  enum class WorkType : uint8_t { SCAN, CONNECT, SHUTDOWN };
  struct WorkItem {
    WorkType type;
    uint32_t scanDurationSec;
    char address[18];
    uint8_t addressType;  // 0=public, 1=random
  };
  TaskHandle_t workerTaskHandle = nullptr;
  QueueHandle_t workQueue = nullptr;
  volatile bool workerRunning = false;

  void doScan(uint32_t durationSec);
  void doConnect(const char* bleAddress, uint8_t addressType);
  // Centralised failure cleanup for doConnect() error paths.
  // Skips bleClient teardown when cancelConnect is set (deinit() owns it).
  void failConnect(bool disconnectFirst);
  static void workerTaskFunc(void* param);

  // Static instance pointer for NimBLE callback (single client only)
  static MeshCoreClient* sInstance;

  bool enqueueCmd(const uint8_t* data, size_t len, uint8_t expectedResp);
  bool sendNextCmd();
  void processResponse(const uint8_t* data, size_t len);
  bool runInitSequence();

  static void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);

  void setState(BleConnectionState newState);
};
