#pragma once

#include <FreeInkUI.h>

#include <cstdint>

#include "components/UITheme.h"

// Theme-driven tab band shared by the FreeInkApp-hosted tab screens
// (UiTabListActivity) and MeshCore hub/thread, which draw through
// GfxRendererFrame. Keeping pill geometry, focused/unfocused treatments and
// the divider here means every tab bar in the firmware follows the active
// theme identically, whichever host draws it.
namespace theme_tab_bar {

inline constexpr int kMaxTabs = 8;
// Touch targets get a fixed comfortable band height, same as UiTabListActivity.
inline constexpr int16_t kTouchBarHeight = 50;
// Action id registered for tab hits on non-FreeInkApp hosts (MeshCore).
inline constexpr freeink::ui::ActionId kAction = 1;

// Band height for the active theme: the theme's tabBarHeight, enlarged when
// the tab label needs more room than the band leaves.
inline int16_t bandHeight(const freeink::ui::DrawTarget& target, const freeink::ui::ThemeTokens& theme,
                          const int16_t preferredHeight) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const freeink::ui::TextStyle& text = metrics.tabPillFullSlot ? theme.bodyText : theme.smallText;
  const int16_t lineHeight = target.lineHeight(text.font);
  return preferredHeight > lineHeight + 10 ? preferredHeight : static_cast<int16_t>(lineHeight + 10);
}

// Draw the tab band. `labels` has `count` entries; `active` is the selected
// index; `focused` is true while the band itself holds the selection (the
// legacy focused/unfocused distinction: the selected pill dims to a dither
// once the selection moves down into the list). Touch hits are registered
// under `action` with the tab index as value.
template <size_t N>
inline void build(freeink::ui::Frame<N>& frame, freeink::ui::DrawTarget& target, const freeink::ui::ThemeTokens& theme,
                  const freeink::ui::Rect band, const char* const* labels, const int count, const int active,
                  const bool focused, const freeink::ui::ActionId action) {
  namespace fui = freeink::ui;
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();

  const int tabCount = count < kMaxTabs ? count : kMaxTabs;
  fui::TabItem tabs[kMaxTabs];
  for (int i = 0; i < tabCount; i++) {
    tabs[i].label = labels[i];
    tabs[i].value = static_cast<int16_t>(i);
    tabs[i].selected = active == i;
  }

  fui::TabBarProps tabProps;
  tabProps.tabs = tabs;
  tabProps.count = static_cast<uint16_t>(tabCount);
  tabProps.action = action;
  tabProps.inputMask = fui::InputTouch;
  // Pill shape and label size are theme-driven. Lyra uses label-hugging pills
  // with small labels; full-slot (RoundedRaff) fills the equal-width slot with
  // body-size labels.
  if (metrics.tabPillFullSlot) {
    tabProps.text = theme.bodyText;
    tabProps.tabInset = fui::Insets{4, 4, 7, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  } else {
    tabProps.text = theme.smallText;
    tabProps.gap = static_cast<int16_t>(metrics.tabSpacing);
    // Unfocused state: no bottom inset, so the pill (and the 2px selected
    // underline drawn along its bottom edge) reaches the band's 1px divider.
    tabProps.tabInset = focused ? fui::Insets{2, 4, 4, 4} : fui::Insets{2, 4, 0, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  }
  const int16_t height = bandHeight(target, theme, band.height);
  const fui::Rect tabRect{band.x, band.y, band.width, height};

  tabProps.divider = true;
  fui::StyleSet tabStyles;
  tabStyles.explicitlySet = true;
  tabStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  if (focused) {
    tabStyles.selected.background = fui::Paint::solid(fui::Color::Black);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = theme.listRowRadius;
  } else if (metrics.tabPillFullSlot) {
    // Legacy RoundedRaff unfocused treatment: same pill, dimmed to dark gray,
    // text stays inverted; no underline.
    tabStyles.selected.background = fui::Paint::dither(fui::Color::DarkGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = theme.listRowRadius;
  } else {
    tabStyles.selected.background = fui::Paint::dither(fui::Color::LightGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::Black);
    tabProps.selectedUnderline = 2;
  }
  // Focus/flash states keep the pill instead of falling back to an unset
  // (blank) style.
  tabStyles.focused = tabStyles.selected;
  tabStyles.active = tabStyles.selected;
  tabProps.tabStyles = tabStyles;

  // Focused band wash is the Lyra treatment; full-slot themes keep the band
  // plain in both states.
  if (focused && !metrics.tabPillFullSlot) {
    target.fill(tabRect, fui::Paint::dither(fui::Color::LightGray));
  }
  fui::tabBar(frame, tabRect, tabProps);
}

}  // namespace theme_tab_bar
