#pragma once

// ── Minimal Arduino.h stub for host-side Google Test builds ─────────────
// The real Arduino.h comes from the ESP32 Arduino framework.  Logging.h
// pulls it in ahead of its Serial/Print helpers; those are provided by the
// dedicated stubs in this directory (Print.h, HardwareSerial.h), so an empty
// header is enough to satisfy the include.  It is only reached by tests that
// include lib/Logging/Logging.h (e.g. via T4InputEngine.h) and must stay free
// of ESP32-only declarations.
