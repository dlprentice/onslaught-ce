#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""BEA_PORT_CLOCK=fixed is the fixed-step clock of the original game's public replay harness, and nothing else changes.

deten/battle-engine-aquila-rebuild (src/bea/platform/test_harness.cpp, BEA_FIXEDSTEP without BEA_CECLOCK) records and replays the
original on a clock of Presents * step + reads * 1e-7 seconds, where reads counts each thread's own QueryPerformanceCounter and
timeGetTime calls so background threads do not shift the main thread's clock; the counter runs at 10 MHz plus one second,
timeGetTime in milliseconds plus 1,000. Tapes recorded on it replay in step here only on the same clock (port/platform/common/clock.cpp,
run.py --fixed-clock). The driver links the production clock.cpp unchanged and checks each read against that formula computed
independently, that another thread's reads do not shift the main thread's clock, that the file time and the DirectSound peek do not
count, and that without the switch the mode is not selected; mutants of the formula must fail it."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import deps

ROOT = Path(__file__).resolve().parents[2]
CLOCK = ROOT / "port/platform/common/clock.cpp"

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include "platform.h"
bool PortCeLayer() { return true; }
static int failures;
static void expect(bool ok, const char *why) { if (!ok) { printf("FAIL %s\n", why); ++failures; } }
static long double dt;
static long double secs(unsigned long long frames, unsigned long long reads) { return (long double)frames * dt + (long double)reads * 1e-7L; }
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "off")) {
        expect(!PortClockVirtual(), "no switch: real clock");
        expect(PortClockQpf() == 0, "no switch: no virtual frequency");
        printf(failures ? "FAILED\n" : "PASS\n"); return failures != 0;
    }
    dt = (long double)atof(getenv("BEA_PORT_CLOCK_DT"));
    expect(PortClockVirtual(), "fixed is a virtual clock");
    expect(PortClockQpf() == 10000000ll, "10 MHz");
    unsigned long long reads = 0, frames = 0;
    long long q = PortClockQpc(); ++reads;
    expect(q == (long long)(secs(frames, reads) * 10000000.0L) + 10000000ll, "first counter read");
    unsigned long m = PortClockMs(); ++reads;
    expect(m == (unsigned long)(secs(frames, reads) * 1000.0L) + 1000, "first millisecond read");
    for (int i = 0; i < 7; ++i) { PortClockFrame(); ++frames; }
    q = PortClockQpc(); ++reads;
    expect(q == (long long)(secs(frames, reads) * 10000000.0L) + 10000000ll, "counter after seven Presents");
    const unsigned long long peek = PortClockPeekNs();
    const unsigned long long ft = PortClockFiletime();
    (void)peek; (void)ft;
    long long otherLast = 0;
    std::thread other([&] { for (int i = 0; i < 1000; ++i) otherLast = PortClockQpc(); });
    other.join();
    expect(otherLast == (long long)(secs(frames, 1000) * 10000000.0L) + 10000000ll, "other thread advances its own counter");
    q = PortClockQpc(); ++reads;
    expect(q == (long long)(secs(frames, reads) * 10000000.0L) + 10000000ll, "another thread's reads do not shift main; peek and file time do not count");
    for (int i = 0; i < 100000; ++i) { PortClockMs(); ++reads; }
    m = PortClockMs(); ++reads;
    expect(m == (unsigned long)(secs(frames, reads) * 1000.0L) + 1000, "many reads move the millisecond clock by 1e-7 s each");
    printf(failures ? "FAILED\n" : "PASS\n");
    return failures != 0;
}
'''


def build_and_run(clock_text, mode, env):
    with tempfile.TemporaryDirectory(prefix="fixed-clock-", dir=deps.WORK) as directory:
        d = Path(directory)
        (d / "clock.cpp").write_text(clock_text)
        (d / "driver.cpp").write_text(DRIVER)
        binary = d / "driver"
        subprocess.run(["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-pthread", "-DCE_VERSION=\"test\"",
                        f"-I{ROOT / 'port/include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}",
                        f"-I{ROOT / 'port/platform/common'}", str(d / "driver.cpp"), str(d / "clock.cpp"), "-o", str(binary)],
                       check=True, capture_output=True, text=True, timeout=120)
        e = {k: v for k, v in os.environ.items() if not k.startswith("BEA_PORT_")}
        e.update(env)
        ran = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=30, env=e)
        return ran.returncode == 0 and ran.stdout.strip().endswith("PASS"), ran.stdout


FIXED_STEP = {"BEA_PORT_CLOCK": "fixed", "BEA_PORT_CLOCK_DT": "0.016667"}


class FixedClock(unittest.TestCase):
    def test_the_fixed_clock_follows_the_harness_formula(self):
        ok, out = build_and_run(CLOCK.read_text(), "on", FIXED_STEP)
        self.assertTrue(ok, out)

    def test_without_the_switch_the_clock_is_real(self):
        ok, out = build_and_run(CLOCK.read_text(), "off", {})
        self.assertTrue(ok, out)

    def test_mutants_of_the_formula_fail(self):
        text = CLOCK.read_text()
        mutants = {
            "read step": ("(long double)c * 1e-7L", "(long double)c * 1e-6L"),
            "shared across threads": (
                "const unsigned long long c = count ? ++tFixedReads : tFixedReads;",
                "static std::atomic<unsigned long long> gSharedFixed{ 0 }; (void)tFixedReads; const unsigned long long c = count ? gSharedFixed.fetch_add(1) + 1 : gSharedFixed.load();",
            ),
            "peek counts": ("case FIXED_STEP: return kEpochNs + (unsigned long long)(FixedSeconds(false)", "case FIXED_STEP: return kEpochNs + (unsigned long long)(FixedSeconds(true)"),
            "no second offset": ("case FIXED_STEP: return (long long)(FixedSeconds(true) * 10000000.0L) + 10000000ll;", "case FIXED_STEP: return (long long)(FixedSeconds(true) * 10000000.0L);"),
            "frames not counted": ("\tcase FIXED_STEP:\n\t\tgFrames.fetch_add(1);\n\t\tbreak;", "\tcase FIXED_STEP:\n\t\tbreak;"),
        }
        for name, (old, new) in mutants.items():
            with self.subTest(name):
                self.assertEqual(text.count(old), 1, f"anchor of {name} changed")
                ok, out = build_and_run(text.replace(old, new), "on", FIXED_STEP)
                self.assertFalse(ok, f"mutant {name} survived: {out}")


if __name__ == "__main__":
    unittest.main()
