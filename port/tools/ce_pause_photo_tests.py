#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the pause menu's Photo Mode item (patch 0138) as the port patches leave PauseMenu.cpp: the build's mirror of
src/PauseMenu.cpp with every port patch's section for it applied in the build's order, each at fuzz 0, against authored
fakes of the menu, the game and the photo module; no game, display or retail data.

What it holds: the constructor puts the item between the options pages and Retry in the pause menu and nowhere else (not on
the options page, not when the CE menus are off); choosing it leaves the menu the way Continue does (control handed back,
game unpaused, mouse restored, menu closed) but writes nothing (no career, no options), switches photo mode as F11 does
(on when it is off, off when it is on), and plays the back sound; the other pause menu items are untouched; the patch only
adds lines; and the item's PC-text id is the one ce_frontend.cpp gives its item. Causal mutants must each be caught.
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

PATCH = ROOT / "port/patches/0138-ce-pause-photo-mode.patch"

DRIVER = r"""// SPDX-License-Identifier: GPL-3.0-only
// Authored boundaries for CPauseMenu's list and ButtonPressed (the fragments are the build's patched PauseMenu.cpp text).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>
typedef int BOOL;
typedef unsigned DWORD;
typedef int EPCText;
const int TRUE = 1, FALSE = 0;
enum { GI_CONTINUE = 1001, GI_MESSAGE_LOG, GI_BRIEFING, GI_RETRY, GI_QUIT, JCL_CREDITS, GI_GOD_OFF };
enum EMemoryType { MEMTYPE_FAKE = 0x80 };
void *operator new(size_t size, EMemoryType) { return ::operator new(size); }
void operator delete(void *p, EMemoryType) noexcept { ::operator delete(p); }
#define MEMTYPE_MENUITEM ((EMemoryType)0x80)

struct CMenuItem
{
	DWORD mText;
	int mId;
	EPCText mPCText;
	CMenuItem(DWORD text, int id) : mText(text), mId(id), mPCText(0) {}
	CMenuItem(EPCText pc, int id) : mText(0), mId(id), mPCText(pc) {}
};

#include "decls.inc"

static std::string calls;
static bool menus = true, photo_on = false;
bool PortCeMenus() { return menus; }
bool PortCePhotoOn() { return photo_on; }
bool PortCePhotoMenuSet(bool on) { calls += on ? "Photo(1) " : "Photo(0) "; photo_on = on; return true; }
bool PortCeTakeOptionsFailure() { calls += "TakeOptionsFailure "; return false; }
void PortCeSaveFailedNotice() { calls += "SaveFailedNotice "; }
CMenuItem *PortCePhotoItem() { return new CMenuItem((EPCText)PORT_CE_PHOTO_PCTEXT, 3); }
void InitMouse() { calls += "InitMouse "; }

#include "pctext.inc"

struct CMenuOptionsList
{
	std::vector<CMenuItem *> items;
	void AddItem(CMenuItem *item) { items.push_back(item); }
};

static std::string Describe(const CMenuItem *item)
{
	switch (item->mText)
	{
	case GI_CONTINUE: return "Continue";
	case GI_MESSAGE_LOG: return "MessageLog";
	case GI_BRIEFING: return "Briefing";
	case GI_RETRY: return "Retry";
	case GI_QUIT: return "Quit";
	case JCL_CREDITS: return "Credits";
	}
	if (item->mPCText == PORT_CE_PHOTO_PCTEXT) return "PhotoMode";
	return "pc" + std::to_string(item->mPCText);
}

static std::string Build(bool options_page)
{
	CMenuOptionsList *list = new CMenuOptionsList;
	{
#include "build.inc"
	}
	std::string out;
	for (CMenuItem *item : list->items)
	{
		out += (out.empty() ? "" : ",") + Describe(item);
		delete item;
	}
	delete list;
	return out;
}

struct Lists
{
	struct One { void Reset() { calls += "Reset "; } } one;
	One *At(int) { return &one; }
};
struct Game { void UnPause() { calls += "UnPause "; } } GAME;

struct CPauseMenu
{
	int mPage = 0;
	Lists mLists;
	void RelinquishControl() { calls += "Relinquish "; }
	void DeActivate(BOOL a) { calls += a ? "DeActivate(1) " : "DeActivate(0) "; }
	void Resume(void *) { calls += "Resume "; }
	bool sound = false;		// the back sound the press asked for (a press that returns early asks for none)
	void Press(CMenuItem *item, void *from_controller)
	{
		BOOL back_sound = FALSE;
		(void)from_controller;
		sound = false;
		switch (item->mText)
		{
		default:
			break;
#include "case0.inc"
		}
		sound = back_sound != FALSE;
	}
};

static void check(bool ok, const char *message)
{
	if (!ok)
	{
		std::fprintf(stderr, "FAIL %s\n", message);
		std::exit(2);
	}
}

int main()
{
	menus = true;
	check(Build(false) == "Continue,MessageLog,Briefing,pc3,pc1,pc2,PhotoMode,Retry,Quit",
	      "the pause menu has the Photo Mode row between the options pages and Retry");
	check(Build(true) == "pc3,pc1,pc2,Credits", "the options page has no Photo Mode row");
	menus = false;
	check(Build(false) == "Continue,MessageLog,Briefing,pc3,pc1,pc2,Retry,Quit", "with the CE menus off the pause menu is the original's");
	check(Build(true) == "pc3,pc1,pc2,Credits", "with the CE menus off the options page is the original's");
	menus = true;

	CPauseMenu menu;
	CMenuItem photo((EPCText)PORT_CE_PHOTO_PCTEXT, 3);
	photo_on = false;
	calls.clear();
	menu.Press(&photo, nullptr);
	check(calls == "Relinquish UnPause InitMouse DeActivate(1) Photo(1) ", "Photo Mode leaves the menu as Continue does, then turns photo mode on");
	check(menu.sound && photo_on, "and plays the back sound");
	check(calls.find("Resume") == std::string::npos && calls.find("TakeOptionsFailure") == std::string::npos,
	      "choosing Photo Mode writes nothing (no career and no options)");
	calls.clear();
	menu.Press(&photo, nullptr);
	check(calls == "Relinquish UnPause InitMouse DeActivate(1) Photo(0) " && !photo_on, "End Photo Mode turns photo mode off");
	check(menu.sound, "with the back sound");

	// the other PC-text items of the pause menu are as before: each moves to its page, 78 and 79 do nothing
	const struct { int pc, step; } pages[] = {{2, 2}, {1, 3}, {3, 1}};
	for (auto &p : pages)
	{
		CMenuItem item((EPCText)p.pc, 3);
		menu.mPage = 0;
		calls.clear();
		menu.Press(&item, nullptr);
		check(menu.mPage == p.step && calls == "Reset " && !menu.sound, "the options items still open their pages");
	}
	for (int pc : {78, 79})
	{
		CMenuItem item((EPCText)pc, 3);
		menu.mPage = 0;
		calls.clear();
		menu.Press(&item, nullptr);
		check(menu.mPage == 0 && calls.empty() && !menu.sound, "items 78 and 79 do nothing");
	}
	std::puts("ok");
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


def patched(out, name, patch_text=None, stop_before=None):
    """The mirror's NAME with every port patch's NAME section applied in the build's order, at fuzz 0."""
    (out / name).write_bytes(mirror_text(name))
    for patch in sorted((ROOT / "port/patches").glob("*.patch")):
        if stop_before and patch.name >= stop_before:
            break
        text = patch_text if patch == PATCH and patch_text is not None else patch.read_text(encoding="latin1")
        for m in re.finditer(r"(?ms)^--- a/" + re.escape(name) + r"\n\+\+\+ b/" + re.escape(name) + r"\n.*?(?=^--- a/|\Z)", text):
            (out / "section.patch").write_text(m.group(0), encoding="latin1")
            r = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", "section.patch"], cwd=out,
                               capture_output=True, text=True, timeout=10, preexec_fn=no_core)
            if r.returncode or "fuzz" in r.stdout.lower():
                raise AssertionError(f"{patch.name}: {r.stdout}{r.stderr}")
    return (out / name).read_text(encoding="latin1")


