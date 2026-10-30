#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The game's clock stands still while its window is minimized (Jeppi's report of 2026-10-29: tabbing out of the game and back "speeds the game up a lot").

The original's loop (CGame::MainLoop) takes its time from GetSysTimeFloat and keeps a base time that advances one frame length per pass; PLATFORM.Process, at the start of every pass, waits in
GetMessage while the window is minimized (d3dapp's WM_SIZE made the app inactive). Unless a slow pass re-bases its clock, the game runs the time it was away as one burst, a frame length per
pass, as fast as the machine can go. The CE layer's real clock now stands still for that wait (common/clock.cpp, window.cpp's GetMessage, the two platform layers' real reads) and goes on from where it
stood. Held here: the clock's own behaviour (an executed driver in the real, off, present and shim cases), that every real read of both platform layers subtracts the shift, that GetMessage brackets its
wait, and that each part has a mutant that breaks one of these checks. The window pump's side (the faked minimize, the begin and end around a real wait) is in window_tests.py."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import deps  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
CLOCK = ROOT / "port/platform/common/clock.cpp"
WINDOW = ROOT / "port/platform/common/window.cpp"
KERNEL = ROOT / "port/platform/linux/kernel.cpp"
WINCLOCK = ROOT / "port/platform/windows/clock.cpp"
DRIVER = ROOT / "port/tools/tests/ce_pause_clock_driver.cpp"
WIN_DRIVER = ROOT / "port/tools/tests/ce_pause_clock_win_driver.cpp"

# An artificial monotonic source makes a reading taken before a pause and two separate pauses reproducible. This
# links the production common clock unchanged; only clock_gettime is replaced, not the clock or shift implementation.
DETERMINISTIC_DRIVER = r'''
#include <stdio.h>
#include <time.h>
#include "platform.h"
static unsigned long long raw = 10000000000ull;
extern "C" int __wrap_clock_gettime(clockid_t, struct timespec *out) {
    out->tv_sec = (time_t)(raw / 1000000000ull);
    out->tv_nsec = (long)(raw % 1000000000ull);
    return 0;
}
bool PortCeLayer() { return true; }
static unsigned failures;
static void expect(bool ok, const char *why) {
    if (!ok) { printf("FAIL %s\n", why); ++failures; }
}
static unsigned long long game(unsigned long long value) { return value - PortClockShiftNs(value); }
int main() {
    const auto old = PortClockRawNs();
    raw += 10000000ull;
    PortClockPauseBegin();
    const auto first = game(raw);
    expect(PortClockShiftNs(old) == 0 && game(old) == old, "pre-wait reading keeps the completed offset");
    raw += 100000000ull;
    expect(game(raw) == first, "first wait is held");
    PortClockPauseEnd();
    raw += 10000000ull;
    const auto beforeSecondRaw = raw;
    const auto beforeSecondGame = game(beforeSecondRaw);
    expect(beforeSecondGame == first + 10000000ull, "first wait remains excluded");
    raw += 10000000ull;
    const auto atSecondBegin = game(raw);
    PortClockPauseBegin();
    expect(game(raw) == atSecondBegin, "second wait retains first wait's offset");
    expect(PortClockShiftNs(old) == 100000000ull, "older raw reading uses the completed offset");
    expect(game(beforeSecondRaw) == atSecondBegin, "stale reading may clamp to the second held value");
    raw += 200000000ull;
    expect(game(raw) == atSecondBegin, "second wait is held");
    PortClockPauseEnd();
    raw += 10000000ull;
    expect(game(raw) == atSecondBegin + 10000000ull, "both waits remain excluded afterwards");
    puts(failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
'''


def build_and_run_deterministic(clock_text):
    """Run production clock code with a controlled monotonic source, including pre-wait reads and repeated waits."""
    with tempfile.TemporaryDirectory(prefix="pause-clock-deterministic-", dir=deps.WORK) as directory:
        d = Path(directory)
        (d / "clock.cpp").write_text(clock_text)
        (d / "driver.cpp").write_text(DETERMINISTIC_DRIVER)
        binary = d / "driver"
        subprocess.run(["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", '-DCE_VERSION="test"',
                        f"-I{ROOT / 'port/include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}",
                        f"-I{ROOT / 'port/platform/common'}", str(d / "driver.cpp"), str(d / "clock.cpp"),
                        "-Wl,--wrap=clock_gettime", "-o", str(binary)], check=True, capture_output=True, text=True, timeout=120)
        import os
        env = {k: v for k, v in os.environ.items() if not k.startswith("BEA_PORT_")}
        ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20, env=env)
        return ran.returncode == 0 and ran.stdout.strip().endswith("PASS"), ran.stdout


