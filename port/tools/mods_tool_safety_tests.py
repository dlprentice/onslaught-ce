#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored fixtures for experimental mod-tool data safety; never reads a real game or save."""
import json
import os
import resource
import struct
import subprocess
import sys
import tempfile
import tracemalloc
import unittest
import zlib
from pathlib import Path

import mods

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "port/tools/mods.py"
OUT = ROOT / "local-data/port/mods-tool-safety"


def dds():
    header = bytearray(128)
    header[:4] = b"DDS "
    struct.pack_into("<IIII", header, 4, 124, 0x1007, 8, 8)
    struct.pack_into("<I", header, 76, 32)
    struct.pack_into("<I", header, 80, 4)
    header[84:88] = b"DXT3"
    return bytes(header) + bytes(64)


class ModToolSafety(unittest.TestCase):
    def setUp(self):
        OUT.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix="case-", dir=OUT))
        self.user = self.root / "user"
        self.game = self.root / "game"
        self.user.mkdir()
        self.game.mkdir()
        self.texture = self.game / mods.BE_TEXTURE
        self.texture.parent.mkdir(parents=True)
        self.original_texture = mods.pack(dds())
        self.texture.write_bytes(self.original_texture)

    def cli(self, *args):
        return subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True, timeout=20)

    def test_enable_refused_before_creating_a_mod_or_following_config_link(self):
        outside = self.root / "outside.ini"
        original = b"keep=\xff\r\nmods=other,another\r\n"
        outside.write_bytes(original)
        (self.user / "ce.ini").symlink_to(outside)
        p = self.cli("test-mod", "recolor", self.user, "--game", self.game, "--enable")
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("automatic mod enabling is disabled", p.stderr)
        self.assertEqual(outside.read_bytes(), original)
        self.assertFalse((self.user / "mods").exists())

    def test_fresh_test_mod_preserves_config_game_and_existing_files(self):
        config = self.user / "ce.ini"
        original = b"unknown=\xff\r\nmods=existing\r\n"
        config.write_bytes(original)
        p = self.cli("test-mod", "recolor", self.user, "--game", self.game)
        self.assertEqual(p.returncode, 0, p.stderr)
        root = self.user / "mods/ce-test-recolor"
        manifest = json.loads((root / "mod.json").read_text())
        self.assertEqual(manifest["files"][0]["path"], mods.BE_TEXTURE)
        self.assertEqual(config.read_bytes(), original)
        self.assertEqual(self.texture.read_bytes(), self.original_texture)
        sentinel = (root / mods.BE_TEXTURE).read_bytes()
        again = self.cli("test-mod", "recolor", self.user, "--game", self.game)
        self.assertNotEqual(again.returncode, 0)
        self.assertEqual((root / mods.BE_TEXTURE).read_bytes(), sentinel)
        self.assertEqual(config.read_bytes(), original)

    def test_symlinked_mods_and_mod_root_never_touch_game(self):
        mods_dir = self.user / "mods"
        mods_dir.mkdir()
        (mods_dir / "ce-test-recolor").symlink_to(self.game)
        p = self.cli("test-mod", "recolor", self.user, "--game", self.game)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(self.texture.read_bytes(), self.original_texture)
        self.assertFalse((self.game / "mod.json").exists())

    def test_symlinked_parent_never_gets_output(self):
        other = self.root / "outside"
        other.mkdir()
        (self.user / "mods").symlink_to(other)
        p = self.cli("test-mod", "recolor", self.user, "--game", self.game)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(list(other.iterdir()), [])

    def test_user_folder_inside_game_is_refused(self):
        p = self.cli("test-mod", "recolor", self.game, "--game", self.game)
        self.assertNotEqual(p.returncode, 0)
        self.assertFalse((self.game / "mods").exists())

    def test_manifest_never_overwrites_existing_file_or_link(self):
        mod = self.user / "mymod"
        data = mod / "data"
        data.mkdir(parents=True)
        (data / "thing.bin").write_bytes(b"content")
        outside = self.root / "outside.json"
        outside.write_bytes(b"keep me")
        (mod / "mod.json").symlink_to(outside)
        p = self.cli("manifest", mod, "--name", "X", "--version", "1", "--game", self.game)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(outside.read_bytes(), b"keep me")

    def test_new_manifest_includes_files_and_refuses_replacement(self):
        mod = self.user / "newmod"
        data = mod / "data"
        data.mkdir(parents=True)
        (data / "thing.bin").write_bytes(b"content")
        p = self.cli("manifest", mod, "--name", "X", "--version", "1", "--game", self.game)
        self.assertEqual(p.returncode, 0, p.stderr)
        saved = (mod / "mod.json").read_bytes()
        manifest = json.loads(saved)
        self.assertEqual(manifest["files"][0]["path"], "data/thing.bin")
        again = self.cli("manifest", mod, "--name", "Y", "--version", "2", "--game", self.game)
        self.assertNotEqual(again.returncode, 0)
        self.assertEqual((mod / "mod.json").read_bytes(), saved)

    def test_manifest_directory_and_total_byte_budgets_fail_before_publication(self):
        mod = self.user / "budget"
        data = mod / "data"
        data.mkdir(parents=True)
        for i in range(3):
            (data / f"{i}.bin").write_bytes(b"xx")
        nodes, byte_limit = mods.MAX_MANIFEST_NODES, mods.MAX_MANIFEST_BYTES
        try:
            mods.MAX_MANIFEST_NODES = 2
            with self.assertRaisesRegex(ValueError, "filesystem entries"):
                mods.write_manifest(mod, "X", "1", game=self.game)
            self.assertFalse((mod / "mod.json").exists())
            mods.MAX_MANIFEST_NODES = nodes
            mods.MAX_MANIFEST_BYTES = 5
            with self.assertRaisesRegex(ValueError, "1 GiB"):
                mods.write_manifest(mod, "X", "1", game=self.game)
            self.assertFalse((mod / "mod.json").exists())
        finally:
            mods.MAX_MANIFEST_NODES, mods.MAX_MANIFEST_BYTES = nodes, byte_limit

    def test_pack_refuses_link_hardlink_existing_and_game_destinations(self):
        source = self.root / "input"
        source.write_bytes(b"test")
        other = self.root / "other"
        other.write_bytes(b"untouched")
        link = self.root / "link"
        link.symlink_to(other)
        hard = self.root / "hard"
        os.link(other, hard)
        for dst in (link, hard, other, self.game / "new.bin"):
            p = self.cli("pack", source, dst, "--game", self.game)
            self.assertNotEqual(p.returncode, 0, (dst, p.stdout, p.stderr))
        self.assertEqual(other.read_bytes(), b"untouched")
        self.assertFalse((self.game / "new.bin").exists())
        fresh = self.root / "fresh.aya"
        p = self.cli("pack", source, fresh, "--game", self.game)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(mods.unpack(fresh.read_bytes()), b"test")

    def test_same_device_alternate_mount_identity_is_refused(self):
        fd = os.open(self.user, os.O_RDONLY | os.O_DIRECTORY)
        try:
            st = os.fstat(fd)
            # Simulate an alias of the selected game's filesystem mounted elsewhere.
            guard = (st.st_dev, st.st_ino + 1, mods._mount_id(fd) + 1)
            with self.assertRaisesRegex(ValueError, "alternate mount"):
                mods._check_output_directory(fd, guard, final=True)
        finally:
            os.close(fd)

    def test_source_links_fifos_and_oversize_are_refused_without_creating_output(self):
        target = self.root / "target"
        target.write_bytes(b"unchanged")
        link = self.root / "source-link"
        link.symlink_to(target)
        fifo = self.root / "fifo"
        os.mkfifo(fifo)
        large = self.root / "large"
        with large.open("wb") as f:
            f.truncate((64 << 20) + 1)
        for source in (link, fifo, large):
            out = self.root / (source.name + ".aya")
            p = self.cli("pack", source, out, "--game", self.game)
            self.assertNotEqual(p.returncode, 0)
            self.assertFalse(out.exists())
        self.assertEqual(target.read_bytes(), b"unchanged")

    def test_unpacked_data_and_stream_bounds(self):
        self.assertEqual(mods.unpack(mods.pack(b"ok")), b"ok")
        with self.assertRaisesRegex(ValueError, "empty packed file"):
            mods.unpack(b"")
        bomb = zlib.compress(bytes(16 << 20), 9)
        tracemalloc.start()
        with self.assertRaisesRegex(ValueError, "over 1 MiB"):
            mods.unpack(struct.pack("<I", len(bomb)) + bomb)
        _, peak = tracemalloc.get_traced_memory()
        tracemalloc.stop()
        self.assertLess(peak, 8 << 20)
        z = zlib.compress(b"ok") + b"trailing"
        with self.assertRaisesRegex(ValueError, "trailing compressed"):
            mods.unpack(struct.pack("<I", len(z)) + z)
        first = zlib.compress(b"short")
        second = zlib.compress(b"last")
        with self.assertRaisesRegex(ValueError, "short block"):
            mods.unpack(struct.pack("<I", len(first)) + first + struct.pack("<I", len(second)) + second)
        packed = mods.pack(b"12345")
        old = mods.MAX_UNPACKED
        try:
            mods.MAX_UNPACKED = 4
            with self.assertRaisesRegex(ValueError, "over 64 MiB"):
                mods.unpack(packed)
        finally:
            mods.MAX_UNPACKED = old

    def test_dds_dimensions_mips_and_payload_are_bounded_before_preview(self):
        image = bytearray(dds())
        self.assertEqual(mods.decode_top(image)[:2], (8, 8))
        small = bytearray(image[:144])
        struct.pack_into("<II", small, 12, 1, 1)
        self.assertEqual(len(mods.decode_top(small)[2]), 4)
        with self.assertRaisesRegex(ValueError, "payload size"):
            mods.decode_top(image[:-1])
        excessive_mips = bytearray(image)
        struct.pack_into("<I", excessive_mips, 28, 99)
        with self.assertRaisesRegex(ValueError, "mip"):
            mods.decode_top(excessive_mips)
        bad = self.root / "huge.dds"
        header = bytearray(image[:128])
        struct.pack_into("<II", header, 12, 20000, 20000)
        bad.write_bytes(header)
        code = ("import sys; sys.path.insert(0,sys.argv[1]); import mods; "
                "mods.decode_top(open(sys.argv[2],'rb').read())")
        def memory_limit():
            resource.setrlimit(resource.RLIMIT_AS, (128 << 20, 128 << 20))
        p = subprocess.run([sys.executable, "-B", "-c", code, str(TOOL.parent), str(bad)],
                           capture_output=True, text=True, timeout=15, preexec_fn=memory_limit)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("DDS dimensions", p.stderr)
        self.assertNotIn("MemoryError", p.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
