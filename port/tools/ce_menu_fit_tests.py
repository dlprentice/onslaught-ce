#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the value fit of the options-style lists (port/game/ce_menu_fit.cpp, patch 0212).

The production helper is compiled against a font that measures nine units a character; the game's actual patched
CMenuMenuItem::RenderList is read as text to prove the patch reaches exactly the value draw, keeps the game's own
arguments, leaves the label and the dropdown alone, and does not move a line of the file. Authored mutants of the helper
and of the patched draw must each be caught."""
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
         "list-fits", "list-cut", "list-layer-off", "list-extent-fails")
PATCH = "0212-ce-menu-value-fit.patch"


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
    check_patched_draw(patched, original)
    helper = PORT / "game/ce_menu_fit.cpp"
    header = PORT / "game/ce_menu_fit.h"
    driver = SUPPORT / "ce_menu_fit_driver.cpp"
    flags = ["clang++", "-std=c++14", "-Wall", "-Werror", "-I", SUPPORT / "menu_fit", "-I", PORT / "game"]

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
        }
        header_mutants = {
            "no-smallest-scale": (head, "kCeValueMinScale = 0.75f;", "kCeValueMinScale = 0.01f;", "squeeze-limit"),
            "wrong-edge": (head, "kCeValueRight = 600.f;", "kCeValueRight = 700.f;", "adapter"),
            "squeezes-fitting-text": (head, "if (!(width > 0.f) || width <= room) return 1.f;", "if (!(width > 0.f)) return 1.f;", "fits"),
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
        column = re.search(r"DrawTextWithShadow\(\s*lx\s*\+\s*([0-9.]+f?)\s*,", patched).group(1)
        for label, (old, new) in draw_mutants.items():
            old, new = old.replace("lx + K", "lx + " + column), new.replace("lx + K", "lx + " + column)
            if patched.count(old) != 1:
                raise AssertionError("nonunique causal anchor: " + label)
            try:
                check_patched_draw(patched.replace(old, new), original)
            except AssertionError:
                controls[label] = "detected"
            else:
                raise AssertionError("the structural check missed the mutant " + label)
    receipt = {"helper_sha256": hashlib.sha256(helper.read_bytes()).hexdigest(), "header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
               "patch_sha256": hashlib.sha256((PORT / "patches" / PATCH).read_bytes()).hexdigest(),
               "observations": observations, "controls": controls,
               "limits": "The production helper with a nine-units-a-character font; the patched RenderList as text. Not a rendered frame."}
    (out / "receipt.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"menu fit: {len(observations)} cases passed; patched draw checked; {len(controls)} causal controls detected")


if __name__ == "__main__":
    main()