def build_and_run(clock_text, mode, env=None):
    """Compile the driver against a clock.cpp text; the driver's stdout line says PASS or what failed."""
    with tempfile.TemporaryDirectory(prefix="pause-clock-", dir=deps.WORK) as directory:
        d = Path(directory)
        (d / "clock.cpp").write_text(clock_text)
        binary = d / "driver"
        subprocess.run(["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-DCE_VERSION=\"test\"",
                        f"-I{ROOT / 'port/include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}", f"-I{ROOT / 'port/platform/common'}",
                        str(DRIVER), str(d / "clock.cpp"), "-o", str(binary)], check=True, capture_output=True, text=True, timeout=120)
        import os
        e = {k: v for k, v in os.environ.items() if not k.startswith("BEA_PORT_")}
        e.update(env or {})
        ran = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=20, env=e)
        return ran.returncode == 0 and ran.stdout.strip().endswith("PASS"), ran.stdout


def build_and_run_windows(wclock_text, frequency, clock_text=None):
    """Compile the Windows clock reads (windows/clock.cpp, with the operating system's clocks stood in by the driver) with common/clock.cpp's raw clock, and run them at a counter frequency."""
    with tempfile.TemporaryDirectory(prefix="pause-clock-win-", dir=deps.WORK) as directory:
        d = Path(directory)
        (d / "clock.cpp").write_text(clock_text if clock_text is not None else CLOCK.read_text())
        (d / "wclock.cpp").write_text(wclock_text)
        # the Windows file uses two SDK calls the Linux shim of windows.h does not declare: declared here for the host build
        (d / "extra.h").write_text('#include <windows.h>\nextern "C" { void WINAPI GetSystemTimeAsFileTime(LPFILETIME); BOOL WINAPI FileTimeToSystemTime(const FILETIME *, LPSYSTEMTIME); }\n')
        binary = d / "driver"
        subprocess.run(["g++", "-m32", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-DNOMINMAX", "-D_WIN32", "-DCE_VERSION=\"test\"", f"-I{ROOT / 'port/include/linux'}", "-include", str(d / "extra.h"),
                        f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}", f"-I{ROOT / 'port/platform/common'}", f"-I{ROOT / 'port/platform/windows'}",
                        str(WIN_DRIVER), str(d / "clock.cpp"), str(d / "wclock.cpp"), "-o", str(binary)], check=True, capture_output=True, text=True, timeout=120)
        import os
        e = {k: v for k, v in os.environ.items() if not k.startswith("BEA_PORT_")}
        e["BEA_TEST_CLOCK_FREQUENCY"] = str(frequency)
        ran = subprocess.run([str(binary), str(frequency)], capture_output=True, text=True, timeout=30, env=e)
        return ran.returncode == 0 and ran.stdout.strip().endswith("PASS"), ran.stdout


def function(text, head):
    start = text.index(head)
    end = text.index("\n}\n", start) + 3
    return text[start:end]


