#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A tape's R lines give each DirectInput mouse read of their frame its own motion (replay.cpp, gReads).

The original game's public replay harness (deten/battle-engine-aquila-rebuild, src/bea/platform/test_harness.cpp, BEA_TAPE_OUT
and BEA_TAPE_IN) writes one R line per mouse read when a frame's motion was read in parts, and replays a frame that has R lines
by giving its k-th read the k-th line and leaving that frame's M line out. Replayed here as one sum on the first read, such a
tape parts from the original wherever the game reads the mouse twice in a frame. The suite compiles the production replay.cpp
with the recorder test driver (record_queue_driver.cpp; no game, window, display or GPU), plays an authored tape and checks
every read; the replay.cpp before R lines is the negative control and must fail the same check. Tapes without R lines replay
as before (frame 1 and frame 4 below).
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from replay_stop_tests import build, ROOT

REPLAY = ROOT / "port/platform/common/replay.cpp"

TAPE = """# onslaught tape 2
M 1 5 6 0 1
M 2 9 9 0 0
R 2 3 4 0 1
R 2 -1 2 0 0
M 3 7 0 0 1
R 3 1 1 0 1
"""
# (frame, read, active, x, y, z, buttons) for two reads in each of frames 1-4
EXPECTED = [[1, 0, 1, 5, 6, 0, 1], [1, 1, 1, 0, 0, 0, 1],
            [2, 0, 1, 3, 4, 0, 1], [2, 1, 1, -1, 2, 0, 0],
            [3, 0, 1, 1, 1, 0, 1], [3, 1, 1, 0, 0, 0, 1],
            [4, 0, 1, 0, 0, 0, 1], [4, 1, 1, 0, 0, 0, 1]]


def previous_replay():
    """replay.cpp as it was before R lines: the commit before the first one that taught it R lines."""
    log = subprocess.run(["git", "-C", str(ROOT), "log", "--format=%H", "-S", "gReads", "--", "port/platform/common/replay.cpp"],
                         capture_output=True, text=True, check=True).stdout.split()
    base = (log[-1] + "^") if log else "HEAD"
    return subprocess.run(["git", "-C", str(ROOT), "show", f"{base}:port/platform/common/replay.cpp"],
                          capture_output=True, text=True, check=True).stdout


class ReplayReads(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory(prefix="replay-reads-")
        cls.candidate = build(cls.dir.name, REPLAY.read_text(), "candidate")
        cls.before = build(cls.dir.name, previous_replay(), "before")

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def reads(self, binary):
        import json
        tape = Path(self.dir.name) / f"{binary.name}.tape"
        tape.write_text(TAPE)
        env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_PORT_", "SDL_"))}
        run = subprocess.run([str(binary), "load", str(tape)], env=dict(env, BEA_PORT_TAPE=str(tape)),
                             capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stderr)
        return json.loads(run.stdout)["mouse"]

    def test_each_read_of_a_frame_with_r_lines_gets_its_own_line_and_the_m_line_is_left_out(self):
        self.assertEqual(self.reads(self.candidate), EXPECTED)

    def test_the_replay_before_r_lines_fails_the_same_check(self):
        self.assertNotEqual(self.reads(self.before), EXPECTED)


if __name__ == "__main__":
    unittest.main()
