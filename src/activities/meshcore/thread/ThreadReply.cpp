#include "ThreadReply.h"

#include <I18n.h>
#include <Logging.h>
#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../utils/MeshCoreHeapLog.h"
#include "MeshCoreThreadActivity.h"
#include "components/OptionPopup.h"

void ThreadReply::refreshTargets(MeshCoreThreadActivity& act) {
  act._replyNames.clear();
  act._hasReplyTargets = false;
  if (!act.isChannel || act._meta.count == 0) return;

  // Channel messages carry no pubkey prefix on the wire (parseChannelMsg
  // zeroes it), so the sender name is the only identity available for
  // de-duplication. Compare C strings directly — no std::string copy.
  const char* selfName = act.client.getCompanion().name;
  act._replyNames.reserve(OptionPopup::MAX_OPTIONS);
  MESHCORE_LOG_HEAP("Menu refreshTargets:before");

  // Scan one message at a time into the activity's reusable scratch. The old
  // MeshCoreMessage[16] heap batch was ~4.3 KB transient; its allocation split
  // the largest free block and left the font prewarm without a contiguous
  // 8 KB run after the reply/repeat flow.
  uint32_t cursor = act._meta.endId;
  while (act._replyNames.size() < static_cast<size_t>(OptionPopup::MAX_OPTIONS) && cursor >= act._meta.startId) {
    uint8_t loaded = 0;
    if (!act.store.loadChannelMessages(act.channelIdx, cursor, 1, /*up=*/true, &act._scanMsg, loaded) || loaded == 0) {
      break;
    }
    const MeshCoreMessage& msg = act._scanMsg;
    if (msg.id == 0) break;  // corrupt or missing record

    if (msg.direction == MsgDirection::RECEIVED && msg.senderName[0] != '\0' &&
        !(selfName[0] != '\0' && strcmp(selfName, msg.senderName) == 0)) {
      const bool seen = std::any_of(act._replyNames.begin(), act._replyNames.end(),
                                    [&](const std::string& name) { return name == msg.senderName; });
      if (!seen) act._replyNames.emplace_back(msg.senderName);
    }
    cursor = msg.id - 1;  // continue below this message
  }

  LOG_DBG("MESH", "Reply targets: %u", static_cast<unsigned>(act._replyNames.size()));
  act._hasReplyTargets = !act._replyNames.empty();
  MESHCORE_LOG_HEAP("Menu refreshTargets:after");
}

void ThreadReply::openPicker(MeshCoreThreadActivity& act) {
  // The list is released when a reply is selected (to hand the composer a
  // cleaner heap); rebuild it lazily from the last known state.
  if (act._replyNames.empty() && act._hasReplyTargets) {
    refreshTargets(act);
  }
  if (act._replyNames.empty()) {
    act._toast.show(tr(STR_MESHCORE_NO_REPLY_TARGETS), 3000);
    act.requestUpdate();
    return;
  }

  MESHCORE_LOG_HEAP("Menu replyPicker:before popup");
  act._replyPopup.show(StrId::STR_MESHCORE_REPLY_TO_LAST, act._replyNames, 0, [&act](int idx) {
    if (idx < 0 || idx >= static_cast<int>(act._replyNames.size())) return;
    // MeshCore reply mention: "@[Name] " (with trailing space) at the start.
    char mention[80];
    snprintf(mention, sizeof(mention), "@[%s] ", act._replyNames[idx].c_str());
    // Release the target list while the composer activity is on top; the
    // popup's own copy is released by OptionPopup on selection.
    std::vector<std::string>().swap(act._replyNames);
    act.sendMessage(mention);
  });
  act.requestUpdate();
}
