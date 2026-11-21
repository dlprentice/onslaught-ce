#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Service Record's check against the game's own reading of a career (port/game/ce_career_view.cpp).

Before a record is shown, the page reads the career a second way: the game's own accessors on a scratch copy of its career structure
(Goodie count, kill counts, each Goodie's state, each mission's number, completion, state and rank letter). Any difference means the
career is shown as one that could not be read. ce_career_view_tests.py checks the record against a second reader written from the
Toolkit's layout; this file checks the cross-check itself, which a test of the record cannot reach: ce_career_view.cpp is compiled
here as the game compiles it, on stand-ins for the game's career and memory card (tests/career_fake/) whose answers are the bytes' and
whose gSkew makes one answer wrong on purpose.

- A good career, in its real and in-memory variants, is read, and a record with values the game never writes is refused.
- Each kind of skewed answer refuses the career and the log names the check that found it.
- Mutants of ce_career_view.cpp (the cross-check inverted, removed, or one comparison turned round) change the result of that driver and
  are caught.

What this does not show: that the real game's accessors agree with the Toolkit's layout. That rests on the evidence run (the tour in the
Extras report) where the real game read the real careers and wrote no "disagrees" line, not on a registered check.
No game, display, sound or network is used.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ce_career_view_tests as views  # noqa: E402

ROOT = views.ROOT
GAME = ROOT / "port/game"
DRIVER = ROOT / "port/tools/tests/ce_career_cross_driver.cpp"
FAKES = ROOT / "port/tools/tests/career_fake"
SOURCE = GAME / "ce_career_view.cpp"
SKEWS = {1: "the Goodie count", 2: "a kill count", 3: "a Goodie's state", 4: "a mission's number", 5: "a mission's state",
         6: "a mission's rank", 7: "the number of missions", 8: "a mission's state",
         9: "the number of missions", 10: "stored volume", 11: "stored volume",
         12: "stored invincibility", 13: "stored invincibility"}


def build(out, game_dir, werror=True):
    """Compiles the driver with ce_career_view.cpp from game_dir (port/game or a mutated copy beside the port/platform/common headers)."""
    done = subprocess.run(["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", *(["-Werror"] if werror else []), "-DCE_VERSION=1", f"-I{FAKES}",
                           f"-I{game_dir}", str(DRIVER), str(game_dir / "ce_career_view.cpp"), "-o", str(out)],
                          capture_output=True, text=True, timeout=180)
    if done.returncode:
        raise RuntimeError("the driver did not compile:\n" + done.stdout + done.stderr)
    return out


def read(binary, path, skew=0):
    done = subprocess.run([str(binary), str(path), str(skew)], capture_output=True, text=True, timeout=20)
    if done.returncode:
        raise AssertionError(done.stderr or f"driver exit {done.returncode}")
    return done.stdout.strip(), done.stderr.strip()


class CrossCheckTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-cross-"))
        cls.binary = build(cls.tmp / "driver", GAME)
        careers = []
        game = os.environ.get("BEA_GAME_DIR")
        if game:
            careers += sorted(Path(game).glob("savegames/*.bes"))
        toolkit = views.toolkit_career()
        if toolkit:
            careers.append(toolkit)
        if not careers:
            raise AssertionError("no real career to read: set BEA_GAME_DIR to a game folder with savegames, or keep the Toolkit checkout beside this one")
        cls.career = careers[-1].read_bytes()
        cls.variants = views.CareerViewTests.variants(None, cls.career)
        for name, data in cls.variants.items():
            (cls.tmp / (name.replace(" ", "-") + ".bes")).write_bytes(data)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_the_probed_career_carries_ranks(self):
        # the rank comparison is reached only by a career with a won mission that has a plain rank (0 to 1); a variant without one would
        # make the rank skew and the rank mutant pass or fail for the wrong reason, so the premise is checked first
        data = self.variants["part-way"]
        won = [n for n in range(100) if views.u32(data, views.MISSIONS + n * views.MISSION + 0x10) and views.u32(data, views.MISSIONS + n * views.MISSION + 0x04)]
        ranked = [n for n in won if 0.0 <= struct.unpack_from("<f", data, views.MISSIONS + n * views.MISSION + 0x3C)[0] <= 1.0]
        self.assertTrue(ranked, "the part-way career has no won mission with a plain rank: the rank comparison is never reached")

    def file(self, name):
        return self.tmp / (name.replace(" ", "-") + ".bes")

    def test_good_careers_are_read(self):
        for name in ("as written", "part-way", "new campaign", "every rank", "alternate routes only", "packed kills"):
            out, err = read(self.binary, self.file(name))
            self.assertTrue(out.startswith("read 1"), f"{name}: {out} {err}")
            self.assertNotIn("disagrees", err, name)

    def test_values_the_game_never_writes_are_not_shown(self):
        out, err = read(self.binary, self.file("every Goodie state"))
        self.assertEqual(out, "read 0")
        self.assertIn("values the game never writes", err)
        for name in ("one byte short", "empty", "another version"):
            self.assertEqual(read(self.binary, self.file(name))[0], "read 0", name)

    def test_each_kind_of_disagreement_refuses_the_career(self):
        for skew, what in SKEWS.items():
            out, err = read(self.binary, self.file("part-way"), skew)
            self.assertEqual(out, "read 0", f"skew {skew} ({what}) was not noticed")
            self.assertIn(f"({what})", err, f"skew {skew}")

    def test_mutants_are_caught(self):
        source = SOURCE.read_text()
        mutants = (
            ("if (const char *what = Disagreement(bytes, *view))", "if (const char *what = (Disagreement(bytes, *view) ? NULL : \"x\"))", "the cross-check inverted"),
            ("if (const char *what = Disagreement(bytes, *view))", "if (const char *what = NULL)", "the cross-check removed"),
            ("if (sScratch->CountGoodies() != view.mGoodiesAll)", "if (sScratch->CountGoodies() == view.mGoodiesAll)", "the Goodie count compared"),
            ("if (sScratch->GetNumKilled((EKilledType)k) != view.mKills[k])", "if (false)", "the kill counts compared"),
            ("return \"a Goodie's state\";", "(void)0;", "each Goodie's state compared"),
            ("if (mission.mWorld != node->mWorldNumber)", "if (false)", "each mission's number compared"),
            ("if (mission.mState != StateOf(sScratch, n))", "if (false)", "each mission's state compared"),
            ("if (PlainRank(node->mRanking) && (mission.mRank != char(sScratch->GetGradeFromRanking(node->mRanking))))", "if (false)", "each rank letter compared"),
            ("if ((listed != view.mMissionCount) || (complete != view.mMissionsComplete))", "if (false)", "the missions counted"),
            ("if (link->mToNode != n)", "if (false)", "a mission's routes in are those that lead to it"),
        )
        probes = [(name, skew) for name in ("part-way", "as written") for skew in range(max(SKEWS) + 1)]
        honest = [read(self.binary, self.file(n), s) for n, s in probes]
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(source.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"mutant-{n}"
            (folder / "platform/common").mkdir(parents=True)
            (folder / "game").mkdir()
            shutil.copyfile(ROOT / "port/platform/common/portlog.h", folder / "platform/common/portlog.h")
            for f in GAME.glob("ce_*.h"):
                shutil.copyfile(f, folder / "game" / f.name)
            (folder / "game/ce_career_view.cpp").write_text(source.replace(old, new))
            binary = build(folder / "driver", folder / "game", werror=False)       # a mutant that does not compile fails the test
            other = [read(binary, self.file(name), s) for name, s in probes]
            self.assertNotEqual(honest, other, f"a change to {what} went unnoticed")


if __name__ == "__main__":
    unittest.main(verbosity=2)
