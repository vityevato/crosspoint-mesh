#pragma once

#include <cstdint>

struct ActivityResult;
class MeshCoreClient;
class MeshCoreMessageStore;
class MeshCoreThreadActivity;

/// Message-sending logic for MeshCoreThreadActivity.
/// Holds immutable conversation identity; the Activity passes itself
/// to onSendComplete() for access to mutable state (via friend).
struct ThreadMessenger {
  MeshCoreClient& client;
  MeshCoreMessageStore& store;
  const bool isCh;
  const uint8_t chIdx;
  const uint8_t* const pubkey;
  const char* name;
  int bodyFontId;

  /// Called from the KeyboardEntryActivity result callback.
  /// Appends to store, sends via BLE, and reloads the visible batch.
  void onSendComplete(MeshCoreThreadActivity& act, const ActivityResult& result);
};
