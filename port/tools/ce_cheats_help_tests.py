#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check CE cheat guidance against the retail callers and its real text layout; no game or display."""
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FRONTEND = ROOT / "port/game/ce_frontend.cpp"
DRIVER = ROOT / "port/tools/ce_cheats_help_driver.cpp"


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end]


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class CheatHelpTests(unittest.TestCase):
    def test_camera_help_matches_the_actual_fixed_bindings(self):
        controls = (ROOT / "src/PCController.cpp").read_text()
        expected = {
            "BUTTON_TOGGLE_FREE_CAMERA": ("KEY_ONCE", "DIK_F"),
            "BUTTON_CAMERA_MOVE_FORAWRD": ("KEY_ON", "DIK_W"),
            "BUTTON_CAMERA_MOVE_BACKWARDS": ("KEY_ON", "DIK_S"),
            "BUTTON_CAMERA_STRAFE_LEFT": ("KEY_ON", "DIK_A"),
            "BUTTON_CAMERA_STRAFE_RIGHT": ("KEY_ON", "DIK_D"),
            "BUTTON_CAMERA_PITCH_UP": ("KEY_ON", "DIK_UP"),
            "BUTTON_CAMERA_PITCH_DOWN": ("KEY_ON", "DIK_DOWN"),
            "BUTTON_CAMERA_YAW_LEFT": ("KEY_ON", "DIK_LEFT"),
            "BUTTON_CAMERA_YAW_RIGHT": ("KEY_ON", "DIK_RIGHT"),
            "BUTTON_PAUSE": ("KEY_ONCE", "DIK_ESCAPE"),
        }
        for button, (kind, key) in expected.items():
            pattern = rf"SControlBinding\(\s*0,\s*{button},\s*{kind},\s*{key},"
            self.assertRegex(controls, pattern, button)
        camera = function((ROOT / "src/Camera.cpp").read_text(),
                          "void CControllableCamera::ReceiveButtonAction(")
        for symbol in ("BUTTON_PAUSE", "GAME.IsPaused()", "GAME.UnPause()", "GAME.Pause()"):
            self.assertIn(symbol, camera)
        game = (ROOT / "src/game.cpp").read_text()
        toggle = game[game.index("case BUTTON_TOGGLE_FREE_CAMERA:"):game.index("case BUTTON_ADVANCE_ONE_FRAME:")]
        for symbol in ("IsCheatActive(4)", "ToggleFreeCameraOn(n)", "ToggleFreeCameraOff(n)"):
            self.assertIn(symbol, toggle)

    def test_goodie_hint_priority_matches_the_help(self):
        source = (ROOT / "src/FEPGoodies.cpp").read_text()
        goodies = function(source, "static EGoodieState\tget_goodie_state(")
        self.assertLess(goodies.index("if (ischeat5active)"), goodies.index("if (ischeatactive)"))
        self.assertIn("return(GS_INSTRUCTIONS)", goodies)
        self.assertIn("return(GS_OLD)", goodies)
        selection = function(source, "void\tCFEPGoodies::ButtonPressed(")
        self.assertIn("get_goodie_state(mCX, mCY) == GS_NEW || get_goodie_state(mCX, mCY) == GS_OLD", selection)
        render = function(source, "void\tCFEPGoodies::Render(")
        self.assertIn("if (which_goodie<78)", render)
        self.assertIn("goodies[which_goodie].GetMethod(body_text)", render)
        self.assertIn('wcscpy(body_text,ToWCHAR(""))', render)

    def test_actual_help_renderer_wraps_without_overlapping(self):
        frontend = FRONTEND.read_text()
        start = frontend.index("struct SCeCheat")
        end = frontend.index("\nCMenuItem\t*PortCeCheatsItem()", start)
        wrap = function((ROOT / "src/Font.cpp").read_text(), "int\t\tCDXBitmapFont::WordWrap(")
        camera = function((ROOT / "src/Camera.cpp").read_text(),
                          "void CControllableCamera::ReceiveButtonAction(")
        controls = (ROOT / "src/Controller.h").read_text()
        first = controls.index("#define BUTTON_TOGGLE_GOD_MODE")
        last = controls.index("#define BUTTON_FRONTEND_MENU_UP", first)
        pause = re.search(r"^#define\s+BUTTON_PAUSE\s+\d+", controls, re.MULTILINE)
        self.assertIsNotNone(pause)
        parent = ROOT / "local-data/port/ce-cheat-help-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="run-", dir=parent) as tmp:
            out = Path(tmp)
            (out / "cheats_help.inc").write_text(frontend[start:end])
            (out / "word_wrap.inc").write_text(wrap)
            (out / "camera_buttons.inc").write_text(controls[first:last] + "\n" + pause.group(0) + "\n")
            (out / "camera_actions.inc").write_text(camera)
            exe = out / "driver"
            cmd = [os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-parameter",
                   "-I", str(out), str(DRIVER), "-o", str(exe)]
            built = subprocess.run(cmd, capture_output=True, text=True, timeout=30, preexec_fn=no_core)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5, preexec_fn=no_core)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            self.assertIn("wrapping, spacing and message box passed", ran.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
