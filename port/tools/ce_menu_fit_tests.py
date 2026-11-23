#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the value fit, the drop-down window and the Video Options row pitch of the options-style lists (port/game/ce_menu_fit.cpp,
patches 0212 and 0215).

The production helpers are compiled against a font that measures nine units a character and a set clock; the game's actual
patched CMenuMenuItem::RenderList and CMenuOptionsList::Render are read as text to prove the patches reach exactly the value
draw, the dropdown's entries, its window and the list's row pitch, keep the game's own arguments wherever the helpers answer
"the original's layout", and do not move a line of either file. The row positions the pitch gives Video Options are computed
from the header's constants and the original's centring. Authored mutants of the helpers and of the patched text must each be
caught."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import resource
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
SUPPORT = PORT / "tools/tests"
CASES = ("fits", "exact", "adapter", "squeeze-limit", "just-inside-limit", "no-room", "layer-off", "extent-fails", "null", "repeat",
         "list-fits", "list-cut", "list-layer-off", "list-extent-fails", "window-limits", "window-fits", "window-sizes", "window-follows",
         "window-forget", "window-edge", "window-page", "window-layer-off", "pitch")
PATCH = "0212-ce-menu-value-fit.patch"
PITCH_PATCH = "0215-ce-video-options-row-pitch.patch"
# The front end's page numbers the helper names (src/FrontEndPage.h: FEP_MAIN, FEP_OPTIONS, and FEP_LANGUAGE_TEST, the slot of
# Onslaught CE's own page), written beside the driver so that no game header is compiled.
FRONT_END_PAGE = """// Written by ce_menu_fit_tests.py: the page numbers of src/FrontEndPage.h the menu fit helper names.
#ifndef MENU_FIT_FRONTENDPAGE_H
#define MENU_FIT_FRONTENDPAGE_H
enum EFrontEndPage { FEP_MAIN = 0, FEP_OPTIONS = 17, FEP_LANGUAGE_TEST = 21 };
#endif
"""
# Every line patch 0212 changes in RenderList to lay out a list as a window, beside the line it was: on the original's path
# (PortCeListWindow answering 0) each computes what the original computed, with the original's calls. The value fit's own lines
# are held by check_patched_draw.
WINDOW_LINES = (
    ("{ extern WCHAR *PortCeValueFit(WCHAR *, float, float *); extern WCHAR *PortCeListFit(WCHAR *, float);",
     "{ extern WCHAR *PortCeValueFit(WCHAR *, float, float *); extern WCHAR *PortCeListFit(WCHAR *, float); "
     "extern int PortCeListWindow(const void *, float, long, int, int, int, float *, int *, int *); extern void PortCeListHover(int); "
     "extern float PortCeLabelLeft(int); extern float PortCeValueGap();"),
    ("\tfloat\ttx = x - size.cx;\n\tfloat\tlx = tx + size.cx + 2.f;\n\tif (tx < 5.f)\n\t{\n\t\tscale = (x - 5.f) / size.cx;\n\t\ttx = 5.f;\n\t}",
     "\tfloat\ttx = x - size.cx; const float ceLeft = PortCeLabelLeft(FRONTEND.GetCurrentPage());\n"
     "\tfloat\tlx = tx + size.cx + PortCeValueGap();\n"
     "\tif (tx < ceLeft)\n\t{\n\t\tscale = (x - ceLeft) / size.cx;\n\t\ttx = ceLeft;\n\t}"),
    ("\t\tint\t\tn = GetNumChoices();",
     "\t\tint\t\tn = GetNumChoices(); int ceFirst = 0; const int ceWindow = PortCeListWindow(this, y, size.cy, n, mSelection, "
     "FRONTEND.GetCurrentPage(), &ly, &ceFirst, &n);"),
    ("\t\t\t\t\t\t\t\t\t(widest + 3) / 16.f, (GetNumChoices() * size.cy) * yscale / 16.f, 0.f, 1.f, 0.f, 1.f);",
     "\t\t\t\t\t\t\t\t\t(widest + 3) / 16.f, ((ceWindow ? n : GetNumChoices()) * size.cy) * yscale / 16.f, 0.f, 1.f, 0.f, 1.f);"),
    ("\t\tfor (i = 0; i < n; i++)", "\t\tfor (i = ceFirst; i < ceFirst + n; i++)"),
    ("\t\t\t\tmSelection = i;\n\n\t\t\tPLATFORM.Font", "\t\t\t\t{ mSelection = i; if (ceWindow) PortCeListHover(i); }\n\n\t\t\tPLATFORM.Font"),
)
# Every line patch 0215 changes in CMenuOptionsList::Render, beside the line it was: with cePitch 0 (any list without CE's Video
# Options rows, and every list outside the ordinary CE menus) each is the original's own expression.
PITCH_LINES = (
    ("WCHAR\t*CMenuOptionsList::Render(CPauseMenu *menu)\n{\n",
     "WCHAR\t*CMenuOptionsList::Render(CPauseMenu *menu)\n{ extern int PortCeCompactPitch(const void *); extern float PortCeCompactShift(int, bool); "
     "extern int PortCeCompactActionGap(int, int, int); "
     "int cePitch = 0; { CMenuItem *ceItem; FOR_ALL_ITEMS_IN(mItems, ceItem) if (!cePitch) cePitch = PortCeCompactPitch(ceItem); }\n"),
    ("\tfloat\ty = mY;\n", "\tfloat\ty = mY + PortCeCompactShift(cePitch, options_page);\n"),
    ("\t\theight += item->GetHeight();\n", "\t\theight += cePitch ? cePitch : item->GetHeight();\n"),
    ("\theight -= 32;\n", "\theight = height - 32 + PortCeCompactActionGap(cePitch, mItems.Size() - 2, mItems.Size());\n"),
    ("\tbool\tclicked = false;\n\tFOR_ALL_ITEMS_IN(mItems, item)\n\t{\n",
     "\tbool\tclicked = false;\n\tFOR_ALL_ITEMS_IN(mItems, item)\n\t{ ty += PortCeCompactActionGap(cePitch, n, mItems.Size());\n"),
    ("if (CMenuItem::IsMouseInRect(0.f, ty, 640.f, ty + item->GetHeight()))",
     "if (CMenuItem::IsMouseInRect(0.f, ty, 640.f, ty + (cePitch ? cePitch : item->GetHeight())))"),
    ("if (CMenuItem::MouseClickedInRect(0.f, ty, 640.f, ty + item->GetHeight()))",
     "if (CMenuItem::MouseClickedInRect(0.f, ty, 640.f, ty + (cePitch ? cePitch : item->GetHeight())))"),
    ("\t\tty += item->GetHeight();\n", "\t\tty += cePitch ? cePitch : item->GetHeight();\n"),
)


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def run(command, expected=0):
    def bounded():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=60, preexec_fn=bounded)
    if result.returncode != expected:
        raise RuntimeError(f"command failed: {command}\n{result.stdout}{result.stderr}")
    return result.stdout