def fragments(source):
    """The pieces of the patched PauseMenu.cpp the driver compiles."""
    decls = re.search(r"#ifdef CE_VERSION\nbool PortCeTakeOptionsFailure\(\);.*?#define PORT_CE_PHOTO_PCTEXT[^\n]*\n#endif\n", source, re.S)
    pctext = re.search(r"static inline int PauseItemPCText\(const CMenuItem \*item\)\n\{.*?\n\}\n", source, re.S)
    build_start = source.index("\tif (!options_page)\n\t{\n#line 1341")
    build_end = source.index("\tmLists.Append(list);", build_start)
    case_start = source.index("\tcase 0:\n\t\tswitch (PauseItemPCText(item))")
    case_end = source.index("\tcase GI_GOD_OFF:", case_start)
    return {"decls.inc": decls.group(0), "pctext.inc": pctext.group(0), "build.inc": source[build_start:build_end],
            "case0.inc": source[case_start:case_end]}


def pctext_value(text):
    return int(re.search(r"#define PORT_CE_PHOTO_PCTEXT\s+(0x[0-9a-fA-F]+|\d+)", text).group(1), 0)


class PausePhotoTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "local-data/port/ce-pause-photo-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def patched_source(self, name):
        folder = self.out / name
        folder.mkdir()
        return folder, patched(folder, "PauseMenu.cpp")

    def run_driver(self, name, mutate=()):
        folder, source = self.patched_source(name)
        parts = fragments(source)
        for fragment, old, new in mutate:
            self.assertEqual(parts[fragment].count(old), 1, f"{name}: {fragment}: {old!r}")
            parts[fragment] = parts[fragment].replace(old, new)
        for part, text in parts.items():
            (folder / part).write_text(text, encoding="latin1")
        (folder / "driver.cpp").write_text(DRIVER)
        exe = folder / "driver"
        r = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", '-DCE_VERSION="test"',
                            "-Wno-unused-function", "-Wno-implicit-fallthrough", "-I", str(folder), str(folder / "driver.cpp"),
                            "-o", str(exe)], capture_output=True, text=True, timeout=120, preexec_fn=no_core)
        self.assertEqual(r.returncode, 0, f"{name}: {r.stderr}")
        return subprocess.run([str(exe)], capture_output=True, text=True, timeout=10, preexec_fn=no_core)

    def test_the_patch_applies_and_only_adds_lines(self):
        text = PATCH.read_text()
        removed = [line for line in text.splitlines() if line.startswith("-") and not line.startswith("---")]
        self.assertEqual(removed, [], "patch 0138 only adds lines, so the original text stays whole")
        self.assertEqual(re.findall(r"^\+\+\+ b/(\S+)", text, re.M), ["PauseMenu.cpp"])
        added = [line[1:] for line in text.splitlines() if line.startswith("+") and not line.startswith("+++")]
        self.assertEqual(added.count("#ifdef CE_VERSION"), added.count("#endif"), "every addition is under CE_VERSION")
        # the series applies in the build's order at fuzz 0 (patched() raises otherwise), 0138 after 0137's hunk
        folder, source = self.patched_source("series")
        self.assertIn("PortCeSaveFailedNotice();", source)
        self.assertLess(source.index("PortCeSaveFailedNotice();"), source.index("PortCePhotoItem();"))

    def test_the_item_id_is_the_one_ce_frontend_gives_it(self):
        folder, source = self.patched_source("ids")
        frontend = (ROOT / "port/game/ce_frontend.cpp").read_text()
        self.assertEqual(pctext_value(fragments(source)["decls.inc"]), pctext_value(frontend))
        self.assertNotEqual(pctext_value(fragments(source)["decls.inc"].replace("0x4346", "0x4347")), pctext_value(frontend))
        # no other item of the game uses it: the PC text ids are small (Localization_GetStringById), the CE ids are 0x4345 and up
        self.assertGreater(pctext_value(frontend), 0x1000)
        case = fragments(source)["case0.inc"]
        for writes in ("CAREER", "Resume(", "WriteDefaultOptionsFile", "MEMORYCARD", "Save("):
            self.assertNotIn(writes, case, "choosing Photo Mode writes nothing")

    def test_the_pause_menu_is_drawn_with_the_front_end_flag_set(self):
        # Why the Photo Mode item may not ask whether the front end is running (a first build did, and the row vanished from the
        # menu in a level, while still being selectable): the retail PC draws the pause menu between gInFrontEnd = true and false.
        engine = (ROOT / "src/DXEngine.cpp").read_text()
        drawn = engine[engine.index("gInFrontEnd = true;\t\t// retail PC (0053ee07)"):]
        drawn = drawn[:drawn.index("gInFrontEnd = false;")]
        self.assertIn("GAME.GetPauseMenu()->Render()", drawn)
        photo = (ROOT / "port/game/ce_photo.cpp").read_text()
        offered = photo[photo.index("bool PortCePhotoAvailable()"):]
        offered = offered[:offered.index("\n")]
        self.assertNotIn("IsRunning", offered)
        self.assertNotIn("Allowed()", offered)
        level = photo[photo.index("bool AllowedInLevel()"):photo.index("bool Allowed()")]
        self.assertNotIn("IsRunning", level)
        self.assertIn("!FRONTEND.IsRunning() && AllowedInLevel()", photo)

    def test_every_route(self):
        r = self.run_driver("base")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("ok", r.stdout)

    def test_causal_mutants_are_caught(self):
        add = "list->AddItem(PortCePhotoItem());"
        guard = "if (PortCeMenus())\n\t\t\t" + add
        credits = "list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)JCL_CREDITS, 1));"
        quit_ = "list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_QUIT, 2));"
        mutants = (
            ("drop-item", [("build.inc", add, "(void)0;")], "the Photo Mode row between"),
            ("item-without-guard", [("build.inc", guard, add)], "CE menus off the pause menu"),
            ("item-on-options-page", [("build.inc", credits, credits + "\n\t\t" + add)], "options page has no Photo Mode row"),
            ("item-after-quit", [("build.inc", guard, "(void)0;"), ("build.inc", quit_, quit_ + "\n\t\tif (PortCeMenus())\n\t\t\t" + add)],
             "the Photo Mode row between"),
            ("case-missing", [("case0.inc", "case PORT_CE_PHOTO_PCTEXT:", "case PORT_CE_PHOTO_PCTEXT + 1:")], "leaves the menu as Continue does"),
            ("no-relinquish", [("case0.inc", "\t\t\tRelinquishControl();\n", "")], "leaves the menu as Continue does"),
            ("no-unpause", [("case0.inc", "\t\t\tGAME.UnPause();\n", "")], "leaves the menu as Continue does"),
            ("no-mouse", [("case0.inc", "\t\t\tInitMouse();\n", "")], "leaves the menu as Continue does"),
            ("no-close", [("case0.inc", "\t\t\tDeActivate(TRUE);\n", "")], "leaves the menu as Continue does"),
            ("always-on", [("case0.inc", "PortCePhotoMenuSet(!PortCePhotoOn());", "PortCePhotoMenuSet(true);")], "End Photo Mode turns photo mode off"),
            ("always-off", [("case0.inc", "PortCePhotoMenuSet(!PortCePhotoOn());", "PortCePhotoMenuSet(false);")], "leaves the menu as Continue does"),
            ("writes-the-career", [("case0.inc", "\t\t\tRelinquishControl();\n\t\t\tGAME.UnPause();\n\t\t\tInitMouse();\n\t\t\tDeActivate(TRUE);\n",
                                    "\t\t\tResume(from_controller);\n")], "leaves the menu as Continue does"),
            ("no-sound", [("case0.inc", "\t\t\tback_sound = TRUE;\n", "")], "and plays the back sound"),
            ("pages-lost", [("case0.inc", "\t\t\tmPage += 2;", "\t\t\tmPage += 1;")], "options items still open their pages"),
        )
        for name, mutate, message in mutants:
            with self.subTest(mutant=name):
                r = self.run_driver(name, mutate)
                self.assertNotEqual(r.returncode, 0, f"mutant {name} survived")
                self.assertIn(message, r.stderr, f"mutant {name}: {r.stderr}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
