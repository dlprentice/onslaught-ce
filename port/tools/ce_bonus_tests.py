#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual CE bonus module with authored in-memory career and frontend objects.

No game, player data, or save file is opened. Both player slots, both toggle directions,
all eight bonus levels (201, the two co-op maps and the five Race Challenges 901-905), unrelated
bytes, and refusal paths are tested. A race comes back through the front end's race branch (patch 0116)
exactly once and is never a debriefing or a co-op level. Mutation controls
prove the fixture detects a missing restore, each lost setting, a wrong-level restore, a lost,
sticking or mislabelled race return, and a debrief or race return that a level which was not the bonus level, or a later
front end entry, would inherit.
Run: python3 port/tools/ce_bonus_tests.py
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/game/ce_bonus.cpp"

# Authored compile boundaries; neither depends on a game or save.
FAKE_HEADER = r"""// SPDX-License-Identifier: GPL-3.0-only
// Authored boundaries for running the actual CE bonus module without a game or save file.
#ifndef CE_BONUS_FAKE_H
#define CE_BONUS_FAKE_H

typedef int BOOL;

struct CCareer
{
	unsigned char unknownBefore[73];
	int progress;
	float sound, music;
	BOOL god[2], invertY[2], invertJetY[2], vibration[2];
	int controllerConfig[2];
	union { struct { unsigned killedAircraft, killedVehicles; }; int mKilledThings[2]; };
	unsigned char unknownAfter[149];

	float GetSoundVolume() { return sound; }
	void SetSoundVolume(float v) { sound = v; }
	float GetMusicVolume() { return music; }
	void SetMusicVolume(float v) { music = v; }
	BOOL GetIsGod(int p) { return god[p]; }
	void SetIsGod(int p, BOOL v) { god[p] = v; }
	BOOL GetInvertYAxis(int p) { return invertY[p]; }
	void SetInvertYAxis(int p, BOOL v) { invertY[p] = v; }
	BOOL GetInvertJetYAxis(int p) { return invertJetY[p]; }
	void SetInvertJetYAxis(int p, BOOL v) { invertJetY[p] = v; }
	BOOL GetVibration(int p) { return vibration[p]; }
	void SetVibration(int p, BOOL v) { vibration[p] = v; }
	int GetControllerConfigurationNum(int p) { return controllerConfig[p]; }
	void SetControllerConfigurationNum(int p, int v) { controllerConfig[p] = v; }
	int GetScreenPosX() { return int(killedAircraft >> 24) - 128; }
	int GetScreenPosY() { return int(killedVehicles >> 24) - 128; }
	void SetScreenPosX(int x) { killedAircraft = (unsigned(x + 128) << 24) | (killedAircraft & 0xffffff); }
	void SetScreenPosY(int y) { killedVehicles = (unsigned(y + 128) << 24) | (killedVehicles & 0xffffff); }
	void Save(char *dest);
	char *WriteOptions(char *dest);
	static int SizeOfSaveGame();
};

enum { FEP_MAIN = 7, FEP_LANGUAGE_TEST = 20, FEP_MULTIPLAYER_START = 23 };
struct FakeFrontend
{
	int mSelectedLevel, quit, page, delay, quitCalls, pageCalls;
	struct { char mSaveGameName[32]; int cheat; int IsCheatActive(int) { return cheat; } } mFEPSaveGame;
	int GetMemoryCardNumber() { return 1; }
	void SetQuit(int level) { quit = level; ++quitCalls; }
	void SetPage(int newPage, int newDelay) { page = newPage; delay = newDelay; ++pageCalls; }
};
struct FakeGame
{
	int level, unpauses, quit;
	int GetCurrentLevel() { return level; }
	void UnPause() { ++unpauses; }
	void SetQuit(int value) { quit = value; }
};
extern CCareer CAREER;
extern FakeFrontend FRONTEND;
extern FakeGame GAME;

#endif
"""

