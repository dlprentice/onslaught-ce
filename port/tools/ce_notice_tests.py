#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CE save error notice (port/game/ce_notice.cpp, patch 0137).

The retail PC code saves the career and the options file at two places without looking at the result: the pause menu's
Continue (CPauseMenu::Resume) and the language change (CFEPMain::Process). Patch 0137 asks for the result there and, where the CE layer
is on, tells the player with the front end's own save error line: a note at the bottom of the screen in a level, the save page's
message box in the front end.

- The patch: where its hooks sit in the patched PauseMenu.cpp, FEPMain.cpp, FEPOptions.cpp, FrontEndText.cpp and DXEngine.cpp, that
  the writes are still made in the retail order with the retail conditions (the second only when the first succeeded), and that the
  faithful build's view of those files (CE_VERSION undefined) is unchanged by it.
- The module (ce_notice_driver.cpp, the console's authored fakes): the options failure flag, the line and who may get it, the note
  and its clock, place, size and character set, the frames it stays out of, and nothing at all where the CE layer is off.
Causal mutants of each must be caught. What the real game draws is covered by the offscreen captures, not here.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ce_palette_tests import function, no_core, SANITIZE  # noqa: E402
from ce_photo_tests import patched, faithful  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
PATCH_NAME = "0137-ce-save-failed-notice.patch"
NOTICE = ROOT / "port/game/ce_notice.cpp"
NOTICE_H = ROOT / "port/game/ce_notice.h"
PHOTO_H = ROOT / "port/game/ce_photo.h"
FAKE = ROOT / "port/tools/ce_palette_fake.h"
DRIVER = ROOT / "port/tools/tests/ce_notice_driver.cpp"
FILES = ("PauseMenu.cpp", "FEPMain.cpp", "FEPOptions.cpp", "FrontEndText.cpp", "DXEngine.cpp")
FLAGS = ("BEA_PORT_CE_LAYER", "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD")

# The writes of the two quiet sites, as the CE build compiles them: the retail calls in the retail order, the second only when the first
# succeeded (a short-circuit), their results kept; the options failure cleared before and asked after the options write.
WRITES = ("\t\t\tsaveFailed = (MEMORYCARD.CreateSave(FRONTEND.GetMemoryCardNumber(), FRONTEND.mFEPSaveGame.mSaveGameName, &save, TRUE) != MCE_SUCCESS) ||\n"
          "\t\t\t\t\t\t (MEMORYCARD.WriteSave(FRONTEND.GetMemoryCardNumber(), save, FRONTEND.mFEPSaveGame.mSaveGameName, buffer, size) != MCE_SUCCESS);\n"
          "#else\n"
          "\t\t\tif (MEMORYCARD.CreateSave(FRONTEND.GetMemoryCardNumber(), FRONTEND.mFEPSaveGame.mSaveGameName, &save, TRUE) == MCE_SUCCESS)\n"
          "\t\t\t\tMEMORYCARD.WriteSave(FRONTEND.GetMemoryCardNumber(), save, FRONTEND.mFEPSaveGame.mSaveGameName, buffer, size);\n"
          "#endif\n\t\t}\n\n\t\tCFEPOptions::WriteDefaultOptionsFile(buffer, size);\n")


def check_structure(files):
    """Where patch 0137 puts the notice in the patched game files (raises AssertionError)."""
    pause = function(files["PauseMenu.cpp"], "void\tCPauseMenu::Resume(")
    assert "\t\tCAREER.Save(buffer);\n#ifdef CE_VERSION\n\t\tbool\tsaveFailed = false;" in pause, "the pause menu keeps its result"
    assert "\t\t\tint save;\n#ifdef CE_VERSION\n" + WRITES in pause, "the pause menu's writes are the retail ones in the retail order"
    assert "\t\tPortCeTakeOptionsFailure();\n#endif\n\n\t\tif (FRONTEND.mFEPSaveGame.mSaveGameName[0])" in pause, "a stale options failure is cleared before the writes"
    assert "WriteDefaultOptionsFile(buffer, size);\n#ifdef CE_VERSION\n\t\tif (PortCeTakeOptionsFailure() || saveFailed)\n\t\t\tPortCeSaveFailedNotice();\n#endif\n\t\tdelete [] buffer;" in pause, \
        "the pause menu tells the player after the options write, before the buffer goes"
    assert pause.index("PortCeSaveFailedNotice();") < pause.index("DeActivate(TRUE)"), "and before the menu is gone"
    main = function(files["FEPMain.cpp"], "if ((FEMESSBOX.BeingDisplayed()) && (FEMESSBOX.GetQuestion() == FEMB_Q_LANGUAGE)")
    assert "\t\tCAREER.Save(buffer);\n#ifdef CE_VERSION\n\t\tbool\tsaveFailed = false;" in main, "the language change keeps its result"
    assert "\t\tPortCeTakeOptionsFailure();\n#endif\n\n\t\tif (FRONTEND.mFEPSaveGame.mSaveGameName[0])" in main, "and clears a stale options failure first"
    assert "\t\t\tint save;\n#ifdef CE_VERSION\n" + WRITES in main, "the language change's writes are the retail ones in the retail order"
    assert ("WriteDefaultOptionsFile(buffer, size);\n#ifdef CE_VERSION\n\t\tif (PortCeTakeOptionsFailure() || saveFailed)\n\t\t{\n"
            "\t\t\t// The save page's generic error box (CFEPSaveGame::Process), in place of the language box that is showing.\n"
            "\t\t\tWCHAR\tmsg[256];\n\t\t\tif (PortCeSaveErrorMessage(msg, 256))\n\t\t\t{\n\t\t\t\tFEMESSBOX.Kill();\n"
            "\t\t\t\tFEMESSBOX.Create(320.f, 240.f, 400, FO(100), msg, PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, FALSE);\n"
            "\t\t\t}\n\t\t}\n#endif\n\t\tdelete [] buffer;") in main, "the language change replaces its box with the save page's error box (Continue, no question)"
    assert main.index("FEMESSBOX.Create(320.f, 240.f, 400") < main.index("MUSIC.PlaySelection(MUS_FRONTEND)"), "before the music goes back"
    options = function(files["FEPOptions.cpp"], "void\tCFEPOptions::WriteDefaultOptionsFile(")
    assert re.search(r'AddMessage\("Couldn\'t write defaultoptions"\);\n#ifdef CE_VERSION\n\t\tPortCeOptionsWriteFailed\(\);[^\n]*\n#endif\n\t\}', options), \
        "the options write reports its failure where it already logs it"
    text = files["FrontEndText.cpp"]
    helper = function(text, "WCHAR\t*PortCeSaveErrorText()")
    assert "TEXT_DB.GetString(FETX_SAVEERROR)" in helper and "GetText" not in helper, "the line comes without GetText's test of the X key"
    assert text.index("PortCeSaveErrorText") > text.index("void CFrontEndText::Shutdown()"), "after the retail functions"
    assert "#ifdef CE_VERSION\n\n// Patch 0137" in text, "only in CE builds"
    post = function(files["DXEngine.cpp"], "HRESULT\tCDXEngine::PostRender(")
    mirror = post[post.index("if (!gMirrorPass)\t\t// retail PC (0053efc4)"):]
    assert re.search(r"GAMEINTERFACE\.Render\(\);\n#ifdef CE_VERSION\n\t\tPortCePaletteRender\(\);\n\t\tPortCePhotoRender\(\);\n\t\tPortCeNoticeRender\(\);\n#endif", mirror), \
        "the notice is drawn after photo mode's note, outside the mirror pass"



class NoticeTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "local-data/port/ce-notice-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    # ---- the patch --------------------------------------------------------------------------------------------------------
    def test_patch_places_and_faithful_view(self):
        files = patched(self.out / "with", FILES)
        check_structure(files)
        without = patched(self.out / "without", FILES, skip=(PATCH_NAME,))
        for name in FILES:
            with self.subTest(file=name):
                self.assertNotEqual(files[name], without[name])
                self.assertEqual(faithful(files[name]), faithful(without[name]), f"patch 0137 changes the faithful {name}")

    def test_structure_mutants_are_caught(self):
        files = patched(self.out / "with", FILES)
        mutants = [
            ("pause-and-not-short-circuit", "PauseMenu.cpp", "!= MCE_SUCCESS) ||\n\t\t\t\t\t\t (MEMORYCARD.WriteSave", "!= MCE_SUCCESS) |\n\t\t\t\t\t\t (MEMORYCARD.WriteSave"),
            ("pause-write-without-create", "PauseMenu.cpp", "saveFailed = (MEMORYCARD.CreateSave(FRONTEND.GetMemoryCardNumber(), FRONTEND.mFEPSaveGame.mSaveGameName, &save, TRUE) != MCE_SUCCESS) ||",
             "saveFailed = (MEMORYCARD.CreateSave(FRONTEND.GetMemoryCardNumber(), FRONTEND.mFEPSaveGame.mSaveGameName, &save, TRUE) == MCE_SUCCESS) ||"),
            ("pause-no-stale-clear", "PauseMenu.cpp", "\t\tbool\tsaveFailed = false;\t\t// patch 0137: the retail PC code ignores a failed save here; CE tells the player\n\t\tPortCeTakeOptionsFailure();\n",
             "\t\tbool\tsaveFailed = false;\t\t// patch 0137: the retail PC code ignores a failed save here; CE tells the player\n"),
            ("pause-ignores-options", "PauseMenu.cpp", "if (PortCeTakeOptionsFailure() || saveFailed)\n\t\t\tPortCeSaveFailedNotice();", "if (saveFailed)\n\t\t\tPortCeSaveFailedNotice();"),
            ("pause-ignores-careers", "PauseMenu.cpp", "if (PortCeTakeOptionsFailure() || saveFailed)\n\t\t\tPortCeSaveFailedNotice();", "if (PortCeTakeOptionsFailure())\n\t\t\tPortCeSaveFailedNotice();"),
            ("pause-notice-after-delete", "PauseMenu.cpp", "\t\tif (PortCeTakeOptionsFailure() || saveFailed)\n\t\t\tPortCeSaveFailedNotice();\n#endif\n\t\tdelete [] buffer;",
             "\t\tdelete [] buffer;\n#endif\n#ifdef CE_VERSION\n\t\tif (PortCeTakeOptionsFailure() || saveFailed)\n\t\t\tPortCeSaveFailedNotice();\n"),
            ("language-no-kill", "FEPMain.cpp", "\t\t\t\tFEMESSBOX.Kill();\n\t\t\t\tFEMESSBOX.Create(320.f", "\t\t\t\tFEMESSBOX.Create(320.f"),
            ("language-box-without-continue", "FEPMain.cpp", "FEMB_OPTION_A_CONTINUE, FALSE);", "FEMB_OPTION_NONE, FEMB_Q_LANGUAGE);"),
            ("language-ignores-careers", "FEPMain.cpp", "if (PortCeTakeOptionsFailure() || saveFailed)\n\t\t{", "if (PortCeTakeOptionsFailure())\n\t\t{"),
            ("language-box-before-writes", "FEPMain.cpp", "WriteDefaultOptionsFile(buffer, size);\n#ifdef CE_VERSION\n\t\tif (PortCeTakeOptionsFailure() || saveFailed)",
             "WriteDefaultOptionsFile(buffer, size);\n#endif\n#ifdef CE_VERSION\n\t\tif (false && (PortCeTakeOptionsFailure() || saveFailed))"),
            ("options-failure-unreported", "FEPOptions.cpp", "\t\tPortCeOptionsWriteFailed();\t// patch 0137: the callers that tell the player ask whether it happened\n", ""),
            ("options-failure-on-success", "FEPOptions.cpp", "#ifdef CE_VERSION\n\t\tPortCeOptionsWriteFailed();\t// patch 0137: the callers that tell the player ask whether it happened\n#endif\n\t}",
             "\t}\n#ifdef CE_VERSION\n\tPortCeOptionsWriteFailed();\n#endif"),
            ("text-through-gettext", "FrontEndText.cpp", "return TEXT_DB.GetString(FETX_SAVEERROR);\n}\n#endif", "return GetText(FEX_SAVEERROR);\n}\n#endif"),
            ("text-in-faithful-build", "FrontEndText.cpp", "\n#ifdef CE_VERSION\n\n// Patch 0137", "\n#if 1\n\n// Patch 0137"),
            ("note-under-photo", "DXEngine.cpp", "\t\tPortCePhotoRender();\n\t\tPortCeNoticeRender();", "\t\tPortCeNoticeRender();\n\t\tPortCePhotoRender();"),
            ("note-in-mirror-pass", "DXEngine.cpp", "\t\tPortCePhotoRender();\n\t\tPortCeNoticeRender();\n#endif\n\t}", "\t\tPortCePhotoRender();\n#endif\n\t}\n\tPortCeNoticeRender();"),
            ("note-not-drawn", "DXEngine.cpp", "\t\tPortCeNoticeRender();\n", ""),
        ]
        for name, where, old, new in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(files[where].count(old), 1, name)
                broken = dict(files)
                broken[where] = files[where].replace(old, new)
                with self.assertRaises(AssertionError, msg=f"mutant {name} survived"):
                    check_structure(broken)

    # ---- the module -------------------------------------------------------------------------------------------------------
    def logic(self, name, notice=None, ce=True, must_build=True):
        folder = self.out / name
        folder.mkdir(parents=True)
        (folder / "ce_layer.inc").write_text(function((ROOT / "port/platform/common/ce.cpp").read_text(), "bool PortCeLayer()"))
        (folder / "ce_notice.cpp").write_text(notice if notice is not None else NOTICE.read_text())
        shutil.copy(NOTICE_H, folder)
        shutil.copy(PHOTO_H, folder)
        shutil.copy(FAKE, folder)
        for header in ("common.h", "game.h", "frontend.h", "CLIParams.h", "ltshell.h", "Hud.h", "SpriteRenderer.h", "renderinfo.h", "Texture.h"):
            (folder / header).write_text('#include "ce_palette_fake.h"\n')
        exe = folder / "driver"
        built = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-g", "-Wall", "-Wextra", "-Werror", *SANITIZE,
                                *(["-DCE_VERSION=1"] if ce else []), "-I", str(folder), str(DRIVER), str(folder / "ce_notice.cpp"), "-o", str(exe)],
                               capture_output=True, text=True, timeout=120, preexec_fn=no_core)
        if not must_build:
            return built
        self.assertEqual(built.returncode, 0, built.stderr)
        return exe

    def run_logic(self, exe, mode="ordinary", env=None):
        e = {k: v for k, v in os.environ.items() if k not in FLAGS}
        e.update(env or {})
        e.update(ASAN_OPTIONS="detect_leaks=0:abort_on_error=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        return subprocess.run([str(exe), mode], env=e, capture_output=True, text=True, timeout=30, preexec_fn=no_core)

    def test_notice(self):
        exe = self.logic("actual")
        ran = self.run_logic(exe)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("save error notice passed", ran.stdout)
        for flag in FLAGS[1:]:
            with self.subTest(flag=flag):
                ran = self.run_logic(exe, "denied", {flag: "1"})
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                self.assertIn("denied passed", ran.stdout)
        ran = self.run_logic(exe, "denied", {"BEA_PORT_CE_LAYER": "0"})
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        # A tape that forces the layer on (screenshots of the additions) gets the notice, as for every CE addition.
        ran = self.run_logic(exe, "ordinary", {"BEA_PORT_TAPE": "x", "BEA_PORT_CE_LAYER": "1"})
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        # Without CE_VERSION the module compiles to nothing (the faithful build).
        plain = self.logic("faithful", ce=False, must_build=False)
        self.assertNotEqual(plain.returncode, 0, "the faithful build has no notice")
        self.assertIn("PortCeNoticeRender", plain.stderr, "the module compiles to nothing without CE_VERSION")

    def test_notice_mutants_are_caught(self):
        source = NOTICE.read_text()
        mutants = [
            ("no-layer-check", "\tif (!PortCeLayer()) return false;\n", "", "denied"),
            ("take-keeps-flag", "\tconst bool failed = optionsFailed;\n\toptionsFailed = false;\n\treturn failed;", "\tconst bool failed = optionsFailed;\n\treturn failed;", "ordinary"),
            ("failure-not-recorded", "void PortCeOptionsWriteFailed() { optionsFailed = true; }", "void PortCeOptionsWriteFailed() { }", "ordinary"),
            ("never-expires", "CE_ScreenshotTicks() >= noteUntil", "false", "ordinary"),
            ("expires-early", "const unsigned long long kShownMs = 6000;", "const unsigned long long kShownMs = 5000;", "ordinary"),
            ("clock-not-reset", "\tnoteUntil = CE_ScreenshotTicks() + kShownMs;", "\tif (!noteUntil) noteUntil = CE_ScreenshotTicks() + kShownMs;", "ordinary"),
            ("note-in-saved-frame", "|| PortCePhotoSavedNext()) return;", ") return;", "ordinary"),
            ("no-hyphens", "text[n++] = font->CanDisplay(*c) ? *c : L'-';", "text[n++] = *c;", "ordinary"),
            ("no-fit", "if (float(extent.cx) * textScale > w - 32.f * scale && extent.cx > 0) textScale = (w - 32.f * scale) / float(extent.cx);", "", "ordinary"),
            ("off-centre", "x = (w - tw) * 0.5f", "x = (w - tw) * 0.25f", "ordinary"),
            ("copy-unterminated", "\tout[i] = 0;\n\treturn true;", "\treturn true;", "ordinary"),
            ("copy-overruns", "for (; text[i] && i < n - 1; ++i)", "for (; text[i] && i < n; ++i)", "ordinary"),
            ("empty-line-accepted", "if (!text || !text[0]) return false;", "if (!text) return false;", "ordinary"),
            ("no-font-check", "if (!font || !white ||", "if (!white ||", "ordinary"),
            ("no-window-check", "\tif (w <= 0.f || h <= 0.f) return;\n", "", "ordinary"),
        ]
        for name, old, new, mode in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, name)
                exe = self.logic("mutant-" + name, notice=source.replace(old, new))
                ran = self.run_logic(exe, mode, {"BEA_PORT_CLOCK": "1"} if mode == "denied" else None)
                self.assertNotEqual(ran.returncode, 0, f"mutant {name} survived:\n{ran.stdout}{ran.stderr}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
