#pragma once

#include <GfxRenderer.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class MappedInputManager;

/**
 * Activity for selecting the additional T4 keyboard layout — the configurable
 * middle slot of the English -> Additional -> Digits cycle. All registered
 * layouts are listed regardless of whether their dictionary is installed on
 * the SD card (a layout without a .trie still works in Multi-tap).
 */
class T4LayoutSelectActivity final : public Activity {
 public:
  explicit T4LayoutSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("T4LayoutSelect", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();
  void onBack() { finish(); }

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  int totalItems = 0;
};
