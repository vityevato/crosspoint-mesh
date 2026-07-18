#include "ThreadScroller.h"

#include <Logging.h>

#include <MeshCore/MeshCoreMessageStore.h>
#include <MeshCore/MeshCoreTypes.h>

bool ThreadScroller::loadBatch(uint32_t startId, bool up) {
  uint8_t loaded = 0;
  if (isCh) {
    store.loadChannelMessages(chIdx, startId, static_cast<uint16_t>(ch), up, msgs, loaded, filler);
  } else {
    store.loadDirectMessages(pubkey, startId, static_cast<uint16_t>(ch), up, msgs, loaded, filler);
  }
  count = loaded;
  if (loaded > 0) {
    firstId = msgs[0].id;
    lastId = msgs[loaded - 1].id;
    accH = 0;
    for (uint8_t i = 0; i < loaded; ++i) {
      accH += msgs[i].heightPx;
      if (accH > static_cast<uint16_t>(ch)) {
        accH -= msgs[i].heightPx;
        lastId = msgs[i - 1].id;
        break;
      }
    }
    return true;
  }
  return false;
}

void ThreadScroller::savePos() {
  if (isCh) {
    store.saveChannelMeta(chIdx, meta);
  } else {
    store.saveDirectMeta(pubkey, meta);
  }
}

void ThreadScroller::scrollDownPage() {
  LOG_DBG("MESH", "scrollDownPage: posPx=%u accH=%u totalPx=%u ch=%d lastId=%u endId=%u", meta.positionPx, accH,
          meta.totalPx, ch, lastId, meta.endId);
  if (meta.totalPx <= static_cast<uint16_t>(ch)) {
    LOG_DBG("MESH", "scrollDownPage: skip — fits in one page");
    return;
  }
  if (lastId >= meta.endId) {
    LOG_DBG("MESH", "scrollDownPage: skip — already at end");
    return;
  }
  meta.positionId = lastId + 1;
  meta.positionPx += accH;
  LOG_DBG("MESH", "scrollDownPage: advancing (posPx=%u posId=%u)", meta.positionPx, meta.positionId);
  loadBatch(meta.positionId > 0 ? meta.positionId : meta.startId, false);
  savePos();
}

void ThreadScroller::scrollUpPage() {
  LOG_DBG("MESH", "scrollUpPage: posPx=%u accH=%u posId=%u startId=%u", meta.positionPx, accH, meta.positionId,
          meta.startId);
  if (meta.positionPx == 0) {
    LOG_DBG("MESH", "scrollUpPage: skip — positionPx==0");
    return;
  }
  uint32_t newStartId = meta.positionId > 0 ? meta.positionId - 1 : meta.startId;
  loadBatch(newStartId, true);
  if (count == 0) {
    LOG_DBG("MESH", "scrollUpPage: no messages loaded from newStartId=%u", newStartId);
    return;
  }
  if (firstId <= meta.startId) {
    LOG_DBG("MESH", "scrollUpPage: hit top — reload from startId=%u down", meta.startId);
    meta.positionPx = 0;
    meta.positionId = meta.startId;
    loadBatch(meta.startId, false);
    savePos();
    return;
  }
  LOG_DBG("MESH", "scrollUpPage: loaded %d msgs [%u..%u] (posPx: %u - %u)", count, firstId, lastId, meta.positionPx,
          accH);
  meta.positionId = firstId;
  meta.positionPx = (meta.positionPx >= accH) ? meta.positionPx - accH : 0;
  LOG_DBG("MESH", "scrollUpPage: final posPx=%u posId=%u", meta.positionPx, meta.positionId);
  savePos();
}

void ThreadScroller::scrollToEnd() {
  if (meta.count == 0) return;
  loadBatch(meta.endId, true);
  if (meta.totalPx > static_cast<uint16_t>(ch)) {
    meta.positionPx = meta.totalPx - static_cast<uint16_t>(ch);
  } else {
    meta.positionPx = 0;
  }
  meta.positionId = (count > 0) ? msgs[0].id : meta.endId;
  savePos();
}

void ThreadScroller::scrollDownByMessage() {
  LOG_DBG("MESH", "scrollDownByMsg: posPx=%u accH=%u lastId=%u endId=%u", meta.positionPx, accH, lastId, meta.endId);
  if (meta.count == 0) return;
  if (lastId >= meta.endId) return;
  meta.positionId = lastId + 1;
  meta.positionPx += accH;
  uint32_t newStartId = meta.positionId > 0 ? meta.positionId : meta.startId;
  loadBatch(newStartId, true);
  if (count == 0) {
    LOG_DBG("MESH", "scrollDownByMsg: no messages loaded from newStartId=%u", newStartId);
    return;
  }
  meta.positionId = firstId;
  meta.positionPx = (meta.positionPx >= accH) ? meta.positionPx - accH + msgs[count - 1].heightPx : 0;
  LOG_DBG("MESH", "scrollDownByMsg: final posPx=%u posId=%u", meta.positionPx, meta.positionId);
  savePos();
  if (meta.positionPx > meta.totalPx - static_cast<uint16_t>(ch)) {
    meta.positionPx = (meta.totalPx > static_cast<uint16_t>(ch)) ? meta.totalPx - static_cast<uint16_t>(ch) : 0;
  }
  loadBatch(meta.positionId > 0 ? meta.positionId : meta.startId, false);
  savePos();
}

void ThreadScroller::scrollUpByMessage() {
  LOG_DBG("MESH", "scrollUpByMsg: posPx=%u posId=%u startId=%u", meta.positionPx, meta.positionId, meta.startId);
  if (meta.positionPx == 0 && meta.positionId <= meta.startId) return;
  MeshCoreMessage prevMsg;
  uint8_t prevLoaded = 0;
  uint32_t searchId = meta.positionId > 0 ? meta.positionId - 1 : 0;
  if (isCh) {
    store.loadChannelMessages(chIdx, searchId, static_cast<uint8_t>(1), true, &prevMsg, prevLoaded);
  } else {
    store.loadDirectMessages(pubkey, searchId, static_cast<uint8_t>(1), true, &prevMsg, prevLoaded);
  }
  if (prevLoaded == 0) return;
  meta.positionId = prevMsg.id;
  loadBatch(meta.positionId, false);
  if (count == 0) return;
  meta.positionPx = (meta.positionPx >= prevMsg.heightPx) ? meta.positionPx - prevMsg.heightPx : 0;
  meta.positionId = firstId;
  savePos();
}
