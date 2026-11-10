#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the Overall Detail level of the ordinary CE menus (patch 0213) against the game's actual rows.

The game's own CVideoDetailLevel, CLandscapeRes and CMultiSample (their classes and functions are extracted from the patched
PauseMenu.cpp) run with the production policy helpers; only the other rows, the device and the tweaks are authored. A fresh profile
at the desktop's size must read High on every authored device (landscape levels 2 to 6, anti-aliasing from none to six levels), a
finer anti-aliasing level must still read High, choosing High or Medium must read back as chosen, and outside the ordinary CE menus
every selection of every row must read exactly what the game's own, unpatched function reads (a table of several tens of thousands
of rows, compared byte for byte, also for a build without CE_VERSION where the patch's fallback macros stand in for the helpers)."""
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
PATCH = "0213-ce-detail-level.patch"
CASES = ("fresh", "fresh-without-anti-aliasing", "choose", "landscape-presets", "finer-anti-aliasing", "each-row", "layer-off")
Custom, Low, Medium, High = 0, 1, 2, 3	# the levels the Overall row reads (the driver's table prints them)


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def run(command, expected=0, capture=True):
    def bounded():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    result = subprocess.run([str(x) for x in command], capture_output=capture, text=True, timeout=120, preexec_fn=bounded)
    if result.returncode != expected:
        raise RuntimeError(f"command failed: {command}\n{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return result.stdout


def block(text, start):
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def actual(text):
    """The classes and functions the Overall row runs, from the patched game text (the first of each name)."""
    out = []
    for pattern in (r"MultiSampleIndex", r"(?:D3DMULTISAMPLE_TYPE|int)\s+MultiSampleType"):
        found = re.search(r"^[^\n;{}]*\b" + pattern + r"\s*\([^;{}]*\)[^;{}]*\{", text, flags=re.M)
        if not found:
            raise AssertionError("helper absent: " + pattern)
        out.append(block(text, found.start()))
    for cls in ("CLandscapeRes", "CMultiSample", "CVideoDetailLevel"):
        found = re.search(r"\bclass\s+" + cls + r"\b[^;{]*\{", text)
        if not found:
            raise AssertionError("class absent: " + cls)
        out.append(block(text, found.start()) + ";")
    for owner, names in (("CLandscapeRes", ("GetNumChoices", "GetValue", "SetValue", "GetChoiceText")),
                         ("CMultiSample", ("GetNumChoices", "GetValue", "SetValue", "GetChoiceText")),
                         ("CVideoDetailLevel", ("GetValue", "SetValue"))):
        for name in names:
            found = re.search(r"^[^\n;{}]*\b" + owner + "::" + name + r"\s*\([^;{}]*\)[^;{}]*\{", text, flags=re.M)
            if not found:
                raise AssertionError(f"function absent: {owner}::{name}")
            out.append(block(text, found.start()))
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--causal-controls", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "local-data/detail-level-tests")
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to((ROOT / "local-data").resolve()):
        raise SystemExit("outputs must be inside this checkout's ignored local-data")
    out.mkdir(parents=True, exist_ok=True)
    build = module("detail_build", PORT / "build.py")
    patched = build.patched_text(["PauseMenu.cpp"])["PauseMenu.cpp"]
    original = build.patched_text(["PauseMenu.cpp"], skip=(PATCH,))["PauseMenu.cpp"]
    lines = patched.split("\n")
    if lines[1:8] != ["#ifdef CE_VERSION", '#include "ce_graphics_policy.h"', "#else",
                      "#define PortCeDetailLandscape(preset, choices, high) (preset)",
                      "#define PortCeDetailAntiAliasingHigh(selection, gameHigh) ((selection) == (gameHigh))", "#endif", "#line 5"]:
        raise AssertionError("the patch's include block and fallback macros are not the reviewed ones")
    rest, was = lines[8:], original.split("\n")[4:]
    differing = [i for i, (a, b) in enumerate(zip(rest, was)) if a != b]
    if len(rest) != len(was) or not 1 <= len(differing) <= 6:
        raise AssertionError("the patch must restore every later line number (#line 5) and change only the Overall row's few sites")
    policy = PORT / "game/ce_graphics_policy.cpp"
    driver = SUPPORT / "ce_detail_level_driver.cpp"
    common = ["clang++", "-std=c++14", "-DBEA_GRAPHICS_POLICY_TEST", "-DCE_VERSION=1", "-I", SUPPORT, "-I", PORT / "game",
              "-I", PORT / "platform/common"]

    def compile_one(directory, text, source=policy, ce=True, header_dir=PORT / "game"):
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "ce_detail_level_actual.inc").write_text(("" if ce else "\n".join(lines[1:7]).replace("#line 5", "") + "\n") + text)
        flags = [f for f in common if f != "-DCE_VERSION=1"] + (["-DCE_VERSION=1"] if ce else [])
        flags = [str(header_dir) if f == str(PORT / "game") else f for f in flags]
        executable = directory / "driver"
        run(flags + ["-I", directory, source, driver, "-o", executable])
        return executable

    patched_actual, original_actual = actual(patched), actual(original)
    binary = compile_one(out / "production", patched_actual)
    observations = {case: run([binary, case]) for case in CASES}
    # The faithful table: the game's own function against the patched one with the layer off, with and without CE_VERSION.
    reference = compile_one(out / "game-own", original_actual, ce=False)
    table = run([reference, "grid"])
    rows = table.count("\n")
    if rows < 20000:
        raise AssertionError(f"the table must cover the rows' combinations; it has {rows}")
    digest = hashlib.sha256(table.encode()).hexdigest()
    off = run([binary, "grid", "off"])
    if hashlib.sha256(off.encode()).hexdigest() != digest:
        raise AssertionError("with the layer off the patched function reads differently from the game's own")
    noncee = compile_one(out / "no-ce", patched_actual, ce=False)
    if hashlib.sha256(run([noncee, "grid"]).encode()).hexdigest() != digest:
        raise AssertionError("without CE_VERSION the patched function reads differently from the game's own")
    # With the layer on, the reading differs from the game's own in a few rows, and each moves one of two ways by design: a Custom the
    # game's presets could not reach on the card becomes Medium or High (the presets name a landscape selection beyond the row's last
    # choice on a card of landscape level 4 or more, and only the first anti-aliasing level), or a High becomes Custom, which happens
    # only on a card of landscape level 3 with the landscape row at its lowest choice: CE's High is the row's top choice, and the
    # game's High preset names selection 0 for that card.
    on = run([binary, "grid"]).splitlines()
    own = table.splitlines()
    if len(on) != len(own):
        raise AssertionError("the two tables must list the same rows")
    changed = gained = lost = 0
    for a, b in zip(on, own):
        if a == b:
            continue
        changed += 1
        now, then = a.split(), b.split()
        if now[:3] != then[:3]:
            raise AssertionError("the two tables must list the same rows in the same order")
        if int(then[3]) == Custom and int(now[3]) in (Medium, High):
            gained += 1
        elif int(then[3]) == High and int(now[3]) == Custom and now[0] == "3" and now[2][5] == "0":
            lost += 1
        else:
            raise AssertionError("a row moved in a way the design does not allow")
    if not gained:
        raise AssertionError("the ordinary CE reading must differ from the game's own somewhere")
    controls = {}
    if args.causal_controls:
        policy_text = policy.read_text()
        header_text = (PORT / "game/ce_graphics_policy.h").read_text()
        mutants = {
            "high-landscape-stays-the-preset": ("policy", "if (high) return choices - 1;", "if (high) return preset;", ("fresh", "choose")),
            "no-finer-anti-aliasing": ("policy", "(selection > gameHigh && PortCeGraphicsPolicyActive())", "false", ("finer-anti-aliasing",)),
            "landscape-ungated": ("policy", "if (!PortCeGraphicsPolicyActive() || choices < 1) return preset;", "if (choices < 1) return preset;", ("layer-off",)),
            "anti-aliasing-ungated": ("policy", "(selection > gameHigh && PortCeGraphicsPolicyActive())", "(selection > gameHigh)", ("grid-off",)),
            "medium-clamp-lost": ("policy", "return preset < choices ? preset : choices - 1;", "return preset;", ("choose",)),
            "medium-landscape-always-last": ("policy", "return preset < choices ? preset : choices - 1;", "return choices - 1;", ("landscape-presets",)),
            "high-landscape-is-the-medium-rule": ("policy", "if (high) return choices - 1;", "if (high && choices > 99) return choices - 1;", ("landscape-presets",)),
        }
        for label, (target, old, new, cases) in mutants.items():
            if policy_text.count(old) != 1:
                raise AssertionError("nonunique causal anchor: " + label)
            directory = out / label
            directory.mkdir(parents=True, exist_ok=True)
            changed_policy = directory / "policy.cpp"
            changed_policy.write_text(policy_text.replace(old, new))
            executable = compile_one(directory, patched_actual, source=changed_policy)
            for case in cases:
                if case == "grid-off":
                    # the table with the layer off must then differ from the game's own
                    if hashlib.sha256(run([executable, "grid", "off"]).encode()).hexdigest() == digest:
                        raise AssertionError("the faithful table did not catch the mutant " + label)
                    controls[label + ":" + case] = "table differs"
                else:
                    controls[label + ":" + case] = run([executable, case], expected=1)
        # the patched function's own sites: each must matter
        site_mutants = {
            "get-high-landscape-is-the-preset": ("mLandscapeRes->GetSelection() == PortCeDetailLandscape(mHighLandscapeRes, mLandscapeRes->GetNumChoices(), 1)",
                                              "mLandscapeRes->GetSelection() == mHighLandscapeRes", ("fresh",)),
            "set-high-landscape-is-the-preset": ("mLandscapeRes->SetSelection(PortCeDetailLandscape(mHighLandscapeRes, mLandscapeRes->GetNumChoices(), 1))",
                                                  "mLandscapeRes->SetSelection(mHighLandscapeRes)", ("choose",)),
            "high-anti-aliasing-is-the-first-level": ("PortCeDetailAntiAliasingHigh(mMultiSample->GetSelection(), 1)", "mMultiSample->GetSelection() == 1",
                                                       ("finer-anti-aliasing",)),
            "set-medium-landscape-takes-the-high-flag": ("mLandscapeRes->SetSelection(PortCeDetailLandscape(mMediumLandscapeRes, mLandscapeRes->GetNumChoices(), 0))",
                                                         "mLandscapeRes->SetSelection(PortCeDetailLandscape(mMediumLandscapeRes, mLandscapeRes->GetNumChoices(), 1))",
                                                         ("landscape-presets",)),
            "get-medium-landscape-is-the-preset": ("mLandscapeRes->GetSelection() == PortCeDetailLandscape(mMediumLandscapeRes, mLandscapeRes->GetNumChoices(), 0)",
                                                    "mLandscapeRes->GetSelection() == mMediumLandscapeRes", ("choose",)),
        }
        for label, (old, new, cases) in site_mutants.items():
            if patched_actual.count(old) != 1:
                raise AssertionError("nonunique causal site: " + label)
            directory = out / label
            executable = compile_one(directory, patched_actual.replace(old, new))
            for case in cases:
                controls[label + ":" + case] = run([executable, case], expected=1)
    receipt = {"policy_sha256": hashlib.sha256(policy.read_bytes()).hexdigest(), "patch_sha256": hashlib.sha256((PORT / "patches" / PATCH).read_bytes()).hexdigest(),
               "table_rows": rows, "table_sha256": digest, "rows_changed_with_the_layer_on": changed, "rows_gaining_a_level": gained, "rows_losing_high": lost,
               "observations": observations, "controls": controls,
               "limits": "The game's actual Overall, landscape and anti-aliasing rows with authored other rows, devices and tweaks; not a rendered menu."}
    (out / "receipt.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"detail level: {len(observations)} cases passed; {rows} rows equal to the game's own with the layer off; "
          f"{changed} rows read differently with it on ({gained} gain Medium or High, {lost} lose High by design); {len(controls)} causal controls detected")


if __name__ == "__main__":
    main()
