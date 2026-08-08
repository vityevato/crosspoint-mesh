#pragma once

// stdin-driven automation interface for the desktop simulator.
//
// Lets external tools (AI agents, test scripts) drive the firmware UI
// without OS-level keyboard-event injection. A background thread reads
// command lines from stdin and synthesizes SDL keyboard events matching the
// simulator's key→button mapping (see HalGPIO.cpp in the crosspoint-
// simulator library: Escape=BACK, Return=CONFIRM, arrows=LEFT/RIGHT/UP/
// DOWN, P=POWER).
//
// Protocol — one command per line, case-insensitive. Replies go to STDOUT,
// log output goes to STDERR, so the two streams never mix:
//
//   HELP                 list commands
//   TAP <btn>            short press (~60 ms)
//   PRESS <btn>          press and hold until RELEASE
//   RELEASE <btn>        release a held button
//   HOLD <btn> <ms>      press, hold <ms>, release (long-press actions)
//   SCREENSHOT           save framebuffer BMP to /screenshots (fs_/screenshots)
//   QUIT                 exit the simulator
//
//   <btn> = BACK | CONFIRM | LEFT | RIGHT | UP | DOWN | POWER  (or 0..6)
//
// Replies: `OK ...` on success, `ERR <reason>` on malformed input.
//
// Example agent session (FIFO kept open read-write so stdin never hits EOF).
// The control FIFO and logs live under fs_/tmp/ — the simulator's own data
// dir — so no /tmp sandboxing/permission issues get in the way:
//   mkdir -p fs_/tmp
//   mkfifo fs_/tmp/xp_in
//   exec 9<>fs_/tmp/xp_in
//   .pio/build/simulator/program < fs_/tmp/xp_in > fs_/tmp/xp_sim.log 2>&1 &
//   echo "TAP CONFIRM" >&9
//   echo "SCREENSHOT" >&9
//   ./src/simulator/convert_screenshot.sh   # newest BMP -> fs_/tmp/*.png

#ifdef SIMULATOR

namespace SimulatorControl {

// Spawns the stdin reader thread. Idempotent — call unconditionally from
// loop(); the thread starts on the first invocation. Must not run before
// SDL is initialized, which the first loop() iteration guarantees.
void begin();

// True once per SCREENSHOT command received on stdin (consume-on-read).
bool consumeScreenshotRequest();

}  // namespace SimulatorControl

#endif