def check_wiring(window_text, kernel_text, wclock_text):
    wait = function(window_text, 'extern "C" BOOL WINAPI PortGetMessageA(LPMSG msg, HWND, UINT, UINT)')
    assert wait.index("PortClockPauseBegin();") < wait.index("Pump();") < wait.index("PortClockPauseEnd();") < wait.index("return msg->message != WM_QUIT;"), \
        "GetMessage's wait begins the stand-still before it waits and ends it before it returns a message"
    linux = function(kernel_text, "static ULONGLONG GameNs()")
    assert linux.index("PortClockReadBegin()") < linux.index("MonotonicNs()") < linux.index("PortClockShiftNs(m)") < linux.index("PortClockReadValid(generation)"), "the Linux raw sample and shift are bracketed by the generation"
    assert "(LONGLONG)GameNs();" in kernel_text and "(GameNs() - kStartNs) / 1000000ull" in kernel_text, "the Linux counter and millisecond clocks read through it"
    assert "MonotonicNs() - kStartNs" not in kernel_text.replace("GameNs() - kStartNs", ""), "no Linux real read bypasses it"
    qpc = function(wclock_text, "BOOL WINAPI PortQueryPerformanceCounter(LARGE_INTEGER *c)")
    assert qpc.index("PortClockReadBegin()") < qpc.index("QueryPerformanceCounter(&raw)") < qpc.index("PortClockReadValid(generation)"), "the Windows counter sample is bracketed"
    assert "gCounterHeld.load()" in qpc and "gCounterOffset.load()" in qpc, "the Windows counter uses its held value and cumulative wait"
    ms = function(wclock_text, "static DWORD GameMs(bool tick)")
    assert ms.index("PortClockReadBegin()") < ms.index("GetTickCount() - gTickOffset.load()") < ms.index("PortClockReadValid(generation)"), "the Windows millisecond samples are bracketed"
    assert "gTickHeld.load()" in ms and "gTimeHeld.load()" in ms and "timeGetTime() - gTimeOffset.load()" in ms, "each millisecond clock uses its own held value and wait"
    assert "GameMs(false)" in wclock_text and "GameMs(true)" in wclock_text, "the Windows millisecond clocks read through the held clocks"


