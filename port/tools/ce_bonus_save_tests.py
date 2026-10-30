#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual patched save/Continue code and the bonus filter on authored in-memory objects.

No player files, game data, display or sound are opened. The driver compiles the real CE bonus module, patched
CCareer::Save and CPauseMenu::Resume bodies and the real CE menu/god policy over small authored boundaries. It
checks exact serialized bytes, changes to both players' settings, the two packed screen-position bytes, bindings,
PC options, errors, ordinary/other-level saves, repeated entries, and the replay/record/private gates. It also runs
the actual console map action and the patched System.cpp's level-transfer loop with an authored destination game.
Mutations show that the central assertions detect leaks, dropped settings, the wrong god-policy order, and a missing
or late bonus restoration at a console map jump. This is a causal source test, not a completed race on real hardware.
It uses the authored fake career from ce_bonus_tests.py and the patch-order helper from save_flow_tests.py.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

import ce_bonus_tests
import save_flow_tests

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
BONUS = PORT / "game/ce_bonus.cpp"
DRIVER = PORT / "tools/tests/ce_bonus_save_driver.cpp"


class BonusSaveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data/port/ce-bonus-save-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.out = Path(cls.tmp.name)
        header = ce_bonus_tests.FAKE_HEADER
        for method in ("GetScreenPosX", "GetScreenPosY", "SetScreenPosX", "SetScreenPosY"):
            # Compile the actual career accessors against the authored fields, not a second copy of their logic.
            header, count = re.subn(r"(\b(?:int|void) " + method + r"\([^)]*\)) \{[^\n]+\}", r"\1;", header)
            assert count == 1, method
        (cls.out / "ce_bonus_fake.h").write_text(header)
        for name in ("common.h", "frontend.h", "Career.h", "game.h"):
            (cls.out / name).write_text('#include "ce_bonus_fake.h"\n')

        career = save_flow_tests.patched(cls.out, "Career.cpp", True, mirror=True)
        pause = save_flow_tests.patched(cls.out, "PauseMenu.cpp", True, mirror=True)
        system = save_flow_tests.patched(cls.out, "System.cpp", True, mirror=True)
        cls.save = save_flow_tests.function(career, r"^void\s+CCareer::Save\(char\s*\*")
        cls.resume = save_flow_tests.function(pause, r"^void\s+CPauseMenu::Resume\(")
        accessors = ("GetScreenPosX", "GetScreenPosY", "SetScreenPosX", "SetScreenPosY")
        (cls.out / "native_screen.inc").write_text("\n".join(
            save_flow_tests.function(career, r"^(?:int|void)\s+CCareer::" + name + r"\(") for name in accessors))
        ce = (PORT / "platform/common/ce.cpp").read_text()
        front = (PORT / "game/ce_frontend.cpp").read_text()
        fragments = [(ce, r"^bool\s+PortCeLayer\("), (front, r"^bool\s+PortCeMenus\("),
                     (front, r"^static bool\s+GodFromSwitchOnly\("), (front, r"^static int\s+GodMask\("),
                     (front, r"^bool\s+PortCeCareerSaving\(")]
        (cls.out / "native_policy.inc").write_text("\n".join(save_flow_tests.function(s, p) for s, p in fragments))
        (cls.out / "native_resume.inc").write_text(cls.resume)
        console = (PORT / "game/ce_console_game.cpp").read_text()
        jump = save_flow_tests.function(console, r"^void\s+JumpToLevel\(int level\)")
        take = save_flow_tests.function(console, r"^int\s+PortCeConsoleTakeLevel\(\)")
        (cls.out / "native_jump_action.inc").write_text("namespace CeConsoleGame {\n" + jump + "\n}\n" + take + "\n")
        loops = re.findall(r"(?m)^[ \t]*for \(SINT next = PortCeConsoleTakeLevel\(\);[^\n]*\n[ \t]*\{\n(?:[^\n]*\n)*?[ \t]*\}", system)
        assert len(loops) == 1, "expected the one patched System console loop"
        cls.jump_loop = loops[0]
        cls.actual = cls.build("actual", cls.save, BONUS)
        cls.private = cls.build("private", cls.save, BONUS, public=False)

    @classmethod
    def build(cls, label, save, bonus, public=True, jump_loop=None):
        (cls.out / "native_save.inc").write_text(save)
        (cls.out / "native_jump_loop.inc").write_text(cls.jump_loop if jump_loop is None else jump_loop)
        result = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-m32", "-O1", "-fwrapv",
                                 "-fno-strict-aliasing", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                                 *(['-DCE_VERSION="test"'] if public else []), "-I", str(cls.out),
                                 str(bonus), str(DRIVER), "-o", str(cls.out / label)],
                                capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise AssertionError(f"{label} compilation failed:\n{result.stdout}{result.stderr}")
        return cls.out / label

    def run_case(self, binary, mode, level=901, extra_env=None):
        env = os.environ.copy()
        for name in ("BEA_PORT_CE_LAYER", "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"):
            env.pop(name, None)
        env.update(extra_env or {})
        return subprocess.run([str(binary), mode, str(level)], env=env, capture_output=True, text=True, timeout=10)

    def passed(self, binary, mode, level=901, extra_env=None):
        result = self.run_case(binary, mode, level, extra_env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pause_continue_filters_each_bonus_level_and_saves_settings(self):
        for level in ce_bonus_tests.LEVELS:
            with self.subTest(level=level):
                self.passed(self.actual, "bonus", level)

    def test_normal_different_and_repeated_levels(self):
        for mode in ("regular", "no-snapshot", "different-game", "different-selected", "other-object", "repeat"):
            with self.subTest(mode=mode):
                self.passed(self.actual, mode)

    def test_god_policy_and_save_failure_paths(self):
        for mode in ("name-cheat", "god-read-fail", "god-write-fail", "create-fail", "write-fail", "options-fail", "no-name", "options-open"):
            with self.subTest(mode=mode):
                self.passed(self.actual, mode)

    def test_replay_record_probe_demo_and_private_fences(self):
        for name in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"):
            with self.subTest(gate=name):
                self.passed(self.actual, "gates", extra_env={name: "1"})
        for mode in ("autoconfig", "demo"):
            self.passed(self.actual, mode)
        self.passed(self.private, "regular")
        # The explicit developer override is deliberately stronger than the normal replay/record gate.
        self.passed(self.actual, "bonus", extra_env={"BEA_PORT_RECORD": "1", "BEA_PORT_CE_LAYER": "1"})

    def test_console_map_restores_the_bonus_and_leaves_other_games_alone(self):
        for level in ce_bonus_tests.LEVELS:
            with self.subTest(level=level):
                self.passed(self.actual, "jump-bonus", level)
        for mode in ("jump-ordinary", "jump-wrong-game", "jump-wrong-selection", "jump-not-departed"):
            with self.subTest(mode=mode):
                self.passed(self.actual, mode, 856)

    def test_console_map_causal_controls(self):
        source = BONUS.read_text()
        hook = "\t\t\t\t\tPortCeBonusBeforeConsoleLevel();\t// patch 0181: restore a bonus before choosing the next level\n"
        selected = "\t\t\t\t\tFRONTEND.mSelectedLevel = next;\n"
        self.assertEqual(self.jump_loop.count(hook), 1)
        self.assertEqual(self.jump_loop.count(selected), 1)
        loops = {
            "no-jump-hook": self.jump_loop.replace(hook, ""),
            "late-jump-hook": self.jump_loop.replace(hook, "").replace(selected, selected + hook),
        }
        for label, loop in loops.items():
            with self.subTest(mutant=label):
                result = self.run_case(self.build(label, self.save, BONUS, jump_loop=loop), "jump-bonus", 901)
                self.assertNotEqual(result.returncode, 0, f"{label} survived")
                self.assertIn("FAIL:", result.stderr)

        guard = "if (!sActive || !sCareer || GAME.GetCurrentLevel() != sLevel || FRONTEND.mSelectedLevel != sLevel)"
        mutants = [
            ("restore-wrong-game", guard, "if (!sActive || !sCareer || FRONTEND.mSelectedLevel != sLevel)", "jump-wrong-game", 856),
            ("lost-vibration-on-jump", "\t\tCAREER.SetVibration(player, vibration[player]);\n", "\t\t(void)vibration;\n", "jump-bonus", 901),
            ("lost-config-on-jump", "\t\tCAREER.SetControllerConfigurationNum(player, controllerConfig[player]);\n", "\t\t(void)controllerConfig;\n", "jump-bonus", 901),
            ("lost-screen-x-on-jump", "\tCAREER.SetScreenPosX(screenX);\n", "\t(void)screenX;\n", "jump-bonus", 901),
            ("lost-screen-y-on-jump", "\tCAREER.SetScreenPosY(screenY);\n", "\t(void)screenY;\n", "jump-bonus", 901),
        ]
        for label, old, replacement, mode, level in mutants:
            with self.subTest(mutant=label):
                self.assertEqual(source.count(old), 1, label)
                changed = self.out / (label + ".cpp")
                changed.write_text(source.replace(old, replacement))
                result = self.run_case(self.build(label, self.save, changed), mode, level)
                self.assertNotEqual(result.returncode, 0, f"{label} survived")
                self.assertIn("FAIL:", result.stderr)

        # Selection is guarded both by the jump wrapper and by the ordinary bonus end. Remove both to test the
        # resulting restore of an unrelated selected level; either check alone correctly prevents it.
        end_guard = "if (FRONTEND.mSelectedLevel != sLevel)"
        self.assertEqual(source.count(guard), 1)
        self.assertEqual(source.count(end_guard), 1)
        wrong_selection = self.out / "restore-wrong-selection.cpp"
        wrong_selection.write_text(source.replace(guard, "if (!sActive || !sCareer || GAME.GetCurrentLevel() != sLevel)")
                                   .replace(end_guard, "if (false)"))
        result = self.run_case(self.build("restore-wrong-selection", self.save, wrong_selection), "jump-wrong-selection", 856)
        self.assertNotEqual(result.returncode, 0, "both missing selection guards survived")
        self.assertIn("FAIL:", result.stderr)

        # Each bonus return flag must be consumed by the jump, not inherited by the destination front end.
        body = save_flow_tests.function(source, r"^void\s+PortCeBonusBeforeConsoleLevel\(\)")
        for flag, level in (("sReturning", 201), ("sRaceReturning", 901), ("sCoopReturning", 856), ("sReturnLevel", 201)):
            with self.subTest(stale_return=flag):
                old = f"\t{flag} = " + ("0;\n" if flag == "sReturnLevel" else "false;\n")
                self.assertEqual(body.count(old), 1)
                changed = self.out / ("stale-" + flag + ".cpp")
                changed.write_text(source.replace(body, body.replace(old, "")))
                result = self.run_case(self.build("stale-" + flag, self.save, changed), "jump-bonus", level)
                self.assertNotEqual(result.returncode, 0, f"{flag} survived")
                self.assertIn("FAIL:", result.stderr)

    def test_causal_controls(self):
        source = BONUS.read_text()
        mutants = [
            ("missing-hook", "save", "\tPortCeBonusFilterSave(this, dest);\n", "", "bonus"),
            ("live-not-entry", "bonus", "(const void *)sCareer, sizeof(filtered)", "(const void *)career, sizeof(filtered)", "bonus"),
            ("allow-other-career", "bonus", "career != &CAREER || ", "", "other-object"),
            ("allow-other-game", "bonus",
             "if (career != &CAREER || !PortCeMenus() || !sActive || !sCareer ||\n\t\tGAME.GetCurrentLevel() != sLevel || FRONTEND.mSelectedLevel != sLevel)",
             "if (career != &CAREER || !PortCeMenus() || !sActive || !sCareer ||\n\t\tFRONTEND.mSelectedLevel != sLevel)", "different-game"),
            ("allow-other-selection", "bonus",
             "if (career != &CAREER || !PortCeMenus() || !sActive || !sCareer ||\n\t\tGAME.GetCurrentLevel() != sLevel || FRONTEND.mSelectedLevel != sLevel)",
             "if (career != &CAREER || !PortCeMenus() || !sActive || !sCareer ||\n\t\tGAME.GetCurrentLevel() != sLevel)", "different-selected"),
            ("lost-music", "bonus", "\tfiltered.SetMusicVolume(career->GetMusicVolume());\n", "", "bonus"),
            ("lost-sound", "bonus", "\tfiltered.SetSoundVolume(career->GetSoundVolume());\n", "", "bonus"),
            ("lost-invert", "bonus", "\t\tfiltered.SetInvertYAxis(player, career->GetInvertYAxis(player));\n", "", "bonus"),
            ("lost-jet-invert", "bonus", "\t\tfiltered.SetInvertJetYAxis(player, career->GetInvertJetYAxis(player));\n", "", "bonus"),
            ("lost-god", "bonus", "\t\tfiltered.SetIsGod(player, career->GetIsGod(player));\n", "", "bonus"),
            ("lost-vibration", "bonus", "\t\tfiltered.SetVibration(player, career->GetVibration(player));\n", "", "bonus"),
            ("lost-config", "bonus", "\t\tfiltered.SetControllerConfigurationNum(player, career->GetControllerConfigurationNum(player));\n", "", "bonus"),
            ("lost-screen-x", "bonus", "\tfiltered.SetScreenPosX(career->GetScreenPosX());\n", "", "bonus"),
            ("lost-screen-y", "bonus", "\tfiltered.SetScreenPosY(career->GetScreenPosY());\n", "", "bonus"),
            ("lost-binding", "save", "if (binding->mConfigurable)", "if (false)", "bonus"),
            ("lost-options", "save", "\tWriteOptions(dest);", "\t(void)dest;", "bonus"),
        ]
        for label, part, old, new, mode in mutants:
            with self.subTest(mutant=label):
                text = self.save if part == "save" else source
                self.assertEqual(text.count(old), 1)
                changed = text.replace(old, new)
                changed_bonus = BONUS
                save = self.save
                if part == "save":
                    save = changed
                else:
                    changed_bonus = self.out / (label + ".cpp")
                    changed_bonus.write_text(changed)
                binary = self.build(label, save, changed_bonus)
                result = self.run_case(binary, mode)
                self.assertNotEqual(result.returncode, 0, f"{label} survived")
                self.assertIn("FAIL:", result.stderr)

        # Moving the filter after patch 0052's restoration leaks the live god flags into serialized progress.
        old = "\tPortCeBonusFilterSave(this, dest);\n"
        after = "\tSetIsGod(1, portGod1);\n"
        self.assertEqual(self.save.count(old), 1)
        self.assertEqual(self.save.count(after), 1)
        changed = self.save.replace(old, "").replace(after, after + old)
        result = self.run_case(self.build("god-order", changed, BONUS), "bonus")
        self.assertNotEqual(result.returncode, 0, "wrong god order survived")
        self.assertIn("FAIL:", result.stderr)

        # Inject failure at the one snapshot-allocation boundary, then test refusal and the ordinary save path.
        old = "malloc(sizeof(CCareer))"
        self.assertEqual(source.count(old), 1)
        failed_alloc = self.out / "failed-allocation.cpp"
        failed_alloc.write_text(source.replace(old, "NULL"))
        self.passed(self.build("failed-allocation", self.save, failed_alloc), "allocation-failed")


if __name__ == "__main__":
    unittest.main(verbosity=2)
