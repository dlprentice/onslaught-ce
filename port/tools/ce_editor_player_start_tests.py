#!/usr/bin/env python3
"""Authored player-start document/history/dispatch controls; no game data or GPU."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FILES = ("port/game/ce_editor.cpp", "port/game/ce_editor.h", "port/game/ce_project.cpp",
         "port/game/ce_project.h", "port/tools/tests/ce_editor_player_start_driver.cpp")

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--causal-controls", action="store_true")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    before = {f: digest(ROOT / f) for f in FILES}
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++ compiler is required.")
    editor = (ROOT / FILES[0]).read_text()
    project = (ROOT / FILES[2]).read_text()
    def run(name: str, owner: str, old: str = "", new: str = "") -> dict:
        out = args.out / name
        out.mkdir()
        texts = {"editor": editor, "project": project}
        if old:
            if texts[owner].count(old) != 1:
                raise RuntimeError(f"Control {name} no longer names one exact behavior")
            texts[owner] = texts[owner].replace(old, new, 1)
        for key, text in texts.items():
            (out / (key + ".cpp")).write_text(text)
        command = [compiler, "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-fsanitize=undefined", "-fno-sanitize-recover=all",
                   "-I" + str(ROOT / "port/game"), str(ROOT / FILES[-1]),
                   str(out / "editor.cpp"), str(out / "project.cpp"), "-o", str(out / "driver")]
        compiled = subprocess.run(command, capture_output=True, timeout=60, check=False)
        (out / "compile.private.log").write_bytes(compiled.stdout + compiled.stderr)
        result = {"compiled": compiled.returncode == 0, "exit": None}
        if result["compiled"]:
            played = subprocess.run([str(out / "driver")], capture_output=True, timeout=10, check=False)
            (out / "checks.private.log").write_bytes(played.stdout + played.stderr)
            result["exit"] = played.returncode
        return result
    baseline = run("baseline", "editor")
    controls = {}
    if args.causal_controls and baseline == {"compiled": True, "exit": 0}:
        mutations = [
            ("undo-start-fallthrough", "editor", "playerStart_ = change.oldStart; selected_ = PlayerStartSelection; return true;", "playerStart_ = change.oldStart; selected_ = PlayerStartSelection;"),
            ("redo-start-fallthrough", "editor", "playerStart_ = change.newStart; selected_ = PlayerStartSelection; return true;", "playerStart_ = change.newStart; selected_ = PlayerStartSelection;"),
            ("saved-start-forgotten", "editor", "savedPlayerStart_ = playerStart_;", "/* do not save the start */"),
            ("dirty-start-hidden", "editor", "if (!SameStart(playerStart_, savedPlayerStart_)) return true;", "/* ignore start dirtiness */"),
            ("cancel-start-keeps-drag", "editor", "if (!accept) { playerStart_ = movePlayerStart_; return true; }", "if (!accept) { return true; }"),
            ("return-loses-saved-start", "editor", "savedPlayerStart_ = before.savedPlayerStart_;", "savedPlayerStart_ = {};"),
            ("start-only-map-refused", "editor", "if (playerStartAvailable_) { sealed_ = true; return true; }", "/* no named actor, no editor */"),
            ("v7-not-admitted", "project", "version == 6 || version == 7),", "version == 6),"),
            ("implicit-update-loses-start", "project", "playerStart ? *playerStart : document->project.playerStart", "playerStart ? *playerStart : CeProjectPlayerStart{}"),
            ("wrong-primary-start", "project", "if (playerNumber != (inverted ? 2 : 1)) return false;", "if (playerNumber != (inverted ? 1 : 2)) return false;"),
            ("flight-origin-lost", "project", "PortCeEditorApplyPlayerStart(x, y, z, flying);", "PortCeEditorApplyPlayerStart(x, y, z, !flying);"),
            ("carrier-leaves-start", "editor", "moveCarriesStart_ = carryStart && playerStartAvailable_;", "moveCarriesStart_ = carryStart && !playerStartAvailable_;"),
            ("carrier-undo-strands-start", "editor", "if (change.startFollows) playerStart_ = change.oldStart;", "/* the start stays where the carrier took it */"),
            ("carrier-cancel-strands-start", "editor", "if (carried) playerStart_ = movePlayerStart_;", "/* the start stays where the drag took it */"),
        ]
        for name, owner, old, new in mutations:
            controls[name] = run(name, owner, old, new)
    after = {f: digest(ROOT / f) for f in FILES}
    passed = before == after and baseline == {"compiled": True, "exit": 0}
    if args.causal_controls:
        passed = passed and len(controls) == 14 and all(x["compiled"] and x["exit"] not in (0, None) for x in controls.values())
    result = {"passed": passed, "scope": "Authored document/history/dispatch; native collision and gameplay are separate acceptance.",
              "before": before, "after": after, "baseline": baseline, "controls": controls}
    (args.out / "RESULT.private.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": passed, "baseline": baseline, "compiled_rejected_controls": sum(x["compiled"] and x["exit"] not in (0, None) for x in controls.values())}))
    return 0 if passed else 1

if __name__ == "__main__":
    raise SystemExit(main())
