#pragma once

class MeshCoreThreadActivity;

/// Reply-target collection and reply picker for MeshCoreThreadActivity.
/// All methods are static — they take the Activity reference for state access
/// (via friend), mirroring ThreadMessenger / ThreadMenuRenderer.
struct ThreadReply {
  /// Collect the most recent channel senders into act._replyNames, newest
  /// first, capped at OptionPopup::MAX_OPTIONS. Runs on MENU tab entry (not
  /// per render). On OOM the scan stops early and keeps the names collected
  /// so far — the caller degrades to a shorter (or empty) picker.
  static void refreshTargets(MeshCoreThreadActivity& act);

  /// Open the option picker over the MENU tab. Selecting a name launches the
  /// normal send activity with a "@[Name] " mention prefilled.
  static void openPicker(MeshCoreThreadActivity& act);
};
