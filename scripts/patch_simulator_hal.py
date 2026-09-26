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


# Simulator HalStorage stub: the upstream tree added the USB Drive surface
# (UsbDriveState, prepareForDeepSleep) to lib/hal. The fetched simulator libdep
# predates it, but src/activities/network/UsbDriveActivity.h and main.cpp
# reference both, so re-inject the minimal surface. UsbDriveActivity.cpp is
# excluded from the simulator build, so only prepareForDeepSleep() needs a body.
HALSTORAGE_ENUM_ANCHOR = "class HalStorage {"
HALSTORAGE_ENUM = """enum class UsbDriveState : uint8_t {
  Unsupported,
  WaitingForHost,
  Connected,
  Ejected,
  Disconnected,
  IoError,
};

class HalStorage {"""

HALSTORAGE_READY_ANCHOR = "  bool ready() const;"
HALSTORAGE_PREPARE_DECL = "  void prepareForDeepSleep();"
HALSTORAGE_PREPARE_IMPL = "void HalStorage::prepareForDeepSleep() {}\n"


def patch_simulator_hal_storage(env):  # noqa: F811
    libdeps_root = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")  # noqa: F821
    if not os.path.isdir(libdeps_root):
        return

    for env_dir in sorted(os.listdir(libdeps_root)):
        header = os.path.join(libdeps_root, env_dir, "simulator", "src", "HalStorage.h")
        source = os.path.join(libdeps_root, env_dir, "simulator", "src", "HalStorage.cpp")
        if not os.path.isfile(header) or not os.path.isfile(source):
            continue

        with open(header, "r", encoding="utf-8") as f:
            header_content = f.read()
        changed = False
        if "enum class UsbDriveState" not in header_content and HALSTORAGE_ENUM_ANCHOR in header_content:
            header_content = header_content.replace(HALSTORAGE_ENUM_ANCHOR, HALSTORAGE_ENUM, 1)
            changed = True
        # Newer simulator libdeps already define prepareForDeepSleep() inline
        # right after ready(); injecting the old declaration would then be a
        # redeclaration error, so skip when any form is already present.
        if "prepareForDeepSleep()" not in header_content and HALSTORAGE_READY_ANCHOR in header_content:
            header_content = header_content.replace(HALSTORAGE_READY_ANCHOR,
                                                    HALSTORAGE_READY_ANCHOR + "\n" + HALSTORAGE_PREPARE_DECL, 1)
            changed = True
        if changed:
            with open(header, "w", encoding="utf-8") as f:
                f.write(header_content)
            print("Patched simulator HalStorage.h with USB Drive surface: %s" % header)

        with open(source, "r", encoding="utf-8") as f:
            source_content = f.read()
        # Only out-of-line stubs need the body; newer libdeps define it inline
        # in the header (see the "prepareForDeepSleep()" guard above).
        if HALSTORAGE_PREPARE_DECL in header_content and "HalStorage::prepareForDeepSleep" not in source_content:
            source_content = source_content.rstrip("\n") + "\n\n" + HALSTORAGE_PREPARE_IMPL
            with open(source, "w", encoding="utf-8") as f:
                f.write(source_content)
            print("Patched simulator HalStorage.cpp with prepareForDeepSleep(): %s" % source)


patch_simulator_hal_storage(env)  # noqa: F821


# Simulator BoardConfig stub: upstream grew X4 Classic and Paper Mono targets.
# The simulator models X3/X4 only, so expose the new predicates as false.
BOARDCONFIG_ANCHOR = "inline bool hasPwmFrontlight() { return isX4Pro(); }"
BOARDCONFIG_ADDITIONS = """inline bool hasPwmFrontlight() { return isX4Pro(); }
inline bool isX4Classic() { return false; }
inline bool isPaperMono() { return false; }"""


def patch_simulator_hal_boardconfig(env):  # noqa: F811
    libdeps_root = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")  # noqa: F821
    if not os.path.isdir(libdeps_root):
        return

    for env_dir in sorted(os.listdir(libdeps_root)):
        header = os.path.join(libdeps_root, env_dir, "simulator", "src", "BoardConfig.h")
        if not os.path.isfile(header):
            continue
        with open(header, "r", encoding="utf-8") as f:
            content = f.read()
        if "isX4Classic" in content or "isPaperMono" in content:
            continue
        if BOARDCONFIG_ANCHOR not in content:
            print("WARNING: simulator BoardConfig.h anchor missing in %s" % header)
            continue
        content = content.replace(BOARDCONFIG_ANCHOR, BOARDCONFIG_ADDITIONS, 1)
        with open(header, "w", encoding="utf-8") as f:
            f.write(content)
        print("Patched simulator BoardConfig.h with X4 Classic / Paper Mono predicates: %s" % header)


patch_simulator_hal_boardconfig(env)  # noqa: F821


# Simulator HalGPIO stub: upstream collapsed verifyPowerButtonWakeup() to a
# zero-argument call (the HAL keeps the press-hold state itself). The fetched
# simulator libdep still exposes the older (duration, shortPress) form; add the
# zero-argument overload. Host wakes are synthetic, so it always succeeds.
HALGPIO_ANCHOR = """  bool verifyPowerButtonWakeup(uint16_t requiredDurationMs,
                               bool shortPressAllowed);"""
HALGPIO_ADDITION = """  bool verifyPowerButtonWakeup(uint16_t requiredDurationMs,
                               bool shortPressAllowed);
  bool verifyPowerButtonWakeup() const { return true; }"""


