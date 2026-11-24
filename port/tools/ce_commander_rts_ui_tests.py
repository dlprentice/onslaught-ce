#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host unit, static-glyph, and causal-mutant suite for the Commander Mode RTS HUD and voice cues.

Exercises port/game/ce_cmd_hud.h and port/game/ce_cmd_voice.h with the host C++ compiler
(ASan/UBSan + -Wall -Wextra -Werror -pedantic) across 640x480, 1280x720, and 1920x1080 layouts,
placard/button hit-testing, double-click focus tracking, short-name truncation & glyph sanitization,
armour bar color boundaries, and deterministic >= 1.8s retail voice cooldown & path safety.
Zero game binaries, retail files, network, or desktop display required.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HUD_H = ROOT / "port/game/ce_cmd_hud.h"
HUD_CPP = ROOT / "port/game/ce_cmd_hud.cpp"
VOICE_H = ROOT / "port/game/ce_cmd_voice.h"
VOICE_CPP = ROOT / "port/game/ce_cmd_voice.cpp"
PROD_CPP = ROOT / "port/game/ce_cmdproduction.cpp"
DRIVER = ROOT / "port/tools/tests/ce_commander_rts_ui_driver.cpp"

FORBIDDEN_GLYPHS = set("$@[\\]^_{|}~`")

# (label, file_name, old_snippet, new_snippet)
MUTANTS = (
    ("bar exceeds 25 percent height budget", "ce_cmd_hud.h",
     "const float maxBarH = std::floor(viewportH * 0.165f);",
     "const float maxBarH = std::floor(viewportH * 0.40f); barH = maxBarH;"),
    ("dialogue reservation ignored", "ce_cmd_hud.h",
     "if (uiBottom > 0.f && uiBottom < viewportH)",
     "if (false && uiBottom > 0.f && uiBottom < viewportH)"),
    ("safe margin drops below 3 percent", "ce_cmd_hud.h",
     "const float safeSide = std::floor(viewportW * 0.032f);",
     "const float safeSide = 0.f;"),
    ("9-slice radius stretches vertically with panel height", "ce_cmd_hud.h",
     "out.radius = r;\n\tout.railHeight = (h > 2.f * r) ? (h - 2.f * r) : 0.f;",
     "out.radius = h * 0.48f;\n\tout.railHeight = 0.f;"),
    ("gap between buttons hits card button", "ce_cmd_hud.h",
     "return r.w > 0.f && r.h > 0.f && px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;",
     "return r.w > 0.f && r.h > 0.f && px >= r.x - 8.f && px < r.x + r.w + 8.f && py >= r.y && py < r.y + r.h;"),
    ("paged placard returns local slot instead of global index", "ce_cmd_hud.h",
     "return {CE_HUD_HIT_PLACARD, layout.visibleStart + i};",
     "return {CE_HUD_HIT_PLACARD, i};"),
    ("hit test ignores placardCount bound", "ce_cmd_hud.h",
     "if (idx < 0 || (placardCount >= 0 && idx >= placardCount)) break;",
     "if (idx < 0) break;"),
    ("glyph sanitizer allows pipe", "ce_cmd_hud.h",
     "case '_': case '{': case '|': case '}': case '~': case '`':",
     "case '_': case '{': case '}': case '~': case '`':"),
    ("armour amber boundary shifted", "ce_cmd_hud.h",
     "if (ratio < 0.60f) return p.armourWarn;",
     "if (ratio < 0.70f) return p.armourWarn;"),
    ("triple click fires twice", "ce_cmd_hud.h",
     "if (primed && lastKind == kind && lastId == id && nowNs >= lastNs && (nowNs - lastNs) <= windowNs)\n\t\t{\n\t\t\tReset();\n\t\t\treturn true;\n\t\t}",
     "if (primed && lastKind == kind && lastId == id && nowNs >= lastNs && (nowNs - lastNs) <= windowNs)\n\t\t{\n\t\t\treturn true;\n\t\t}"),
    ("voice cooldown drops below 1.8s", "ce_cmd_voice.h",
     "static constexpr std::uint64_t kCeCmdVoiceCooldownNs = 1800000000ull;",
     "static constexpr std::uint64_t kCeCmdVoiceCooldownNs = 500000000ull;"),
    ("voice gate ignores cooldown", "ce_cmd_voice.h",
     "return (nowNs - lastPlayedNs) >= cooldownNs;",
     "return true;"),
    ("voice language allows path traversal", "ce_cmd_voice.h",
     "return \"english\";\n\t}\n\n\t// Formats",
     "return lang;\n\t}\n\n\t// Formats"),
    ("voice path allows unsafe stem characters", "ce_cmd_voice.h",
     "if (!ok) return false;",
     "(void)ok;"),
)