class PauseClock(unittest.TestCase):
    def test_pre_wait_read_and_repeated_waits_have_deterministic_controls(self):
        text = CLOCK.read_text()
        ok, out = build_and_run_deterministic(text)
        self.assertTrue(ok, out)
        for old, new, expected in (
                ("return rawNs >= frozen ? rawNs - frozen : gOffsetNs.load();", "return rawNs - frozen;", "pre-wait reading keeps the completed offset"),
                ("gFrozenNs.store(raw - gOffsetNs.load());", "gFrozenNs.store(raw);", "second wait retains first wait's offset")):
            self.assertEqual(text.count(old), 1, old)
            ok, out = build_and_run_deterministic(text.replace(old, new))
            self.assertFalse(ok, f"the deterministic driver did not catch {new!r}: {out}")
            self.assertIn(expected, out)

    def test_the_clock_stands_still_for_a_wait_and_goes_on(self):
        ok, out = build_and_run(CLOCK.read_text(), "real")
        self.assertTrue(ok, out)

    def test_it_changes_nothing_outside_the_ce_layer_and_in_a_virtual_clock(self):
        ok, out = build_and_run(CLOCK.read_text(), "off")
        self.assertTrue(ok, out)
        for clock in ("present", "shim"):
            ok, out = build_and_run(CLOCK.read_text(), "virtual", {"BEA_PORT_CLOCK": clock})		# the layer on (the driver says so) with a virtual clock named
            self.assertTrue(ok, f"{clock}: {out}")

    def test_the_comparison_switch_keeps_the_originals_behaviour(self):
        ok, out = build_and_run(CLOCK.read_text(), "run", {"BEA_PORT_MINIMIZED_CLOCK": "run"})
        self.assertTrue(ok, out)
        ok, out = build_and_run(CLOCK.read_text(), "real", {"BEA_PORT_MINIMIZED_CLOCK": "stop"})		# any other value is the default
        self.assertTrue(ok, out)

    def test_the_windows_reads_stand_still_at_each_counter_frequency(self):
        for frequency in (10000000, 3579545, 1000000000):
            ok, out = build_and_run_windows(WINCLOCK.read_text(), frequency)
            self.assertTrue(ok, f"{frequency} Hz: {out}")

    def test_mutants_of_the_windows_reads_are_caught(self):
        text = WINCLOCK.read_text()
        for old, new in (
                ("gCounterHeld.store(raw - gCounterOffset.load());", "gCounterHeld.store(raw);"),
                ("(unsigned long long)raw.QuadPart - gCounterOffset.load()", "(unsigned long long)raw.QuadPart"),
                ("gTimeHeld.store(ms - gTimeOffset.load())", "gTimeHeld.store(ms)"),
                ("timeGetTime() - gTimeOffset.load()", "timeGetTime()"),
                ("GetTickCount() - gTickOffset.load()", "GetTickCount()")):
            self.assertEqual(text.count(old), 1, old)
            ok, out = build_and_run_windows(text.replace(old, new), 10000000)
            self.assertFalse(ok, f"the Windows driver passes with {new!r}")

    def test_every_real_read_and_the_wait_are_wired(self):
        check_wiring(WINDOW.read_text(), KERNEL.read_text(), WINCLOCK.read_text())

    def test_mutants_of_the_clock_are_caught_by_the_driver(self):
        text = CLOCK.read_text()
        for old, new in (
                ("\t\treturn rawNs >= frozen ? rawNs - frozen : gOffsetNs.load();", "\t\treturn gOffsetNs.load() + 0 * frozen * rawNs;"),		# the shift does not grow while the wait lasts
                ("\t\tgOffsetNs.store(raw - gFrozenNs.load());\t// a failed edge cannot replace the last known offset", "\t\t(void)raw;"),		# the wait's end does not re-base: the clock would jump back
                ("\tif (keepRunning || Mode() != REAL || !PortCeLayer() || gFrozen.load())\n\t\treturn;", "\tif (keepRunning || Mode() != REAL || gFrozen.load())\n\t\treturn;"),		# outside the CE layer too
                ("unsigned long long PortClockShiftNs(unsigned long long rawNs) { return Mode() == REAL ? Shift(rawNs) : 0; }", "unsigned long long PortClockShiftNs(unsigned long long rawNs) { (void)rawNs; return 0; }")):
            self.assertEqual(text.count(old), 1, old)
            ok_real, _ = build_and_run(text.replace(old, new), "real")
            ok_off, _ = build_and_run(text.replace(old, new), "off")
            self.assertFalse(ok_real and ok_off, f"the driver passes in both modes with {new!r}")
        # the comparison switch ignored: the driver's run case catches a clock that stands still anyway
        old = "\tif (keepRunning || Mode() != REAL"
        self.assertEqual(text.count(old), 1, old)
        ok, out = build_and_run(text.replace(old, "\t(void)keepRunning;\n\tif (Mode() != REAL"), "run", {"BEA_PORT_MINIMIZED_CLOCK": "run"})
        self.assertFalse(ok, "the driver passes the run case with the switch ignored")
        # a clock that stands still in a virtual clock too (both its gates removed): the driver's virtual case catches it
        both = text.replace("\tif (keepRunning || Mode() != REAL || !PortCeLayer() || gFrozen.load())\n\t\treturn;", "\tif (keepRunning || !PortCeLayer() || gFrozen.load())\n\t\treturn;").replace(
            "return Mode() == REAL ? Shift(rawNs) : 0; }", "return Shift(rawNs); }")
        self.assertNotEqual(both, text)
        ok, out = build_and_run(both, "virtual", {"BEA_PORT_CLOCK": "present"})
        self.assertFalse(ok, "the driver passes in the virtual case with no Mode gate")

    def test_mutants_of_the_wiring_are_caught(self):
        window, kernel, wclock = WINDOW.read_text(), KERNEL.read_text(), WINCLOCK.read_text()
        cases = (
            ("window", "\tPortClockPauseBegin();\n\tfor (;;)", "\tfor (;;)"),
            ("window", "\t\t\tPortClockPauseEnd();\n\t\t\treturn msg->message != WM_QUIT;", "\t\t\treturn msg->message != WM_QUIT;"),
            ("kernel", "PortClockShiftNs(m)", "0"),
            ("kernel", "(DWORD)((GameNs() - kStartNs) / 1000000ull)", "(DWORD)((MonotonicNs() - kStartNs) / 1000000ull)"),
            ("wclock", "gCounterHeld.load()", "0"),
            ("wclock", "GameMs(true)", "GetTickCount()"),
        )
        for which, old, new in cases:
            texts = {"window": window, "kernel": kernel, "wclock": wclock}
            self.assertEqual(texts[which].count(old), 1, old)
            texts[which] = texts[which].replace(old, new)
            with self.assertRaises((AssertionError, ValueError), msg=f"{which}: {old!r} -> {new!r}"):
                check_wiring(texts["window"], texts["kernel"], texts["wclock"])


if __name__ == "__main__":
    unittest.main()
