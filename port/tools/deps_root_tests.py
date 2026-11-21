#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A build reads its dependencies from one root and says which: no silent fallback to another cache.

    python3 -B port/tools/deps_root_tests.py

- A root without the optional ICE build stops the build (ice_deps.require), naming the missing receipt; nothing else is tried.
- build.write_deps_record records the root it read (deps.DATA, resolved) and the path and SHA-256 of each library receipt
  (SDL 3, and libjuice in an ICE build), and stops when a receipt is missing or lies outside that root (a link into another
  cache included). No game data, network or compiler is used: the receipts are small files in a scratch root."""
import hashlib
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import build  # noqa: E402
import deps  # noqa: E402
import ice_deps  # noqa: E402


class DepsRootTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.root, self.other, self.out = base / "root", base / "other", base / "build"
        for d in (self.root, self.other, self.out / "linux"):
            d.mkdir(parents=True)
        self.patches = [mock.patch.object(deps, "DATA", self.root), mock.patch.object(deps, "PREFIX", self.root / "prefix"),
                        mock.patch.object(build, "BUILD", self.out), mock.patch.object(build, "WITH_ICE", True)]
        for p in self.patches:
            p.start()

    def tearDown(self):
        for p in self.patches:
            p.stop()
        self.tmp.cleanup()

    def put(self, path, text="{}\n"):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def test_a_root_without_the_ice_build_stops_the_build(self):
        with self.assertRaises(SystemExit) as stop:
            ice_deps.require("linux")
        self.assertIn("optional ICE receipt missing", str(stop.exception))
        self.assertIn(str(self.root), str(stop.exception))

    def test_the_record_names_the_root_and_each_receipt(self):
        sdl = self.put(deps.sdl_prefix("linux") / ".bea-sdl3-build.json", '{"sdl": 1}\n')
        ice = self.put(ice_deps.prefix("linux") / ".bea-ice-build.json", '{"ice": 1}\n')
        build.write_deps_record("linux")
        record = json.loads((self.out / "linux/.bea-deps.private.json").read_text())
        self.assertEqual(str(self.root.resolve()), record["dependency_root"])
        self.assertIsNone(record["fallback"])
        self.assertEqual(hashlib.sha256(sdl.read_bytes()).hexdigest(), record["libraries"]["sdl3"]["sha256"])
        self.assertEqual(str(ice.resolve()), record["libraries"]["libjuice"]["receipt"])

    def test_a_missing_receipt_stops_instead_of_using_another_root(self):
        self.put(self.other / "prefix/.bea-sdl3-build.json")          # present in another cache only
        with self.assertRaises(SystemExit) as stop:
            build.write_deps_record("linux")
        self.assertIn("missing from the dependency root", str(stop.exception))
        self.assertFalse((self.out / "linux/.bea-deps.private.json").exists())

    def test_a_receipt_linked_into_another_root_is_refused(self):
        real = self.put(self.other / "prefix/.bea-sdl3-build.json")
        link = deps.sdl_prefix("linux") / ".bea-sdl3-build.json"
        link.parent.mkdir(parents=True, exist_ok=True)
        os.symlink(real, link)
        self.put(ice_deps.prefix("linux") / ".bea-ice-build.json")
        with self.assertRaises(SystemExit) as stop:
            build.write_deps_record("linux")
        self.assertIn("outside the dependency root", str(stop.exception))
        self.assertFalse((self.out / "linux/.bea-deps.private.json").exists())


if __name__ == "__main__":
    unittest.main(verbosity=1)
