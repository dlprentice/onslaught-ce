#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A tape or a probe file that cannot be started stops the game at once with status 3, not through exit()'s handlers (replay.cpp, StopAtOnce).

BEA_PORT_TAPE naming a file that does not exist (or set together with BEA_PORT_RECORD), or BEA_PORT_PROBES naming a file that cannot be
written, is found at the first Present, after the device exists and dxvk's worker threads run. The game printed "replay: cannot open tape ..."
and then exit(3) ran every library's destructors under those threads: a segmentation fault in the graphics driver (exit status 139, seen with
the published 0.1.10 on Linux, on the NVIDIA and the Intel GPU). The suite compiles the production replay.cpp and window.cpp with the recorder
test driver (record_queue_driver.cpp; no game, SDL window, display or GPU), whose exit handler crashes when it runs, and checks status 3, the
message and that the handler never ran; mutants that go back to exit(3) (the whole stop, or the probe-file call alone) must crash with it, and
one that skips the flush must lose what is buffered on standard output. The crashes are on purpose and leave no core file. A recording that is
open when the probe file fails keeps its stage and publishes nothing. Only authored files in a private temporary folder are used.
"""
import os
import resource
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

import deps

ROOT = Path(__file__).resolve().parents[2]
REPLAY = (ROOT / "port/platform/common/replay.cpp").read_text()
WINDOW = ROOT / "port/platform/common/window.cpp"
DRIVER = (ROOT / "port/tools/tests/record_queue_driver.cpp").read_text()
STOP = "fflush(NULL);\n\t_Exit(3);"
PROBES = "\t\tFinalizeRecord();\t\t// what exit(3) would have run for an open recording: it was marked failed above, so nothing is published\n\t\tStopAtOnce();\n"


def no_core():
    """The negative control crashes on purpose: no core file (systemd-coredump would keep one for each)."""
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def build(folder, replay_text, name):
    folder = Path(folder)
    replay = folder / f"{name}-replay.cpp"
    replay.write_text(replay_text)
    driver = folder / f"{name}-driver.cpp"
    driver.write_text(DRIVER.replace("REPLAY_CPP", str(replay)).replace("WINDOW_CPP", str(WINDOW)))
    binary = folder / name
    command = [str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17", "-DNOMINMAX",
               "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar", "-fms-extensions", "-fno-autolink", "-w", "-O0",
               f"-I{deps.PORT / 'include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}", f"-I{deps.PREFIX / 'include'}",
               f"-I{ROOT / 'port/platform/common'}", str(driver), f"-L{deps.PREFIX / 'lib'}", "-lSDL3",
               f"-Wl,-rpath,{deps.PREFIX / 'lib'}", "-o", str(binary)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120, env=dict(os.environ, TMPDIR=str(folder)))
    if result.returncode:
        raise RuntimeError(f"{name} did not compile:\n{result.stdout}{result.stderr}")
    return binary


class StopAtOnce(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory(prefix="replay-stop-")
        assert STOP in REPLAY and PROBES in REPLAY, "StopAtOnce or its probe-file call changed: update the mutation anchors"
        cls.candidate = build(cls.dir.name, REPLAY, "candidate")
        cls.mutant = build(cls.dir.name, REPLAY.replace(STOP, "fflush(NULL);\n\texit(3);"), "mutant")   # the earlier code: exit() runs the handlers
        cls.no_flush = build(cls.dir.name, REPLAY.replace(STOP, "_Exit(3);"), "no-flush")             # stops at once but loses what is buffered
        cls.probes_exit = build(cls.dir.name, REPLAY.replace(PROBES, "\t\texit(3);\n"), "probes-exit")   # the probe-file stop alone, as it was

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def run_case(self, binary, case):
        tape = Path(self.dir.name) / f"{binary.name}-{case}.tape"
        env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_PORT_", "SDL_"))}
        r = subprocess.run([str(binary), case, str(tape)], env=env, capture_output=True, text=True, timeout=30, preexec_fn=no_core)
        return r, tape

    def test_a_tape_that_does_not_exist_stops_the_game_with_the_message_and_status_3(self):
        r, tape = self.run_case(self.candidate, "tape-missing")
        self.assertEqual(3, r.returncode, r.stderr)
        self.assertIn(f"replay: cannot open tape {tape}", r.stderr)

    def test_a_tape_set_together_with_a_recording_stops_the_game_with_the_message_and_status_3(self):
        r, _ = self.run_case(self.candidate, "tape-and-record")
        self.assertEqual(3, r.returncode, r.stderr)
        self.assertIn("replay: BEA_PORT_RECORD and BEA_PORT_TAPE are exclusive", r.stderr)

    def test_a_tape_that_can_be_read_does_not_stop_the_game(self):
        r, _ = self.run_case(self.candidate, "tape-ok")
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertNotIn("cannot open tape", r.stderr)

    def test_the_earlier_exit_crashes_in_the_exit_handler_after_the_message(self):
        """The negative control: with exit(3) the handlers run, and this one stands in for a library destructor under a running thread."""
        for case, message in (("tape-missing", "cannot open tape"), ("tape-and-record", "are exclusive")):
            with self.subTest(case=case):
                r, _ = self.run_case(self.mutant, case)
                self.assertEqual(-signal.SIGSEGV, r.returncode, r.stderr)
                self.assertIn(message, r.stderr)

    def test_what_is_buffered_on_standard_output_is_written_at_the_stop(self):
        """stdout is a pipe here (fully buffered): the stop's fflush(NULL) is what puts it out before _Exit."""
        r, _ = self.run_case(self.candidate, "tape-missing-stdout")
        self.assertEqual(3, r.returncode, r.stderr)
        self.assertIn("STDOUT-PENDING-AT-THE-STOP", r.stdout)
        r, _ = self.run_case(self.no_flush, "tape-missing-stdout")        # the mutant stops at once as well, but the bytes are lost
        self.assertEqual(3, r.returncode, r.stderr)
        self.assertNotIn("STDOUT-PENDING-AT-THE-STOP", r.stdout)

    def test_a_probe_file_that_cannot_be_written_stops_the_game_with_the_message_and_status_3(self):
        r, _ = self.run_case(self.candidate, "probes-unwritable")
        self.assertEqual(3, r.returncode, r.stderr)
        self.assertIn("replay: cannot write probes", r.stderr)

    def test_with_a_recording_open_the_probe_file_stop_leaves_its_stage_and_publishes_nothing(self):
        r, tape = self.run_case(self.candidate, "probes-unwritable-recording")
        self.assertEqual(3, r.returncode, r.stderr)
        self.assertIn("replay: cannot write probes", r.stderr)
        self.assertEqual(1, r.stderr.count(": recording failed: probe output initialization failed"), r.stderr)
        self.assertFalse(tape.exists(), "a failed capture publishes no final tape")
        self.assertEqual(1, len(list(tape.parent.glob(tape.name + ".recording-*.incomplete"))), "the failed capture keeps its stage")
        self.assertNotIn("recording finalized", r.stderr)

    def test_the_deliberate_crashes_leave_no_coredump_record_and_so_no_desktop_notification(self):
        """The negative controls crash on purpose. RLIMIT_CORE only stops the file: systemd-coredump still writes a journal record and the
        desktop shows "Process crashed". The driver makes itself not dumpable before it raises, so the kernel runs no core handler at all."""
        if not shutil.which("coredumpctl"):
            self.skipTest("no coredumpctl to ask")
        since = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(time.time() - 1))
        for binary, case in ((self.mutant, "tape-missing"), (self.mutant, "tape-and-record"), (self.probes_exit, "probes-unwritable")):
            r, _ = self.run_case(binary, case)
            self.assertEqual(-signal.SIGSEGV, r.returncode, r.stderr)        # it did crash
        time.sleep(1.5)                                                      # systemd-coredump records a moment after the crash
        listed = subprocess.run(["coredumpctl", "list", "--no-pager", "--no-legend", f"--since={since}"], capture_output=True, text=True, timeout=30)
        ours = [line for line in listed.stdout.splitlines() if str(self.dir.name) in line]
        self.assertEqual([], ours, "a deliberate crash was recorded:\n" + "\n".join(ours))

    def test_the_probe_file_stop_alone_back_at_exit_crashes_in_the_exit_handler(self):
        for case in ("probes-unwritable", "probes-unwritable-recording"):
            with self.subTest(case=case):
                r, _ = self.run_case(self.probes_exit, case)
                self.assertEqual(-signal.SIGSEGV, r.returncode, r.stderr)
                self.assertIn("replay: cannot write probes", r.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
