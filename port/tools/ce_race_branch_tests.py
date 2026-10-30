#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the end-of-level page choice of CFrontEnd::Init as the port patches leave it (FrontEnd.cpp in the build's mirror, after
patches 0050, 0107, 0114 and 0116 in the build's order, each at fuzz 0), against authored fakes of the front end and of the
CE bonus module's answers; no game, display or retail data.

The branch picks the page a finished level goes to: a race (levels 901-905) went through the autosave page to the Goodies page
in retail; patch 0116 sends a CE bonus race to the Onslaught CE page with no save, and asks the bonus module once. A CE bonus
co-op map (856, 858) comes back to the Onslaught CE page too, in place of the Multiplayer page. Every other route (the original
race, the debriefing, the multiplayer page of a map started there, the title screen) must be as retail's. Causal mutants of
the patch must each be caught.
"""
import os
from pathlib import Path
import re
import resource
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402

PATCH = ROOT / "port/patches/0116-ce-bonus-race.patch"

DRIVER = r"""// SPDX-License-Identifier: GPL-3.0-only
// Authored boundaries for CFrontEnd::Init's page choice (the block is the build's patched FrontEnd.cpp text).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
typedef int BOOL;
const int TRUE = 1, FALSE = 0;
enum EFrontEndPage { FEP_MAIN = 1, FEP_UNKNOWN, FEP_SAVEGAME, FEP_GOODIES, FEP_DEBRIEFING, FEP_INTRO, FEP_MULTIPLAYER,
                     FEP_LANGUAGE_TEST, FEP_OPTIONS, FEP_TRANSITION };
enum EFrontEndEntry { FEE_START = 0, FEE_FROM_ATTRACT, FEE_TITLE_SCREEN };
static const char *name(int page)
{
	static const char *names[] = {"?", "MAIN", "UNKNOWN", "SAVEGAME", "GOODIES", "DEBRIEFING", "INTRO", "MULTIPLAYER",
	                              "LANGUAGE_TEST", "OPTIONS", "TRANSITION"};
	return page >= 0 && page <= FEP_TRANSITION ? names[page] : "?";
}
int performing_stress_test = 0;
char gAllCheatsEnabled = 0;
struct { int mAutoConfigTest = 0; int mAttractMode = 0; } CLIPARAMS;
static int demo = 0;
#define PLAYABLE_DEMO demo
static bool pendingRace = false, pendingDebrief = false, pendingCoop = false;
static int raceAsked = 0, debriefAsked = 0, coopAsked = 0;
bool PortCeBonusReturning() { ++debriefAsked; return pendingDebrief; }
bool PortCeBonusRaceReturning() { ++raceAsked; const bool r = pendingRace; pendingRace = false; return r; }
bool PortCeBonusCoopReturning() { ++coopAsked; const bool r = pendingCoop; pendingCoop = false; return r; }

struct CFrontEnd
{
	enum { AUTO_SAVE_NOT, AUTO_SAVE_NORMAL, AUTO_SAVE_PRETEND };
	int mSelectedLevel = 0;
	EFrontEndPage mActivePage = FEP_MAIN;
	int mAutoSave = AUTO_SAVE_NORMAL;
	std::string calls;
	int GetAutoSave() { return mAutoSave; }
	void SetSaveMode(BOOL on) { calls += "SetSaveMode(" + std::to_string(on) + ") "; }
	void SetPage(EFrontEndPage page, int time) { calls += std::string("SetPage(") + name(page) + "," + std::to_string(time) + ") "; }
	void SetSuccessFEP(EFrontEndPage page, int time)
	{
		calls += std::string("SetSuccessFEP(") + name(page) + "," + std::to_string(time) + ") ";
	}
	void Branch(EFrontEndEntry entry);
} FRONTEND;

void CFrontEnd::Branch(EFrontEndEntry entry)
{
	if (false)
	{
	}
#include "branch.inc"
}

int main(int argc, char **argv)
{
	if (argc != 8)
		return 1;
	// level autosave(0 not, 1 normal, 2 pretend) race-pending debrief-pending coop-pending entry demo
	FRONTEND.mSelectedLevel = std::atoi(argv[1]);
	FRONTEND.mAutoSave = std::atoi(argv[2]);
	pendingRace = std::atoi(argv[3]) != 0;
	pendingDebrief = std::atoi(argv[4]) != 0;
	pendingCoop = std::atoi(argv[5]) != 0;
	const int entry = std::atoi(argv[6]);
	demo = std::atoi(argv[7]);
	FRONTEND.Branch((EFrontEndEntry)entry);
	std::printf("page=%s calls=[%s] raceAsked=%d debriefAsked=%d coopAsked=%d\n", name(FRONTEND.mActivePage),
	            FRONTEND.calls.empty() ? "" : FRONTEND.calls.substr(0, FRONTEND.calls.size() - 1).c_str(), raceAsked, debriefAsked,
	            coopAsked);
	return 0;
}
"""


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def mirror_text(name):
    """src/NAME as the port's mirror holds it before any patch (include names and their case normalised as build.py does)."""
    build.SYSTEM_NAMES.update(build.system_header_names())
    rows = [l.split(maxsplit=1) for l in (ROOT / "config/game-data.sha256").read_text().splitlines() if l.strip()]
    targets = ([build.MIRROR_ROOT / f.relative_to(ROOT) for f in build.tracked_sources()] +
               [build.MIRROR / r for _, r in rows] + [build.MIRROR / "data/NVShaderFragments.inc"])
    raw = (ROOT / "src" / name).read_bytes().decode("latin-1")
    return build.normalise(raw, build.MIRROR_ROOT / "src", build.CaseIndex(targets),
                           [build.MIRROR, build.MIRROR / "MissionScript"]).encode("latin-1")


