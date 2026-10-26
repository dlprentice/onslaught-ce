#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Linux launcher's log, session recording and "Report a problem", against a stand-in game (a shell script named
bea beside a copy of the launcher), with no display, no network and a private HOME and user folder.

    python3 port/updater/tests/launcher_tests.py [-v]     # needs build.py --public --target linux first
"""
import json
import os
import resource
import re
import hashlib
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
echo "game_pid=$$"
echo "out: args $*"
echo "onslaught-ce: adapter Stand-in GPU (vendor 8086, device 1234, driver 1.2.3.4)" >&2
echo "env: rec=${BEA_PORT_RECORD:-} clock=${BEA_PORT_CLOCK:-} build=${BEA_CE_BUILD:-} monitor=${BEA_AUDIO_MONITOR:-}"
echo "folder_logged=${BEA_CE_GAME_FOLDER_LOGGED:-}"
# The Linux entry point receives the launcher's marker; a direct launch still reports both paths.
if [ "${BEA_CE_GAME_FOLDER_LOGGED:-}" = 1 ]; then
  echo "onslaught-ce: writable folder $BEA_USER_DIR" >&2
else
  echo "onslaught-ce: game folder $BEA_GAME_DIR, writable folder $BEA_USER_DIR" >&2
fi
echo "a mounted drive: /run/media/${USER:-}/Games and $HOME/x but not ${HOME}y"
case "${FAKE:-}" in
  big) yes 'a line of game output, sixty-four bytes long, to fill the log up' | head -c 5000000 >&2 ;;
  segv) kill -SEGV $$ ;;
esac
# A fixture tape and the recorder's exact publication acknowledgement, not evidence of gameplay recording.
if [ -n "${BEA_PORT_RECORD:-}" ] && [ -z "${BEA_PORT_TAPE:-}" ]; then
  stage="$BEA_PORT_RECORD.recording-$$-0.incomplete"
  if (set -C; printf '# onslaught tape 2\nK 0 down 1b 01 escape\nK 1 up 1b 01 escape\n' > "$stage"); then
    if [ "${FAKE:-}" = record-fail ]; then
      echo 'onslaught-ce: recording failed: stand-in finalization failure' >&2
    elif ln -T "$stage" "$BEA_PORT_RECORD"; then
      rm "$stage"
      printf 'onslaught-ce: recording finalized: final tape published %s\n' "$BEA_PORT_RECORD" >&2
    else
      echo 'onslaught-ce: recording failed: stand-in publication collision' >&2
    fi
  else
    echo 'onslaught-ce: recording failed: stand-in stage creation failed' >&2
  fi
fi
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
        self.assertEqual(lines[-1], "onslaught-ce: the game closed")
        # the terminal gets the same output
        self.assertIn("out: args -level 100", r.stderr)
        self.assertIn("folder_logged=1", r.stderr)
        self.assertEqual(r.stderr.count("onslaught-ce: game folder "), 1)
        self.assertEqual(self.log().count("onslaught-ce: game folder "), 1)
        self.assertNotIn("run 'cd .'", r.stderr)

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

    def test_log_identifies_only_own_launcher_and_child(self):
        result = self.run_launcher()
        self.assertEqual(result.returncode, 0)
        text = self.log()
        launcher = re.search(r"onslaught-ce: launcher process pid=(\d+) SDL=(\d+)", text)
        child = re.search(r"onslaught-ce: game process pid=(\d+) launcher_pid=(\d+) output_capture=pipe", text)
        actual = re.search(r"game_pid=(\d+)", text)
        self.assertIsNotNone(launcher, text)
        self.assertIsNotNone(child, text)
        self.assertIsNotNone(actual, text)
        self.assertEqual(launcher[1], child[2])
        self.assertEqual(child[1], actual[1])
        self.assertNotEqual(child[1], child[2])
        self.assertGreater(int(launcher[2]), 0)
        digest = hashlib.sha256((self.tmp / "bin/bea").read_bytes()).hexdigest()
        self.assertIn("prelaunch game executable sha256=" + digest, text)

    def test_executable_digest_failure_does_not_block_execute_only_game(self):
        game = self.tmp / "bin/bea"
        shutil.copyfile(shutil.which("true"), game)
        game.chmod(0o111)
        try:
            self.assertFalse(os.access(game, os.R_OK), "execute-only test requires an unprivileged account")
            result = self.run_launcher()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("sha256=unavailable (launch will still be attempted)", self.log())
            self.assertIn("the game closed", self.log())
        finally:
            game.chmod(0o755)

    def test_record_flag_and_ini(self):
        (self.user).mkdir()
        (self.user / "defaultoptions.bea").write_bytes(b"\x01\x02\x03")
        (self.user / "savegames").mkdir()
        (self.user / "savegames/Career 1.bes").write_bytes(b"career")
        r = self.run_launcher("--record", "-level", "100")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        tapes = sorted((self.user / "tapes").glob("*.tape.json"))
        self.assertEqual(len(tapes), 1)
        meta = json.loads(tapes[0].read_text())
        tape = self.user / "tapes" / meta["tape"]
        self.assertTrue(tape.read_text().startswith("# onslaught tape 2\n"))
        self.assertIn("recording saved to", r.stderr)
        self.assertIn(f"env: rec={tape} clock=shim", r.stderr)
        self.assertEqual(meta["game_args"], ["-level", "100"])
        self.assertEqual(meta["clock"], "shim")
        # session tape 2, its line kinds, and the write-only audio monitor switched on for the recorded game
        self.assertEqual(meta["format"], "onslaught-ce session tape 2")
        self.assertEqual(meta["tape_lines"], ["K", "M", "W", "W_CLIENT", "C"])
        self.assertIs(meta["audio_monitor"], True)
        self.assertIn("monitor=1", r.stderr)
        self.assertEqual(sorted(meta["start_files"]), ["defaultoptions.bea", "savegames/Career 1.bes"])
        self.assertIn("recording this session to", self.log())
        # ce.ini turns it on for every session; a replay never records
        (self.user / "ce.ini").write_text("record_sessions=1\n")
        self.assertIn("clock=shim", self.run_launcher().stderr)
        r = self.run_launcher(BEA_PORT_TAPE="/nonexistent.tape")
        self.assertIn("env: rec= clock=", r.stderr)
        self.assertIn("monitor=\n", r.stderr + "\n")       # no recording, no monitor
        self.assertIn("not recording this session", r.stderr)

    def test_record_monitor_off(self):
        # BEA_AUDIO_MONITOR=0 set by the player is kept, and the session's .json says the monitor was off
        r = self.run_launcher("--record", BEA_AUDIO_MONITOR="0")
        meta = json.loads(sorted((self.user / "tapes").glob("*.tape.json"))[0].read_text())
        self.assertIs(meta["audio_monitor"], False)
        self.assertIn("monitor=0", r.stderr)

    def test_normal_game_exit_does_not_hide_recording_failure(self):
        result = self.run_launcher("--record", FAKE="record-fail")
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertIn("the game closed", result.stderr)
        self.assertIn("game closed, recording failed", result.stderr)
        self.assertNotIn("recording saved to", result.stderr)
        self.assertEqual(list((self.user / "tapes").glob("*.tape")), [])
        self.assertEqual(len(list((self.user / "tapes").glob("*.incomplete"))), 1)

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
        game = self.fake_install("game")
        r = self.run_launcher("--record", BEA_USER_DIR=str(user), BEA_GAME_DIR=str(game), USER="ceplayer")
        self.assertEqual(r.returncode, 0)
        text = (user / "onslaught-ce.log").read_text()
        self.assertNotIn(str(self.home), text.replace(f"{self.home}y", ""))
        self.assertNotIn("ceplayer", text)
        self.assertEqual(text.count("onslaught-ce: game folder "), 1)
        self.assertEqual(r.stderr.count("onslaught-ce: game folder "), 1)
        self.assertIn("onslaught-ce: game folder ~/game\n", r.stderr)
        self.assertIn("writable folder ~/.local/share/onslaught-ce\n", text)
        self.assertIn("a mounted drive: /run/media/<user>/Games and ~/x but not ", text)
        self.assertIn(f"{self.home}y", text)	# only a whole home folder is shortened
        self.assertIn("onslaught-ce: recording this session to ~/.local/share/onslaught-ce/tapes/", text)
        # the terminal keeps the game's own text; the launcher's lines are short there too
        self.assertIn(f"writable folder {user}", r.stderr)
        self.assertIn("recording this session to ~/", r.stderr)

    def test_replay_has_no_log(self):
        for env in ({"BEA_PORT_TAPE": "/nonexistent.tape"}, {"BEA_PORT_RECORD": str(self.tmp / "harness.tape")}):
            r = self.run_launcher(BEA_CE_LOG_CONTINUE="1", **env)
            self.assertEqual(r.returncode, 0)
            self.assertIn("build= monitor=", r.stdout + r.stderr)	# BEA_CE_BUILD not set for the CE layer
            self.assertFalse((self.user / "onslaught-ce.log").exists())
            self.assertFalse((self.user / "onslaught-ce.old.log").exists())
            self.assertNotIn("run 'cd .'", r.stderr)

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
        hint = "the update replaced the install folder; if this terminal was open there, run 'cd .' before starting again"
        self.assertEqual(r.stderr.count(hint), 1)
        self.assertEqual(after.count(hint), 1)
        self.run_launcher()	# and the next ordinary start rotates as usual
        self.assertEqual(self.log("onslaught-ce.old.log"), after)
        self.assertNotIn(hint, self.log())

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

    def test_linked_user_folder_refused_before_launcher_writes(self):
        outside = self.home / "outside"
        outside.mkdir()
        (outside / "sentinel").write_bytes(b"keep sentinel")
        linked = self.home / "linked"
        linked.symlink_to(outside, target_is_directory=True)
        for user in (linked, linked / "new" / "nested"):
            with self.subTest(user=user):
                r = self.run_launcher("--update-only", BEA_USER_DIR=str(user))
                self.assertEqual(r.returncode, 1)
                self.assertIn("could not prepare the settings folder", r.stderr)
                self.assertEqual(sorted(p.name for p in outside.iterdir()), ["sentinel"])

    def test_temp_symlink_does_not_overwrite_target(self):
        self.user.mkdir()
        ini = self.user / "ce.ini"
        ini.write_bytes(b"record_sessions=1\nunknown=retain\n")
        sentinel = self.home / "sentinel"
        sentinel.write_bytes(b"keep sentinel")
        (self.user / "ce.ini.tmp").symlink_to(sentinel)
        r = self.run_launcher("--no-record")
        self.assertEqual(r.returncode, 0)
        self.assertEqual(ini.read_bytes(), b"record_sessions=0\nunknown=retain\n")
        self.assertFalse(ini.is_symlink())
        self.assertEqual(sentinel.read_bytes(), b"keep sentinel")
        self.assertTrue((self.user / "ce.ini.tmp").is_symlink())

    def fake_install(self, name, msl=True, stf=b"real"):
        """A folder under the private HOME with the install's data/MissionScripts files: onsldef.msl copied (or absent),
        text.stf copied (stf "real"), absent (None) or these bytes."""
        d = self.home / name / "data/MissionScripts/text"
        d.mkdir(parents=True)
        real = Path(INSTALL, "data/MissionScripts")
        if msl:
            shutil.copy2(real / "onsldef.msl", d.parent / "onsldef.msl")
        if stf == b"real":
            shutil.copy2(real / "text/text.stf", d / "text.stf")
        elif stf is not None:
            (d / "text.stf").write_bytes(stf)
        return self.home / name

    def test_rejected_game_dir_says_why(self):
        # No Steam library in the private HOME and no display for the folder picker: every reason is said, then the
        # no-dialog message, and nothing starts.
        cases = [(self.fake_install("empty", msl=False, stf=None), "it has no data/MissionScripts/onsldef.msl"),
                 (self.fake_install("nostf", stf=None), "it has no data/MissionScripts/text/text.stf"),
                 (self.fake_install("other", stf=b"another edition"),
                  "its data/MissionScripts/text/text.stf differs from the supported edition's (hash mismatch)")]
        unreadable = self.fake_install("unreadable")
        stf = next(unreadable.rglob("text.stf"))
        stf.chmod(0)
        if not os.access(stf, os.R_OK):  # (root reads it anyway)
            cases.append((unreadable, "its data/MissionScripts/text/text.stf could not be read"))
        for folder, why in cases:
            with self.subTest(why=why):
                r = self.run_launcher(BEA_GAME_DIR=str(folder))
                self.assertEqual(r.returncode, 1)
                # the home folder is written as ~
                self.assertIn(f"onslaught-ce: BEA_GAME_DIR ~/{folder.name} is not a supported Battle Engine Aquila "
                              f"install: {why}", r.stderr)
                self.assertIn("onslaught-ce: could not find Battle Engine Aquila in Steam's libraries; set BEA_GAME_DIR "
                              "to its folder", r.stderr)
                self.assertNotIn("out: args", r.stderr + r.stdout)
                self.assertNotIn(str(self.home), r.stderr)

    def test_rejected_ini_game_dir_says_why(self):
        folder = self.fake_install("moved", stf=None)
        self.user.mkdir()
        (self.user / "ce.ini").write_text(f"gamedir={folder}\n")
        r = self.run_launcher(BEA_GAME_DIR="")
        self.assertEqual(r.returncode, 1)
        self.assertIn("onslaught-ce: ce.ini's gamedir ~/moved is not a supported Battle Engine Aquila install: it has no "
                      "data/MissionScripts/text/text.stf", r.stderr)
        self.assertIn("set BEA_GAME_DIR to its folder", r.stderr)
        # a good BEA_GAME_DIR is used without a word about ce.ini
        r = self.run_launcher()
        self.assertEqual(r.returncode, 0)
        self.assertNotIn("not a supported", r.stderr)


if __name__ == "__main__":
    unittest.main()
