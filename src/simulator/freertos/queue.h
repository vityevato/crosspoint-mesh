#pragma once
#include <freertos/FreeRTOS.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// Missing FreeRTOS macros not in the simulator's FreeRTOS.h
#ifndef pdPASS
#define pdPASS pdTRUE
#endif
#ifndef pdFAIL
#define pdFAIL pdFALSE
#endif

// pdMS_TO_TICKS: convert milliseconds to FreeRTOS ticks.
// The simulator uses portTICK_PERIOD_MS = 1, so 1 tick = 1 ms.
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(xTimeInMs) (static_cast<uint32_t>(xTimeInMs))
#endif

// vTaskDelay: sleep the calling thread for xTicksToDelay ticks.
inline void vTaskDelay(uint32_t xTicksToDelay) {
  std::this_thread::sleep_for(std::chrono::milliseconds(xTicksToDelay));
}

// A simple thread-safe queue that mirrors the FreeRTOS xQueue API shape.
// Backed by std::queue<std::vector<uint8_t>> so items are stored by value
// (matching FreeRTOS' "copy into queue storage" semantics).
struct SimQueue {
  std::queue<std::vector<uint8_t>> q;
  std::mutex mtx;
  std::condition_variable cv;
  size_t itemSize = 0;
  size_t maxItems = 0;
};
typedef SimQueue* QueueHandle_t;

inline QueueHandle_t xQueueCreate(uint32_t uxQueueLength, uint32_t uxItemSize) {
  auto* q = new SimQueue();
  q->itemSize = uxItemSize;
  q->maxItems = uxQueueLength;
  return q;
}

inline int xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue, uint32_t /*xTicksToWait*/) {
  if (!xQueue) return 0;
  std::vector<uint8_t> item(xQueue->itemSize);
  memcpy(item.data(), pvItemToQueue, xQueue->itemSize);
  {
    std::lock_guard<std::mutex> lk(xQueue->mtx);
    xQueue->q.push(std::move(item));
  }
  xQueue->cv.notify_one();
  return 1;  // pdTRUE
}

inline int xQueueReceive(QueueHandle_t xQueue, void* pvBuffer, uint32_t xTicksToWait) {
  if (!xQueue) return 0;
  std::unique_lock<std::mutex> lk(xQueue->mtx);
  if (xTicksToWait == portMAX_DELAY) {
    xQueue->cv.wait(lk, [xQueue] { return !xQueue->q.empty(); });
  } else if (xTicksToWait > 0) {
    bool got =
        xQueue->cv.wait_for(lk, std::chrono::milliseconds(xTicksToWait), [xQueue] { return !xQueue->q.empty(); });
    if (!got) return 0;
  } else {
    if (xQueue->q.empty()) return 0;
  }
  auto item = std::move(xQueue->q.front());
  xQueue->q.pop();
  lk.unlock();
  memcpy(pvBuffer, item.data(), xQueue->itemSize);
  return 1;  // pdTRUE
}

// Simulator-specific cleanup helper (call when destroying a queue).
inline void vQueueDelete(QueueHandle_t xQueue) {
  if (xQueue) delete xQueue;
}
