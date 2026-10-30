#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The pointer of the game's in-game menus (Jeppi's report of 2026-10-29: "the mouse does not work in the pause menu", and the Windows arrow shows with the game's own cursor displaced).

Retail scales the pointer to the 640 x 480 virtual screen only while the front end runs (HandleMouseMessage); the pause menu, the message and briefing logs and the end-of-level menus draw their own
cursor and test their items in that virtual screen but were handed window pixels, which is right at 640 x 480 only, and with a wide window the screen is a 4:3 box inside it. Patch 0042's mouse hunks now
ask the layer (PortUiMouseMenu) and map the position and each button's position (PortUiMouseMenuPoint); window.cpp's PortGetMouseClientPosition (the pointer a released mouse starts from) does the same.
Held here: widescreen.cpp's two functions run in a driver (with and without CE_VERSION), their mutants are caught, patch 0042 puts the mapping in front of each of the four front-end branches, and the
commander map's own open (which reads window pixels) is kept out of the mapping while ShutdownMouse seeds the pointer. The patched mouse module itself is run by input_lifetime_tests.py:
its authored button-event controls also exercise the real hit test, scaling math, focus and default replay/recording fences. Missing down/up updates must fail those controls. This is not a physical
mouse, compositor or native Windows test."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import deps  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
WIDE = ROOT / "port/platform/common/widescreen.cpp"
WINDOW = ROOT / "port/platform/common/window.cpp"
CMDMAP = ROOT / "port/game/ce_cmdmap.cpp"
PATCH = ROOT / "port/patches/0042-widescreen-virtual-ui.patch"
BUTTON_PATCH = ROOT / "port/patches/0186-ce-menu-button-pointer.patch"
INPUT_HARNESS = ROOT / "port/tools/input_lifetime_tests.py"
DRIVER = ROOT / "port/tools/tests/ce_menu_mouse_driver.cpp"


