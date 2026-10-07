#include "ThreadReply.h"

#include <I18n.h>
#include <Logging.h>
#include <MeshCore/MeshCoreClient.h>
#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

#include <cstdio>
#include <cstring>

#include "../utils/MeshCoreHeapLog.h"
#include "MeshCoreThreadActivity.h"
#include "components/OptionPopup.h"

void ThreadReply::refreshTargets(MeshCoreThreadActivity& act, bool buildIfMissing) {
  act._replySenderCount = 0;
  act._hasReplyTargets = false;
  if (!act.isChannel || act._meta.count == 0) return;

  MESHCORE_LOG_HEAP("Menu refreshTargets:before");
  bool cachePresent = true;
  const uint8_t count = buildIfMissing ? act.store.loadRecentChannelSenders(act.channelIdx, act._replySenders,
                                                                            MESHCORE_MAX_RECENT_SENDERS)
                                       : act.store.readRecentChannelSenders(act.channelIdx, act._replySenders,
                                                                            MESHCORE_MAX_RECENT_SENDERS, cachePresent);

  // Channel messages carry no pubkey prefix on the wire (parseChannelMsg
  // zeroes it), so the sender name is the only identity available. Own
  // messages are stored as SENT and never reach the cache; drop our own name
  // defensively anyway.
  uint8_t kept = count;
  const char* selfName = act.client.getCompanion().name;
  if (selfName[0] != '\0') {
    kept = 0;
    for (uint8_t i = 0; i < count; ++i) {
      if (act._replySenders[i][0] == '\0' || strcmp(act._replySenders[i], selfName) == 0) continue;
      if (kept != i) memcpy(act._replySenders[kept], act._replySenders[i], sizeof(act._replySenders[kept]));
      ++kept;
    }
  }
  act._replySenderCount = kept;
  // A legacy thread without senders.bin may still have targets: keep the MENU
  // item enabled so the picker can run its one-time backfill on open.
  act._hasReplyTargets = kept > 0 || !cachePresent;
  LOG_DBG("MESH", "Reply targets: %u", static_cast<unsigned>(kept));
  MESHCORE_LOG_HEAP("Menu refreshTargets:after");
}

void ThreadReply::openPicker(MeshCoreThreadActivity& act) {
  // Refresh on open (one cache read; on legacy threads this builds the cache
  // with a one-time bounded scan) so the list reflects senders cached since
  // the MENU tab was entered.
  refreshTargets(act, /*buildIfMissing=*/true);
  if (act._replySenderCount == 0) {
    act._toast.show(tr(STR_MESHCORE_NO_REPLY_TARGETS), 3000);
    act.requestUpdate();
    return;
  }

  const char* labels[MESHCORE_MAX_RECENT_SENDERS];
  for (uint8_t i = 0; i < act._replySenderCount; ++i) labels[i] = act._replySenders[i];

  MESHCORE_LOG_HEAP("Menu replyPicker:before popup");
  act._replyPopup.show(tr(STR_MESHCORE_REPLY_TO_LAST), labels, act._replySenderCount, 0, [&act](int idx) {
    if (idx < 0 || idx >= static_cast<int>(act._replySenderCount)) return;
    // MeshCore reply mention: "@[Name] " (with trailing space) at the start.
    char mention[80];
    snprintf(mention, sizeof(mention), "@[%s] ", act._replySenders[idx]);
    act.sendMessage(mention);
  });
  act.requestUpdate();
}