def compile_and_run(cxx: str, headers: dict[str, str], strict: bool = True) -> tuple[bool, bool, str]:
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        for name, text in headers.items():
            (d / name).write_text(text)
        exe = d / "rts_ui_check"
        cmd = [
            cxx, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-pedantic",
            *(["-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if strict else []),
            "-I", str(d), str(DRIVER), "-o", str(exe),
        ]
        built = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if built.returncode != 0:
            return False, False, built.stderr[-800:]
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30, env=env)
        out = ran.stdout + ran.stderr
        ok = ran.returncode == 0 and "PASS" in ran.stdout and "Sanitizer" not in out and "runtime error:" not in out
        return True, ok, out[-800:]


def lint_shown_strings() -> tuple[list[str], int]:
    """Check user-facing string literals in new HUD/production files for forbidden bitmap-font glyphs."""
    problems: list[str] = []
    checked = 0
    targets = (HUD_H, HUD_CPP, PROD_CPP)
    patterns = (
        re.compile(r'\bdraw\.text\([^;]*?"((?:[^"\\]|\\.)*)"'),
        re.compile(r'\bmapped\s*=\s*(?:plural\s*\?\s*)?"((?:[^"\\]|\\.)*)"'),
        re.compile(r'\bsnprintf\([^;]*?"((?:[^"\\]|\\.)*)"'),
    )
    for path in targets:
        text = path.read_text()
        for pat in patterns:
            for literal in pat.findall(text):
                # Skip pure format specifiers and internal path/debug strings
                if "\\" in literal and "data\\" in literal:
                    continue
                checked += 1
                bad = sorted(set(literal) & FORBIDDEN_GLYPHS)
                if bad:
                    problems.append(f"{path.name}: {literal!r} contains forbidden glyph(s) {''.join(bad)}")
    return problems, checked


def lint_safety_fences() -> list[str]:
    problems: list[str] = []
    voice_cpp = VOICE_CPP.read_text()
    voice_h = VOICE_H.read_text()
    hud_cpp = HUD_CPP.read_text()
    for path in (HUD_H, HUD_CPP, VOICE_H, VOICE_CPP):
        first = path.read_text().splitlines()[0]
        if first != "// SPDX-License-Identifier: GPL-3.0-only":
            problems.append(f"{path.name}: missing GPL-3.0-only SPDX header")
    for bad in ("FloatRandom", "IntRandom", "rand(", "srand(", "Random(", "CMessageBox", "fprintf("):
        if bad in voice_cpp or bad in voice_h:
            problems.append(f"voice module references forbidden symbol {bad!r}")
    for req in ("PortClockVirtual()", "SOUND.IsInitialised()", "PortCeCmdMapOpen()", "CeCmdMapOn()"):
        if req not in voice_cpp:
            problems.append(f"ce_cmd_voice.cpp missing required safety gate {req!r}")
    for bad in ("WORLD.", "new ", "malloc(", "FloatRandom", "rand("):
        if bad in hud_cpp:
            problems.append(f"ce_cmd_hud.cpp references forbidden symbol {bad!r}")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cxx", default=shutil.which("c++") or "g++")
    args = ap.parse_args()

    headers = {
        "ce_cmd_hud.h": HUD_H.read_text(),
        "ce_cmd_voice.h": VOICE_H.read_text(),
    }
    ok = True
    compiled, passed, output = compile_and_run(args.cxx, headers, strict=True)
    print(f"{'PASS' if compiled and passed else 'FAIL'} baseline RTS HUD + voice host checks (ASan/UBSan)")
    if not (compiled and passed):
        print(output)
        return 1

    for label, fname, old, new in MUTANTS:
        if headers[fname].count(old) != 1:
            print(f"FAIL mutant {label!r}: anchor found {headers[fname].count(old)} times in {fname}")
            ok = False
            continue
        mutated = dict(headers)
        mutated[fname] = headers[fname].replace(old, new, 1)
        m_comp, m_pass, m_out = compile_and_run(args.cxx, mutated, strict=False)
        if not m_comp:
            print(f"FAIL mutant {label!r}: failed to compile\n{m_out}")
            ok = False
        elif m_pass:
            print(f"FAIL mutant {label!r}: driver unexpectedly passed")
            ok = False
        else:
            print(f"PASS mutant caught: {label}")

    glyph_problems, checked = lint_shown_strings()
    print(f"{'PASS' if not glyph_problems and checked >= 15 else 'FAIL'} bitmap-font glyph lint ({checked} strings checked)")
    for p in glyph_problems:
        print("  " + p)
    ok = ok and not glyph_problems and checked >= 15

    fence_problems = lint_safety_fences()
    print(f"{'PASS' if not fence_problems else 'FAIL'} voice/HUD safety & SPDX fences")
    for p in fence_problems:
        print("  " + p)
    ok = ok and not fence_problems

    print("commander RTS UI suite: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
