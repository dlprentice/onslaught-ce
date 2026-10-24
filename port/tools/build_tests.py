#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused launcher build checks; no compiler, game files or network required."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build  # noqa: E402


class LauncherVersionTests(unittest.TestCase):
    def test_release_versions_are_padded_without_losing_components(self):
        for version, expected in [("0.1", (0, 1, 0, 0)), ("0.1.4", (0, 1, 4, 0)),
                                  ("1", (1, 0, 0, 0)), ("1.2.3.65535", (1, 2, 3, 65535))]:
            with self.subTest(version=version):
                self.assertEqual(expected, build.launcher_version_words(version))

    def test_prerelease_and_build_suffixes_leave_numeric_core_intact(self):
        for version in ("0.1.4-rc.2+build.9", "0.1.4-rc--2+build-9"):
            with self.subTest(version=version):
                self.assertEqual((0, 1, 4, 0), build.launcher_version_words(version))

    def test_named_development_versions_use_zero(self):
        self.assertEqual((0, 0, 0, 0), build.launcher_version_words("dev"))
        self.assertEqual((0, 0, 0, 0), build.launcher_version_words("feature-test+dirty"))

    def test_overflow_and_malformed_release_labels_fail(self):
        for version in ("65536.0", "0.1.2.65536", "1.2.3.4.5", "1..2", "1.2-", "1.2+", "1.2preview",
                        "1.2-rc..1", "1.2+build+other", "", "-1.2", "bad\"label", "bad\nlabel", "9" * 5000):
            with self.subTest(version=version), self.assertRaises(ValueError):
                build.launcher_version_words(version)

    def test_resource_compiler_receives_exact_label_and_numeric_core(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(build.subprocess, "run") as run:
            out = Path(folder) / "launcher.rc.o"
            self.assertEqual(out, build.launcher_version_object(out, "0.1.4-rc.2+build.9"))
            args = run.call_args.args[0]
            header = (out.parent / "launcher-version.h").read_text()
            self.assertIn("#define CE_VERSION_WORDS 0,1,4,0\n", header)
            self.assertIn('#define CE_VERSION_TEXT "0.1.4-rc.2+build.9"\n', header)
            self.assertEqual(str(out.parent), args[args.index("-I") + 1])
            self.assertIn(str(build.RESOURCE.with_name("launcher-version.rc")), args)
            self.assertTrue(run.call_args.kwargs["check"])

    def test_windows_launcher_links_both_resources(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "monocypher-4.0.2/src").mkdir(parents=True)
            with patch.object(build.deps, "SRC", root), patch.object(build, "source_commit", return_value="test"), \
                 patch.object(build, "manifest_object", return_value=root / "utf8.o") as manifest, \
                 patch.object(build, "launcher_version_object", return_value=root / "version.o") as version, \
                 patch.object(build.subprocess, "run") as run:
                run.return_value.returncode = 0
                self.assertEqual(0, build.build_launcher("windows", "0.1.4-rc.2"))
                manifest.assert_called_once()
                version.assert_called_once_with(build.BUILD / "windows/launcher-version.rc.o", "0.1.4-rc.2")
                args = run.call_args.args[0]
                self.assertIn(str(root / "utf8.o"), args)
                self.assertIn(str(root / "version.o"), args)

    def test_invalid_windows_version_fails_before_the_game_mirror(self):
        with patch.object(sys, "argv", ["build.py", "--public", "--target", "windows", "--ce-version", "65536.0"]), \
             patch.object(build, "PUBLIC", False), patch.object(build, "build_mirror") as mirror:
            with self.assertRaisesRegex(SystemExit, "Windows file-version parts must be between 0 and 65535"):
                build.main()
            mirror.assert_not_called()


if __name__ == "__main__":
    unittest.main()
