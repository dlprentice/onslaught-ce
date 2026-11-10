#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run a real mod-loader fixture under a synthetic Unicode user-folder path."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import mods_tests


class UnicodeUserFolderTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform.startswith("linux"), "This fixture exercises the Linux loader")
    def test_loader_accepts_space_and_unicode_in_user_folder(self):
        mods_tests.driver()
        original_directory = tempfile.TemporaryDirectory
        original_popen = subprocess.Popen
        marker = "CE test user é 世界"
        observed = []

        class UnicodeTemporaryDirectory:
            def __init__(self, *args, **kwargs):
                self._inner = original_directory(*args, **kwargs)
                self.name = str(Path(self._inner.name) / marker)
                Path(self.name).mkdir()

            def cleanup(self):
                self._inner.cleanup()

            def __enter__(self):
                return self.name

            def __exit__(self, *args):
                self.cleanup()

        class ObservedPopen(original_popen):
            def __init__(self, *args, **kwargs):
                env = kwargs.get("env") or os.environ
                path = env.get("BEA_USER_DIR")
                if path is not None:
                    observed.append(marker in path and " " in path and any(ord(ch) > 127 for ch in path))
                super().__init__(*args, **kwargs)

        case = mods_tests.ModsTests("test_disabled_mod_is_not_loaded")
        result = unittest.TestResult()
        with mock.patch.object(tempfile, "TemporaryDirectory", UnicodeTemporaryDirectory), \
                mock.patch.object(subprocess, "Popen", ObservedPopen):
            case.run(result)
        self.assertEqual(result.testsRun, 1)
        self.assertEqual(result.errors, [])
        self.assertEqual(result.failures, [])
        self.assertEqual(result.skipped, [])
        self.assertTrue(observed, "the fixture must supply a user folder to its driver")
        self.assertTrue(all(observed), "every supplied user folder must use the Unicode fixture")


if __name__ == "__main__":
    unittest.main()