def block(text, start):
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def render_list(text):
    found = re.search(r"^[^\n;{}]*\bCMenuMenuItem::RenderList\s*\([^;{}]*\)[^;{}]*\{", text, flags=re.M)
    if not found:
        raise AssertionError("RenderList is absent")
    return block(text, found.start())


def calls_of(body, callee):
    """Each call of `callee` in the function: its argument text, spaces folded."""
    calls = []
    for found in re.finditer(callee + r"\s*\(", body):
        i, depth = found.end(), 1
        while depth:
            depth += (body[i] == "(") - (body[i] == ")")
            i += 1
        calls.append(re.sub(r"\s+", " ", body[found.end():i - 1]).strip())
    return calls


def check_lines(patched, original, lines, what):
    """`patched` is `original` with exactly these replacements, each made once, and no line moved."""
    if patched.count("\n") != original.count("\n"):
        raise AssertionError(what + ": the patch moved a line")
    expected = original
    for old, new in lines:
        if expected.count(old) != 1:
            raise AssertionError(what + ": the original line the patch changes is not the one the test knows: " + old[:60])
        expected = expected.replace(old, new)
    if patched != expected:
        raise AssertionError(what + ": the patch changes more, or other, than the reviewed lines")


def check_window(patched, before_window):
    """RenderList's window lines: exactly the reviewed ones, on top of the value fit."""
    check_lines(patched, before_window, WINDOW_LINES, "RenderList's window")
    body = render_list(patched)
    # ly is set before the window call writes it, and the original's clamps and height read the window's count after it
    order = [body.index(s) for s in ("float\tly = float(top);", "PortCeListWindow(this", "float\theight = float(n * size.cy);",
                                      "if (ly < 0.f)", "for (i = ceFirst;")]
    if order != sorted(order):
        raise AssertionError("the window must be laid out after the original's top and before its clamps and its entries")


