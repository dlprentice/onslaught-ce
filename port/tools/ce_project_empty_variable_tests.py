#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""BEA_CE_PROJECT set to an empty string: what the project-launch prototype's switch does to an ordinary start.

    python3 -I port/tools/ce_project_empty_variable_tests.py [-v]

BEA_CE_PROJECT is the developer switch of the project-launch prototype (port/PROJECTS.md): it names a .ceproject file, the
game then skips the front end, loads that project's level and reads or writes no career. port/game/ce_project.cpp asks
only whether the variable EXISTS (PortCeProjectRequested), so an empty value counts as a request. Everywhere else in the
port an empty variable is an unset one. This suite builds the actual ce_project.cpp with
port/tools/ce_project_empty_variable_driver.cpp and asks it what each caller asks, with the variable unset, empty and set:

- unset: no request, careers allowed, the preflight passes without looking, no launch;
- any value that is not empty: a request, careers refused (the prototype's design, covered by ce_project_tests.py). That holds
  for an absolute path, for a relative one and for a value of one space alike: the launch then refuses what it was given
  ("Select an absolute .ceproject file path") and the run ends with status 1, so a mistyped value is reported, never taken
  for an ordinary start. Only the empty string is in question;
- empty: recorded as EMPTY_IS_A_REQUEST. Today it IS a request: every career create, read, write and delete is refused,
  the map editor and museum visit are refused, the start-up preflight refuses a start that has no explicit
  BEA_GAME_DIR and BEA_USER_DIR, and with them the launch finds no project, so the run ends with exit status 1
  before any menu. A reading of release 0.1.14 found this by eye; it is a developer variable and no ce.ini line, so
  a player meets it only with that variable in their environment.

When PortCeProjectRequested treats an empty value as unset (`value && *value`, as the other switches do), set
EMPTY_IS_A_REQUEST to False: the suite then requires the empty case to answer exactly as the unset one. A mutant of the
source with that one change is built and must answer so, so that the flip is known to be the whole change; it must also
keep every non-empty value a request (a gate that let only an absolute path through would quietly turn a relative path
into an ordinary start).

No game is run: that a real start with the empty variable ends as the driver's answers say is not shown here.
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OWNER = ROOT / "port/game/ce_project.cpp"
DRIVER = ROOT / "port/tools/ce_project_empty_variable_driver.cpp"

# What an empty BEA_CE_PROJECT is today (see the docstring).
EMPTY_IS_A_REQUEST = False

UNSET = ["requested 0", "career allowed 1", "authoring allowed 1", "preflight without explicit folders 1", "preflight with explicit folders 1",
         "launch level -1", "exit status 0"]
REQUEST = ["requested 1", "career allowed 0", "authoring allowed 0", "preflight without explicit folders 0", "preflight with explicit folders 1",
           "launch level -1", "exit status 1"]
# Values that are not empty, with what the driver prints for each: every one is a request, before the gate and after it.
NOT_EMPTY = (("an absolute path", None), ("a relative path", "none.ceproject"), ("one space", " "))
GATE = ('static const bool selected = std::getenv("BEA_CE_PROJECT") != nullptr;',
        'static const char* const named = std::getenv("BEA_CE_PROJECT"); static const bool selected = named != nullptr && *named != 0;')


def build(folder, owner):
    binary = Path(folder) / "driver"
    done = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-O0", "-w", "-I" + str(ROOT / "port/game"), str(DRIVER), str(owner), "-o", str(binary)],
                          capture_output=True, text=True, timeout=300)
    if done.returncode:
        raise AssertionError("the driver did not compile:\n" + done.stderr[-3000:])
    return binary


def ask(binary, folders, value):
    env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "CE_"))}
    if value is not None:
        env["BEA_CE_PROJECT"] = value
    done = subprocess.run([str(binary), *map(str, folders)], capture_output=True, text=True, timeout=30, env=env)
    if done.returncode:
        raise AssertionError(f"the driver ended with {done.returncode}: {done.stderr[-500:]}")
    lines = done.stdout.splitlines()
    return lines[0], lines[1:]


class EmptyProjectVariableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="ce-project-empty-")
        cls.folder = Path(cls.tmp.name)
        cls.user = (cls.folder / "game", cls.folder / "user")       # the driver's two folders
        for folder in cls.user:
            folder.mkdir(mode=0o700)
        cls.binary = build(cls.folder, OWNER)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_unset_is_an_ordinary_start(self):
        self.assertEqual(("variable unset", UNSET), ask(self.binary, self.user, None))

    def not_empty(self):
        return [(name, str(self.folder / "none.ceproject") if value is None else value) for name, value in NOT_EMPTY]

    def test_a_value_is_a_request_and_fences_the_career(self):
        # No such file: the request stands, the launch refuses it, the run ends with status 1.
        for name, value in self.not_empty():
            with self.subTest(name):
                self.assertEqual(("variable set", REQUEST), ask(self.binary, self.user, value))

    def test_what_an_empty_value_is(self):
        state, answers = ask(self.binary, self.user, "")
        self.assertEqual("variable empty", state)
        self.assertEqual(REQUEST if EMPTY_IS_A_REQUEST else UNSET, answers,
                         "an empty BEA_CE_PROJECT no longer does what EMPTY_IS_A_REQUEST records: update it and the docstring")

    def test_the_one_line_gate_makes_empty_an_ordinary_start_and_keeps_a_path_a_request(self):
        text = OWNER.read_text()
        if text.count(GATE[0]) != 1:
            self.assertFalse(EMPTY_IS_A_REQUEST, "PortCeProjectRequested is no longer the line this suite mutates: is the gate in?")
            return
        mutant = self.folder / "gated"
        mutant.mkdir()
        (mutant / "ce_project.cpp").write_text(text.replace(GATE[0], GATE[1]))
        binary = build(mutant, mutant / "ce_project.cpp")
        self.assertEqual(("variable empty", UNSET), ask(binary, self.user, ""))
        self.assertEqual(("variable unset", UNSET), ask(binary, self.user, None))
        for name, value in self.not_empty():
            with self.subTest(name):
                self.assertEqual(("variable set", REQUEST), ask(binary, self.user, value))


if __name__ == "__main__":
    unittest.main(verbosity=2 if "-v" in sys.argv else 1, argv=[a for a in sys.argv if a != "-v"])
