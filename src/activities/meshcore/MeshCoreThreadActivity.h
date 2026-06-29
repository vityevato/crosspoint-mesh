#pragma once

#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdint>
#include <string>
#include <vector>

#include "StatusMessageOverlay.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

/**
 * MeshCoreThreadActivity shows a pixel-paginated message thread for either a
 * LoRa channel (group chat) or a direct message conversation with a
 * contact, with a two-tab UI:
 *  - MESSAGES — batch-loaded message list with send capability.
 *  - MENU — context-sensitive actions that differ for channels vs DMs.
 *
 * Only the visible batch (~10 messages) is kept in RAM. Messages are loaded
 * on demand from SD card using height-based batch queries. Scroll state is
 * managed via ConvMeta (positionId / positionPx / totalPx / fontId).
 *
 * Two constructors:
 *  - Channel thread: takes a channel index and name.
 *  - Direct message thread: takes a MeshCoreContact (stores pubkey).
 *
 * Tab navigation follows the same Settings-style pattern as the hub:
 *   selectedIndex == 0 → tab bar is highlighted, Confirm cycles tabs.
 *   selectedIndex > 0  → list item is highlighted, Confirm selects it.
 */
class MeshCoreThreadActivity final : public Activity {
 public:
  // Channel thread constructor
  MeshCoreThreadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, MeshCoreClient& client,
                         MeshCoreMessageStore& store, uint8_t channelIdx, const char* channelName);

  // Direct message thread constructor
  MeshCoreThreadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, MeshCoreClient& client,
                         MeshCoreMessageStore& store, const MeshCoreContact& contact);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum class Tab : uint8_t { MESSAGES = 0, MENU, TAB_COUNT };

  MeshCoreClient& client;
  MeshCoreMessageStore& store;
  ButtonNavigator buttonNavigator;

  bool isChannel = false;
  uint8_t channelIdx = 0;
  char threadName[64] = {};
  uint8_t contactPubkey[32] = {};

  // Batch loading — only visible messages, not the whole thread
  static constexpr uint8_t MAX_VISIBLE_BATCH = 10;
  MeshCoreMessage _visibleMsgs[MAX_VISIBLE_BATCH] = {};
  uint8_t _visibleCount = 0;

  // Cached conversation metadata (scroll state lives here)
  ConvMeta _meta = {};

  // Rendering bookkeeping (derived during draw, reset on scroll)
  uint16_t _accHeight = 0;
  uint32_t _firstVisibleId = 0;
  uint32_t _lastVisibleId = 0;

  // Cached font/layout for current session
  int _bodyFontId = 0;
  int _contentAreaWidth = 0;
  int _contentAreaHeight = 0;

  // Font recalculation state (async, non-blocking)
  enum class RecalcState : uint8_t { IDLE = 0, RUNNING, DONE };
  RecalcState _recalcState = RecalcState::IDLE;
  uint32_t _recalcGid = 0;
  uint32_t _recalcEndId = 0;
  uint16_t _recalcNewTotalPx = 0;
  ConvMeta _recalcMeta = {};
  int _recalcFontId = 0;
  int _recalcContentWidth = 0;
  bool _recalcIsChannel = false;

  // Tab state
  Tab currentTab = Tab::MESSAGES;
  int selectedIndex = 0;

  // Menu navigation states
  bool showingStatus = false;
  MeshCoreCompanion lastCompanion = {};
  uint32_t lastBatteryRequestMs = 0;
  uint16_t lastBatteryMv = 0;

  // Advert status feedback
  bool _advertInFlight = false;
  bool _advertIsFlood = false;
  uint32_t _advertSentTime = 0;

  // Ephemeral toast overlay
  StatusMessageOverlay _toast;

  int contentHeight() const;
  void loadVisibleBatch();
  void loadVisibleBatchUp();
  void drawVisibleMessages(const GfxRenderer& renderer, Rect rect, bool useReaderFontSettings,
                           bool scanOnly = false);
  void scrollDownPage();
  void scrollUpPage();
  void scrollDownByMessage();
  void scrollUpByMessage();
  void savePosition();
  void sendMessage();

  // Font recalculation helpers
  void _recalcStep();
  void _finishRecalc();

  void switchTab(Tab tab);
  int getListCountForCurrentTab() const;

  void renderMenu(const Rect& contentRect);

  /** Trampoline for StatusMessageOverlay subtitle provider. */
  static void provideSubtitle(const void* ctx, char* buf, size_t bufSize);

  /// Word-wrap message body text, respecting \n as hard line breaks.
  /// Normalizes \r\n → \n. Preserves empty lines from consecutive newlines.
  static std::vector<std::string> wrapMessageBody(const GfxRenderer& renderer, int fontId, const char* text,
                                                  int maxWidth, int maxLines);
};
