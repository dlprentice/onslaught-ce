#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Shared ce.ini writer against scratch files only; compiles ce_ini_driver.c itself (BEA_PORT_DATA) unless CE_INI_TEST_DRIVER names a built one.

Linux includes symlinks, a FIFO, and a real short-write failure. The ordinary/fault-injection cases also run on Windows;
Linux results do not validate Windows reparse-point handling. No game, user profile, SDL device, or network is used.
"""
import os
import signal
import subprocess
import tempfile
import unittest
from pathlib import Path

def build_driver():
    """Compile ce_ini_driver.c into a scratch folder when CE_INI_TEST_DRIVER is not set (needs BEA_PORT_DATA's SDL headers)."""
    data = os.environ.get("BEA_PORT_DATA")
    include = Path(data) / "prefix-sniper/include" if data else None
    if os.name == "nt" or not include or not include.is_dir():
        return None
    folder = Path(tempfile.mkdtemp(prefix="ce-ini-driver-"))
    exe = folder / "ce-ini-driver"
    built = subprocess.run(["gcc", "-std=gnu11", "-O0", "-I", str(include), "-o", str(exe),
                            str(Path(__file__).with_name("ce_ini_driver.c"))], capture_output=True)
    return str(exe) if built.returncode == 0 else None


DRIVER = os.environ.get("CE_INI_TEST_DRIVER") or build_driver()


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

    # The [Experimental] block (ce_ini_add_section) and the keys written around it.
    BLOCK = (b"[Experimental]\n; Unfinished features, off in this release.\n"
             b"map_editor=0\nmuseum=0\ncommander=0\nplay_unit=0\n")

    def add_block(self, path=None, **env):
        return subprocess.run([DRIVER, str(path or self.ini), "--experimental"], env=self.env | env, capture_output=True, timeout=10)

    def test_block_follows_the_lines_and_is_written_once(self):
        self.assertEqual(self.add_block().returncode, 0)
        self.assertEqual(self.ini.read_bytes(), self.original + b"\n" + self.BLOCK)
        written = self.ini.read_bytes()
        self.assertEqual(self.add_block().returncode, 0)
        self.assertEqual(self.ini.read_bytes(), written)
        self.assertEqual(list(self.user.glob("ce.ini.tmp-*")), [])

    def test_block_in_a_new_file_and_a_new_folder(self):
        path = self.root / "fresh" / "ce.ini"
        self.assertEqual(self.add_block(path).returncode, 0)
        self.assertEqual(path.read_bytes(), self.BLOCK)

    def test_block_keeps_a_value_the_player_set_and_does_not_repeat_it(self):
        self.ini.write_bytes(b"commander=1\nplay_unit=tank\r\nlast=no-newline")
        self.assertEqual(self.add_block().returncode, 0)
        self.assertEqual(self.ini.read_bytes(),
                         b"commander=1\nplay_unit=tank\r\nlast=no-newline\n\n[Experimental]\n; Unfinished features, off in this release.\nmap_editor=0\nmuseum=0\n")

    def test_block_with_a_header_already_there_changes_nothing(self):
        for header in (b"[Experimental]\n", b"[Experimental]\r\n", b"[Experimental]  \n"):
            self.ini.write_bytes(b"a=1\n" + header + b"museum=1\n")
            before = self.ini.read_bytes()
            self.assertEqual(self.add_block().returncode, 0)
            self.assertEqual(self.ini.read_bytes(), before, header)

    def test_new_key_goes_above_the_header_and_existing_keys_change_in_place(self):
        self.ini.write_bytes(self.original + b"\n" + self.BLOCK)
        self.assertEqual(self.write(value="0").returncode, 0)     # in place, above the header
        self.assertEqual(self.ini.read_bytes(), b"record_sessions=0\nunknown=retain\n\n" + self.BLOCK)
        result = subprocess.run([DRIVER, str(self.ini), "update_check", "0"], env=self.env, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(self.ini.read_bytes(), b"record_sessions=0\nunknown=retain\nupdate_check=0\n\n" + self.BLOCK)
        result = subprocess.run([DRIVER, str(self.ini), "museum", "1"], env=self.env, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(self.ini.read_bytes(), b"record_sessions=0\nunknown=retain\nupdate_check=0\n\n" + self.BLOCK.replace(b"museum=0", b"museum=1"))

    def test_a_duplicate_of_a_key_in_the_block_collapses_to_one(self):
        self.ini.write_bytes(b"museum=1\n" + self.BLOCK)
        result = subprocess.run([DRIVER, str(self.ini), "museum", "0"], env=self.env, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(self.ini.read_bytes(), b"museum=0\n" + self.BLOCK.replace(b"museum=0\n", b""))

    def key(self, key, value, path=None, **env):
        return subprocess.run([DRIVER, str(path or self.ini), key, value], env=self.env | env, capture_output=True, timeout=10)

    def test_a_file_that_has_the_header_is_not_replaced_at_all(self):
        # F1: the block is written once, and a later start does not rewrite the file (temp file, fsync and rename) for nothing
        for text in (b"a=1\n" + self.BLOCK, b"a=1\r\n[Experimental]\r\nmuseum=1\r\nlast=no-newline", b"[Experimental]  \n", self.BLOCK.replace(b"play_unit=0\n", b"")):
            with self.subTest(text=text):
                self.ini.write_bytes(text)
                os.utime(self.ini, ns=(1_000_000_000, 2_000_000_000))
                before = self.ini.stat()
                self.assertEqual(self.add_block().returncode, 0)
                after = self.ini.stat()
                self.assertEqual(self.ini.read_bytes(), text)
                self.assertEqual((after.st_ino, after.st_mtime_ns, after.st_mode), (before.st_ino, before.st_mtime_ns, before.st_mode))
                self.assertEqual(list(self.user.glob("ce.ini.tmp-*")), [])

    @unittest.skipIf(os.name == "nt", "Linux permission bits")
    def test_a_file_that_has_the_header_is_not_even_opened_for_writing(self):
        # the early return must not need the folder to be writable: a read-only folder and a read-only file are fine when nothing is to be written
        self.ini.write_bytes(self.original + b"\n" + self.BLOCK)
        self.ini.chmod(0o444)
        self.user.chmod(0o555)
        try:
            self.assertEqual(self.add_block().returncode, 0)
        finally:
            self.user.chmod(0o755)
        self.assertEqual(self.ini.stat().st_mode & 0o777, 0o444)

    @unittest.skipIf(os.name == "nt", "Linux permission bits")
    def test_a_rewrite_keeps_the_files_mode(self):
        # V1: the temporary file is private (0600); the replacement must have the old file's bits, not that
        for mode in (0o644, 0o640, 0o600, 0o664, 0o620):
            with self.subTest(mode=oct(mode)):
                self.ini.write_bytes(self.original)
                self.ini.chmod(mode)
                self.assertEqual(self.write().returncode, 0)
                self.assertEqual(self.ini.stat().st_mode & 0o7777, mode)
                self.ini.write_bytes(self.original)
                self.ini.chmod(mode)
                self.assertEqual(self.add_block().returncode, 0)
                self.assertEqual(self.ini.read_bytes(), self.original + b"\n" + self.BLOCK)
                self.assertEqual(self.ini.stat().st_mode & 0o7777, mode)

    @unittest.skipIf(os.name == "nt", "Linux permission bits")
    def test_a_new_file_is_private_to_its_owner(self):
        path = self.root / "brand-new" / "ce.ini"
        self.assertEqual(self.add_block(path).returncode, 0)
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    @unittest.skipIf(os.name == "nt", "Linux permission bits")
    def test_a_read_only_file_is_never_replaced(self):
        # V1: the player made it read-only; neither a key nor the block replaces it, and the call says so (the caller logs once)
        for mode in (0o444, 0o400, 0o440):
            with self.subTest(mode=oct(mode)):
                self.ini.write_bytes(self.original)
                self.ini.chmod(mode)
                before = self.ini.stat()
                self.assertEqual(self.write().returncode, 1)
                self.assertEqual(self.key("new_key", "1").returncode, 1)
                self.assertEqual(self.add_block().returncode, 1)
                after = self.ini.stat()
                self.assertEqual(self.ini.read_bytes(), self.original)
                self.assertEqual((after.st_ino, after.st_mode), (before.st_ino, before.st_mode))
                self.assertEqual(list(self.user.glob("ce.ini.tmp-*")), [])
                self.ini.chmod(0o600)

    def pad(self, size):
        """size bytes of ce.ini lines: 'a=1' lines, then one line to land exactly on the size."""
        line = b"a=1\n"
        body = line * (size // len(line))
        rest = size - len(body)
        if rest:
            body = body[:-len(line)] + b"b=" + b"x" * (len(line) + rest - 3) + b"\n"
        self.assertEqual(len(body), size)
        return body

    def test_the_writer_never_grows_the_file_past_what_the_readers_accept(self):
        # F2: the readers refuse a ce.ini over 1 MiB and use their defaults; the writer made one by adding a line to a file at the limit
        limit = 1 << 20
        full = self.pad(limit)
        self.ini.write_bytes(full)
        self.assertEqual(self.add_block().returncode, 1)
        self.assertEqual(self.key("new_key", "1").returncode, 1)
        self.assertEqual(self.ini.read_bytes(), full)
        self.assertEqual(list(self.user.glob("ce.ini.tmp-*")), [])
        # ... and a write that lands exactly on the limit is allowed, one byte more is not
        body = self.pad(limit - len(b"new_key=1\n"))
        self.ini.write_bytes(body)
        self.assertEqual(self.key("new_key", "1").returncode, 0)
        self.assertEqual(self.ini.read_bytes(), body + b"new_key=1\n")
        self.assertEqual(self.ini.stat().st_size, limit)
        body = self.pad(limit - len(b"new_key=1\n") + 1)
        self.ini.write_bytes(body)
        self.assertEqual(self.key("new_key", "1").returncode, 1)
        self.assertEqual(self.ini.read_bytes(), body)
        # a file at the limit that already has the block needs no write and is fine
        full = self.pad(limit - len(self.BLOCK)) + self.BLOCK
        self.ini.write_bytes(full)
        self.assertEqual(self.add_block().returncode, 0)
        self.assertEqual(self.ini.read_bytes(), full)

    def test_a_key_that_only_begins_like_a_block_key_is_not_that_key(self):
        # the review's key-prefix gap: museum_x=1 sits next to the block writer; only "museum=" is the key museum
        self.ini.write_bytes(b"museum_x=1\nplay_unit_y=tank\nmap_editor_=1\n")
        self.assertEqual(self.add_block().returncode, 0)
        self.assertEqual(self.ini.read_bytes(),
                         b"museum_x=1\nplay_unit_y=tank\nmap_editor_=1\n\n" + self.BLOCK)
        self.assertEqual(self.key("museum", "1").returncode, 0)
        self.assertEqual(self.ini.read_bytes(),
                         b"museum_x=1\nplay_unit_y=tank\nmap_editor_=1\n\n" + self.BLOCK.replace(b"museum=0", b"museum=1"))
        # a key set while there is no block goes in as its own line beside the look-alike, and the look-alike stays
        self.ini.write_bytes(b"museum_x=1\n")
        self.assertEqual(self.key("museum", "1").returncode, 0)
        self.assertEqual(self.ini.read_bytes(), b"museum_x=1\nmuseum=1\n")
        self.assertEqual(self.key("museum_x", "0").returncode, 0)
        self.assertEqual(self.ini.read_bytes(), b"museum_x=0\nmuseum=1\n")

    def test_block_refused_and_failed_writes_preserve_the_original(self):
        self.assertEqual(self.add_block(CE_TEST_FAIL_SYNC="1").returncode, 1)
        self.assertEqual(self.ini.read_bytes(), self.original)
        self.assertEqual(self.add_block(CE_TEST_FAIL_RENAME="1").returncode, 1)
        self.assertEqual(self.ini.read_bytes(), self.original)
        self.assertEqual(list(self.user.glob("ce.ini.tmp-*")), [])

    @unittest.skipIf(os.name == "nt", "Linux symlink check")
    def test_block_not_written_through_a_link(self):
        sentinel = self.root / "outside-sentinel"
        sentinel.write_bytes(b"keep sentinel")
        linked = self.user / "linked.ini"
        linked.symlink_to(sentinel)
        self.assertEqual(self.add_block(linked).returncode, 1)
        self.assertEqual(sentinel.read_bytes(), b"keep sentinel")


if __name__ == "__main__":
    unittest.main()
