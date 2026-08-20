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


# Simulator HalClock stub: inject HalClock::getEpochUtc() so the fetched
# crosspoint-simulator libdep matches the mesh fork's lib/hal/HalClock.h.
# The simulator keeps its clock as UTC (gmtime_r on time()), so the stub
# returns the system time cast to a uint32 epoch.
HALCLOCK_H_GETTIME = "  bool getTime(uint8_t& hour, uint8_t& minute) const;"
HALCLOCK_H_ANCHOR = "bool HalClock::syncFromNTP()"
HALCLOCK_CPP_IMPL = (
    "bool HalClock::getEpochUtc(uint32_t& out) const {\n"
    "  if (!_available) return false;\n"
    "  const std::time_t now = std::time(nullptr);\n"
    "  if (now < 0 || static_cast<std::uint64_t>(now) > UINT32_MAX) return false;\n"
    "  out = static_cast<uint32_t>(now);\n"
    "  return true;\n"
    "}\n"
)


def patch_simulator_hal_clock(env):  # noqa: F811
    libdeps_root = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")  # noqa: F821
    if not os.path.isdir(libdeps_root):
        return

    for env_dir in sorted(os.listdir(libdeps_root)):
        header = os.path.join(libdeps_root, env_dir, "simulator", "src", "HalClock.h")
        source = os.path.join(libdeps_root, env_dir, "simulator", "src", "HalClock.cpp")
        if not os.path.isfile(header) or not os.path.isfile(source):
            continue

        with open(header, "r", encoding="utf-8") as f:
            header_content = f.read()
        if "getEpochUtc" not in header_content and HALCLOCK_H_GETTIME in header_content:
            header_content = header_content.replace(
                HALCLOCK_H_GETTIME, HALCLOCK_H_GETTIME + "\n  bool getEpochUtc(uint32_t& out) const;", 1)
            with open(header, "w", encoding="utf-8") as f:
                f.write(header_content)
            print("Patched simulator HalClock.h with getEpochUtc(): %s" % header)
        elif "getEpochUtc" not in header_content:
            print("WARNING: simulator HalClock.h anchor missing in %s" % header)

        with open(source, "r", encoding="utf-8") as f:
            source_content = f.read()
        if "getEpochUtc" not in source_content and HALCLOCK_H_ANCHOR in source_content:
            source_content = source_content.replace(HALCLOCK_H_ANCHOR, HALCLOCK_CPP_IMPL + HALCLOCK_H_ANCHOR, 1)
            with open(source, "w", encoding="utf-8") as f:
                f.write(source_content)
            print("Patched simulator HalClock.cpp with getEpochUtc(): %s" % source)
        elif "getEpochUtc" not in source_content:
            print("WARNING: simulator HalClock.cpp anchor missing in %s" % source)


patch_simulator_hal(env)  # noqa: F821
patch_simulator_hal_clock(env)  # noqa: F821