DRIVER = r"""// SPDX-License-Identifier: GPL-3.0-only
// Calls the production CE bonus module against authored in-memory objects. No .bes files or game data.
#include "ce_bonus_fake.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

CCareer CAREER;
FakeFrontend FRONTEND;
FakeGame GAME;
static bool menus = true;
bool PortCeMenus() { return menus; }
bool PortCeBonusStart(int level);
bool PortCeBonusCoopRunning();
bool PortCeBonusLevelEnd();
bool PortCeBonusReturning();
bool PortCeBonusRaceReturning();
bool PortCeBonusCoopReturning();
int PortCeBonusTakeReturn();
void PortCeBonusLeaveDebriefing();

static void check(bool condition, const char *message)
{
	if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); }
}

static void initialise()
{
	// Deliberately nonzero opaque bytes and progress: the test compares the entire object after restore.
	std::memset(&CAREER, 0xa5, sizeof(CAREER));
	CAREER.progress = 1234;
	CAREER.SetSoundVolume(0.25f);
	CAREER.SetMusicVolume(0.5f);
	for (int p = 0; p < 2; ++p)
	{
		CAREER.SetIsGod(p, p);
		CAREER.SetInvertYAxis(p, p);
		CAREER.SetInvertJetYAxis(p, p);
	}
	std::memset(&FRONTEND, 0, sizeof(FRONTEND));
	FRONTEND.mSelectedLevel = -1;
	GAME.level = -1;
}

static void spoilUnrelatedState()
{
	CAREER.progress = 9876;
	CAREER.unknownBefore[17] ^= 0x6c;
	CAREER.unknownAfter[37] ^= 0x29;
}

static void selectSetting(CCareer &career, int option, int player, int value)
{
	if (option == 0) career.SetInvertYAxis(player, value);
	else if (option == 1) career.SetInvertJetYAxis(player, value);
	else if (option == 2) career.SetIsGod(player, value);
	else check(false, "invalid authored option");
}

int main(int argc, char **argv)
{
	check(argc == 6, "expected case, level, option, player, initial");
	const char *mode = argv[1];
	int level = std::atoi(argv[2]), option = std::atoi(argv[3]), player = std::atoi(argv[4]);
	int initial = std::atoi(argv[5]);
	initialise();
	check(!PortCeBonusLevelEnd(), "inactive end must not restore");
	if (std::strcmp(mode, "blocked") == 0)
	{
		CCareer original = CAREER;
		menus = false;
		check(!PortCeBonusStart(201), "disabled CE menus must not start");
		check(!PortCeBonusStart(903), "disabled CE menus must not start a race");
		menus = true;
		check(!PortCeBonusStart(100), "an unrelated level must not start");
		check(!PortCeBonusStart(900), "the level before the first race must not start");
		check(!PortCeBonusStart(906), "the level after the last race must not start");
		check(FRONTEND.mSelectedLevel == -1 && FRONTEND.quitCalls == 0 && FRONTEND.pageCalls == 0,
		      "rejected start must leave frontend alone");
		check(std::memcmp(&CAREER, &original, sizeof(CAREER)) == 0, "rejected start must leave career alone");
		check(!PortCeBonusLevelEnd(), "rejected start must not activate restore");
		return 0;
	}

	const bool race = level >= 901 && level <= 905;
	const bool coop = level == 856 || level == 858;
	check(level == 201 || coop || race, "invalid authored level");
	if (std::strcmp(mode, "race-stale-end") == 0)
	{
		check(race, "stale mode takes a race");
		check(PortCeBonusStart(level), "race starts");
		FRONTEND.mSelectedLevel = level;
		check(PortCeBonusLevelEnd(), "race end restores");
		// the front end never asked (it entered another way); the next entry after a level begins with nothing pending
		check(!PortCeBonusLevelEnd(), "an inactive end restores nothing");
		check(!PortCeBonusRaceReturning(), "the next front end entry forgets an unasked race return");
		check(PortCeBonusTakeReturn() == 0, "the next front end entry forgets an unasked list return");
		return 0;
	}
	if (std::strcmp(mode, "debrief-stale-end") == 0)
	{
		check(level == 201, "debrief mode takes the solo bonus level");
		check(PortCeBonusStart(level), "bonus starts");
		FRONTEND.mSelectedLevel = level;
		check(PortCeBonusLevelEnd(), "bonus end restores");
		check(PortCeBonusReturning(), "the solo bonus returns to its debrief");
		// the debrief was never shown (the front end entered another way): the next entry begins with nothing pending
		check(!PortCeBonusLevelEnd(), "an inactive end restores nothing");
		check(!PortCeBonusReturning(), "the next front end entry forgets a debrief that was never shown");
		return 0;
	}
	if (std::strcmp(mode, "race-stale") == 0)
	{
		check(race, "stale mode takes a race");
		check(PortCeBonusStart(level), "race starts");
		FRONTEND.mSelectedLevel = level;
		check(PortCeBonusLevelEnd(), "race end restores");
		// the front end never asked: another bonus level starts and ends without the race branch
		check(PortCeBonusStart(201), "bonus starts after an unasked race");
		check(!PortCeBonusRaceReturning(), "a new start forgets an unasked race return");
		check(PortCeBonusTakeReturn() == 0, "a new start forgets an unasked list return");
		FRONTEND.mSelectedLevel = 201;
		check(PortCeBonusLevelEnd(), "bonus end restores");
		check(!PortCeBonusRaceReturning(), "a non-race end is not a race return");
		check(PortCeBonusReturning(), "the solo bonus still returns to its debrief");
		return 0;
	}
	check(option >= 0 && option < 3 && player >= 0 && player < 2 && (initial == 0 || initial == 1),
	      "invalid authored option or player");
	selectSetting(CAREER, option, player, initial);
	CCareer original = CAREER;
	check(PortCeBonusStart(level), "bonus starts");
	check(FRONTEND.mSelectedLevel == level, "selected level");
	check(((level == 201 || race) && FRONTEND.quitCalls == 1 && FRONTEND.quit == level && FRONTEND.pageCalls == 0) ||
	      (coop && FRONTEND.quitCalls == 0 && FRONTEND.pageCalls == 1 &&
	       FRONTEND.page == FEP_MULTIPLAYER_START && FRONTEND.delay == 30), "frontend route");
	check(!PortCeBonusRaceReturning(), "no race return before the level has ended");
	GAME.level = level;
	check(PortCeBonusCoopRunning() == coop, "co-op active only for a co-op level");
	GAME.level = level + 1;
	check(!PortCeBonusCoopRunning(), "a different running level is not bonus co-op");

	selectSetting(CAREER, option, player, !initial);
	if (std::strcmp(mode, "all") == 0)
	{
		for (int p = 0; p < 2; ++p)
		{
			CAREER.SetInvertYAxis(p, !original.GetInvertYAxis(p));
			CAREER.SetInvertJetYAxis(p, !original.GetInvertJetYAxis(p));
			CAREER.SetIsGod(p, !original.GetIsGod(p));
			CAREER.SetVibration(p, !original.GetVibration(p));
			CAREER.SetControllerConfigurationNum(p, p + 3);
		}
		CAREER.SetScreenPosX(-13);
		CAREER.SetScreenPosY(22);
	}
	CAREER.SetMusicVolume(0.75f);
	CAREER.SetSoundVolume(0.125f);
	spoilUnrelatedState();
	if (std::strcmp(mode, "wrong-level") == 0)
	{
		FRONTEND.mSelectedLevel = level + 1;
		CCareer live = CAREER;
		check(!PortCeBonusLevelEnd(), "different selected level must not restore snapshot");
		// asked straight after the end that ended it: a second end would clear every pending return itself and hide a stale one
		check(!PortCeBonusRaceReturning() && !PortCeBonusReturning(), "a different level is neither a race return nor a debrief");
		check(!PortCeBonusCoopReturning() && PortCeBonusTakeReturn() == 0, "a different level returns to no bonus list");
		check(std::memcmp(&CAREER, &live, sizeof(CAREER)) == 0, "different selected level retains live career");
		check(!PortCeBonusLevelEnd(), "different-level end deactivates bonus");
		return 0;
	}
	check(std::strcmp(mode, "setting") == 0 || std::strcmp(mode, "all") == 0, "invalid authored mode");
	if (std::strcmp(mode, "all") == 0)
	{
		for (int p = 0; p < 2; ++p)
		{
			original.SetInvertYAxis(p, !original.GetInvertYAxis(p));
			original.SetInvertJetYAxis(p, !original.GetInvertJetYAxis(p));
			original.SetIsGod(p, !original.GetIsGod(p));
			original.SetVibration(p, !original.GetVibration(p));
			original.SetControllerConfigurationNum(p, p + 3);
		}
		original.SetScreenPosX(-13);
		original.SetScreenPosY(22);
	}
	else
		selectSetting(original, option, player, !initial);
	original.SetMusicVolume(0.75f);
	original.SetSoundVolume(0.125f);
	check(PortCeBonusLevelEnd(), "matching level restores snapshot");
	check(std::memcmp(&CAREER, &original, sizeof(CAREER)) == 0,
	      "changed settings survive while every other byte returns to its original value");
	check(PortCeBonusReturning() == (level == 201), "only solo bonus shows bonus debrief");
	check(PortCeBonusRaceReturning() == race, "only a race takes the front end's race branch back to the CE page");
	check(!PortCeBonusRaceReturning(), "the race branch is asked once");
	check(PortCeBonusCoopReturning() == coop, "only a co-op map takes the front end's multiplayer branch back to the CE page");
	check(!PortCeBonusCoopReturning(), "the multiplayer branch is asked once");
	check(PortCeBonusTakeReturn() == level, "the CE page opens on the Bonus Levels list with this level highlighted");
	check(PortCeBonusTakeReturn() == 0, "the list return is taken once");
	check(!PortCeBonusLevelEnd(), "end restores only once");
	check(!PortCeBonusCoopRunning(), "end deactivates bonus co-op");
	if (level == 201)
	{
		PortCeBonusLeaveDebriefing();
		check(!PortCeBonusReturning() && FRONTEND.page == FEP_LANGUAGE_TEST && FRONTEND.delay == 30,
		      "leaving the bonus debrief opens the Onslaught CE page");
	}
	return 0;
}
"""


