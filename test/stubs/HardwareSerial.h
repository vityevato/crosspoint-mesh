#pragma once

// ── Minimal HardwareSerial stub for host-side Google Test builds ───────
// The real HardwareSerial.h comes from the ESP32 Arduino framework.
// This stub provides just enough for Logging.h to compile when
// the Arduino framework is not available (CMake/CTest on macOS/Linux).

#include <Print.h>

class HWCDC {
 public:
  void begin(unsigned long /*baud*/) {}
  operator bool() const { return true; }
  size_t write(uint8_t) { return 1; }
  size_t write(const uint8_t*, size_t s) { return s; }
  void flush() {}
};

extern HWCDC Serial;