def patched_frontend(out, patch_text=None):
    """The mirror's FrontEnd.cpp with every port patch's FrontEnd.cpp section applied in the build's order, at fuzz 0."""
    (out / "FrontEnd.cpp").write_bytes(mirror_text("FrontEnd.cpp"))
    for patch in sorted((ROOT / "port/patches").glob("*.patch")):
        text = patch_text if patch == PATCH and patch_text is not None else patch.read_text(encoding="latin1")
        for m in re.finditer(r"(?ms)^--- a/FrontEnd\.cpp\n\+\+\+ b/FrontEnd\.cpp\n.*?(?=^--- a/|\Z)", text):
            (out / "section.patch").write_text(m.group(0), encoding="latin1")
            r = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", "section.patch"], cwd=out,
                               capture_output=True, text=True, timeout=10, preexec_fn=no_core)
            if r.returncode or "fuzz" in r.stdout.lower():
                raise AssertionError(f"{patch.name}: {r.stdout}{r.stderr}")
    return (out / "FrontEnd.cpp").read_text(encoding="latin1")


def branch_block(source):
    start = source.index("\telse if (entry == FEE_TITLE_SCREEN)")
    end = source.index("\n\n\tif (mActivePage != FEP_TRANSITION)", start)
    return source[start:end] + "\n"


# (level, autosave, race pending, debrief pending, co-op pending, entry, demo) -> the line the driver prints
NORMAL, PRETEND = 1, 2
SAVE = "SetSaveMode(1) SetPage(SAVEGAME,0) SetSuccessFEP(%s,50)"


def want(page, calls="", race=0, debrief=0, coop=0):
    return f"page={page} calls=[{calls}] raceAsked={race} debriefAsked={debrief} coopAsked={coop}"


CASES = {
    # a CE bonus race: no save, the Onslaught CE page, asked once
    (903, NORMAL, 1, 0, 0, 0, 0): want("LANGUAGE_TEST", race=1),
    (901, PRETEND, 1, 0, 0, 0, 0): want("LANGUAGE_TEST", race=1),
    (905, NORMAL, 1, 1, 1, 0, 0): want("LANGUAGE_TEST", race=1),
    # the original's race, as retail: the autosave page, then the Goodies page (or the Goodies page at once)
    (903, NORMAL, 0, 0, 0, 0, 0): want("UNKNOWN", SAVE % "GOODIES", race=1),
    (902, PRETEND, 0, 0, 0, 0, 0): want("GOODIES", race=1),
    # a CE bonus co-op map comes back to the Onslaught CE page, asked once; a map started from the Multiplayer page stays there
    (856, NORMAL, 0, 0, 1, 0, 0): want("LANGUAGE_TEST", coop=1),
    (858, NORMAL, 0, 0, 1, 0, 0): want("LANGUAGE_TEST", coop=1),
    (860, NORMAL, 0, 0, 0, 0, 0): want("MULTIPLAYER", coop=1),
    (850, NORMAL, 0, 0, 0, 0, 0): want("MULTIPLAYER", coop=1),
    (879, NORMAL, 0, 0, 0, 0, 0): want("MULTIPLAYER", coop=1),
    # a level that is not a race or a multiplayer map never asks those questions, even with a stale answer pending
    (900, NORMAL, 1, 0, 1, 0, 0): want("UNKNOWN", SAVE % "DEBRIEFING", debrief=1),
    (906, NORMAL, 1, 0, 1, 0, 0): want("UNKNOWN", SAVE % "DEBRIEFING", debrief=1),
    (880, NORMAL, 0, 0, 1, 0, 0): want("UNKNOWN", SAVE % "DEBRIEFING", debrief=1),
    (100, NORMAL, 1, 0, 1, 0, 0): want("UNKNOWN", SAVE % "DEBRIEFING", debrief=1),
    (100, PRETEND, 0, 0, 0, 0, 0): want("DEBRIEFING", debrief=1),
    # the CE bonus level's debriefing (patch 0107) and the title screen are as before
    (201, NORMAL, 0, 1, 0, 0, 0): want("DEBRIEFING", debrief=1),
    (903, NORMAL, 1, 0, 0, 2, 0): want("UNKNOWN", "SetPage(MAIN,0)"),
    (856, NORMAL, 0, 0, 1, 2, 0): want("UNKNOWN", "SetPage(MAIN,0)"),
}


class RaceBranchTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "local-data/port/ce-race-branch-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, name, patch_text=None):
        folder = self.out / name
        folder.mkdir()
        block = branch_block(patched_frontend(folder, patch_text))
        (folder / "branch.inc").write_text(block)
        (folder / "driver.cpp").write_text(DRIVER)
        exe = folder / "driver"
        r = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", '-DCE_VERSION="test"',
                            "-I", str(folder),
                            str(folder / "driver.cpp"), "-o", str(exe)], capture_output=True, text=True, timeout=120,
                           preexec_fn=no_core)
        self.assertEqual(r.returncode, 0, f"{name}: {r.stderr}")
        return exe

    def run_case(self, exe, case):
        return subprocess.run([str(exe), *map(str, case)], capture_output=True, text=True, timeout=10, preexec_fn=no_core)

    def test_every_route(self):
        exe = self.build("actual")
        for case, want in CASES.items():
            with self.subTest(case=case):
                ran = self.run_case(exe, case)
                self.assertEqual(ran.returncode, 0, ran.stderr)
                self.assertEqual(ran.stdout.strip(), want)

    def test_the_hook_sits_in_the_race_branch_before_the_autosave(self):
        block = branch_block(patched_frontend(self.out))
        race = block[block.index("// Race levels"):block.index("else if ((mSelectedLevel >= 850)")]
        self.assertEqual(block.count("PortCeBonusRaceReturning()"), 1)
        self.assertEqual(block.count("PortCeBonusCoopReturning()"), 1)
        self.assertLess(race.index("PortCeBonusRaceReturning()"), race.index("SetSaveMode"))
        self.assertLess(race.index("PortCeBonusRaceReturning()"), race.index("SetPage(FEP_SAVEGAME"))
        self.assertIn("#ifdef CE_VERSION", race)

    def test_the_faithful_build_has_no_hook(self):
        # Without CE_VERSION the patched block reads as retail's.
        block = branch_block(patched_frontend(self.out))
        faithful = re.sub(r"#ifdef CE_VERSION.*?#endif\n", "", block, flags=re.DOTALL)
        self.assertNotIn("PortCe", faithful)
        self.assertNotIn("FEP_LANGUAGE_TEST", faithful)
        self.assertIn("SetSuccessFEP(FEP_GOODIES, 50)", faithful)

    def test_causal_mutants_are_caught(self):
        patch = PATCH.read_text()
        race_set = "\t\t\t\tmActivePage = FEP_LANGUAGE_TEST;\n+\t\t\telse"
        coop_set = "\t\t\t\tmActivePage = FEP_LANGUAGE_TEST;\n+#endif\n+\t\t}"
        mutants = (
            ("wrong-page", race_set, race_set.replace("FEP_LANGUAGE_TEST", "FEP_GOODIES")),
            ("hook-off", "\t\t\tif (PortCeBonusRaceReturning())", "\t\t\tif (false)"),
            ("hook-inverted", "\t\t\tif (PortCeBonusRaceReturning())", "\t\t\tif (!PortCeBonusRaceReturning())"),
            ("hook-always", "\t\t\tif (PortCeBonusRaceReturning())", "\t\t\tif (PortCeBonusRaceReturning() || true)"),
            ("hook-asked-twice", "\t\t\tif (PortCeBonusRaceReturning())", "\t\t\tif (PortCeBonusRaceReturning() | PortCeBonusRaceReturning())"),
            ("debrief-page", race_set, race_set.replace("FEP_LANGUAGE_TEST", "FEP_DEBRIEFING")),
            ("coop-wrong-page", coop_set, coop_set.replace("FEP_LANGUAGE_TEST", "FEP_GOODIES")),
            ("coop-hook-off", "\t\t\tif (PortCeBonusCoopReturning())", "\t\t\tif (false)"),
            ("coop-hook-inverted", "\t\t\tif (PortCeBonusCoopReturning())", "\t\t\tif (!PortCeBonusCoopReturning())"),
            ("coop-hook-always", "\t\t\tif (PortCeBonusCoopReturning())", "\t\t\tif (PortCeBonusCoopReturning() || true)"),
            ("coop-hook-asked-twice", "\t\t\tif (PortCeBonusCoopReturning())",
             "\t\t\tif (PortCeBonusCoopReturning() | PortCeBonusCoopReturning())"),
        )
        for name, old, new in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(patch.count(old), 1, name)
                exe = self.build(name, patch.replace(old, new))
                failed = [case for case, want in CASES.items() if self.run_case(exe, case).stdout.strip() != want]
                self.assertTrue(failed, f"mutant {name} survived every case")


if __name__ == "__main__":
    unittest.main(verbosity=2)
