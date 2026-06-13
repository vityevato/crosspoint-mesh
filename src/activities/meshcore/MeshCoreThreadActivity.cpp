#include "MeshCoreThreadActivity.h"

#include <I18n.h>
#include <Logging.h>

#ifdef SIMULATOR
#include <MeshCoreMockHotkeys.h>
#endif

#include <cstring>
#include <string>

#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Channel thread constructor
MeshCoreThreadActivity::MeshCoreThreadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               MeshCoreClient& client, MeshCoreMessageStore& store, uint8_t channelIdx,
                                               const char* channelName)
    : Activity("MeshCoreThread", renderer, mappedInput),
      client(client),
      store(store),
      isChannel(true),
      channelIdx(channelIdx) {
  snprintf(threadName, sizeof(threadName), "%s", channelName);
}

// Direct message constructor
MeshCoreThreadActivity::MeshCoreThreadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               MeshCoreClient& client, MeshCoreMessageStore& store,
                                               const MeshCoreContact& contact)
    : Activity("MeshCoreThread", renderer, mappedInput), client(client), store(store), isChannel(false) {
  memcpy(contactPubkey, contact.publicKey, 32);
  snprintf(threadName, sizeof(threadName), "%s", contact.name);
}

void MeshCoreThreadActivity::onEnter() {
  Activity::onEnter();
  loadPage();
}

void MeshCoreThreadActivity::onExit() { Activity::onExit(); }

void MeshCoreThreadActivity::loadPage() {
  if (isChannel) {
    totalMessages = store.getChannelMessageCount(channelIdx);
  } else {
    totalMessages = store.getDirectMessageCount(contactPubkey);
  }

  // Start at most recent page
  if (pageOffset == 0 && totalMessages > MSGS_PER_PAGE) {
    pageOffset = totalMessages - MSGS_PER_PAGE;
  }

  if (isChannel) {
    store.loadChannelMessages(channelIdx, pageOffset, messages, MSGS_PER_PAGE, msgCount);
  } else {
    store.loadDirectMessages(contactPubkey, pageOffset, messages, MSGS_PER_PAGE, msgCount);
  }
  requestUpdate();
}

void MeshCoreThreadActivity::loop() {
  client.poll();

#ifdef SIMULATOR
  if (handleMockKey("Thread", client.getBleClient())) {
    requestUpdate();
    return;
  }
  pollMock(client.getBleClient(), millis());
#endif

  // Detect new messages appended to the store by the hub's callbacks.
  // If count grew and we are on the last page, reload so they appear.
  uint16_t currentTotal;
  if (isChannel) {
    currentTotal = store.getChannelMessageCount(channelIdx);
  } else {
    currentTotal = store.getDirectMessageCount(contactPubkey);
  }
  if (currentTotal > totalMessages) {
    bool onLastPage = (pageOffset + MSGS_PER_PAGE >= totalMessages);
    totalMessages = currentTotal;
    if (onLastPage) {
      pageOffset = (totalMessages > MSGS_PER_PAGE) ? totalMessages - MSGS_PER_PAGE : 0;
      if (isChannel) {
        store.loadChannelMessages(channelIdx, pageOffset, messages, MSGS_PER_PAGE, msgCount);
      } else {
        store.loadDirectMessages(contactPubkey, pageOffset, messages, MSGS_PER_PAGE, msgCount);
      }
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    sendMessage();
    return;
  }

  // Page navigation with Up/Down
  buttonNavigator.onPreviousRelease([this] { prevPage(); });
  buttonNavigator.onNextRelease([this] { nextPage(); });
}

void MeshCoreThreadActivity::sendMessage() {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_MESHCORE_SEND), "",
                                                                 MESHCORE_SEND_CHAR_LIMIT, InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             requestUpdate();
                             return;
                           }
                           const auto& text = std::get<KeyboardResult>(result.data).text;
                           if (text.empty()) {
                             requestUpdate();
                             return;
                           }

                           bool sent = false;
                           if (isChannel) {
                             sent = client.sendChannelMessage(channelIdx, text.c_str());
                           } else {
                             sent = client.sendDirectMessage(contactPubkey, text.c_str());
                           }

                           if (sent) {
                             LOG_INF("MESH", "Message queued");
                             // Build a local copy for display
                             MeshCoreMessage msg = {};
                             msg.direction = MsgDirection::SENT;
                             msg.type = isChannel ? MsgType::CHANNEL : MsgType::DIRECT;
                             msg.channelIdx = channelIdx;
                             msg.timestamp = static_cast<uint32_t>(millis() / 1000);
                             msg.deliveryStatus = DeliveryStatus::SENT;
                             snprintf(msg.text, sizeof(msg.text), "%s", text.c_str());

                             if (isChannel) {
                               store.appendChannelMessage(channelIdx, msg);
                             } else {
                               store.appendDirectMessage(contactPubkey, msg);
                             }
                             pageOffset = 0;  // Jump to latest
                             loadPage();
                           } else {
                             LOG_ERR("MESH", "Failed to queue message");
                             requestUpdate();
                           }
                         });
}

void MeshCoreThreadActivity::nextPage() {
  if (pageOffset + MSGS_PER_PAGE < totalMessages) {
    pageOffset += MSGS_PER_PAGE;
    loadPage();
  }
}

void MeshCoreThreadActivity::prevPage() {
  if (pageOffset >= MSGS_PER_PAGE) {
    pageOffset -= MSGS_PER_PAGE;
  } else {
    pageOffset = 0;
  }
  loadPage();
}

void MeshCoreThreadActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header with thread name
  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), threadName, nullptr);

  int contentTop = metrics.topPadding + metrics.headerHeight;
  int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.topPadding;

  if (msgCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, tr(STR_MESHCORE_NO_MESSAGES));
  } else {
    const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 2;
    const int msgBlockH = lineH * 2 + 4;  // sender line + text line + spacing
    int y = contentTop + 4;

    for (int i = 0; i < msgCount && y + msgBlockH <= contentTop + contentHeight; ++i) {
      const auto& msg = messages[i];
      const int x = metrics.contentSidePadding;

      // Sender/direction line
      char header[80];
      if (msg.direction == MsgDirection::SENT) {
        snprintf(header, sizeof(header), "> You");
      } else {
        snprintf(header, sizeof(header), "< %s", msg.senderName[0] ? msg.senderName : "Unknown");
      }
      renderer.drawText(UI_10_FONT_ID, x, y, header, true, EpdFontFamily::BOLD);
      y += lineH;

      // Message text (truncate at display width)
      renderer.drawText(UI_10_FONT_ID, x + 8, y, msg.text, true);
      y += lineH + 4;
    }
  }

  // Page indicator in sub-header area
  if (totalMessages > MSGS_PER_PAGE) {
    char pageInfo[32];
    uint16_t currentPage = (pageOffset / MSGS_PER_PAGE) + 1;
    uint16_t totalPages = (totalMessages + MSGS_PER_PAGE - 1) / MSGS_PER_PAGE;
    snprintf(pageInfo, sizeof(pageInfo), "%d/%d", currentPage, totalPages);
    int infoW = renderer.getTextWidth(UI_10_FONT_ID, pageInfo);
    renderer.drawText(UI_10_FONT_ID, pageWidth - infoW - metrics.contentSidePadding,
                      contentTop + contentHeight - renderer.getLineHeight(UI_10_FONT_ID), pageInfo, true);
  }

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_MESHCORE_SEND), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
