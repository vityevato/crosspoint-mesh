#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstddef>
#include <cstdint>

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
  bool sendSelfAdvert(bool flood);

  bool isCommandPending() const { return cmdPending; }

  // Callbacks (set before connect)
  void setStateCallback(StateCallback cb, void* ctx);
  void setMessageCallback(MessageCallback cb, void* ctx);
  void setContactCallback(ContactCallback cb, void* ctx);
  void setAdvertCallback(AdvertCallback cb, void* ctx);
  void setChannelCallback(ChannelCallback cb, void* ctx);
  void setPinCallback(PinCallback cb, void* ctx);

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

  // Command queue (simple ring buffer)
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
  uint32_t cmdSentTime = 0;

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

  char autoReconnectAddr[18] = {};
  uint8_t autoReconnectAddrType = 0;
  uint32_t connectPin = 123456;  // BLE pairing passkey (MeshCore default)

  // Notification receive ring-buffer.
  // Multiple BLE notifications can arrive during e-ink refresh (100-200 ms);
  // a single-slot buffer would silently overwrite earlier entries.
  // 8 slots × 256 bytes = 2 KB — sufficient to hold a full contact list burst.
  static constexpr size_t RX_BUF_SIZE = 256;
  static constexpr uint8_t RX_QUEUE_SIZE = 8;
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