LEVELS = (201, 856, 858, 901, 902, 903, 904, 905)		# the bonus level, the two co-op maps, the five Race Challenges


class BonusSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data/port/ce-bonus-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        cls.out = Path(cls.tmp.name)
        (cls.out / "ce_bonus_fake.h").write_text(FAKE_HEADER)
        (cls.out / "ce_bonus_driver.cpp").write_text(DRIVER)
        for name in ("common.h", "frontend.h", "Career.h", "game.h"):
            (cls.out / name).write_text('#include "ce_bonus_fake.h"\n')
        cls.actual = cls.build("actual", SOURCE)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    @classmethod
    def build(cls, label, source):
        binary = cls.out / label
        result = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror",
                                 '-DCE_VERSION="test"', "-I", str(cls.out), str(source),
                                 str(cls.out / "ce_bonus_driver.cpp"), "-o", str(binary)],
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(f"{label} compilation failed:\n{result.stdout}{result.stderr}")
        return binary

    def run_case(self, binary, mode, level=201, option=0, player=0, initial=0):
        return subprocess.run([str(binary), mode, str(level), str(option), str(player), str(initial)],
                              capture_output=True, text=True, timeout=10)

    def test_settings_and_nonsettings_across_all_bonus_levels(self):
        for level in LEVELS:
            for option in range(3):
                for player in range(2):
                    for initial in (0, 1):
                        with self.subTest(level=level, option=option, player=player, initial=initial):
                            result = self.run_case(self.actual, "setting", level, option, player, initial)
                            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejected_start_and_changed_level_never_restore(self):
        result = self.run_case(self.actual, "blocked")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for level in LEVELS:
            with self.subTest(level=level):
                result = self.run_case(self.actual, "wrong-level", level)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_all_settings_changed_together(self):
        for level in LEVELS:
            with self.subTest(level=level):
                result = self.run_case(self.actual, "all", level)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_race_return_is_asked_once_and_never_inherited(self):
        for level in (901, 902, 903, 904, 905):
            for mode in ("race-stale", "race-stale-end"):
                with self.subTest(level=level, mode=mode):
                    result = self.run_case(self.actual, mode, level)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_debrief_is_shown_once_and_never_inherited(self):
        result = self.run_case(self.actual, "debrief-stale-end", 201)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_causal_mutants_fail(self):
        source = SOURCE.read_text()
        mutants = (
            ("lost-y", "CAREER.SetInvertYAxis(player, invertY[player]);", "(void)invertY;", "setting", 0),
            ("lost-jet-y", "CAREER.SetInvertJetYAxis(player, invertJetY[player]);", "(void)invertJetY;", "setting", 1),
            ("lost-god", "CAREER.SetIsGod(player, isGod[player]);", "(void)isGod;", "setting", 2),
            ("lost-music", "CAREER.SetMusicVolume(music);", "(void)music;", "setting", 0),
            ("lost-sound", "CAREER.SetSoundVolume(sound);", "(void)sound;", "setting", 0),
            ("lost-vibration", "CAREER.SetVibration(player, vibration[player]);", "(void)vibration;", "all", 0),
            ("lost-controller-config", "CAREER.SetControllerConfigurationNum(player, controllerConfig[player]);", "(void)controllerConfig;", "all", 0),
            ("lost-screen-x", "CAREER.SetScreenPosX(screenX);", "(void)screenX;", "all", 0),
            ("lost-screen-y", "CAREER.SetScreenPosY(screenY);", "(void)screenY;", "all", 0),
            ("lost-snapshot", "memcpy((void *)&CAREER, (const void *)sCareer, sizeof(CCareer));", "(void)sCareer;", "setting", 0),
            ("wrong-level-restore", "if (FRONTEND.mSelectedLevel != sLevel)", "if (false)", "wrong-level", 0),
            # the races (patch 0116): each of these breaks one thing a race must do
            ("lost-race-return", "sRaceReturning = IsRace(sLevel);", "sRaceReturning = false;", "setting", 0, 903),
            ("race-return-sticks", "\tsRaceReturning = false;\n\treturn returning;", "\treturn returning;", "setting", 0, 903),
            ("race-return-inherited", "\tsReturning = false;\n\tsRaceReturning = false;\n\tsCoopReturning = false;\n\tsReturnLevel = 0;\n\tFRONTEND.mSelectedLevel = level;",
             "\tsReturning = false;\n\tsCoopReturning = false;\n\tsReturnLevel = 0;\n\tFRONTEND.mSelectedLevel = level;", "race-stale", 0, 903),
            ("race-return-outlives-its-entry", "bool PortCeBonusLevelEnd()\n{\n\tsReturning = false;\n\tsRaceReturning = false;\n",
             "bool PortCeBonusLevelEnd()\n{\n\tsReturning = false;\n", "race-stale-end", 0, 903),
            # every bonus level comes back to the list it was started from (the CE menus' return rule)
            ("lost-list-return", "\tsReturnLevel = sLevel;", "\tsReturnLevel = 0;", "setting", 0, 856),
            ("list-return-sticks", "\tconst int level = sReturnLevel;\n\tsReturnLevel = 0;\n\treturn level;",
             "\tconst int level = sReturnLevel;\n\treturn level;", "setting", 0, 201),
            ("list-return-outlives-its-entry", "bool PortCeBonusLevelEnd()\n{\n\tsReturning = false;\n\tsRaceReturning = false;\n\tsCoopReturning = false;\n\tsReturnLevel = 0;\n",
             "bool PortCeBonusLevelEnd()\n{\n\tsReturning = false;\n\tsRaceReturning = false;\n\tsCoopReturning = false;\n", "race-stale-end", 0, 903),
            ("list-return-inherited", "\tsCoopReturning = false;\n\tsReturnLevel = 0;\n\tFRONTEND.mSelectedLevel = level;",
             "\tsCoopReturning = false;\n\tFRONTEND.mSelectedLevel = level;", "race-stale", 0, 903),
            ("coop-return-lost", "\tsCoopReturning = IsCoop(sLevel);", "\tsCoopReturning = false;", "setting", 0, 858),
            ("coop-return-for-a-race", "\tsCoopReturning = IsCoop(sLevel);", "\tsCoopReturning = true;", "setting", 0, 903),
            ("coop-return-sticks", "\tconst bool returning = sCoopReturning;\n\tsCoopReturning = false;\n\treturn returning;",
             "\tconst bool returning = sCoopReturning;\n\treturn returning;", "setting", 0, 856),
            ("debrief-to-main", "FRONTEND.SetPage(FEP_LANGUAGE_TEST, 30);", "FRONTEND.SetPage(FEP_MAIN, 30);", "setting", 0, 201),
            ("race-as-coop", "bool IsCoop(int level) { return level == 856 || level == 858; }",
             "bool IsCoop(int level) { return level != kBonusLevel; }", "setting", 0, 903),
            ("race-on-start-page", "if (level == kBonusLevel || IsRace(level))", "if (level == kBonusLevel)", "setting", 0, 903),
            ("race-refused", "!IsCoop(level) && !IsRace(level)", "!IsCoop(level)", "setting", 0, 903),
            ("race-wrong-range", "level >= kRaceFirst && level <= kRaceLast", "level > kRaceFirst && level <= kRaceLast", "setting", 0, 901),
            # a debrief or race return that outlives its front end entry, or that a level which was not the bonus level leaves
            ("debrief-outlives-its-entry", "bool PortCeBonusLevelEnd()\n{\n\tsReturning = false;\n\tsRaceReturning = false;\n",
             "bool PortCeBonusLevelEnd()\n{\n\tsRaceReturning = false;\n", "debrief-stale-end", 0, 201),
            ("wrong-level-leaves-a-debrief", "\t\treturn false;\t// backed out of the start page",
             "\t{ sReturning = true; return false; }\t// backed out of the start page", "wrong-level", 0, 201),
            ("wrong-level-leaves-a-race-return", "\t\treturn false;\t// backed out of the start page",
             "\t{ sRaceReturning = true; return false; }\t// backed out of the start page", "wrong-level", 0, 903),
        )
        for label, old, replacement, mode, option, *rest in mutants:
            with self.subTest(mutant=label):
                self.assertEqual(source.count(old), 1, label)
                altered = self.out / (label + ".cpp")
                altered.write_text(source.replace(old, replacement))
                binary = self.build(label, altered)
                result = self.run_case(binary, mode, rest[0] if rest else 201, option, 0, 0)
                self.assertNotEqual(result.returncode, 0, f"{label} survived: {result.stdout}")
                self.assertIn("FAIL:", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