def check_pitch(patched, original):
    """CMenuOptionsList::Render's pitch lines: exactly the reviewed ones; GetMenuPanelSize and the rest of the file are the game's."""
    check_lines(patched, original, PITCH_LINES, "the options list's pitch")
    found = re.search(r"^WCHAR\s*\*CMenuOptionsList::Render\s*\(", patched, flags=re.M)
    body = block(patched, found.start())
    if "item->GetHeight()" in body.replace("(cePitch ? cePitch : item->GetHeight())", "").replace("cePitch ? cePitch : item->GetHeight()", ""):
        raise AssertionError("every row height Render uses must follow the list's pitch")


def rows(pitch, shift, count, centre, title, action_gap=0):
    """The tops of a CMenuOptionsList's rows (src/MenuOptionsList.cpp Render): `count` rows `pitch` apart centred on centre + shift,
    plus `action_gap` before Apply/Back (the final two rows), after the list's own title (30 units) when it draws one (a level's Pause Menu)."""
    height = count * pitch + action_gap
    ty = (centre + shift) - height * 0.5 - 15.0
    if title:
        ty += 30.0
    return [ty + pitch * i + (action_gap if action_gap and i >= count - 2 else 0) for i in range(count)]


def check_capacity(header):
    """The window's entry count is an integer division of whole units: the game's units convert floats to integers by rounding
    (/QIfist), so a float quotient such as 348 / 16 = 21.75 would give 22 entries there while the host driver truncates to 21
    (observed 2026-11-12: a 22-entry list at 5120 x 2880 drew 22 and ended under the help line)."""
    body = header[header.index("inline bool PortCeListPlace("):]
    body = body[:body.index("\n}\n")]
    if "int capacity = int(bottomLimit - topLimit) / int(room);" not in body or re.search(r"int\([^()]*\([^()]*\)\s*/", body):
        raise AssertionError("the window's capacity must be an integer division, not a float quotient converted to int")


def check_rows(header):
    """Video Options at CE's pitch: 21 rows stay below the front-end emblem (y > 127) and above the help line (<= 452);
    in a level, Back clears the message bar (<= 406) where 20 units drew it across the bar."""
    pitch = int(re.search(r"kCeCompactPitch = (\d+);", header).group(1))
    gap = int(re.search(r"kCeCompactActionGap = (\d+);", header).group(1))
    shift = float(re.search(r"kCeCompactFrontShift = (-?[0-9.]+)f;", header).group(1))
    pause_shift = float(re.search(r"kCeCompactPauseShift = (-?[0-9.]+)f;", header).group(1))
    front = rows(pitch, shift, 21, 300, False, gap)
    before = rows(20, 0, 21, 300, False)
    if not (136.0 <= front[0] <= 145.0):
        raise AssertionError(f"the front end's first row must clear the top emblem: {front[0]}")
    if not (front[-1] + 16 <= 452 and front[-1] <= before[-1]):
        raise AssertionError(f"Back must end above the help line (y 452): {front[-1] + 16}")
    pause, pause_before = rows(pitch, pause_shift, 21, 240, True, gap), rows(20, 0, 21, 240, True)
    if not (pause[-1] + 16 <= 406 < pause_before[-1] + 16):
        raise AssertionError(f"in a level Back must end above the message bar (y 406): {pause[-1] + 16}, was {pause_before[-1] + 16}")
    return {"front_first": front[0], "front_back_end": front[-1] + 16, "pause_first": pause[0], "pause_back_end": pause[-1] + 16,
            "pause_back_end_at_20": pause_before[-1] + 16}