def patch_simulator_hal_gpio(env):  # noqa: F811
    libdeps_root = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")  # noqa: F821
    if not os.path.isdir(libdeps_root):
        return

    for env_dir in sorted(os.listdir(libdeps_root)):
        header = os.path.join(libdeps_root, env_dir, "simulator", "src", "HalGPIO.h")
        if not os.path.isfile(header):
            continue
        with open(header, "r", encoding="utf-8") as f:
            content = f.read()
        if "verifyPowerButtonWakeup() const" in content:
            continue
        if HALGPIO_ANCHOR not in content:
            print("WARNING: simulator HalGPIO.h anchor missing in %s" % header)
            continue
        content = content.replace(HALGPIO_ANCHOR, HALGPIO_ADDITION, 1)
        with open(header, "w", encoding="utf-8") as f:
            f.write(content)
        print("Patched simulator HalGPIO.h with zero-arg verifyPowerButtonWakeup(): %s" % header)


patch_simulator_hal_gpio(env)  # noqa: F821


# Simulator HalDisplay stub: upstream added combinesGrayscaleBase() (true only
# for Paper Mono, which folds the grayscale base into the panel). The simulator
# panels use the classic separate-base path, so return false.
HALDISPLAY_H_ANCHOR = "  bool supportsStripGrayscale() const;"
HALDISPLAY_C_ANCHOR = "bool HalDisplay::supportsStripGrayscale() const { return true; }"


def patch_simulator_hal_display(env):  # noqa: F811
    libdeps_root = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")  # noqa: F821
    if not os.path.isdir(libdeps_root):
        return

    for env_dir in sorted(os.listdir(libdeps_root)):
        header = os.path.join(libdeps_root, env_dir, "simulator", "src", "HalDisplay.h")
        source = os.path.join(libdeps_root, env_dir, "simulator", "src", "HalDisplay.cpp")
        if not os.path.isfile(header) or not os.path.isfile(source):
            continue

        with open(header, "r", encoding="utf-8") as f:
            header_content = f.read()
        if "combinesGrayscaleBase" not in header_content and HALDISPLAY_H_ANCHOR in header_content:
            header_content = header_content.replace(
                HALDISPLAY_H_ANCHOR, HALDISPLAY_H_ANCHOR + "\n  bool combinesGrayscaleBase() const;", 1)
            with open(header, "w", encoding="utf-8") as f:
                f.write(header_content)
            print("Patched simulator HalDisplay.h with combinesGrayscaleBase(): %s" % header)

        with open(source, "r", encoding="utf-8") as f:
            source_content = f.read()
        if "HalDisplay::combinesGrayscaleBase" not in source_content and HALDISPLAY_C_ANCHOR in source_content:
            source_content = source_content.replace(
                HALDISPLAY_C_ANCHOR,
                HALDISPLAY_C_ANCHOR + "\nbool HalDisplay::combinesGrayscaleBase() const { return false; }", 1)
            with open(source, "w", encoding="utf-8") as f:
                f.write(source_content)
            print("Patched simulator HalDisplay.cpp with combinesGrayscaleBase(): %s" % source)


patch_simulator_hal_display(env)  # noqa: F821


# Simulator HalSystem stub: the mesh fork samples the heap from the main loop
# for the panic report. The simulator has no RTC memory and no panic report, so
# expose the call as a no-op.
HALSYSTEM_H_ANCHOR = "void begin();"
HALSYSTEM_H_ADDITION = """void begin();

// Record the current heap state into RTC memory (mesh fork panic report).
// The simulator has no RTC memory or panic report, so this is a no-op.
void sampleHeap();"""
HALSYSTEM_C_ANCHOR = "void HalSystem::begin() {}"
HALSYSTEM_C_ADDITION = "void HalSystem::begin() {}\nvoid HalSystem::sampleHeap() {}"


def patch_simulator_hal_system(env):  # noqa: F811
    libdeps_root = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")  # noqa: F821
    if not os.path.isdir(libdeps_root):
        return

    for env_dir in sorted(os.listdir(libdeps_root)):
        header = os.path.join(libdeps_root, env_dir, "simulator", "src", "HalSystem.h")
        source = os.path.join(libdeps_root, env_dir, "simulator", "src", "HalSystem.cpp")
        if not os.path.isfile(header) or not os.path.isfile(source):
            continue

        with open(header, "r", encoding="utf-8") as f:
            header_content = f.read()
        if "sampleHeap" not in header_content and HALSYSTEM_H_ANCHOR in header_content:
            header_content = header_content.replace(HALSYSTEM_H_ANCHOR, HALSYSTEM_H_ADDITION, 1)
            with open(header, "w", encoding="utf-8") as f:
                f.write(header_content)
            print("Patched simulator HalSystem.h with sampleHeap(): %s" % header)
        elif "sampleHeap" not in header_content:
            print("WARNING: simulator HalSystem.h anchor missing in %s" % header)

        with open(source, "r", encoding="utf-8") as f:
            source_content = f.read()
        if "HalSystem::sampleHeap" not in source_content and HALSYSTEM_C_ANCHOR in source_content:
            source_content = source_content.replace(HALSYSTEM_C_ANCHOR, HALSYSTEM_C_ADDITION, 1)
            with open(source, "w", encoding="utf-8") as f:
                f.write(source_content)
            print("Patched simulator HalSystem.cpp with sampleHeap(): %s" % source)
        elif "HalSystem::sampleHeap" not in source_content:
            print("WARNING: simulator HalSystem.cpp anchor missing in %s" % source)


patch_simulator_hal_system(env)  # noqa: F821
