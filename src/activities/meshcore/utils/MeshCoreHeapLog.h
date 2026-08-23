#pragma once

#include <Logging.h>

// Heap diagnostics for the MeshCore activities.
//
// Prints both the total free heap and the largest contiguous block
// (ESP.getMaxAllocHeap()). The gap between the two reveals fragmentation:
// when free is large but largest is small, a single big allocation (e.g. the
// 34 KB font glyph group during prewarm) fails even though "enough" RAM is
// reported free.
//
// Tag "MEMM" (mesh-mem) is intentionally distinct so the whole memory trace
// can be grepped out of the serial/SD log in isolation.
#define MESHCORE_LOG_HEAP(label) \
  LOG_DBG("MEMM", "%s free=%u largest=%u", (label), ESP.getFreeHeap(), ESP.getMaxAllocHeap())
