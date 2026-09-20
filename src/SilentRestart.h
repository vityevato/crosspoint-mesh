#pragma once

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

void silentRestart();          // home screen
void silentRestartToReader();  // currently-open EPUB (APP_STATE.openEpubPath)

// Heap-defrag reboot before entering MeshCore from Home. The hub session starts
// from a fresh heap, so prewarm and SD-font arenas get the whole free block
// instead of the fragmented remains of earlier activities (Settings/font
// changes, reading, T4). setup() routes straight to the hub after the reboot.
void silentRestartToMeshCore();

// Reboots immediately after an activity releases exclusive raw storage. The
// RTC target ensures setup() lands on Home instead of resuming a reader.
void restartToHomeAfterStorageHandoff();