def check_patched_draw(patched, original):
    """The patch reaches the value draw and the dropdown's entries only: the label and the rest of the file are the game's own text."""
    if patched.count("\n") != original.count("\n"):
        raise AssertionError("the patch moved a line of MenuItem.cpp")
    body, before = render_list(patched), render_list(original)
    calls, was = calls_of(body, "DrawTextWithShadow"), calls_of(before, "DrawTextWithShadow")
    extents, was_extents = calls_of(body, "GetTextExtent"), calls_of(before, "GetTextExtent")
    if len(calls) != 3 or len(was) != 3 or len(extents) != 2 or len(was_extents) != 2:
        raise AssertionError("RenderList must draw its label, its dropdown and its value, and measure its label and its entries")
    column = re.fullmatch(r"lx \+ ([0-9.]+f?), ly, c, GetChoiceText\(i\), .*", was[1])
    if not column:
        raise AssertionError("the dropdown draw is not the one the test knows")
    k = column.group(1)
    if calls[0] != was[0] or was_extents[0] != extents[0] or was_extents[1] != "GetChoiceText(i), &s":
        raise AssertionError("the label's draw and measure must stay the game's own")
    if calls[1] != was[1].replace("GetChoiceText(i)", f"PortCeListFit(GetChoiceText(i), lx + {k})", 1):
        raise AssertionError("the dropdown must draw each entry from its fitted text, at the column it starts")
    if extents[1] != f"PortCeListFit(GetChoiceText(i), lx + {k}), &s":
        raise AssertionError("the dropdown must measure its widest entry from the same fitted text")
    if was[2] != "lx, y, col, GetChoiceText(mSelection), 0, 0, 1.f, 1.f":
        raise AssertionError("the value draw the patch fits is not the one the test knows")
    if calls[2] != "lx, y, col, ceText, 0, 0, ceFit, 1.f":
        raise AssertionError("the value must be drawn from the fitted text, squeezed in x only (the game's label squeeze)")
    if body.count("PortCeValueFit(") != 2 or body.count("PortCeListFit(") != 3 or \
            body.count("PortCeValueFit(GetChoiceText(mSelection), lx, &ceFit)") != 1:
        raise AssertionError("each fit helper must be declared once and called at its sites, with the choice text and the column")
    opening = body.split("\n")[1]
    if not opening.startswith("{ extern WCHAR *PortCeValueFit(WCHAR *, float, float *); extern WCHAR *PortCeListFit(WCHAR *, float);"):
        raise AssertionError("the declarations belong on the opening brace's own line")
    outside = patched.replace(body, "")
    if outside != original.replace(before, ""):
        raise AssertionError("the patch changed the file outside RenderList")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--causal-controls", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "local-data/menu-fit-tests")
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to((ROOT / "local-data").resolve()):
        raise SystemExit("outputs must be inside this checkout's ignored local-data")
    out.mkdir(parents=True, exist_ok=True)
    build = module("menu_fit_build", PORT / "build.py")
    patched = build.patched_text(["MenuItem.cpp"])["MenuItem.cpp"]
    original = build.patched_text(["MenuItem.cpp"], skip=(PATCH,))["MenuItem.cpp"]
    # The value fit's lines alone (the window's undone): check_patched_draw holds them against the original as before.
    before_window = patched
    for old, new in WINDOW_LINES:
        before_window = before_window.replace(new, old)
    check_patched_draw(before_window, original)
    check_window(patched, before_window)
    listed = build.patched_text(["MenuOptionsList.cpp"])["MenuOptionsList.cpp"]
    list_original = build.patched_text(["MenuOptionsList.cpp"], skip=(PITCH_PATCH,))["MenuOptionsList.cpp"]
    check_pitch(listed, list_original)
    positions = check_rows((PORT / "game/ce_menu_fit.h").read_text())
    check_capacity((PORT / "game/ce_menu_fit.h").read_text())
    helper = PORT / "game/ce_menu_fit.cpp"
    header = PORT / "game/ce_menu_fit.h"
    driver = SUPPORT / "ce_menu_fit_driver.cpp"
    (out / "stand-in").mkdir(exist_ok=True)
    (out / "stand-in/FrontEndPage.h").write_text(FRONT_END_PAGE)
    flags = ["clang++", "-std=c++14", "-Wall", "-Werror", '-DCE_VERSION="menu-fit"', "-I", out / "stand-in", "-I", SUPPORT / "menu_fit",
             "-I", PORT / "game"]

    def compile_one(directory, source=helper, include=PORT / "game"):
        directory.mkdir(parents=True, exist_ok=True)
        executable = directory / "driver"
        run(flags[:-1] + [include, source, driver, "-o", executable])
        return executable

    binary = compile_one(out / "production")
    observations = {case: run([binary, case]) for case in CASES}
    controls = {}
    if args.causal_controls:
        text, head = helper.read_text(), header.read_text()
        helper_mutants = {
            "no-layer-gate": (text, "*scale = 1.f;\n    if (!text || !PortCeQualityOptionsAvailable()) return text;", "*scale = 1.f;\n    if (!text) return text;", "layer-off"),
            "list-no-layer-gate": (text, "WCHAR *PortCeListFit(WCHAR *text, float x)\n{\n    if (!text || !PortCeQualityOptionsAvailable()) return text;",
                                   "WCHAR *PortCeListFit(WCHAR *text, float x)\n{\n    if (!text) return text;", "list-layer-off"),
            "never-fits": (text, "if ((float)size.cx * fit <= room + 0.01f)", "if (false)", "fits"),
            "list-never-fits": (text, "if ((float)size.cx <= room) return text;", "if (false) return text;", "list-fits"),
            "cut-never-marked": (text, "cut[length] = cut[length + 1] = cut[length + 2] = (WCHAR)'.';", "cut[length] = cut[length + 1] = cut[length + 2] = (WCHAR)'-';", "squeeze-limit"),
            "keeps-too-much": (text, "(float)size->cx * scale <= room + 0.01f) break;", "(float)size->cx * scale <= room + 40.f) break;", "squeeze-limit"),
            "list-keeps-too-much": (text, "return Cut(font, text, room, 1.f, &size);", "return Cut(font, text, room + 60.f, 1.f, &size);", "list-cut"),
            "ignores-failed-measure": (text, "if (!font || !font->GetTextExtent(text, &size) || size.cx <= 0) return text;\n    const float room = kCeValueRight - x;\n    float fit",
                                       "if (!font) return text;\n    if (!font->GetTextExtent(text, &size)) size.cx = 4000;\n    const float room = kCeValueRight - x;\n    float fit", "extent-fails"),
            "list-ignores-failed-measure": (text, "if (!font || !font->GetTextExtent(text, &size) || size.cx <= 0) return text;\n    const float room = kCeValueRight - x;\n    if ((float)size.cx <= room) return text;",
                                            "if (!font) return text;\n    if (!font->GetTextExtent(text, &size)) size.cx = 4000;\n    const float room = kCeValueRight - x;\n    if ((float)size.cx <= room) return text;", "list-extent-fails"),
            "window-no-layer-gate": (text, "if (!owner || !PortCeQualityOptionsAvailable()) return 0;", "if (!owner) return 0;", "window-layer-off"),
            "window-recentres-every-frame": (text, "open ? gWindow.first : -1", "-1", "window-follows"),
            "window-never-forgets": (text, "gWindow.owner == owner && Within(now, gWindow.seen, kCeListForget)", "gWindow.owner == owner", "window-forget"),
            "window-shared-by-lists": (text, "gWindow.owner == owner && Within(now, gWindow.seen, kCeListForget)", "Within(now, gWindow.seen, kCeListForget)", "window-forget"),
            "no-edge-scroll": (text, "const int step = PortCeListEdgeStep(place.first, place.shown, count, hovered);", "const int step = 0 * hovered;", "window-edge"),
            "edge-scroll-unthrottled": (text, "if (step && !Within(now, gWindow.scrolled, kCeListScrollEvery))", "if (step)", "window-edge"),
            "hover-kept-forever": (text, "    gWindow.hovered = -1;\n", "\n", "window-edge"),
            "front-limits-everywhere": (text, "return PortCeAsOptions((EFrontEndPage)page) == FEP_OPTIONS;", "return true;", "window-page"),
            "ce-page-as-pause": (text, "return PortCeAsOptions((EFrontEndPage)page) == FEP_OPTIONS;", "return page == FEP_OPTIONS;", "window-page"),
            "pitch-ungated": (text, "return (PortCeQualityRow(item) && PortCeQualityOptionsAvailable()) ? kCeCompactPitch : 0;", "return PortCeQualityRow(item) ? kCeCompactPitch : 0;", "pitch"),
            "pitch-every-list": (text, "return (PortCeQualityRow(item) && PortCeQualityOptionsAvailable()) ? kCeCompactPitch : 0;", "return PortCeQualityOptionsAvailable() ? kCeCompactPitch : 0;", "pitch"),
            "shift-in-pause": (text, "return options_page ? kCeCompactFrontShift : kCeCompactPauseShift;", "return kCeCompactFrontShift;", "pitch"),
            "label-left-ungated": (text, "return (PortCeQualityOptionsAvailable() && OptionsPage(page)) ? kCeLabelLeft : 5.f;", "return OptionsPage(page) ? kCeLabelLeft : 5.f;", "pitch"),
            "value-gap-ungated": (text, "return PortCeQualityOptionsAvailable() ? kCeValueGap : 2.f;", "return kCeValueGap;", "pitch"),
            "action-gap-every-row": (text, "return (pitch && count >= 2 && index == count - 2) ? kCeCompactActionGap : 0;", "return pitch ? kCeCompactActionGap : 0;", "pitch"),
        }
        header_mutants = {
            "no-smallest-scale": (head, "kCeValueMinScale = 0.75f;", "kCeValueMinScale = 0.01f;", "squeeze-limit"),
            "wrong-edge": (head, "kCeValueRight = 600.f;", "kCeValueRight = 700.f;", "adapter"),
            "squeezes-fitting-text": (head, "if (!(width > 0.f) || width <= room) return 1.f;", "if (!(width > 0.f)) return 1.f;", "fits"),
            "window-always": (head, "if ((float)top >= topLimit && (float)top + (float)(count * rowh) <= bottomLimit) return false;", "(void)top;", "window-fits"),
            "window-squeezed-capacity": (head, "const int shown = count < capacity ? count : capacity;", "const int shown = count + 0 * capacity;", "window-sizes"),
            "window-not-centred": (head, "if (first < 0) first = selection - shown / 2;", "if (first < 0) first = selection;", "window-sizes"),
            "window-not-following": (head, "else if (selection >= first + shown) first = selection - shown + 1;", "", "window-follows"),
            "window-past-end": (head, "if (first > count - shown) first = count - shown;", "", "window-sizes"),
            "window-over-title": (head, "if (placed < topLimit) placed = topLimit;", "", "window-sizes"),
            "window-under-help": (head, "if (placed + room * (float)shown > bottomLimit) placed = bottomLimit - room * (float)shown;", "", "window-sizes"),
            "window-front-top": (head, "kCeListFrontTop = 104.f;", "kCeListFrontTop = 80.f;", "window-limits"),
            "window-sized-by-font": (head, "const float room = (float)rowh > kCeListEntryRoom ? (float)rowh : kCeListEntryRoom;", "const float room = (float)rowh;", "window-sizes"),
            "edge-step-unbounded": (head, "if (hovered == first && first > 0) return -1;", "if (hovered == first) return -1;", "window-edge"),
            "pitch-twenty": (head, "kCeCompactPitch = 14;", "kCeCompactPitch = 20;", "pitch"),
        }
        for label, (original_text, old, new, case) in {**helper_mutants, **header_mutants}.items():
            if original_text.count(old) != 1:
                raise AssertionError("nonunique causal anchor: " + label)
            directory = out / label
            directory.mkdir(parents=True, exist_ok=True)
            changed_source, changed_header = directory / "ce_menu_fit.cpp", directory / "ce_menu_fit.h"
            changed_source.write_text(text.replace(old, new) if original_text is text else text)
            changed_header.write_text(head.replace(old, new) if original_text is head else head)
            controls[label] = run([compile_one(directory, changed_source, directory), case], expected=1)
        # the patched draw: each of these must break the structural check
        draw_mutants = {
            "scale-in-y": ("0, 0, ceFit, 1.f", "0, 0, 1.f, ceFit"),
            "unfitted-text": ("DrawTextWithShadow(lx, y, col, ceText, 0, 0, ceFit, 1.f)", "DrawTextWithShadow(lx, y, col, GetChoiceText(mSelection), 0, 0, ceFit, 1.f)"),
            "scale-ignored": ("0, 0, ceFit, 1.f", "0, 0, 1.f, 1.f"),
            "wrong-column": ("PortCeValueFit(GetChoiceText(mSelection), lx, &ceFit)", "PortCeValueFit(GetChoiceText(mSelection), y, &ceFit)"),
            "list-measure-unfitted": ("GetTextExtent(PortCeListFit(GetChoiceText(i), lx + K), &s)", "GetTextExtent(GetChoiceText(i), &s)"),
            "list-draw-unfitted": ("c, PortCeListFit(GetChoiceText(i), lx + K),", "c, GetChoiceText(i),"),
            "list-wrong-column": ("PortCeListFit(GetChoiceText(i), lx + K), &s)", "PortCeListFit(GetChoiceText(i), lx), &s)"),
        }
        column = re.search(r"DrawTextWithShadow\(\s*lx\s*\+\s*([0-9.]+f?)\s*,", before_window).group(1)
        for label, (old, new) in draw_mutants.items():
            old, new = old.replace("lx + K", "lx + " + column), new.replace("lx + K", "lx + " + column)
            if before_window.count(old) != 1:
                raise AssertionError("nonunique causal anchor: " + label)
            try:
                check_patched_draw(before_window.replace(old, new), original)
            except AssertionError:
                controls[label] = "detected"
            else:
                raise AssertionError("the structural check missed the mutant " + label)
        # the window's and the pitch's lines: each of these must break their structural checks
        text_mutants = {
            "window-loop-from-zero": ("MenuItem", "for (i = ceFirst; i < ceFirst + n; i++)", "for (i = 0; i < n; i++)"),
            "window-box-full-height": ("MenuItem", "((ceWindow ? n : GetNumChoices()) * size.cy)", "(GetNumChoices() * size.cy)"),
            "window-box-always-window": ("MenuItem", "((ceWindow ? n : GetNumChoices()) * size.cy)", "(n * size.cy)"),
            "window-hover-unrecorded": ("MenuItem", "{ mSelection = i; if (ceWindow) PortCeListHover(i); }", "{ mSelection = i; }"),
            "window-hover-always": ("MenuItem", "{ mSelection = i; if (ceWindow) PortCeListHover(i); }", "{ mSelection = i; PortCeListHover(i); }"),
            "window-no-call": ("MenuItem", " const int ceWindow = PortCeListWindow(this, y, size.cy, n, mSelection, FRONTEND.GetCurrentPage(), &ly, &ceFirst, &n);", " const int ceWindow = 0;"),
            "window-after-clamps": ("MenuItem", "\t\tif (ly < 0.f)\n", "\t\tif (ly < -1.f)\n"),
            "pitch-mouse-at-item-height": ("MenuOptionsList", "IsMouseInRect(0.f, ty, 640.f, ty + (cePitch ? cePitch : item->GetHeight()))", "IsMouseInRect(0.f, ty, 640.f, ty + item->GetHeight())"),
            "pitch-click-at-item-height": ("MenuOptionsList", "MouseClickedInRect(0.f, ty, 640.f, ty + (cePitch ? cePitch : item->GetHeight()))", "MouseClickedInRect(0.f, ty, 640.f, ty + item->GetHeight())"),
            "pitch-step-at-item-height": ("MenuOptionsList", "\t\tty += cePitch ? cePitch : item->GetHeight();", "\t\tty += item->GetHeight();"),
            "pitch-height-at-item-height": ("MenuOptionsList", "\t\theight += cePitch ? cePitch : item->GetHeight();", "\t\theight += item->GetHeight();"),
            "pitch-no-shift": ("MenuOptionsList", "\tfloat\ty = mY + PortCeCompactShift(cePitch, options_page);", "\tfloat\ty = mY;"),
            "pitch-panel-too": ("MenuOptionsList", "\t\tph += item->GetHeight();", "\t\tph += 18;"),
        }
        for label, (which, old, new) in text_mutants.items():
            text_now = patched if which == "MenuItem" else listed
            if text_now.count(old) != 1:
                raise AssertionError("nonunique causal anchor: " + label)
            try:
                if which == "MenuItem":
                    check_window(text_now.replace(old, new), before_window)
                else:
                    check_pitch(text_now.replace(old, new), list_original)
            except AssertionError:
                controls[label] = "detected"
            else:
                raise AssertionError("the structural check missed the mutant " + label)
        try:
            check_capacity(head.replace("int capacity = int(bottomLimit - topLimit) / int(room);", "int capacity = int((bottomLimit - topLimit) / room);"))
        except AssertionError:
            controls["capacity-float-quotient"] = "detected"
        else:
            raise AssertionError("the capacity check missed the float quotient")
        for label, (old, new) in {"rows-first-moved": ("kCeCompactFrontShift = 8.f;", "kCeCompactFrontShift = 0.f;"),
                                  "rows-pause-unshifted": ("kCeCompactPauseShift = -8.f;", "kCeCompactPauseShift = 0.f;"),
                                  "rows-pitch-twenty": ("kCeCompactPitch = 14;", "kCeCompactPitch = 20;")}.items():
            try:
                check_rows(head.replace(old, new))
            except AssertionError:
                controls[label] = "detected"
            else:
                raise AssertionError("the row check missed the mutant " + label)
    receipt = {"helper_sha256": hashlib.sha256(helper.read_bytes()).hexdigest(), "header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
               "patch_sha256": hashlib.sha256((PORT / "patches" / PATCH).read_bytes()).hexdigest(),
               "pitch_patch_sha256": hashlib.sha256((PORT / "patches" / PITCH_PATCH).read_bytes()).hexdigest(),
               "observations": observations, "controls": controls, "video_options_rows": positions,
               "limits": "The production helpers with a nine-units-a-character font and a set clock; the patched RenderList and "
                         "CMenuOptionsList::Render as text; row positions computed, not drawn. Not a rendered frame."}
    (out / "receipt.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"menu fit: {len(observations)} cases passed; patched draw, window and pitch checked; {len(controls)} causal controls detected")


if __name__ == "__main__":
    main()
