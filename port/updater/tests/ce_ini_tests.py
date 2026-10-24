#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Shared ce.ini writer against scratch files only; compile ce_ini_driver.c first and set CE_INI_TEST_DRIVER.

Linux includes symlinks, a FIFO, and a real short-write failure. The ordinary/fault-injection cases also run on Windows;
Linux results do not validate Windows reparse-point handling. No game, user profile, SDL device, or network is used.
"""
import os
import signal
import subprocess
import tempfile
import unittest
from pathlib import Path

DRIVER = os.environ.get("CE_INI_TEST_DRIVER")


@unittest.skipUnless(DRIVER and Path(DRIVER).is_file(), "set CE_INI_TEST_DRIVER to the compiled isolated driver")
class SettingsWriter(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ce-settings-test-")
        self.root = Path(self.temp.name)
        self.user = self.root / "user"
        self.user.mkdir()
        self.ini = self.user / "ce.ini"
        self.original = b"record_sessions=1\nunknown=retain\n"
        self.ini.write_bytes(self.original)
        self.env = {k: v for k, v in os.environ.items() if not k.startswith("CE_TEST_")}
        self.env.update(HOME=str(self.root), USERPROFILE=str(self.root))

    def tearDown(self):
        self.temp.cleanup()

    def write(self, path=None, value="0", **env):
        return subprocess.run([DRIVER, str(path or self.ini), "record_sessions", value],
                              env=self.env | env, capture_output=True, timeout=10)

    def assert_refused(self, result):
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(self.ini.read_bytes(), self.original)
        self.assertEqual(list(self.user.glob("ce.ini.tmp-*")), [])

    def test_replace_preserves_other_bytes_and_normalizes_final_newline(self):
        self.ini.write_bytes(b"before=\xff\x00bytes\r\nrecord_sessions=1\r\nrecord_sessions=1\nlast=no-newline")
        self.assertEqual(self.write().returncode, 0)
        self.assertEqual(self.ini.read_bytes(), b"before=\xff\x00bytes\r\nrecord_sessions=0\nlast=no-newline\n")
        self.assertEqual(list(self.user.glob("ce.ini.tmp-*")), [])

    def test_create_missing_parents(self):
        path = self.root / "new" / "nested" / "ce.ini"
        self.assertEqual(self.write(path).returncode, 0)
        self.assertEqual(path.read_bytes(), b"record_sessions=0\n")

    @unittest.skipIf(os.name == "nt", "Linux execute-only directory traversal")
    def test_execute_only_ancestor_is_supported(self):
        parent = self.root / "search-only"
        user = parent / "user"
        user.mkdir(parents=True)
        ini = user / "ce.ini"
        ini.write_bytes(self.original)
        parent.chmod(0o111)
        try:
            result = self.write(ini)
        finally:
            parent.chmod(0o700)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(ini.read_bytes(), b"record_sessions=0\nunknown=retain\n")

    def test_unrelated_existing_temp_is_preserved(self):
        old = self.user / "ce.ini.tmp"
        old.write_bytes(b"retained old temporary output")
        self.assertEqual(self.write().returncode, 0)
        self.assertEqual(old.read_bytes(), b"retained old temporary output")

    def test_sync_failure_preserves_original(self):
        self.assert_refused(self.write(CE_TEST_FAIL_SYNC="1"))

    def test_rename_failure_preserves_original(self):
        self.assert_refused(self.write(CE_TEST_FAIL_RENAME="1"))

    def test_nonregular_settings_refused(self):
        directory = self.user / "directory.ini"
        directory.mkdir()
        self.assertEqual(self.write(directory).returncode, 1)
        self.assertTrue(directory.is_dir())

    @unittest.skipIf(os.name == "nt", "Linux symlink check; Windows reparse points require a Windows run")
    def test_old_temp_symlink_cannot_overwrite_target(self):
        sentinel = self.root / "outside-sentinel"
        sentinel.write_bytes(b"keep sentinel")
        old = self.user / "ce.ini.tmp"
        old.symlink_to(sentinel)
        self.assertEqual(self.write().returncode, 0)
        self.assertEqual(sentinel.read_bytes(), b"keep sentinel")
        self.assertTrue(old.is_symlink())
        self.assertFalse(self.ini.is_symlink())

    @unittest.skipIf(os.name == "nt", "Linux symlink check")
    def test_exclusive_temp_collisions_preserve_every_target(self):
        sentinel = self.root / "outside-sentinel"
        sentinel.write_bytes(b"keep sentinel")
        with subprocess.Popen([DRIVER, str(self.ini), "record_sessions", "0", "--wait"], env=self.env,
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as p:
            pid = int(p.stdout.readline())
            links = [self.user / f"ce.ini.tmp-{pid}-{i}" for i in range(64)]
            for link in links:
                link.symlink_to(sentinel)
            _, err = p.communicate("\n", timeout=10)
            self.assertEqual(p.returncode, 1, err)
        self.assertEqual(self.ini.read_bytes(), self.original)
        self.assertEqual(sentinel.read_bytes(), b"keep sentinel")
        self.assertTrue(all(link.is_symlink() for link in links))

    @unittest.skipIf(os.name == "nt", "Linux symlink check")
    def test_settings_symlink_refused(self):
        sentinel = self.root / "outside-sentinel"
        sentinel.write_bytes(b"keep sentinel")
        linked = self.user / "linked.ini"
        linked.symlink_to(sentinel)
        self.assertEqual(self.write(linked).returncode, 1)
        self.assertEqual(sentinel.read_bytes(), b"keep sentinel")
        self.assertTrue(linked.is_symlink())

    @unittest.skipIf(os.name == "nt", "Linux symlink check")
    def test_root_and_ancestor_symlinks_refused_before_creation(self):
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "sentinel").write_bytes(b"keep sentinel")
        linked = self.root / "linked"
        linked.symlink_to(outside, target_is_directory=True)
        for path in (linked / "ce.ini", linked / "new" / "nested" / "ce.ini"):
            with self.subTest(path=path):
                self.assertEqual(self.write(path).returncode, 1)
                self.assertEqual(sorted(p.name for p in outside.iterdir()), ["sentinel"])

    @unittest.skipIf(os.name == "nt", "Linux retained-directory-fd check")
    def test_parent_replaced_during_write_does_not_redirect_replacement(self):
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "sentinel").write_bytes(b"keep sentinel")
        moved = self.root / "retained-user"
        result = self.write(CE_TEST_SWAP_PARENT=str(self.user), CE_TEST_MOVED_PARENT=str(moved),
                            CE_TEST_OUTSIDE=str(outside))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((moved / "ce.ini").read_bytes(), b"record_sessions=0\nunknown=retain\n")
        self.assertEqual(sorted(p.name for p in outside.iterdir()), ["sentinel"])
        self.assertTrue(self.user.is_symlink())

    @unittest.skipIf(os.name == "nt", "Linux FIFO check")
    def test_fifo_is_refused_without_blocking(self):
        fifo = self.user / "pipe.ini"
        os.mkfifo(fifo)
        self.assertEqual(self.write(fifo).returncode, 1)

    @unittest.skipIf(os.name == "nt", "Linux RLIMIT_FSIZE short-write check")
    def test_short_write_preserves_original(self):
        import resource

        def limit():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (16, 16))

        result = subprocess.run([DRIVER, str(self.ini), "record_sessions", "x" * 8192], env=self.env,
                                capture_output=True, timeout=10, preexec_fn=limit)
        self.assert_refused(result)


if __name__ == "__main__":
    unittest.main()
