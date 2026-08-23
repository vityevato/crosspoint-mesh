#include "T4LayoutSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <T4Layout.h>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace {

// The list shows a leading "None" entry (list index 0) followed by one entry
// per registered additional layout (list index 1..count).
constexpr int kNoneItemIndex = 0;

// Localized display name for a list row. Row 0 is "None"; rows 1..count map to
// additional layout (row - 1), resolved from its i18n ISO code (e.g. "RU" ->
// "Русский").
const char* itemDisplayName(int listIndex) {
  if (listIndex == kNoneItemIndex) return tr(STR_NONE_OPT);
  const uint8_t layoutIndex = static_cast<uint8_t>(listIndex - 1);
  const Language lang = I18n::languageFromCode(t4::getAdditionalLayoutI18nCode(layoutIndex));
  return I18N.getLanguageName(lang);
}

// List row currently selected by the saved setting.
int settingToListIndex(uint8_t setting) {
  if (setting == t4::kNoAdditionalLayout || setting >= t4::getAdditionalLayoutCount()) return kNoneItemIndex;
  return setting + 1;
}

}  // namespace

void T4LayoutSelectActivity::onEnter() {
  Activity::onEnter();

  // "None" + one row per additional layout.
  totalItems = t4::getAdditionalLayoutCount() + 1;
  selectedIndex = settingToListIndex(SETTINGS.t4AdditionalLayout);

  requestUpdate();
}

void T4LayoutSelectActivity::onExit() { Activity::onExit(); }

void T4LayoutSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void T4LayoutSelectActivity::handleSelection() {
  // Row 0 = "None"; rows 1..count map to additional layout (row - 1).
  SETTINGS.t4AdditionalLayout =
      (selectedIndex == kNoneItemIndex) ? t4::kNoAdditionalLayout : static_cast<uint8_t>(selectedIndex - 1);
  t4::setActiveAdditionalLayout(SETTINGS.t4AdditionalLayout);
  SETTINGS.saveToFile();

  // Return to previous page
  onBack();
}

void T4LayoutSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_ADDITIONAL_KEYBOARD_LAYOUT));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int currentListIndex = settingToListIndex(SETTINGS.t4AdditionalLayout);
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems, selectedIndex,
      [](int index) { return itemDisplayName(index); }, nullptr, nullptr,
      [currentListIndex](int index) { return index == currentListIndex ? tr(STR_SELECTED) : ""; }, true);

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
