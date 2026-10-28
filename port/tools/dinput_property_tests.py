#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The DirectInput adapter's device properties (port/platform/common/dinput.cpp PropId): input_lifetime_tests' case
pad-properties, built at -O2 as the game is, must pass; a mutant with the old comparison (&prop == &DIPROP_RANGE,
which clang folds to false because MAKEDIPROP(n) is a reference made from an integer) must fail. Before the fix the
Controller Options page listed every pad as "Not present".

    python3 port/tools/dinput_property_tests.py
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "port/tools/input_lifetime_tests.py"
MUTATIONS = (("PropId(prop) == 13 || PropId(prop) == 14", "&prop == &DIPROP_INSTANCENAME || &prop == &DIPROP_PRODUCTNAME"),
             ("PropId(prop) == 4", "&prop == &DIPROP_RANGE"))


def harness(source_root):
    return subprocess.run([sys.executable, str(HARNESS), "--source-root", str(source_root), "--case", "pad-properties",
                           "--optimize"], capture_output=True, text=True, timeout=600)


class PropertyTests(unittest.TestCase):
    def test_fixed_adapter_passes_optimized(self):
        ran = harness(ROOT)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("PASS pad-properties", ran.stdout)

    def test_mutant_with_the_old_comparison_fails(self):
        with tempfile.TemporaryDirectory(prefix="dinput-property-mutant-", dir=ROOT / "local-data") as d:
            root = Path(d)
            (root / "port/platform").mkdir(parents=True)
            (root / "src").symlink_to(ROOT / "src")
            (root / "port/patches").symlink_to(ROOT / "port/patches")
            shutil.copytree(ROOT / "port/platform/common", root / "port/platform/common")
            dinput = root / "port/platform/common/dinput.cpp"
            text = dinput.read_text()
            for old, new in MUTATIONS:
                self.assertIn(old, text)
                text = text.replace(old, new)
            dinput.write_text(text)
            ran = harness(root)
        self.assertNotEqual(ran.returncode, 0, "mutant survived: " + ran.stdout)
        self.assertIn("FAIL pad-properties", ran.stdout)


if __name__ == "__main__":
    unittest.main()
