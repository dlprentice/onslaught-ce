#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Interleavings of the real platform clocks and a minimized-window wait, with fake OS clocks.

These execute the production common clock, Linux or Windows wrapper and CE-layer gate on a 32-bit
host. The Windows OS calls are stand-ins: this does not establish behaviour on a Windows machine.
"""
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import deps  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "port/tools/tests/ce_clock_race_driver.cpp"
COMMON = ROOT / "port/platform/common/clock.cpp"
LAYER = ROOT / "port/platform/common/ce.cpp"
LINUX = ROOT / "port/platform/linux/kernel.cpp"
WINDOWS = ROOT / "port/platform/windows/clock.cpp"


def function(text, head):
    start = text.index(head)
    return text[start:text.index("\n}\n", start) + 3]


class Interleavings(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        deps.WORK.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="clock-interleave-", dir=deps.WORK)
        d = Path(cls.temp.name)
        # The exact production CE gate, extracted only to avoid linking its unrelated SDL/UI settings code.
        gate = d / "layer.cpp"
        gate.write_text('#include <stdlib.h>\n' + function(LAYER.read_text(), "bool PortCeLayer()"))
        extra = d / "win-extra.h"
        extra.write_text('#include <windows.h>\nextern "C" { void WINAPI GetSystemTimeAsFileTime(LPFILETIME); BOOL WINAPI FileTimeToSystemTime(const FILETIME *, LPSYSTEMTIME); }\n')
        cls.directory, cls.gate, cls.extra = d, gate, extra
        cls.binaries = {(platform, build): cls.compile_binary(platform, build)
                        for platform in ("linux", "windows") for build in ("ce", "private")}

    @classmethod
    def compile_binary(cls, platform, build, source=None, suffix="", defines=(), common_source=None):
        binary = cls.directory / f"{platform}-{build}{suffix}"
        command = ["g++", "-m32", "-std=c++17", "-O1", "-pthread", "-ffunction-sections", "-fdata-sections",
                   "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-DNOMINMAX",
                   f"-I{ROOT / 'port/include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}",
                   f"-I{ROOT / 'port/platform/common'}", f"-I{ROOT / 'port/platform/linux'}",
                   f"-I{ROOT / 'port/platform/windows'}", str(DRIVER), str(common_source or COMMON), str(cls.gate)]
        if build == "ce":
            command.append('-DCE_VERSION="test"')
        command.extend(defines)
        if platform == "windows":
            command += ["-D_WIN32", "-DTEST_WINDOWS", "-include", str(cls.extra), str(source or WINDOWS)]
        else:
            command += [str(source or LINUX), "-Wl,--wrap=clock_gettime"]
        command += ["-Wl,--gc-sections", "-o", str(binary)]
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=120)
        return binary

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, platform, scenario, clock, mode="freeze", build="ce", env=None, frequency=3579545, binary=None, fails=False):
        clean = {k: v for k, v in os.environ.items() if not k.startswith("BEA_PORT_")}
        clean.update(env or {})
        clean["BEA_TEST_CLOCK_FREQUENCY"] = str(frequency)
        def no_core():
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        result = subprocess.run([str(binary or self.binaries[platform, build]), scenario, clock, mode, str(frequency)],
                                env=clean, capture_output=True, text=True, timeout=5, preexec_fn=no_core)
        self.assertEqual(result.returncode, 1 if fails else 0, result.stdout + result.stderr)
        self.assertTrue(result.stdout.startswith("FAIL" if fails else "PASS"), result.stdout + result.stderr)

    def test_crossing_whole_wait_after_an_earlier_raw_sample(self):
        for platform in ("linux", "windows"):
            for scenario in ("across", "small", "twice", "wrap"):
                for clock in ("qpc", "time", "tick"):
                    with self.subTest(platform=platform, scenario=scenario, clock=clock):
                        self.run_case(platform, scenario, clock)

    def test_reads_delayed_inside_a_wait_remain_held(self):
        for platform in ("linux", "windows"):
            for clock in ("qpc", "time", "tick"):
                with self.subTest(platform=platform, clock=clock):
                    self.run_case(platform, "during", clock)

    def test_windows_counter_does_not_depend_on_frequency_query(self):
        # The original real QPC wrapper returns the native counter result independently
        # of QPF. Fail QPF after startup while QPC still succeeds, including after a wait.
        for scenario in ("frequency_fail", "frequency_fail_after_wait"):
            with self.subTest(scenario=scenario):
                self.run_case("windows", scenario, "qpc")

    def test_windows_counter_failure_still_propagates(self):
        self.run_case("windows", "counter_fail", "qpc")

    def test_windows_raw_clock_query_failure_at_startup_does_not_abort(self):
        for api in ("QPF", "QPC", "INVALID_QPF"):
            with self.subTest(api=api):
                binary = self.compile_binary("windows", "ce", suffix=f"-fail-{api.lower()}",
                                             defines=(f"-DTEST_FAIL_{api}_START",))
                self.run_case("windows", "startup_recovery", "qpc", binary=binary)
                if api in ("QPF", "INVALID_QPF"):
                    self.run_case("windows", "startup_frequency_failed_wait", "qpc", binary=binary)

    def test_windows_failed_pause_edge_leaves_native_clocks_and_generation_consistent(self):
        for api in ("qpf", "qpc"):
            for edge in ("begin", "end"):
                with self.subTest(api=api, edge=edge):
                    self.run_case("windows", f"{api}_{edge}_failure", "qpc", frequency=10000000)

    def test_windows_fallback_keeps_the_newest_successful_raw_sample(self):
        self.run_case("windows", "last_sample_fallback", "qpc", frequency=10000000)
        text = COMMON.read_text()
        old = "while (last < raw && !gLastRawNs.compare_exchange_weak(last, raw)) {}"
        self.assertEqual(text.count(old), 1)
        mutant = self.directory / "common-fallback-mutant.cpp"
        mutant.write_text(text.replace(old, "(void)last; gLastRawNs.store(raw);"))
        binary = self.compile_binary("windows", "ce", suffix="-fallback-mutant", common_source=mutant)
        self.run_case("windows", "last_sample_fallback", "qpc", frequency=10000000, binary=binary, fails=True)

    def test_non_ce_record_and_replay_fences_keep_the_raw_clock(self):
        for platform in ("linux", "windows"):
            for clock in ("qpc", "time", "tick"):
                for build, env in (("private", {}), ("ce", {"BEA_PORT_RECORD": "1"}),
                                   ("ce", {"BEA_PORT_TAPE": "fixture"}),
                                   ("ce", {"BEA_PORT_MINIMIZED_CLOCK": "run"}),
                                   ("ce", {"BEA_PORT_CE_LAYER": "0"})):
                    with self.subTest(platform=platform, clock=clock, build=build, env=env):
                        self.run_case(platform, "across", clock, "running", build, env)

    def test_native_unit_rounding_and_wrap(self):
        for platform in ("linux", "windows"):
            for clock in ("qpc", "time", "tick"):
                for frequency in (3579545, 10000000, 1000000000):
                    with self.subTest(platform=platform, clock=clock, frequency=frequency):
                        self.run_case(platform, "rounding", clock, frequency=frequency)
                if clock != "qpc":
                    self.run_case(platform, "wrap", clock, "running", "ce", {"BEA_PORT_MINIMIZED_CLOCK": "run"})

    def test_mutants_of_the_retry_and_held_value_are_caught(self):
        cases = (
            ("linux", LINUX, "if (PortClockReadValid(generation)) return value;", "(void)generation; return value;", "across", "qpc"),
            ("windows", WINDOWS, "if (!PortClockReadValid(generation)) continue;", "(void)generation;", "across", "qpc"),
            ("windows", WINDOWS, "if (PortClockReadValid(generation)) return value;", "(void)generation; return value;", "across", "time"),
            ("windows", WINDOWS, "gTimeHeld.load()", "timeGetTime() - gTimeOffset.load()", "during", "time"),
            ("windows", WINDOWS, "gTickHeld.load()", "GetTickCount() - gTickOffset.load()", "during", "tick"),
        )
        for n, (platform, source, old, new, scenario, clock) in enumerate(cases):
            with self.subTest(platform=platform, scenario=scenario, clock=clock):
                text = source.read_text()
                self.assertGreater(text.count(old), 0)
                mutant = self.directory / f"mutant-{n}.cpp"
                mutant.write_text(text.replace(old, new))
                binary = self.compile_binary(platform, "ce", mutant, f"-mutant-{n}")
                self.run_case(platform, scenario, clock, binary=binary, fails=True)


if __name__ == "__main__":
    unittest.main()
