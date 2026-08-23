#pragma once

// ── Minimal Print stub for host-side Google Test builds ─────────────────
// The real Print class comes from the Arduino framework (cores/esp32/).
// This stub provides just enough for Logging.h to compile when
// HardwareSerial.h / Print.h are not available (CMake/CTest on macOS/Linux).

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buf, size_t size) {
    size_t n = 0;
    while (size--) {
      n += write(*buf++);
    }
    return n;
  }
  size_t printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vprintf(fmt, args);
    va_end(args);
    return n >= 0 ? static_cast<size_t>(n) : 0;
  }
  virtual void flush() {}
  operator bool() const { return true; }
};
