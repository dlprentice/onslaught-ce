#!/usr/bin/env python3
"""Check private input-test runtime directories under an ordinary POSIX umask."""

import contextlib
import io
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest
from unittest import mock

import jeppi_fkey_focus_tests as input_tests


@unittest.skipUnless(os.name == 'posix', 'POSIX directory modes are not available')
class RuntimeDirectoryPermissionsTests(unittest.TestCase):
    def test_private_runtime_directory_with_umask_022(self):
        with tempfile.TemporaryDirectory(prefix='ce-fkey-mode-') as temp:
            directory = Path(temp)

            def check_child(arguments, *, cwd, env, stdout, stderr, timeout, check):
                runtime = Path(env['XDG_RUNTIME_DIR'])
                self.assertEqual(runtime, Path(env['HOME']))
                self.assertEqual(stat.S_IMODE(runtime.stat().st_mode), 0o700)
                stdout.write(b'PASS: synthetic child\n')
                return subprocess.CompletedProcess(arguments, 0)

            previous_umask = os.umask(0o022)
            try:
                with mock.patch.object(input_tests.subprocess, 'run', side_effect=check_child):
                    with contextlib.redirect_stdout(io.StringIO()):
                        ok = input_tests.run_case(
                            directory, 'runtime permissions',
                            'SDL_SCANCODE_F1', 'SDL_SCANCODE_F1', False,
                        )
            finally:
                os.umask(previous_umask)
            self.assertTrue(ok)


if __name__ == '__main__':
    unittest.main()
