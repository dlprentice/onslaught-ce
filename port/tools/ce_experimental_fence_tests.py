#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A faithful run never writes the [Experimental] lines into ce.ini, and what a forced CE layer does there.

    python3 -I port/tools/ce_experimental_fence_tests.py [-v]

The first CE main menu adds the [Experimental] block to ce.ini (PortCeMenus in port/game/ce_frontend.cpp calls
PortCeExperimentalBlock in port/platform/common/ce.cpp). PortCeMenus is fenced by PortCeLayer, which is off under a virtual
clock, a tape, probes or a recording, so a faithful run never reaches the writer. This suite builds the actual PortCeMenus
(extracted whole) and the actual ce.cpp with port/tools/ce_experimental_fence_driver.cpp and starts it in scratch user
folders with each of those variables set:

- the faithful case: with BEA_PORT_CLOCK, BEA_PORT_TAPE, BEA_PORT_PROBES or BEA_PORT_RECORD set (each alone and all four),
  the menus are off, an existing ce.ini keeps its bytes and its modification time, no file is added to the folder, and a
  user folder that does not exist is not created;
- the ordinary case, for contrast: the menus are on and ce.ini gains the block;
- BEA_PORT_CE_LAYER=0 alone, -autoconfigtest and the playable demo: off, nothing written; beside a tape, a value of the
  switch other than 1 (0, empty, 2) forces nothing: off, nothing written;