def build_and_run(widescreen_text, ce_version=True):
    """Compile the driver against a widescreen.cpp text (the target the game is built for: 32-bit, with the SDL the port uses); the driver's last line says PASS or what failed."""
    with tempfile.TemporaryDirectory(prefix="menu-mouse-", dir=deps.WORK) as directory:
        d = Path(directory)
        (d / "widescreen.cpp").write_text(widescreen_text)
        binary = d / "driver"
        command = [str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17", "-fshort-wchar", "-fms-extensions", "-w", "-O0"]
        if ce_version:
            command.append('-DCE_VERSION="test"')
        command += [f"-I{ROOT / 'port/include'}", f"-I{ROOT / 'port/include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}", f"-I{deps.PREFIX / 'include'}",
                    f"-I{ROOT / 'port/platform/common'}", str(DRIVER), str(d / "widescreen.cpp"), f"-L{deps.PREFIX / 'lib'}", "-lSDL3", f"-Wl,-rpath,{deps.PREFIX / 'lib'}", "-o", str(binary)]
        built = subprocess.run(command, capture_output=True, text=True, timeout=180)
        if built.returncode:
            raise RuntimeError(built.stdout + built.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        return ran.returncode == 0 and ran.stdout.strip().endswith("PASS"), ran.stdout


def mouse_section(patch_text):
    start = patch_text.index("--- a/mouse.cpp\n+++ b/mouse.cpp\n")
    end = patch_text.find("\n--- a/", start + 10)
    return patch_text[start:end if end >= 0 else len(patch_text)]


class MenuMouse(unittest.TestCase):
    def test_the_menus_pointer_lands_in_the_virtual_screen(self):
        ok, out = build_and_run(WIDE.read_text())
        self.assertTrue(ok, out)

    def test_a_private_build_keeps_the_windows_pixels(self):
        ok, out = build_and_run(WIDE.read_text(), ce_version=False)
        self.assertTrue(ok, out)

    def test_mutants_of_the_two_functions_are_caught(self):
        text = WIDE.read_text()
        for old, new in (
                ("return !mouseInputActive && PortCeLayer() && !PortCeCmdMapOpen() ? 1 : 0;", "return PortCeLayer() && !PortCeCmdMapOpen() ? 1 : 0;"),		# the game's own mouse look not left alone
                ("return !mouseInputActive && PortCeLayer() && !PortCeCmdMapOpen() ? 1 : 0;", "return !mouseInputActive && !PortCeCmdMapOpen() ? 1 : 0;"),		# a replay or a recording mapped too
                ("return !mouseInputActive && PortCeLayer() && !PortCeCmdMapOpen() ? 1 : 0;", "return !mouseInputActive && PortCeLayer() ? 1 : 0;"),		# the commander map's pixels mapped
                ("void PortUiMouseMenuPoint(int *x, int *y)\n{", "void PortUiMouseMenuPoint(int *x, int *y)\n{\n\t(void)PortSharpUi();"),	# unrelated sharp-text setting read
                ("\tif (gActive)\n\t{\n\t\tPortUiMouse(x, y);\n\t\treturn;\n\t}\n\tif (gW && gH)", "\tif (!gActive)\n\t{\n\t\tPortUiMouse(x, y);\n\t\treturn;\n\t}\n\tif (gW && gH)"),		# the box for a 4:3 window and none for a wide one
                ("*x = (int)((long long)*x * 640 / (long long)gW);", "*x = (int)((long long)*x * 480 / (long long)gW);"),		# a wrong scale
                ("*y = (int)((long long)*y * 480 / (long long)gH);", "*y = (int)((long long)*y * 640 / (long long)gH);")):
            self.assertEqual(text.count(old), 1, old)
            ok, out = build_and_run(text.replace(old, new))
            self.assertFalse(ok, f"the driver passes with {new!r}")
        # the box mapping itself (shared with the front end): its clamp and its scale
        old = "\t*x = vx < 0 ? 0 : vx > 639 ? 639 : vx;"
        self.assertEqual(text.count(old), 1, old)
        ok, out = build_and_run(text.replace(old, "\t*x = vx;"))
        self.assertFalse(ok, "the driver passes with the clamp removed")

    def test_patch_0042_maps_the_pointer_of_each_mouse_branch(self):
        section = mouse_section(PATCH.read_text())
        hunks = re.split(r"(?m)^(?=@@ )", section)[1:]
        self.assertEqual(len(hunks), 4, "the move and the three buttons")
        targets = []
        for h in hunks:
            m = re.search(r"\+\t+if \(!FRONTEND\.IsRunning\(\) && PortUiMouseMenu\(sMouseInputActive\)\)\n\+\t+PortUiMouseMenuPoint\(&([A-Za-z_\[\]0-9.]*), &([A-Za-z_\[\]0-9.]*)\);[^\n]*\n\+\t+else\n"
                          r"\+\t+if \(FRONTEND\.IsRunning\(\) && PortWideActive\(\)\)\n", h)
            self.assertIsNotNone(m, h[:200])
            targets.append((m.group(1), m.group(2)))
        self.assertEqual(targets, [("MOUSE_X", "MOUSE_Y")] + [(f"MOUSE_BUTTONS[{i}].mX", f"MOUSE_BUTTONS[{i}].mY") for i in (2, 0, 1)])

    def test_button_coordinates_and_each_missing_update(self):
        # Compile the actual patched handler and game hit test through the SDL/window harness. Remove each down/up
        # update in turn: the corresponding left/right/middle control must reach its assertion and fail.
        cases = ("menu-click-right", "menu-click-right", "menu-click-left", "menu-click-left",
                 "menu-click-middle", "menu-click-middle")
        command = [sys.executable, str(INPUT_HARNESS), "--optimize"]
        for case in dict.fromkeys(cases):
            command.extend(("--case", case))
        good = subprocess.run(command, capture_output=True, text=True, timeout=120)
        self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
        original = BUTTON_PATCH.read_text()
        target = "+\t\tmenuButtonPoint();"
        self.assertEqual(original.count(target), len(cases))
        parent = ROOT / "local-data/port/menu-button-mutants"
        parent.mkdir(parents=True, exist_ok=True)
        for index, case in enumerate(cases):
            with self.subTest(case=case, update=index):
                root = Path(tempfile.mkdtemp(prefix=f"button-{index}-", dir=parent))
                (root / "src").symlink_to(ROOT / "src", target_is_directory=True)
                (root / "port").mkdir()
                (root / "port/platform").symlink_to(ROOT / "port/platform", target_is_directory=True)
                patches = root / "port/patches"
                patches.mkdir()
                for patch in sorted((ROOT / "port/patches").glob("*.patch")):
                    if patch.name != BUTTON_PATCH.name:
                        (patches / patch.name).symlink_to(patch)
                pieces = original.split(target)
                for part in range(len(cases)):
                    pieces[part] += "+\t\t(void)0;" if part == index else target
                (patches / BUTTON_PATCH.name).write_text("".join(pieces))
                result = subprocess.run([sys.executable, str(INPUT_HARNESS), "--source-root", str(root),
                                         "--case", case, "--optimize"], capture_output=True, text=True, timeout=120)
                self.assertNotEqual(result.returncode, 0, f"missing update {index} survived\n" + result.stdout)
                self.assertIn("FAIL " + case, result.stdout, "mutant did not reach the selected case\n" + result.stdout + result.stderr)

    def mutant_platform_root(self, name, window_text):
        """A source root whose port/platform is the real one except for common/window.cpp (the other files are links), for the input harness's --source-root."""
        parent = ROOT / "local-data/port/menu-batch-mutants"
        parent.mkdir(parents=True, exist_ok=True)
        root = Path(tempfile.mkdtemp(prefix=f"{name}-", dir=parent))
        (root / "src").symlink_to(ROOT / "src", target_is_directory=True)
        patches = root / "port/patches"
        patches.mkdir(parents=True)
        for patch in sorted((ROOT / "port/patches").glob("*.patch")):
            (patches / patch.name).symlink_to(patch)
        platform = root / "port/platform"
        common = platform / "common"
        common.mkdir(parents=True)
        for entry in (ROOT / "port/platform").iterdir():
            if entry.name != "common":
                (platform / entry.name).symlink_to(entry)
        for entry in (ROOT / "port/platform/common").iterdir():
            if entry.name != "window.cpp":
                (common / entry.name).symlink_to(entry)
        (common / "window.cpp").write_text(window_text)
        return root

    def test_a_release_ends_the_batch_so_a_click_is_judged_where_it_was_made(self):
        # The game reads every waiting message and judges a click afterwards, at the pointer's position then: a movement behind the release in the same batch would move the click (the gap the
        # click fix alone left). The real handler and the real hit test run through the real window pump: with the CE layer's menus the click stays on its item and the movement comes with the
        # next batch; with the layer off (a setting, a tape) the movement is the last word, as in retail.
        cases = ("menu-click-late-motion", "menu-click-late-motion-off", "menu-click-late-motion-tape")
        command = [sys.executable, str(INPUT_HARNESS), "--optimize"]
        for case in cases:
            command.extend(("--case", case))
        good = subprocess.run(command, capture_output=True, text=True, timeout=120)
        self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
        window = WINDOW.read_text()
        mutants = (
            ("no-wait", "\tif (gMouseAfterReleaseWaits && IsMouseMessage(gQueue[gHead].message))", "\tif (false && IsMouseMessage(gQueue[gHead].message))", ("menu-click-late-motion",)),
            ("no-menu-gate", "\t\tif (IsButtonRelease(out->message) && PortUiMouseMenu(PortDinputOwnsMouseEvent(0) ? 1 : 0))\n\t\t\tgMouseAfterReleaseWaits = true;", "\t\tif (IsButtonRelease(out->message))\n\t\t\tgMouseAfterReleaseWaits = true;",
             ("menu-click-late-motion-off", "menu-click-late-motion-tape")),
            ("movement-lost", "\t\tif (remove)\n\t\t\tgMouseAfterReleaseWaits = false;\t\t// this batch is over: the waiting message opens the next one\n\t\treturn false;",
             "\t\tif (remove)\n\t\t\tgHead = (gHead + 1) % 256;\t\t// (mutant: the waiting message is dropped)\n\t\treturn false;", ("menu-click-late-motion",)),
        )
        for name, old, new, failing in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(window.count(old), 1, name)
                root = self.mutant_platform_root(name, window.replace(old, new))
                for case in failing:
                    result = subprocess.run([sys.executable, str(INPUT_HARNESS), "--source-root", str(root), "--case", case, "--optimize"],
                                            capture_output=True, text=True, timeout=120)
                    self.assertNotEqual(result.returncode, 0, f"mutant {name} survived {case}\n" + result.stdout)
                    self.assertIn("FAIL " + case, result.stdout, "mutant did not reach the selected case\n" + result.stdout + result.stderr)

    def test_the_released_mouse_starts_from_the_menus_point_and_the_map_keeps_window_pixels(self):
        window = WINDOW.read_text()
        start = window.index("extern \"C\" BOOL PortGetMouseClientPosition(LPPOINT p)")
        body = window[start:window.index("\n}\n", start)]
        self.assertIn("if (PortUiMouseMenu(0))\n\t\tPortUiMouseMenuPoint(&ix, &iy);", body)
        cmdmap = CMDMAP.read_text()
        self.assertIn("bool PortCeCmdMapOpen() { return gOpen || gOpening; }", cmdmap)
        self.assertIn("\tgOpening = true;\n\tShutdownMouse();", cmdmap)
        self.assertIn("\tgOpening = false;\n\tGAME.GetController(0)->SetToControl(&gSink);", cmdmap)


if __name__ == "__main__":
    unittest.main()
