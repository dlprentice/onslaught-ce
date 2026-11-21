#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Looking writes nothing: the check that a tour of the Extras pages leaves the player's files as they were.

A tour is a run that opens Extras and visits every page (the Service Record's careers, one career's record, its missions and Goodies, the
Music Player). Before it, the run's writable folder holds the files its start seed put there; after it, `check` requires every one of them
byte-identical (careers, options, ce.ini), no file gone, and no file new except the ones the game itself writes at every start (the allowed
list below). The check is itself tested with a negative control: a changed byte, a removed file, an added career, a changed option, a
changed ce.ini line and an empty file must each be reported, so a tour that passed means something.

    python3 port/tools/ce_extras_tour_tests.py                          the check's own tests
    python3 port/tools/ce_extras_tour_tests.py --control RUNDIR/user --user RUNDIR/user    a tour's folder against a control run's (the same start seed,
                                                                         nothing visited): the game rewrites ce.ini and writes setuphistory.txt at every
                                                                         start, so what a tour must equal is a run that only started
No game, display, sound or network is used. The registered part is the check and its negative controls; the --control comparison of a real
tour's folder is an evidence run (it needs a game run), not a registered test. What a tour cannot show, ce_career_cross_tests.py does:
the page's cross-check against the game's own reading, compiled and mutated on stand-ins.
"""
import hashlib
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ALLOWED_NEW = {"setuphistory.txt"}      # written by the game at every start, with no tour at all


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(folder):
    folder = Path(folder)
    return {p.relative_to(folder).as_posix(): digest(p) for p in sorted(folder.rglob("*")) if p.is_file()}


def check(before, after):
    """What differs between two snapshots ({name: sha256}), as a list of sentences; empty when the tour changed nothing."""
    found = []
    for name, sha in before.items():
        if name not in after:
            found.append(f"{name} is gone")
        elif after[name] != sha:
            found.append(f"{name} changed")
    for name in after:
        if name not in before and name not in ALLOWED_NEW:
            found.append(f"{name} is new")
    return found


class CheckTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="ce-tour-"))
        (self.tmp / "savegames").mkdir()
        (self.tmp / "ce.ini").write_text("[Experimental]\nextras=1\n")
        (self.tmp / "savegames/A.bes").write_bytes(b"\x01" * 10004)
        (self.tmp / "savegames/B.bes").write_bytes(b"\x02" * 10004)
        (self.tmp / "defaultoptions.bea").write_bytes(b"\x07" * 5000)
        self.before = snapshot(self.tmp)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_an_untouched_folder_passes(self):
        self.assertEqual(check(self.before, snapshot(self.tmp)), [])

    def test_the_game_own_start_file_is_allowed(self):
        (self.tmp / "setuphistory.txt").write_text("x")
        self.assertEqual(check(self.before, snapshot(self.tmp)), [])

    def test_every_kind_of_change_is_reported(self):
        def after(mutate):
            copy = Path(tempfile.mkdtemp(prefix="ce-tour-copy-"))
            try:
                shutil.copytree(self.tmp, copy, dirs_exist_ok=True)
                mutate(copy)
                return check(self.before, snapshot(copy))
            finally:
                shutil.rmtree(copy, ignore_errors=True)
        cases = {
            "a changed byte": lambda d: (d / "savegames/A.bes").write_bytes(b"\x01" * 10003 + b"\x00"),
            "a removed career": lambda d: (d / "savegames/B.bes").unlink(),
            "an added career": lambda d: (d / "savegames/C.bes").write_bytes(b"\x03" * 10004),
            "a changed option": lambda d: (d / "defaultoptions.bea").write_bytes(b"\x07" * 4999 + b"\x08"),
            "a changed ce.ini line": lambda d: (d / "ce.ini").write_text("[Experimental]\nextras=0\n"),
            "an empty new file": lambda d: (d / "savegames/D.bes").write_bytes(b""),
            "a new file beside the allowed one": lambda d: (d / "setuphistory.txt.bak").write_text("x"),
        }
        for name, mutate in cases.items():
            self.assertTrue(after(mutate), f"{name} went unnoticed")


def main():
    if "--control" in sys.argv:
        control, user = sys.argv[sys.argv.index("--control") + 1], sys.argv[sys.argv.index("--user") + 1]
        found = check(snapshot(control), snapshot(user))
        print("\n".join(found) if found else "the tour changed nothing")
        return 1 if found else 0
    unittest.main(verbosity=2)


if __name__ == "__main__":
    sys.exit(main())