- the forced case, BEA_PORT_CE_LAYER=1 beside a tape (the switch for screenshots of the CE layer during a replay; not a
  faithful run and not a player's): PortCeLayer answers yes before it looks at the tape, so the menus are on and THE BLOCK IS
  WRITTEN into that run's ce.ini. Two readings of the release disagreed on this; it is recorded here as FORCED_WRITES. The
  display mode code refuses to touch ce.ini in the same case (port/platform/common/display_mode.cpp). When the writer is
  fenced for forced captures, set FORCED_WRITES to False: the suite then requires the file to stay as it was.

Mutants of the two sources: the layer ignoring one fence variable, and the menus ignoring the layer. Each must fail the
faithful case. One more, the switch taking any value but 0 as a force, must write beside a tape with the switch at 2. No game, display or device is used; whether a real replay's process environment reaches this code as the
driver's does is not shown here.
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ce_setting_text_tests import SDL_STUB  # noqa: E402  (the authored SDL header ce.cpp's includes need)

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
CE_CPP = PORT / "platform/common/ce.cpp"
CE_REPORT = PORT / "updater/ce_report.h"
FRONTEND = PORT / "game/ce_frontend.cpp"
DRIVER = PORT / "tools/ce_experimental_fence_driver.cpp"

FENCES = ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD")
HEADER = b"[Experimental]"
BEFORE = b"hud_scale=1\ncommander=1\n"
# What a forced CE layer beside a fence variable does to ce.ini today: it writes the block (see the docstring).
FORCED_WRITES = True


def menus_function(text):
    """PortCeMenus, whole: from its head to its closing brace."""
    m = re.search(r"(?m)^bool\tPortCeMenus\(\)\n\{\n", text)
    if not m or len(re.findall(r"(?m)^bool\tPortCeMenus\(\)\n\{", text)) != 1:
        raise AssertionError("ce_frontend.cpp: PortCeMenus was not found once")
    end = text.index("\n}\n", m.end()) + 3
    body = text[m.start():end]
    if body.count("{") != body.count("}") or "return on == 1;" not in body or "PortCeExperimentalBlock();" not in body:
        raise AssertionError("ce_frontend.cpp: PortCeMenus is not the function this suite reads")
    return body


def build(folder, ce_text, frontend_text):
    include = '#include "../../updater/ce_report.h"'
    if ce_text.count(include) != 1:
        raise AssertionError("ce.cpp no longer includes ce_report.h as the test expects")
    (folder / "ce.cpp").write_text(ce_text.replace(include, '#include "ce_report.h"'))
    (folder / "ce_report.h").write_text(CE_REPORT.read_text())
    (folder / "port_ce_menus.inc").write_text(menus_function(frontend_text))
    (folder / "SDL3").mkdir(exist_ok=True)
    (folder / "SDL3/SDL.h").write_text(SDL_STUB)
    binary = folder / "driver"
    done = subprocess.run(["g++", "-std=c++17", "-O0", "-g", "-w", '-DCE_VERSION="test"', "-I", str(folder), "-I", str(PORT / "platform/common"),
                           str(DRIVER), str(folder / "ce.cpp"), "-o", str(binary)], capture_output=True, text=True, timeout=180)
    if done.returncode:
        raise AssertionError("the driver did not compile:\n" + (done.stdout + done.stderr)[-3000:])
    return binary


def start(binary, base, name, variables, switch=None, existing=True):
    """One process. Returns (menus answer, the folder's state after: None when it does not exist, else {file: bytes}, whether
    ce.ini's bytes and modification time are as they were)."""
    folder = base / name
    stamp = None
    if existing:
        folder.mkdir()
        (folder / "ce.ini").write_bytes(BEFORE)
        os.utime(folder / "ce.ini", ns=(1_600_000_000_000_000_000, 1_600_000_000_000_000_000))
        stamp = (folder / "ce.ini").stat().st_mtime_ns
    env = {"PATH": os.environ.get("PATH", "/usr/bin"), "HOME": "/nonexistent"}
    env.update(variables)
    done = subprocess.run([str(binary), str(folder)] + ([switch] if switch else []), capture_output=True, text=True, timeout=20, env=env)
    if done.returncode:
        raise AssertionError(f"{name}: the driver ended with {done.returncode}: {done.stderr[-500:]}")
    answer = done.stdout.strip()
    if not folder.exists():
        return answer, None, True
    files = {p.name: p.read_bytes() for p in sorted(folder.iterdir())}
    same = existing and files.get("ce.ini") == BEFORE and (folder / "ce.ini").stat().st_mtime_ns == stamp
    return answer, files, same


def faithful_problems(binary, base, tag=""):
    """What a faithful run did that it must not: one entry per failed expectation."""
    problems = []
    cases = [(name.lower(), {name: "1"}) for name in FENCES] + [("all-four", {name: "1" for name in FENCES}),
                                                                ("a-tape-file", {"BEA_PORT_TAPE": "/tmp/some.tape"})]
    for label, variables in cases:
        answer, files, same = start(binary, base, f"{tag}faithful-{label}", variables)
        if answer != "menus 0":
            problems.append(f"{label}: the CE menus are on")
        if not same or sorted(files) != ["ce.ini"]:
            problems.append(f"{label}: ce.ini or its folder changed")
        answer, files, _ = start(binary, base, f"{tag}faithful-{label}-no-folder", variables, existing=False)
        if answer != "menus 0" or files is not None:
            problems.append(f"{label}: a user folder that did not exist was created or the menus are on")
    return problems


# name: (file, text that occurs once, its replacement)
MUTANTS = {
    "the layer ignores a tape": ("ce", '{"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"};\n\t\ton = 1;',
                                 '{"BEA_PORT_CLOCK", "BEA_PORT_CLOCK", "BEA_PORT_PROBES", "BEA_PORT_RECORD"};\n\t\ton = 1;'),
    "the layer ignores a recording": ("ce", '{"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"};\n\t\ton = 1;',
                                      '{"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_PROBES"};\n\t\ton = 1;'),
    "the layer is on whatever is set": ("ce", "\t\t\tif (v && *v)\n\t\t\t\ton = 0;", "\t\t\tif (v && *v)\n\t\t\t\ton = 1;"),
    "the menus do not ask the layer": ("frontend", "on = (PortCeLayer() && !CLIPARAMS.mAutoConfigTest && !PLAYABLE_DEMO) ? 1 : 0;",
                                       "on = (!CLIPARAMS.mAutoConfigTest && !PLAYABLE_DEMO) ? 1 : 0;"),
}


class ExperimentalFenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="ce-experimental-fence-")
        cls.base = Path(cls.tmp.name)
        cls.ce, cls.frontend = CE_CPP.read_text(), FRONTEND.read_text()
        (cls.base / "build").mkdir()
        cls.binary = build(cls.base / "build", cls.ce, cls.frontend)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_a_faithful_run_leaves_ce_ini_and_its_folder_alone(self):
        self.assertEqual([], faithful_problems(self.binary, self.base))

    def test_an_ordinary_start_has_the_menus_and_adds_the_block_once(self):
        answer, files, same = start(self.binary, self.base, "ordinary", {})
        self.assertEqual("menus 1", answer)
        self.assertFalse(same)
        self.assertTrue(files["ce.ini"].startswith(BEFORE))
        self.assertEqual(1, files["ce.ini"].count(HEADER))
        self.assertEqual(1, files["ce.ini"].count(b"commander="), "the player's own line is not repeated")
        self.assertIn(b"map_editor=1\n", files["ce.ini"])

    def test_the_layer_switched_off_the_autoconfig_test_and_the_demo_write_nothing(self):
        for name, variables, switch in (("layer-0", {"BEA_PORT_CE_LAYER": "0"}, None), ("autoconfig", {}, "autoconfig"), ("demo", {}, "demo"),
                                        ("layer-0-and-tape", {"BEA_PORT_CE_LAYER": "0", "BEA_PORT_TAPE": "1"}, None),
                                        ("layer-empty-and-tape", {"BEA_PORT_CE_LAYER": "", "BEA_PORT_TAPE": "1"}, None),
                                        ("layer-2-and-tape", {"BEA_PORT_CE_LAYER": "2", "BEA_PORT_TAPE": "1"}, None)):
            with self.subTest(name):
                answer, files, same = start(self.binary, self.base, name, variables, switch)
                self.assertEqual("menus 0", answer)
                self.assertTrue(same)
                self.assertEqual(["ce.ini"], sorted(files))

    def test_what_a_forced_layer_beside_a_tape_does(self):
        for fence in FENCES:
            with self.subTest(fence):
                answer, files, same = start(self.binary, self.base, "forced-" + fence.lower(), {"BEA_PORT_CE_LAYER": "1", fence: "1"})
                self.assertEqual("menus 1", answer, "the force switch turns the CE layer on beside a fence variable")
                wrote = HEADER in files["ce.ini"]
                self.assertEqual(FORCED_WRITES, wrote, "the forced case no longer does what FORCED_WRITES records: update it and the docstring")
                self.assertEqual(not FORCED_WRITES, same)

    def test_only_a_1_forces_the_layer(self):
        old, new = "return (on = force[0] == '1');", "return (on = force[0] != '0');"
        self.assertEqual(1, self.ce.count(old), "the force switch's test must occur once")
        folder = self.base / "mutant-any-force"
        folder.mkdir()
        binary = build(folder, self.ce.replace(old, new), self.frontend)
        answer, files, same = start(binary, self.base, "mutant-any-force-layer-2-and-tape", {"BEA_PORT_CE_LAYER": "2", "BEA_PORT_TAPE": "1"})
        self.assertEqual("menus 1", answer, "the mutant must force the layer with the switch at 2")
        self.assertFalse(same, "the mutant must write ce.ini, or the case with the switch at 2 tests nothing")

    def test_each_mutant_fails_the_faithful_case(self):
        survivors = []
        for number, (name, (where, old, new)) in enumerate(MUTANTS.items()):
            ce, frontend = self.ce, self.frontend
            if where == "ce":
                self.assertEqual(1, ce.count(old), f"{name}: the anchor must occur once")
                ce = ce.replace(old, new)
            else:
                self.assertEqual(1, frontend.count(old), f"{name}: the anchor must occur once")
                frontend = frontend.replace(old, new)
            folder = self.base / f"mutant-{number}"
            folder.mkdir()
            binary = build(folder, ce, frontend)
            if not faithful_problems(binary, self.base, f"mutant-{number}-"):
                survivors.append(name)
        self.assertEqual([], survivors)


if __name__ == "__main__":
    unittest.main(verbosity=2 if "-v" in sys.argv else 1, argv=[a for a in sys.argv if a != "-v"])
