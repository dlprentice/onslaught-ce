#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual CE bonus module with authored in-memory career and frontend objects.

No game, player data, or save file is opened. Both player slots, both toggle directions,
all three bonus levels, unrelated bytes, and refusal paths are tested. Mutation controls
prove the fixture detects a missing restore, each lost setting and a wrong-level restore.
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
};

enum { FEP_MAIN = 7, FEP_MULTIPLAYER_START = 23 };
struct FakeFrontend
{
	int mSelectedLevel, quit, page, delay, quitCalls, pageCalls;
	void SetQuit(int level) { quit = level; ++quitCalls; }
	void SetPage(int newPage, int newDelay) { page = newPage; delay = newDelay; ++pageCalls; }
};
struct FakeGame { int level; int GetCurrentLevel() { return level; } };
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
		menus = true;
		check(!PortCeBonusStart(100), "an unrelated level must not start");
		check(FRONTEND.mSelectedLevel == -1 && FRONTEND.quitCalls == 0 && FRONTEND.pageCalls == 0,
		      "rejected start must leave frontend alone");
		check(std::memcmp(&CAREER, &original, sizeof(CAREER)) == 0, "rejected start must leave career alone");
		check(!PortCeBonusLevelEnd(), "rejected start must not activate restore");
		return 0;
	}

	check(level == 201 || level == 856 || level == 858, "invalid authored level");
	check(option >= 0 && option < 3 && player >= 0 && player < 2 && (initial == 0 || initial == 1),
	      "invalid authored option or player");
	selectSetting(CAREER, option, player, initial);
	CCareer original = CAREER;
	check(PortCeBonusStart(level), "bonus starts");
	check(FRONTEND.mSelectedLevel == level, "selected level");
	check((level == 201 && FRONTEND.quitCalls == 1 && FRONTEND.quit == level && FRONTEND.pageCalls == 0) ||
	      (level != 201 && FRONTEND.quitCalls == 0 && FRONTEND.pageCalls == 1 &&
	       FRONTEND.page == FEP_MULTIPLAYER_START && FRONTEND.delay == 30), "frontend route");
	GAME.level = level;
	check(PortCeBonusCoopRunning() == (level != 201), "co-op active only for a co-op level");
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
		}
	}
	CAREER.SetMusicVolume(0.75f);
	CAREER.SetSoundVolume(0.125f);
	spoilUnrelatedState();
	if (std::strcmp(mode, "wrong-level") == 0)
	{
		FRONTEND.mSelectedLevel = level + 1;
		CCareer live = CAREER;
		check(!PortCeBonusLevelEnd(), "different selected level must not restore snapshot");
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
		}
	}
	else
		selectSetting(original, option, player, !initial);
	original.SetMusicVolume(0.75f);
	original.SetSoundVolume(0.125f);
	check(PortCeBonusLevelEnd(), "matching level restores snapshot");
	check(std::memcmp(&CAREER, &original, sizeof(CAREER)) == 0,
	      "changed settings survive while every other byte returns to its original value");
	check(PortCeBonusReturning() == (level == 201), "only solo bonus shows bonus debrief");
	check(!PortCeBonusLevelEnd(), "end restores only once");
	check(!PortCeBonusCoopRunning(), "end deactivates bonus co-op");
	if (level == 201)
	{
		PortCeBonusLeaveDebriefing();
		check(!PortCeBonusReturning() && FRONTEND.page == FEP_MAIN && FRONTEND.delay == 30,
		      "leaving the bonus debrief returns to main");
	}
	return 0;
}
"""


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
        for level in (201, 856, 858):
            for option in range(3):
                for player in range(2):
                    for initial in (0, 1):
                        with self.subTest(level=level, option=option, player=player, initial=initial):
                            result = self.run_case(self.actual, "setting", level, option, player, initial)
                            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejected_start_and_changed_level_never_restore(self):
        result = self.run_case(self.actual, "blocked")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for level in (201, 856, 858):
            with self.subTest(level=level):
                result = self.run_case(self.actual, "wrong-level", level)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_all_settings_changed_together(self):
        for level in (201, 856, 858):
            with self.subTest(level=level):
                result = self.run_case(self.actual, "all", level)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_causal_mutants_fail(self):
        source = SOURCE.read_text()
        mutants = (
            ("lost-y", "CAREER.SetInvertYAxis(player, invertY[player]);", "(void)invertY;", "setting", 0),
            ("lost-jet-y", "CAREER.SetInvertJetYAxis(player, invertJetY[player]);", "(void)invertJetY;", "setting", 1),
            ("lost-god", "CAREER.SetIsGod(player, isGod[player]);", "(void)isGod;", "setting", 2),
            ("lost-music", "CAREER.SetMusicVolume(music);", "(void)music;", "setting", 0),
            ("lost-sound", "CAREER.SetSoundVolume(sound);", "(void)sound;", "setting", 0),
            ("lost-snapshot", "memcpy((void *)&CAREER, (const void *)sCareer, sizeof(CCareer));", "(void)sCareer;", "setting", 0),
            ("wrong-level-restore", "if (FRONTEND.mSelectedLevel != sLevel)", "if (false)", "wrong-level", 0),
        )
        for label, old, replacement, mode, option in mutants:
            with self.subTest(mutant=label):
                self.assertEqual(source.count(old), 1, label)
                altered = self.out / (label + ".cpp")
                altered.write_text(source.replace(old, replacement))
                binary = self.build(label, altered)
                result = self.run_case(binary, mode, 201, option, 0, 0)
                self.assertNotEqual(result.returncode, 0, f"{label} survived: {result.stdout}")
                self.assertIn("FAIL:", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
