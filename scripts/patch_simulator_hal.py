"""
PlatformIO pre-build script: restore CrossPoint Mesh's older HAL surface on the
fetched simulator libdep.

The simulator is developed against upstream `crosspoint-reader`, which migrated
`HalPowerManager` energy saving to the IDLE_DOWNCLOCK_MS / IDLE_LIGHT_SLEEP_MS /
lightSleep scheme. CrossPoint Mesh (a fork) still drives the older
`IDLE_POWER_SAVING_MS` constant from src/main.cpp. This script re-injects that
constant into the simulator's HalPowerManager.h after every dep fetch, so the
fork keeps building against the latest simulator.

Idempotency is handled by string search: if the constant is already present the
patch is a no-op; the presence check makes it safe across `pio clean` /
dependency refetches.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os

# Matches lib/hal/HalPowerManager.h in the mesh fork (idle downclock threshold).
IDLE_POWER_SAVING_MS_LINE = "static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000; // mesh fork"

ANCHOR = "static constexpr int LOW_POWER_FREQ = 10;"


def patch_simulator_hal(env):  # noqa: F811
    libdeps_root = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")  # noqa: F821
    if not os.path.isdir(libdeps_root):
        return

    for env_dir in sorted(os.listdir(libdeps_root)):
        header = os.path.join(libdeps_root, env_dir, "simulator", "src", "HalPowerManager.h")
        if not os.path.isfile(header):
            continue

        with open(header, "r", encoding="utf-8") as f:
            content = f.read()
        if "IDLE_POWER_SAVING_MS =" in content:
            continue  # already patched

        if IDLE_POWER_SAVING_MS_LINE in content:
            continue  # already present

        # Insert the constant right after LOW_POWER_FREQ (present in both HALs).
        if ANCHOR not in content:
            print("WARNING: simulator HalPowerManager.h anchor missing in %s" % header)
            continue
        content = content.replace(ANCHOR, ANCHOR + "\n  " + IDLE_POWER_SAVING_MS_LINE, 1)
        with open(header, "w", encoding="utf-8") as f:
            f.write(content)
        print("Patched simulator HalPowerManager.h with IDLE_POWER_SAVING_MS: %s" % header)


patch_simulator_hal(env)  # noqa: F821
