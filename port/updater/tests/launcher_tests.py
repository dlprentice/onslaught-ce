#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Linux launcher's log, session recording and "Report a problem", against a stand-in game (a shell script named
bea beside a copy of the launcher), with no display, no network and a private HOME and user folder.

    python3 port/updater/tests/launcher_tests.py [-v]     # needs build.py --public --target linux first
"""
import json
import os
import resource
import shutil
import subprocess
import sys
import tempfile
import unittest
import urllib.parse
from pathlib import Path

PORT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PORT / "tools"))
import deps  # noqa: E402

LAUNCHER = deps.WORK / "build/linux/onslaught-ce-launch"
GAME = r'''#!/bin/sh
echo "out: args $*"
echo "onslaught-ce: adapter Stand-in GPU (vendor 8086, device 1234, driver 1.2.3.4)" >&2
echo "env: rec=${BEA_PORT_RECORD:-} clock=${BEA_PORT_CLOCK:-} build=${BEA_CE_BUILD:-}"
echo "onslaught-ce: game folder $BEA_GAME_DIR, writable folder $BEA_USER_DIR" >&2
echo "a mounted drive: /run/media/${USER:-}/Games and $HOME/x but not ${HOME}y"
case "${FAKE:-}" in
  big) yes 'a line of game output, sixty-four bytes long, to fill the log up' | head -c 5000000 >&2 ;;
  segv) kill -SEGV $$ ;;
esac
exit ${CODE:-0}
'''
# The launcher checks the game folder's two hash-pinned files; the stand-in folder is the player's install if present.
INSTALL = os.environ.get("BEA_GAME_DIR") or str(Path.home() / ".local/share/Steam/steamapps/common/Battle Engine Aquila")


@unittest.skipUnless(LAUNCHER.exists(), f"{LAUNCHER}: build with build.py --public --target linux")
@unittest.skipUnless(Path(INSTALL, "data").is_dir(), "no game install (BEA_GAME_DIR)")
class Launcher(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="ce-launcher-test-"))
        (self.tmp / "bin").mkdir()
        shutil.copy2(LAUNCHER, self.tmp / "bin/onslaught-ce-launch")
        (self.tmp / "bin/bea").write_text(GAME)
        (self.tmp / "bin/bea").chmod(0o755)
        self.user = self.tmp / "user"
        self.home = self.tmp / "home"
        self.home.mkdir()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def run_launcher(self, *args, **env):
        e = {k: v for k, v in os.environ.items()
             if k not in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_DATA_HOME", "BEA_PORT_TAPE", "BEA_PORT_RECORD", "BEA_PORT_CLOCK")}
        e.update(HOME=str(self.home), BEA_USER_DIR=str(self.user), BEA_GAME_DIR=INSTALL, BEA_NO_UPDATE_CHECK="1",
                 BEA_REPORT_NO_OPEN="1", LD_LIBRARY_PATH=str(deps.PREFIX / "lib"))
        e.update(env)
        # no core file from the stand-in's SIGSEGV
        return subprocess.run([str(self.tmp / "bin/onslaught-ce-launch"), *args], env=e, capture_output=True, text=True,
                              preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))

    def log(self, name="onslaught-ce.log"):
        return (self.user / name).read_text()

    def test_log_header_and_tee(self):
        r = self.run_launcher("--", "-level", "100")
        self.assertEqual(r.returncode, 0)
        lines = self.log().splitlines()
        self.assertRegex(lines[0], r"^Onslaught CE v\S+ \(preview\), build \S+ \(linux-i686\)$")
        self.assertTrue(lines[1].startswith("OS: Linux"))
        self.assertTrue(lines[2].startswith("Started: "))
        self.assertEqual(lines[3], "GPU (as Direct3D reports it): Stand-in GPU (vendor 8086, device 1234, driver 1.2.3.4)")
        self.assertIn("out: args -level 100", lines)
        self.assertEqual(lines[-1], "onslaught-ce: the game exited with status 0")
        # the terminal gets the same output
        self.assertIn("out: args -level 100", r.stderr)

    def test_old_log_and_cap(self):
        self.run_launcher()
        first = self.log()
        r = self.run_launcher(FAKE="big")
        self.assertEqual(r.returncode, 0)
        self.assertEqual(self.log("onslaught-ce.old.log"), first)
        size = (self.user / "onslaught-ce.log").stat().st_size
        self.assertLessEqual(size, 4 << 20)
        self.assertTrue(self.log().rstrip().endswith("later output went to the terminal only]"))
        self.assertGreater(len(r.stderr), 5_000_000)

    def test_exit_status_and_signal(self):
        self.assertEqual(self.run_launcher(CODE="3").returncode, 3)
        r = self.run_launcher(FAKE="segv")
        self.assertEqual(r.returncode, 128 + 11)
        self.assertIn("the game stopped on signal 11", self.log())

    def test_record_flag_and_ini(self):
        (self.user).mkdir()
        (self.user / "defaultoptions.bea").write_bytes(b"\x01\x02\x03")
        (self.user / "savegames").mkdir()
        (self.user / "savegames/Career 1.bes").write_bytes(b"career")
        r = self.run_launcher("--record", "-level", "100")
        tapes = sorted((self.user / "tapes").glob("*.tape.json"))
        self.assertEqual(len(tapes), 1)
        meta = json.loads(tapes[0].read_text())
        tape = self.user / "tapes" / meta["tape"]
        self.assertIn(f"env: rec={tape} clock=shim", r.stderr)
        self.assertEqual(meta["game_args"], ["-level", "100"])
        self.assertEqual(meta["clock"], "shim")
        self.assertEqual(sorted(meta["start_files"]), ["defaultoptions.bea", "savegames/Career 1.bes"])
        self.assertIn("recording this session to", self.log())
        # ce.ini turns it on for every session; a replay never records
        (self.user / "ce.ini").write_text("record_sessions=1\n")
        self.assertIn("clock=shim", self.run_launcher().stderr)
        r = self.run_launcher(BEA_PORT_TAPE="/nonexistent.tape")
        self.assertIn("env: rec= clock=", r.stderr)
        self.assertIn("not recording this session", r.stderr)

    def test_record_prompt_and_no_record(self):
        # record_sessions=1 asks each start: Not this time records nothing and keeps the setting; Stop recording and
        # --no-record set record_sessions=0 (every other line kept, written through ce.ini.tmp, none left behind)
        self.user.mkdir()
        ini = self.user / "ce.ini"
        ini.write_text("gamedir=/somewhere\nrecord_sessions=1\nupdate_check=0")
        r = self.run_launcher(BEA_RECORD_ANSWER="once")
        self.assertIn("env: rec= clock=", r.stderr)
        self.assertIn("record_sessions=1", ini.read_text())
        r = self.run_launcher(BEA_RECORD_ANSWER="record")
        self.assertIn("clock=shim", r.stderr)
        r = self.run_launcher(BEA_RECORD_ANSWER="stop")
        self.assertIn("env: rec= clock=", r.stderr)
        self.assertEqual(ini.read_text(), "gamedir=/somewhere\nrecord_sessions=0\nupdate_check=0\n")
        self.assertIn("session recording switched off", self.log())
        ini.write_text("record_sessions=1\n")
        r = self.run_launcher("--no-record")
        self.assertIn("env: rec= clock=", r.stderr)
        self.assertEqual(ini.read_text(), "record_sessions=0\n")
        self.assertFalse((self.user / "ce.ini.tmp").exists())
        # a ce.ini that cannot be replaced is left as it was, and the session is not recorded
        ini.write_text("record_sessions=1\n")
        self.user.chmod(0o500)
        try:
            r = self.run_launcher("--no-record")
        finally:
            self.user.chmod(0o700)
        self.assertEqual(ini.read_text(), "record_sessions=1\n")
        self.assertIn("could not write record_sessions to ce.ini", r.stderr)
        self.assertIn("env: rec= clock=", r.stderr)

    def test_log_names_no_account(self):
        # the user folder and the game folder under the private home, an account name in another path
        user = self.home / ".local/share/onslaught-ce"
        r = self.run_launcher("--record", BEA_USER_DIR=str(user), USER="ceplayer")
        self.assertEqual(r.returncode, 0)
        text = (user / "onslaught-ce.log").read_text()
        self.assertNotIn(str(self.home), text.replace(f"{self.home}y", ""))
        self.assertNotIn("ceplayer", text)
        self.assertIn("onslaught-ce: game folder ", text)
        self.assertIn("writable folder ~/.local/share/onslaught-ce\n", text)
        self.assertIn("a mounted drive: /run/media/<user>/Games and ~/x but not ", text)
        self.assertIn(f"{self.home}y", text)	# only a whole home folder is shortened
        self.assertIn("onslaught-ce: recording this session to ~/.local/share/onslaught-ce/tapes/", text)
        # the terminal keeps the game's own text; the launcher's lines are short there too
        self.assertIn(f"writable folder {user}", r.stderr)
        self.assertIn("recording this session to ~/", r.stderr)

    def test_replay_has_no_log(self):
        for env in ({"BEA_PORT_TAPE": "/nonexistent.tape"}, {"BEA_PORT_RECORD": str(self.tmp / "harness.tape")}):
            r = self.run_launcher(**env)
            self.assertEqual(r.returncode, 0)
            self.assertIn("build=\n", r.stdout + r.stderr)	# BEA_CE_BUILD not set for the CE layer
            self.assertFalse((self.user / "onslaught-ce.log").exists())
            self.assertFalse((self.user / "onslaught-ce.old.log").exists())

    def test_update_restart_continues_log(self):
        self.run_launcher(CODE="5")	# the run the player wants to report
        reported = self.log()
        self.run_launcher()	# the launch that finds the update: the reported run becomes the old log
        before = self.log()
        self.assertEqual(self.log("onslaught-ce.old.log"), reported)
        r = self.run_launcher(BEA_CE_LOG_CONTINUE="1")	# the new version, started by the update
        self.assertIn("env: rec= clock= build=", r.stdout + r.stderr)
        self.assertEqual(self.log("onslaught-ce.old.log"), reported)
        after = self.log()
        self.assertTrue(after.startswith(before))
        self.assertIn("\nRestarted after an update: ", after[len(before):])
        self.run_launcher()	# and the next ordinary start rotates as usual
        self.assertEqual(self.log("onslaught-ce.old.log"), after)

    def test_report_url(self):
        user = self.home / ".local/share/onslaught-ce"
        (user / "tapes").mkdir(parents=True)
        (user / "tapes/20260101-000000.tape").write_text("")
        (user / "tapes/20260102-000000.tape").write_text("")
        r = self.run_launcher("--report", BEA_USER_DIR=str(user))
        self.assertEqual(r.returncode, 0)
        url = next(l for l in r.stdout.splitlines() if l.startswith("https://"))
        u = urllib.parse.urlparse(url)
        self.assertTrue(url.startswith("https://github.com/dlprentice/onslaught-ce/issues/new?"), url)
        q = urllib.parse.parse_qs(u.query)
        self.assertEqual(q["template"], ["bug_report.md"])
        body = q["body"][0]
        self.assertIn("`~/.local/share/onslaught-ce/onslaught-ce.log`", body)
        self.assertIn("`~/.local/share/onslaught-ce/tapes/20260102-000000.tape`", body)
        self.assertIn("(preview), build ", body)
        self.assertIn("- Linux", body)
        self.assertNotIn(str(self.home), body)


if __name__ == "__main__":
    unittest.main()
