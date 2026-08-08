#pragma once

#include <cstdint>

struct ConvMeta;
struct MeshCoreMessage;
class MeshCoreMessageStore;

/// Scroll state machine for MeshCoreThreadActivity.
/// Holds references to all mutable scroll-related fields so the full
/// scroll logic (including savePosition) lives in one place.
/// The Activity wrapper methods only add requestUpdate() calls.
struct ThreadScroller {
  ConvMeta& meta;
  MeshCoreMessage* const msgs;
  uint8_t& count;
  MeshCoreMessage& filler;
  uint32_t& accH;
  uint32_t& firstId;
  uint32_t& lastId;
  const int ch;

  MeshCoreMessageStore& store;
  const bool isCh;
  const uint8_t chIdx;
  const uint8_t* const pubkey;

  bool loadBatch(uint32_t startId, bool up);
  void savePos();

  void scrollDownPage();
  void scrollUpPage();
  void scrollToEnd();
  void scrollDownByMessage();
  void scrollUpByMessage();
};
