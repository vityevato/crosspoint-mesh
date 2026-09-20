#pragma once

// Heap accounting helpers for diagnosing where RAM goes.
//
// All lines use the "MEM" tag so the whole trace can be grepped out of
// /system.log. Each snapshot carries the free total, the largest contiguous
// block and the heap block counts: the block counts are the fragmentation
// metric — they grow when short-lived allocations churn the heap even if the
// free total stays flat.
//
// Boot stages run before the SD log sink exists, so recordStage() buffers a
// snapshot in RAM and flushStages() replays the sequence once logging is ready.

#include <Arduino.h>
#include <Logging.h>

#include <cstdint>

#ifndef SIMULATOR
#include <esp_heap_caps.h>
#endif

#ifndef SIMULATOR
extern "C" {
extern int _data_start, _data_end, _bss_start, _bss_end, _iram_start, _iram_end;
}
#endif

namespace heap_log {

struct Snapshot {
  uint32_t freeHeap = 0;
  uint32_t largest = 0;
  uint32_t minFree = 0;
  uint32_t heapTotal = 0;
  uint32_t freeBlocks = 0;
  uint32_t totalBlocks = 0;
};

inline Snapshot capture() {
  Snapshot s;
  s.freeHeap = ESP.getFreeHeap();
  s.largest = ESP.getMaxAllocHeap();
  s.minFree = ESP.getMinFreeHeap();
  s.heapTotal = ESP.getHeapSize();
#ifndef SIMULATOR
  multi_heap_info_t info = {};
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s.freeBlocks = static_cast<uint32_t>(info.free_blocks);
  s.totalBlocks = static_cast<uint32_t>(info.total_blocks);
#endif
  return s;
}

inline void logSnapshot(const char* label) {
  const Snapshot s = capture();
  LOG_DBG("MEM", "%s: free=%u largest=%u min=%u total=%u blocks=%u/%u", label, static_cast<unsigned>(s.freeHeap),
          static_cast<unsigned>(s.largest), static_cast<unsigned>(s.minFree), static_cast<unsigned>(s.heapTotal),
          static_cast<unsigned>(s.freeBlocks), static_cast<unsigned>(s.totalBlocks));
}

// One-time static RAM layout: where the data-accessible SRAM went. IRAM code
// and .data/.bss are linker-placed; the heap total is measured at runtime.
inline void logLayout() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t heapTotal = ESP.getHeapSize();
#ifndef SIMULATOR
  const auto span = [](const int& from, const int& to) -> uint32_t {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&to) - reinterpret_cast<uintptr_t>(&from));
  };
  const uint32_t data = span(_data_start, _data_end);
  const uint32_t bss = span(_bss_start, _bss_end);
  const uint32_t iram = span(_iram_start, _iram_end);
  LOG_INF("MEM", "layout: iram=%u data=%u bss=%u heap=%u free=%u static=%u", static_cast<unsigned>(iram),
          static_cast<unsigned>(data), static_cast<unsigned>(bss), static_cast<unsigned>(heapTotal),
          static_cast<unsigned>(freeHeap), static_cast<unsigned>(data + bss));
#else
  LOG_INF("MEM", "layout: heap=%u free=%u", static_cast<unsigned>(heapTotal), static_cast<unsigned>(freeHeap));
#endif
}

inline constexpr uint8_t MAX_BOOT_STAGES = 12;

struct StageEntry {
  const char* label = nullptr;
  Snapshot snap;
};

inline StageEntry bootStages[MAX_BOOT_STAGES];
inline uint8_t bootStageCount = 0;

inline void recordStage(const char* label) {
  if (bootStageCount >= MAX_BOOT_STAGES) return;
  bootStages[bootStageCount].label = label;
  bootStages[bootStageCount].snap = capture();
  bootStageCount++;
}

inline void flushStages() {
  for (uint8_t i = 0; i < bootStageCount; i++) {
    const StageEntry& e = bootStages[i];
    LOG_DBG("MEM", "boot[%u] %s: free=%u largest=%u min=%u total=%u blocks=%u/%u", static_cast<unsigned>(i), e.label,
            static_cast<unsigned>(e.snap.freeHeap), static_cast<unsigned>(e.snap.largest),
            static_cast<unsigned>(e.snap.minFree), static_cast<unsigned>(e.snap.heapTotal),
            static_cast<unsigned>(e.snap.freeBlocks), static_cast<unsigned>(e.snap.totalBlocks));
  }
  bootStageCount = 0;
}

}  // namespace heap_log

#define HEAP_LOG(label) heap_log::logSnapshot(label)
#define HEAP_STAGE(label) heap_log::recordStage(label)
