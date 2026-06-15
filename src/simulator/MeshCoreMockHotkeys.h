#pragma once

// Keyboard mock hotkey handler for MeshCore BLE simulation.
// Uses SDL2 keyboard state to detect digit key presses (1-8).
// Dispatches only when MockSession::isMockActive().
// Keys inject events as if they arrived via Bluetooth.

#ifdef SIMULATOR

#include <Logging.h>
#include <SDL.h>

#include <cstdint>
#include <ctime>

#include "MockSession.h"
#include "NimBLEDevice.h"

namespace {

// Track previous-frame keyboard state for edge detection.
static uint8_t sPrevKeyState[SDL_NUM_SCANCODES] = {};

// Returns true if `sc` transitioned from released to pressed
// between the previous and current frames.
inline bool wasKeyPressedThisFrame(SDL_Scancode sc) {
  int numKeys = 0;
  const Uint8* state = SDL_GetKeyboardState(&numKeys);
  bool now = (sc < numKeys) ? (state[sc] != 0) : false;
  bool prev = (sc < (int)sizeof(sPrevKeyState)) ? (sPrevKeyState[sc] != 0) : false;
  sPrevKeyState[sc] = now ? 1 : 0;
  return now && !prev;
}

}  // namespace

// Check SDL keyboard for digit keys 1-6 and dispatch to mock handlers.
// Returns true if a mock key was consumed.
// `activityName` identifies current activity for log context.
// `bleClient` is the active NimBLEClient* (may be nullptr if not connected).
inline bool handleMockKey(const char* activityName, NimBLEClient* bleClient) {
  if (!MockSession::isMockActive()) return false;

  if (wasKeyPressedThisFrame(SDL_SCANCODE_1)) {
    // Inject BLE disconnect (connection loss)
    LOG_INF("MOCK", "[%s] key 1: inject disconnect", activityName);
    mockInjectDisconnect(bleClient);
    return true;
  }

  if (wasKeyPressedThisFrame(SDL_SCANCODE_2)) {
    // Inject a new contact into the mock session via TX notify.
    // Build PKT_CONTACT (0x03, 148 bytes) with generated name and pubkey.
    if (!bleClient || !bleClient->isConnected()) return false;

    ++MockSession::injectContactCounter;
    uint16_t n = MockSession::injectContactCounter;

    // Build PKT_CONTACT packet
    uint8_t buf[148] = {};
    size_t off = 0;

    buf[off++] = 0x03;  // PKT_CONTACT

    // Generate pseudo-random pubkey prefix from counter + name hash
    char hexBuf[65] = {};
    snprintf(hexBuf, sizeof(hexBuf), "MOCKPK%04X%04XCCCCCCCCCCCCCCCCCCCCCC", n, static_cast<uint16_t>(n * 0x9E37));
    // Convert hex stash to binary (first 64 hex chars → 32 binary bytes)
    for (int i = 0; i < 32; ++i) {
      uint8_t hi = static_cast<uint8_t>(hexBuf[i * 2]);
      uint8_t lo = static_cast<uint8_t>(hexBuf[i * 2 + 1]);
      auto nib = [](uint8_t c) -> uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
      };
      buf[off + i] = (nib(hi) << 4) | nib(lo);
    }
    off += 32;

    buf[off++] = 0;            // type = COMPANION
    buf[off++] = 0;            // flags
    buf[off++] = 0;            // out_path_len
    memset(buf + off, 0, 64);  // out_path
    off += 64;

    // Name: "Mock Contact N" (null-padded to 32)
    memset(buf + off, 0, 32);
    char nameBuf[33] = {};
    snprintf(nameBuf, sizeof(nameBuf), "Mock Contact %d", (int)n);
    size_t nameLen = strlen(nameBuf);
    if (nameLen > 31) nameLen = 31;
    memcpy(buf + off, nameBuf, nameLen);
    off += 32;

    uint32_t ts = 1640995200 + n;  // deterministic timestamp, varies per inject
    memcpy(buf + off, &ts, 4);
    off += 4;  // last_advert_timestamp
    memset(buf + off, 0, 4);
    off += 4;  // gps_lat
    memset(buf + off, 0, 4);
    off += 4;                   // gps_lon
    memcpy(buf + off, &ts, 4);  // lastmod

    bleClient->injectPacket(buf, sizeof(buf));
    LOG_INF("MOCK", "[%s] key 2: inject contact \"%s\"", activityName, nameBuf);
    return true;
  }

  if (wasKeyPressedThisFrame(SDL_SCANCODE_3)) {
    // Inject a new channel into the mock session via TX notify.
    // Build PKT_CHANNEL_INFO (0x12, 50 bytes) with generated name.
    if (!bleClient || !bleClient->isConnected()) return false;

    uint8_t idx = MockSession::injectChannelIdx;
    ++MockSession::injectChannelIdx;

    // Build PKT_CHANNEL_INFO packet
    uint8_t buf[50] = {};
    size_t off = 0;

    buf[off++] = 0x12;  // PKT_CHANNEL_INFO
    buf[off++] = idx;   // channel index

    // Name: "Mock Channel N" where N = idx (null-padded to 32)
    memset(buf + off, 0, 32);
    char nameBuf[33] = {};
    snprintf(nameBuf, sizeof(nameBuf), "Mock Channel %d", (int)idx);
    size_t nameLen = strlen(nameBuf);
    if (nameLen > 31) nameLen = 31;
    memcpy(buf + off, nameBuf, nameLen);
    off += 32;

    memset(buf + off, 0, 16);  // secret (all zeros = HASHTAG)

    bleClient->injectPacket(buf, sizeof(buf));
    LOG_INF("MOCK", "[%s] key 3: inject channel \"%s\" at idx %d", activityName, nameBuf, (int)idx);
    return true;
  }

  if (wasKeyPressedThisFrame(SDL_SCANCODE_4)) {
    // Inject an incoming channel message via TX notify.
    // Build PKT_CHANNEL_MSG_V3 (0x11) with "Mock incoming message".
    if (!bleClient || !bleClient->isConnected()) return false;

    const char* msgText = "Mock User: Mock incoming message";
    size_t textLen = strlen(msgText);
    size_t pktLen = 11 + textLen;
    uint8_t buf[256];
    size_t off = 0;
    buf[off++] = 0x11;  // PKT_CHANNEL_MSG_V3
    buf[off++] = 0;     // snr
    buf[off++] = 0;
    buf[off++] = 0;  // 2 reserved
    buf[off++] = 0;  // channelIdx = 0 (public channel)
    buf[off++] = 0;  // pathLen
    buf[off++] = 0;  // txtType
    uint32_t ts = static_cast<uint32_t>(time(nullptr));
    memcpy(buf + off, &ts, 4);
    off += 4;  // timestamp LE
    memcpy(buf + off, msgText, textLen);
    off += textLen;
    bleClient->injectPacket(buf, off);
    LOG_INF("MOCK", "[%s] key 4: inject channel message", activityName);
    return true;
  }

  if (wasKeyPressedThisFrame(SDL_SCANCODE_5)) {
    // Inject an incoming direct message via TX notify.
    // Build PKT_CONTACT_MSG_V3 (0x10) with "Mock DM".
    if (!bleClient || !bleClient->isConnected()) return false;

    const char* msgText = "Mock DM";
    size_t textLen = strlen(msgText);
    size_t pktLen = 16 + textLen;
    uint8_t buf[256];
    size_t off = 0;
    buf[off++] = 0x10;  // PKT_CONTACT_MSG_V3
    buf[off++] = 0;     // snr
    buf[off++] = 0;
    buf[off++] = 0;  // 2 reserved
    // Use the first contact's pubkey prefix if available
    const MockCompanion* comp = bleClient->getMockCompanion();
    if (comp && comp->contactCount > 0) {
      uint8_t pk6[6] = {};
      // Convert first 12 hex chars of contact 0's pubkey to 6 binary bytes
      for (int i = 0; i < 6 && comp->contacts[0].publicKey[i * 2]; ++i) {
        auto nib = [](uint8_t c) -> uint8_t {
          if (c >= '0' && c <= '9') return c - '0';
          if (c >= 'A' && c <= 'F') return c - 'A' + 10;
          if (c >= 'a' && c <= 'f') return c - 'a' + 10;
          return 0;
        };
        pk6[i] = (nib(comp->contacts[0].publicKey[i * 2]) << 4) | nib(comp->contacts[0].publicKey[i * 2 + 1]);
      }
      memcpy(buf + off, pk6, 6);
    } else {
      memset(buf + off, 0, 6);  // fallback: all zeros
    }
    off += 6;
    buf[off++] = 0;  // pathLen
    buf[off++] = 0;  // txtType
    uint32_t ts = static_cast<uint32_t>(time(nullptr));
    memcpy(buf + off, &ts, 4);
    off += 4;  // timestamp LE
    memcpy(buf + off, msgText, textLen);
    off += textLen;
    bleClient->injectPacket(buf, off);
    LOG_INF("MOCK", "[%s] key 5: inject DM", activityName);
    return true;
  }

  if (wasKeyPressedThisFrame(SDL_SCANCODE_6)) {
    // Trigger status refresh by injecting PKT_ADVERTISEMENT,
    // which causes Hub's advertCb → requestBattery() → CMD_GET_BATTERY
    // → mock responds with PKT_BATTERY.
    if (!bleClient || !bleClient->isConnected()) return false;

    const MockCompanion* comp = bleClient->getMockCompanion();
    if (!comp) return false;

    // Build PKT_ADVERTISEMENT (0x80): [0x80][32-byte pubkey] = 33 bytes
    uint8_t buf[33] = {};
    buf[0] = 0x80;  // PKT_ADVERTISEMENT
    // Convert companion's hex pubkey (64 chars) to 32 binary bytes
    for (int i = 0; i < 32 && comp->publicKey[i * 2]; ++i) {
      auto nib = [](uint8_t c) -> uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
      };
      buf[1 + i] = (nib(comp->publicKey[i * 2]) << 4) | nib(comp->publicKey[i * 2 + 1]);
    }
    bleClient->injectPacket(buf, sizeof(buf));
    LOG_INF("MOCK", "[%s] key 6: status refresh", activityName);
    return true;
  }

  if (wasKeyPressedThisFrame(SDL_SCANCODE_7)) {
    // Inject PKT_OK (0x00) to simulate advert success
    if (!bleClient || !bleClient->isConnected()) return false;
    uint8_t buf[] = {0x00};  // PKT_OK
    bleClient->injectPacket(buf, sizeof(buf));
    LOG_INF("MOCK", "[%s] key 7: inject advert success", activityName);
    return true;
  }

  if (wasKeyPressedThisFrame(SDL_SCANCODE_8)) {
    // Inject PKT_OK (0x00) to simulate flood advert success
    if (!bleClient || !bleClient->isConnected()) return false;
    uint8_t buf[] = {0x00};  // PKT_OK
    bleClient->injectPacket(buf, sizeof(buf));
    LOG_INF("MOCK", "[%s] key 8: inject flood advert success", activityName);
    return true;
  }

  return false;
}

#else  // !SIMULATOR

// Production builds: always return false (no mock)
inline bool handleMockKey(const char* activityName, void* bleClient) {
  (void)activityName;
  (void)bleClient;
  return false;
}

#endif  // SIMULATOR
