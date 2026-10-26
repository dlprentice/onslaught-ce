#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fixture-only checks of actual launcher recording functions. No game, profile, display, network or downloads.

Uses already-installed pinned port inputs. --windows-link also links the same driver for native Windows execution;
Windows execution is a separate gate. All generated files live in ignored local-data/recording-publication/.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
PORT = HERE.parents[1]
sys.path.insert(0, str(PORT / "tools"))
import deps  # noqa: E402

OPTIONS = argparse.Namespace(windows_link=False, baseline=None)


def compile_driver(output, windows=False):
    mc = deps.SRC / "monocypher-4.0.2/src"
    if windows:
        command = [str(deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang")]
        includes = deps.PREFIX_WIN / "include"
    else:
        command = [str(deps.DATA / "llvm/bin/clang"), "--target=i686-pc-linux-gnu", *deps.sysroot_flags()]
        includes = deps.PREFIX / "include"
    command += ["-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-variable",
                "-ffunction-sections", "-fdata-sections", f"-I{includes}", f"-I{mc}", f"-I{mc / 'optional'}",
                f"-I{HERE.parent}"]
    if OPTIONS.baseline:
        command += [f'-DCE_LAUNCH_SOURCE="{OPTIONS.baseline.resolve()}"', "-DREPRO_BASELINE"]
    command += [str(HERE / "recording_publication_driver.c"), str(HERE.parent / "sha256.c"),
                "-Wl,--gc-sections", "-o", str(output)]
    if not windows:
        command += deps.sysroot_link_flags()
    subprocess.run(command, check=True, timeout=60)


class RecordingPublication(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.output = PORT.parent / "local-data/recording-publication"
        cls.output.mkdir(parents=True, exist_ok=True)
        cls.driver = cls.output / "recording-publication"
        compile_driver(cls.driver)
        if OPTIONS.windows_link:
            compile_driver(cls.output / "recording-publication.exe", windows=True)

    def run_case(self, mode):
        folder = Path(tempfile.mkdtemp(prefix=mode + "-", dir=self.output))
        home = folder / "home"
        home.mkdir()
        user = home / "fixture-user"
        user.mkdir()
        (user / "defaultoptions.bea").write_bytes(b"fixture options\x00\xff")
        if mode.startswith("child-"):
            (user / "bin").mkdir()
            game = user / "bin/bea"
            game.write_text('''#!/bin/sh
case "$1" in
  child-success)
    (set -C; printf 'fixture tape\\n' > "$BEA_PORT_RECORD") || exit 9
    printf 'bea-port: recording finalized: final tape published %s\\n' "$BEA_PORT_RECORD" >&2 ;;
  child-stage)
    printf 'incomplete fixture\\n' > "$BEA_PORT_RECORD.recording-1-0.incomplete" ;;
  child-foreign)
    printf 'foreign fixture\\n' > "$BEA_PORT_RECORD" ;;
  child-failure-eof|child-long-failure-eof)
    (set -C; printf 'fixture tape\\n' > "$BEA_PORT_RECORD") || exit 9
    printf 'bea-port: recording finalized: final tape published %s\\n' "$BEA_PORT_RECORD" >&2
    if [ "$1" = child-long-failure-eof ]; then
      printf 'bea-port: recording failed: %8192s' x >&2
    else
      printf 'bea-port: recording failed: fixture EOF without newline' >&2
    fi ;;
esac
exit 0
''')
            game.chmod(0o700)
        env = dict(os.environ, HOME=str(home))
        result = subprocess.run([str(self.driver), mode, str(user)], env=env, text=True, capture_output=True, timeout=10)
        return result, user

    def test_two_starts_at_same_clock_preserve_first_metadata_and_tape(self):
        result, user = self.run_case("pair")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(len(list((user / "tapes").glob("*.tape.json"))), 2)

    def test_exclusive_create_race_preserves_foreign_metadata(self):
        result, _ = self.run_case("race")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)

    def test_existing_tape_only_and_metadata_only_are_preserved(self):
        for mode in ("tape-only", "json-only"):
            with self.subTest(mode=mode):
                result, _ = self.run_case(mode)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_collision_exhaustion_fails_with_all_existing_bytes_preserved(self):
        if OPTIONS.baseline:
            self.skipTest("bounded reservation is absent from baseline")
        result, user = self.run_case("exhausted")
        self.assertEqual(result.returncode, 3, result.stderr)
        files = list((user / "tapes").iterdir())
        self.assertEqual(len(files), 256)
        self.assertTrue(all(path.read_bytes() == b"preexisting sentinel" for path in files))

    def test_normal_metadata_shape_and_private_permissions(self):
        result, user = self.run_case("start")
        self.assertEqual(result.returncode, 0, result.stderr)
        tape = Path(result.stdout.strip())
        meta = json.loads(Path(str(tape) + ".json").read_text())
        self.assertEqual(meta["format"], "onslaught-ce session tape 2")
        self.assertEqual(meta["tape_lines"], ["K", "M", "W", "W_CLIENT", "C"])
        self.assertEqual(meta["tape"], tape.name)
        self.assertEqual(meta["game_args"], ["-level", "100", 'quote"and\\slash'])
        self.assertEqual(meta["clock"], "shim")
        self.assertIs(meta["audio_monitor"], True)
        self.assertEqual(meta["presents_per_second"], 30)
        self.assertEqual(meta["start_files"], {"defaultoptions.bea": "Zml4dHVyZSBvcHRpb25zAP8="})
        self.assertEqual(Path(str(tape) + ".json").stat().st_mode & 0o777, 0o600)
        self.assertEqual(tape.parent, user / "tapes")

    def test_partial_metadata_failures_do_not_start_recording_or_leave_valid_metadata(self):
        if OPTIONS.baseline:
            self.skipTest("new failure checks are absent from baseline")
        for mode in ("fdopen-failure", "flush-failure", "close-failure", "write-failure"):
            with self.subTest(mode=mode):
                result, user = self.run_case(mode)
                self.assertEqual(result.returncode, 3, result.stderr)
                self.assertIn("recording failed", result.stderr)
                self.assertEqual(list((user / "tapes").iterdir()), [])

    def test_completion_requires_exact_finalizer_and_regular_final_file(self):
        if OPTIONS.baseline:
            self.skipTest("completion checks are absent from baseline")
        for mode in ("missing", "stage", "foreign", "wrong-path", "failed", "symlink", "overlong"):
            with self.subTest(mode=mode):
                result, _ = self.run_case(mode)
                self.assertEqual(result.returncode, 3, result.stderr)
                self.assertIn("game closed, recording failed", result.stderr)
                self.assertNotIn("recording saved to", result.stderr)

    def test_chunk_split_success_and_game_failure_exit_preservation(self):
        if OPTIONS.baseline:
            self.skipTest("completion checks are absent from baseline")
        for mode, code in (("success", 0), ("long-ordinary", 0), ("game-failure", 7)):
            with self.subTest(mode=mode):
                result, _ = self.run_case(mode)
                self.assertEqual(result.returncode, code, result.stderr)
                self.assertIn("recording saved to", result.stderr)

    def test_long_failure_line_remains_sticky_in_both_orders_and_prefixes(self):
        if OPTIONS.baseline:
            self.skipTest("completion checks are absent from baseline")
        for prefix in ("bea", "ce"):
            for order in ("before", "after"):
                mode = f"long-failed-{prefix}-{order}"
                with self.subTest(mode=mode):
                    result, _ = self.run_case(mode)
                    self.assertEqual(result.returncode, 3, result.stderr)
                    self.assertIn("game closed, recording failed", result.stderr)
                    self.assertNotIn("recording saved to", result.stderr)

    def test_real_child_exit_requires_publication_not_just_normal_exit_or_foreign_tape(self):
        if OPTIONS.baseline:
            self.skipTest("completion checks are absent from baseline")
        for mode, code in (("child-success", 0), ("child-stage", 3), ("child-foreign", 3),
                           ("child-failure-eof", 3), ("child-long-failure-eof", 3)):
            with self.subTest(mode=mode):
                result, _ = self.run_case(mode)
                self.assertEqual(result.returncode, code, result.stderr)
                self.assertIn("the game closed", result.stderr)
                self.assertEqual("recording saved to" in result.stderr, code == 0)

    def test_recorded_spawn_never_execs_past_completion_check_when_pipe_fails(self):
        if OPTIONS.baseline:
            self.skipTest("completion checks are absent from baseline")
        result, user = self.run_case("child-pipe-failure")
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertIn("output capture is unavailable", result.stderr)
        self.assertNotIn("the game closed", result.stderr)
        self.assertEqual(list((user / "tapes").glob("*.tape")), [])

    def test_ordinary_nonrecorded_exit_status_is_unchanged(self):
        if OPTIONS.baseline:
            self.skipTest("completion checks are absent from baseline")
        for mode, code in (("ordinary", 0), ("ordinary-failure", 7)):
            with self.subTest(mode=mode):
                result, user = self.run_case(mode)
                self.assertEqual(result.returncode, code, result.stderr)
                self.assertNotIn("recording", result.stderr)
                self.assertFalse((user / "tapes").exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows-link", action="store_true")
    parser.add_argument("--baseline", type=Path, help="reproduce original collisions against an unmodified launcher copy")
    OPTIONS, rest = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *rest])
