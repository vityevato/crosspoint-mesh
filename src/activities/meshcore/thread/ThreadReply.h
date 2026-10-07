#pragma once

class MeshCoreThreadActivity;

/// Reply-target collection and reply picker for MeshCoreThreadActivity.
/// All methods are static — they take the Activity reference for state access
/// (via friend), mirroring ThreadMessenger / ThreadMenuRenderer.
struct ThreadReply {
  /// Refresh act._replySenders / _replySenderCount from the store's
  /// per-channel sender cache (newest first, capped at
  /// MESHCORE_MAX_RECENT_SENDERS). Runs on MENU tab entry and on picker open.
  /// @param buildIfMissing when true, a missing cache is built once with a
  ///        bounded backward scan of the stored messages (the picker's open
  ///        path); when false the read is strictly read-only and a missing
  ///        cache leaves the MENU item optimistically enabled.
  static void refreshTargets(MeshCoreThreadActivity& act, bool buildIfMissing = false);

  /// Open the option picker over the MENU tab. Selecting a name launches the
  /// normal send activity with a "@[Name] " mention prefilled.
  static void openPicker(MeshCoreThreadActivity& act);
};
