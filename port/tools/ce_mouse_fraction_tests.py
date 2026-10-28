#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Source-coupled fractional relative-mouse tests, using the existing input fixture and bounded API fakes.

These checks do not establish whether any real mouse or platform actually emits fractional deltas.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "port/tools/input_lifetime_tests.py"
MUTANTS = (
    ("drop-remainder", "mouse-fraction", "double value = (double)delta + (double)remainder;",
     "double value = (double)delta;"),
    ("retain-on-focus", "mouse-fraction-focus",
     "if (!focused || focusVersion != ceMouseFocusVersion)\n\t\t\t\t{\n\t\t\t\t\tceMouseRemainderX = ceMouseRemainderY = 0;",
     "if (!focused || focusVersion != ceMouseFocusVersion)\n\t\t\t\t{\n\t\t\t\t\t/* retained across focus */"),
    ("retain-pending-motion", "mouse-fraction-pending-focus", "dx = dy = 0;", "/* stale SDL motion retained */"),
    ("fence-removed", "mouse-fraction-tape", "if (OrdinaryCeInput())\n\t\t\t{", "if (true)\n\t\t\t{"),
    ("invalid-accepted", "mouse-fraction-invalid", "if (!std::isfinite(value) || value < (double)(std::numeric_limits<LONG>::min)() ||\n\t\tvalue > (double)(std::numeric_limits<LONG>::max)())", "if (false)"),
)


class FractionalMouseTests(unittest.TestCase):
    def test_each_causal_mutation_is_detected(self):
        parent = ROOT / "local-data/port/mouse-fraction-mutants"
        parent.mkdir(parents=True, exist_ok=True)
        for name, case, old, new in MUTANTS:
            with self.subTest(mutant=name):
                root = Path(tempfile.mkdtemp(prefix=name + "-", dir=parent))
                (root / "src").symlink_to(ROOT / "src")
                (root / "port").mkdir()
                (root / "port/patches").symlink_to(ROOT / "port/patches")
                (root / "port/platform").mkdir()
                shutil.copytree(ROOT / "port/platform/common", root / "port/platform/common")
                target = root / "port/platform/common/dinput.cpp"
                text = target.read_text()
                self.assertEqual(text.count(old), 1, name)
                target.write_text(text.replace(old, new, 1))
                result = subprocess.run([sys.executable, str(HARNESS), "--source-root", str(root), "--case", case,
                                         "--optimize"], capture_output=True, text=True, timeout=600)
                self.assertNotEqual(result.returncode, 0, "mutant survived: " + name + "\n" + result.stdout)
                self.assertIn("FAIL " + case, result.stdout, "mutant did not reach its case: " + name + "\n" + result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
