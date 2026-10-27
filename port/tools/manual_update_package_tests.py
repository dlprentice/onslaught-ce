#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual package text/wrapper controls for suspended automatic updating; authored launchers only, no game/network."""
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("ce_package", HERE / "package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class ManualUpdatePackage(unittest.TestCase):
    def test_generated_readmes_describe_manual_only_migration_on_both_targets(self):
        with patch.object(package.subprocess, "run", side_effect=AssertionError("text generation must not spawn a process")):
            for windows in (False, True):
                with self.subTest(windows=windows):
                    text = package.readme("0.1.7", windows).decode()
                    for phrase in ("Automatic update checking and installation are temporarily disabled", "exits 2",
                                   "without checking whether this version is current", "new empty folder",
                                   "Do not start an Onslaught CE 0.1.0-0.1.6 launcher at all", ".download-* and .unpack-*",
                                   "removes no neighbouring paths", "Manual updates",
                                   "both launcher and game user-data folders", "relative BEA_USER_DIR", "different locations",
                                   "each whole folder to an independent backup", "separate working copy", "game's user-data folder", "its absolute path",
                                   "do not merge or overwrite"):
                        self.assertIn(phrase, text)
                    for stale in ("checks this project's GitHub releases", "how to turn checks back", "Check for updates",
                                  "Use a published release whose notes address"):
                        self.assertNotIn(stale, text)
                    with patch.object(package, "source_commit", return_value="authored"):
                        notices = package.notices("0.1.7", windows).decode()
                    self.assertIn("compiled into the launcher; unused while automatic updating is suspended", notices)
        self.assertNotIn("the update check needs curl", package.PREFLIGHT)

    def test_actual_wrapper_uses_the_real_launcher_option_boundary(self):
        owner = HERE.parents[1] / "local-data/port/manual-package-tests"
        owner.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="case-", dir=owner) as tmp:
            root = Path(tmp)
            wrapper = root / "onslaught-ce"
            wrapper.write_text(package.LAUNCHER)
            wrapper.chmod(0o755)
            # Compile the production entry point and manual message with inert, counted host boundaries. The child
            # returns the real entry point's exit code; its JSON exposes whether game dispatch was reached.
            spec = importlib.util.spec_from_file_location("launcher_controls", HERE.parent / "updater/tests/update_restart_tests.py")
            controls = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(controls)
            seams = controls.SEAMS
            original = 'puts("]}");return 0;'
            self.assertEqual(seams.count(original), 1)
            seams = seams.replace(original, 'puts("]}");return rc;')
            source = (HERE.parent / "updater/ce_launch.c").read_text()
            driver = root / "driver.c"
            driver.write_text(seams.replace("@FUNCTIONS@", controls.production_functions(source)))
            compiled = subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                                       "-Wno-unused-function", "-Wno-unused-variable", str(driver), "-o",
                                       str(root / "onslaught-ce-launch")], capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            (root / "preflight.sh").write_text('#!/bin/sh\nprintf invoked > "$(dirname "$0")/preflight-called"\nexit "${TEST_PREFLIGHT_STATUS:-90}"\n')
            (root / "preflight.sh").chmod(0o755)
            env = {"PATH": os.environ.get("PATH", ""), "HOME": str(root), "BEA_SKIP_PREFLIGHT": "0"}
            marker = root / "preflight-called"

            def invoke(args, **extra):
                marker.unlink(missing_ok=True)
                return subprocess.run([str(wrapper), *args], env=env | extra, capture_output=True, text=True, timeout=5)

            def row(result):
                lines = [line[7:] for line in result.stdout.splitlines() if line.startswith("RESULT ")]
                self.assertEqual(len(lines), 1, result.stdout + result.stderr)
                return json.loads(lines[0])

            # Every listed prefix exits before the game, even with a failing preflight. Order and the first
            # unrecognized argument match the C launcher, including version/apply precedence over update-only.
            nongame = [
                (("--version",), 0, "version"), (("--no-update-check", "--version"), 0, "version"),
                (("--update-only", "--version"), 0, "version"),
                (("--apply", "fresh", "install", "42"), 2, "apply"),
                (("--no-update-check", "--apply", "fresh"), 2, "apply"),
                (("--update-only", "--apply"), 2, "apply"),
                (("--update-only",), 2, "update"), (("--no-update-check", "--update-only"), 2, "update"),
                (("--record", "--no-record", "--update-only"), 2, "update"),
                (("--update-only", "--", "--version"), 2, "update"),
                (("--update-only", "--", "--apply"), 2, "update"),
                (("--update-only", "-level", "100"), 2, "update"),
                (("--report",), 0, "report"), (("--no-update-check", "--report"), 0, "report"),
                (("--report", "--", "--update-only"), 0, "report"),
                (("--report", "-level", "100"), 0, "report"),
            ]
            for args, expected, kind in nongame:
                with self.subTest(args=args):
                    result = invoke(args)
                    self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                    self.assertFalse(marker.exists())
                    data = row(result)
                    self.assertEqual((data["return"], data["games"], data["finds"], data["forbidden"]), (expected, 0, 0, 0))
                    if kind == "update":
                        self.assertIn("did not check whether your version is current", result.stderr)
                        self.assertEqual(data["paths"], 0)
                    if kind == "apply":
                        self.assertIn("--apply was refused", result.stderr)
                        self.assertEqual(data["paths"], 0)
                    if kind == "version":
                        self.assertIn("Onslaught CE v", result.stdout)
                        self.assertEqual(data["paths"], 0)
                    if kind == "report":
                        self.assertEqual(data["reports"], 1)

            game = [
                ((), []), (("--record",), []), (("-level", "100"), ["-level", "100"]),
                (("-level", "100", "--version"), ["-level", "100", "--version"]),
                (("--", "--version"), ["--version"]), (("--", "--update-only"), ["--update-only"]),
                (("--", "--apply"), ["--apply"]), (("--record", "--", "--report"), ["--report"]),
                (("--no-update-check", "--", "--version"), ["--version"]),
                (("--other", "--version"), ["--other", "--version"]),
                (("--no-update-check", "-level", "--update-only"), ["-level", "--update-only"]),
                (("--update-only=1", "--apply"), ["--update-only=1", "--apply"]),
                (("--no-record", "--", "a b"), ["a b"]),
            ]
            for args, game_args in game:
                with self.subTest(args=args):
                    failed = invoke(args)
                    self.assertEqual(failed.returncode, 1, failed.stdout + failed.stderr)
                    self.assertEqual(marker.read_text(), "invoked")
                    self.assertNotIn("RESULT ", failed.stdout)
                    allowed = invoke(args, TEST_PREFLIGHT_STATUS="0")
                    self.assertEqual(allowed.returncode, 0, allowed.stdout + allowed.stderr)
                    self.assertEqual(marker.read_text(), "invoked")
                    data = row(allowed)
                    self.assertEqual((data["games"], data["forbidden"], data["game_args"]), (1, 0, game_args))

            explicit_skip = invoke(("--", "--update-only"), BEA_SKIP_PREFLIGHT="1")
            self.assertEqual(explicit_skip.returncode, 0)
            self.assertFalse(marker.exists())
            self.assertEqual(row(explicit_skip)["game_args"], ["--update-only"])


if __name__ == "__main__":
    unittest.main()
