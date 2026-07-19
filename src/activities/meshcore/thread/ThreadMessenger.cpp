#include "ThreadMessenger.h"

#include <Logging.h>
#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include "../utils/MeshCoreMessageHeight.h"
#include "MeshCoreThreadActivity.h"
#include "activities/Activity.h"
#include "components/UITheme.h"

void ThreadMessenger::onSendComplete(MeshCoreThreadActivity& act, const ActivityResult& result) {
  if (result.isCancelled) {
    act.requestUpdate();
    return;
  }
  const auto& text = std::get<KeyboardResult>(result.data).text;
  if (text.empty()) {
    act.requestUpdate();
    return;
  }

  // For DMs: append to store FIRST to obtain the assigned id,
  // then send with that id for delivery tracking.
  MeshCoreMessage msg = {};
  msg.direction = MsgDirection::SENT;
  msg.type = isCh ? MsgType::CHANNEL : MsgType::DIRECT;
  msg.channelIdx = chIdx;
  msg.timestamp = static_cast<uint32_t>(millis() / 1000);
  msg.deliveryStatus = DeliveryStatus::SENT;
  snprintf(msg.text, sizeof(msg.text), "%s", text.c_str());

  // Compute rendered height for batch-load scrolling
  const auto& tmetrics = UITheme::getInstance().getMetrics();
  int tcontentWidth = act.renderer.getScreenWidth() - 2 * tmetrics.contentSidePadding;
  msg.heightPx = measureMeshCoreMessageHeight(act.renderer, bodyFontId, tcontentWidth, isCh, msg, tmetrics);

  bool sent = false;
  if (isCh) {
    sent = client.sendChannelMessage(chIdx, text.c_str());
    if (sent) store.appendChannelMessage(chIdx, msg);
  } else {
    // Append first to obtain the assigned id
    uint32_t msgId = 0;
    if (store.appendDirectMessage(pubkey, msg, &msgId)) {
      // Now send with the store-assigned id for delivery tracking.
      // Load the real contact from the store to get pathLength
      // (0xFF = no path -> start at flood; otherwise -> direct).
      MeshCoreContact contact = {};
      memcpy(contact.publicKey, pubkey, 32);
      snprintf(contact.name, sizeof(contact.name), "%s", name);
      // Try to load saved contact for pathLength
      constexpr uint8_t kMaxSend = 20;
      MeshCoreContact saved[kMaxSend] = {};
      uint8_t savedCount = store.loadContacts(saved, kMaxSend);
      for (uint8_t i = 0; i < savedCount; ++i) {
        if (memcmp(saved[i].publicKey, pubkey, 32) == 0) {
          contact.pathLength = saved[i].pathLength;
          contact.type = saved[i].type;
          break;
        }
      }
      sent = client.sendDirectMessage(contact, text.c_str(), msgId);
      if (!sent) {
        // Send failed -- update the persisted record to FAILED
        store.updateDirectMessage(pubkey, msgId, DeliveryStatus::FAILED);
      }
    }
  }

  if (sent) {
    LOG_INF("MESH", "Message queued");
    // Reload meta and batch-load from end
    if (isCh) {
      store.getChannelMeta(chIdx, act._meta);
    } else {
      store.getDirectMeta(pubkey, act._meta);
    }
    act.loadMessages(act._meta.endId, true);
    act._meta.positionPx = (act._meta.totalPx > static_cast<uint16_t>(act._contentAreaHeight))
                               ? act._meta.totalPx - static_cast<uint16_t>(act._contentAreaHeight)
                               : 0;
    act._meta.positionId = (act._visibleCount > 0) ? act._visibleMsgs[0].id : act._meta.endId;
    act.savePosition();
  } else {
    LOG_ERR("MESH", "Failed to queue message");
    act.requestUpdate();
  }
}
