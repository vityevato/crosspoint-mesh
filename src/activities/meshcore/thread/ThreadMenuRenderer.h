#pragma once

class MeshCoreThreadActivity;
struct Rect;

/// Menu and popup rendering for MeshCoreThreadActivity.
/// All methods are static — they take the Activity reference for state access (via friend).
struct ThreadMenuRenderer {
  /// Returns true if the font-rebuild popup was rendered (consuming the frame).
  static bool renderFontRebuildPopup(MeshCoreThreadActivity& act);

  /// Returns true if a confirmation popup was rendered (consuming the frame).
  static bool renderConfirmPopup(MeshCoreThreadActivity& act);

  /// Render the MENU tab content within the given rect.
  static void renderMenu(MeshCoreThreadActivity& act, const Rect& contentRect);
};
