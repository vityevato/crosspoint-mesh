#pragma once

#include <cstdint>
#include <string>

namespace HalSystem {
struct StackFrame {
  uint32_t sp;
  uint32_t spp[8];
};

// Last heap state recorded by the main loop, kept in RTC memory so a panic
// report can show how tight the heap was even when no USB serial log exists.
struct HeapSnapshot {
  uint32_t freeHeap;
  uint32_t minFreeHeap;
  uint32_t maxAllocHeap;
};

void begin();

// Record the current heap state into RTC memory. Cheap; call it from the main
// loop (throttled), not from ISR or panic context.
void sampleHeap();

// Dump panic info to SD card if necessary
void checkPanic();
void clearPanic();

std::string getPanicInfo(bool full = false);
bool isRebootFromPanic();
}  // namespace HalSystem
