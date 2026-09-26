#include "ThreadReply.h"

#include <I18n.h>
#include <Logging.h>
#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "Memory.h"
#include "MeshCoreThreadActivity.h"
#include "components/OptionPopup.h"

void ThreadReply::refreshTargets(MeshCoreThreadActivity& act) {
  act._replyNames.clear();
  if (!act.isChannel || act._meta.count == 0) return;

  auto batch = makeUniqueNoThrow<MeshCoreMessage[]>(OptionPopup::MAX_OPTIONS);
  if (!batch) {
    // OOM: keep whatever was collected before the failure (nothing, here) and
    // let the caller degrade — an empty list leaves the menu item dimmed.
    LOG_ERR("MESH", "OOM: reply scan buffer");
    return;
  }

  // Channel messages carry no pubkey prefix on the wire (parseChannelMsg
  // zeroes it), so the sender name is the only identity available for
  // de-duplication.
  const std::string selfName = act.client.getCompanion().name;
  act._replyNames.reserve(OptionPopup::MAX_OPTIONS);

  uint32_t cursor = act._meta.endId;
  while (act._replyNames.size() < static_cast<size_t>(OptionPopup::MAX_OPTIONS) && cursor >= act._meta.startId) {
    uint8_t loaded = 0;
    if (!act.store.loadChannelMessages(act.channelIdx, cursor, static_cast<uint8_t>(OptionPopup::MAX_OPTIONS),
                                       /*up=*/true, batch.get(), loaded) ||
        loaded == 0) {
      break;
    }

    // The batch comes back ascending; walk it backwards so the newest sender
    // lands first in _replyNames.
    for (int i = loaded - 1; i >= 0; --i) {
      const MeshCoreMessage& msg = batch[i];
      if (msg.direction != MsgDirection::RECEIVED || msg.senderName[0] == '\0') continue;
      // Skip the echo of our own channel messages.
      if (!selfName.empty() && selfName == msg.senderName) continue;
      const bool seen = std::any_of(act._replyNames.begin(), act._replyNames.end(),
                                    [&](const std::string& name) { return name == msg.senderName; });
      if (seen) continue;
      act._replyNames.emplace_back(msg.senderName);
      if (act._replyNames.size() >= static_cast<size_t>(OptionPopup::MAX_OPTIONS)) break;
    }

    if (batch[0].id == 0) break;
    cursor = batch[0].id - 1;  // continue below the oldest message in the batch
  }

  LOG_DBG("MESH", "Reply targets: %u", static_cast<unsigned>(act._replyNames.size()));
}

void ThreadReply::openPicker(MeshCoreThreadActivity& act) {
  if (act._replyNames.empty()) {
    act._toast.show(tr(STR_MESHCORE_NO_REPLY_TARGETS), 3000);
    act.requestUpdate();
    return;
  }

  act._replyPopup.show(StrId::STR_MESHCORE_REPLY_TO_LAST, act._replyNames, 0, [&act](int idx) {
    if (idx < 0 || idx >= static_cast<int>(act._replyNames.size())) return;
    // MeshCore reply mention: "@[Name] " (with trailing space) at the start.
    char mention[80];
    snprintf(mention, sizeof(mention), "@[%s] ", act._replyNames[idx].c_str());
    act.sendMessage(mention);
  });
  act.requestUpdate();
}
