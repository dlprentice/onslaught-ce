#!/usr/bin/env python3
"""Compile native unit aim ownership against authored targets, without game data.

The runner applies every checked-in Unit.cpp patch in order to a private copy,
then compiles its turret yaw and weapon pitch branches. The fake world separates
the player's aim from the AI target; neither geometry nor timing is a rendering
or hardware measurement.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def block(text, brace):
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[brace:end], end


def function(text, name):
    match = re.search(re.escape(name) + r"\s*\([^;{}]*\)\s*\{", text)
    if not match:
        raise RuntimeError("native aim function missing")
    return block(text, text.index("{", match.start()))[0]


def unit_mirror(folder, before_aim=False):
    dest = folder / "Unit.cpp"
    dest.write_text((ROOT / "src/Unit.cpp").read_text())
    for patch in sorted((ROOT / "port/patches").glob("*.patch")):
        if before_aim and patch.name.startswith("0275-"):
            continue
        parts = re.split(r"(?=^--- )", patch.read_text(), flags=re.M)
        selected = "".join(part for part in parts
                           if re.search(r"^\+\+\+ (?:[ab]/)?Unit\.cpp(?:\s|$)", part, re.M))
        if selected:
            result = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(folder)],
                                    input=selected, text=True, capture_output=True)
            if result.returncode:
                raise RuntimeError("Unit.cpp patch did not apply: " + patch.name)
    return dest.read_text()


def native_aim(text):
    clean = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    move = function(clean, "CUnit::Move")
    mark = move.index("target->GetTargetablePos")
    stack = []
    for pos, char in enumerate(move[:mark]):
        if char == "{":
            stack.append(pos)
        elif char == "}":
            stack.pop()
    # Outer function, then native live-unit turret branch, then target branch.
    yaw, _ = block(move, stack[1])
    pitch = function(clean, "CUnit::Unknown4fb280")
    mark = pitch.index("target->GetTargetablePos")
    stack = []
    for pos, char in enumerate(pitch[:mark]):
        if char == "{":
            stack.append(pos)
        elif char == "}":
            stack.pop()
    brace = stack[1]
    start = pitch.rfind("if", 0, brace)
    aim, end = block(pitch, brace)
    tail = re.match(r"\s*else\s*\{", pitch[end:])
    if tail:
        _, stop = block(pitch, end + tail.end() - 1)
        end = stop
    else:
        tail = re.match(r"\s*else\s+[^;]+;", pitch[end:])
        if tail:
            end += tail.end()
    # Retain local target declarations and ownership hook before the branch.
    prefix = pitch[1:start]
    return ("void CUnit::YawStep() { const float mult=1.0f; " + yaw + " }\n"
            "void CUnit::PitchStep() { " + prefix + pitch[start:end] + " }\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--causal-controls", action="store_true")
    args = parser.parse_args()
    parent = args.output or ROOT / "local-data/ce-playunit-aim-tests"
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="run-", dir=parent) as tmp:
        work = Path(tmp)
        mirror = unit_mirror(work, before_aim=args.baseline)
        source = (ROOT / "port/tools/tests/ce_playunit_aim_driver.cpp").read_text()
        owner = (ROOT / "port/game/ce_playunit.cpp").read_text()
        pieces = [native_aim(mirror), function(owner, "PortCePlayUnitAimPoint"), function(owner, "DriveFire")]
        variants = [("native", pieces)]
        if args.causal_controls:
            if args.baseline:
                parser.error("causal controls exercise the current owner")
            changes = [
                ("yaw-still-ai", 0, "const bool playerAiming = PortCePlayUnitAimPoint(this, playerAim);",
                 "const bool playerAiming = false;", 1),
                ("pitch-still-ai", 0, "const bool playerAiming = PortCePlayUnitAimPoint(this, playerAim);",
                 "const bool playerAiming = false;", 2),
                ("other-unit-owned", 1, "unit != Held() || ", "", 1),
                ("handback-owned", 1, "!gDriving || ", "", 1),
                ("mode-fence-ignored", 1, " || !Allowed(true)", "", 1),
                ("invalid-point-reused", 1, "!gAimValid || ", "", 1),
                ("idle-aim-missing", 2, "gAimValid = haveAim;", "gAimValid = haveAim && held;", 1),
            ]
            for label, index, old, new, occurrence in changes:
                changed = list(pieces)
                locations = [m.start() for m in re.finditer(re.escape(old), changed[index])]
                if len(locations) < occurrence:
                    raise RuntimeError("causal mutation no longer applies: " + label)
                start = locations[occurrence - 1]
                changed[index] = changed[index][:start] + new + changed[index][start + len(old):]
                variants.append((label, changed))
        records = []
        for label, selected in variants:
            generated = source.replace("@NATIVE_AIM@", selected[0])
            generated = generated.replace("@OWNER_API@", "bool PortCePlayUnitAimPoint(const CUnit* unit,FVector& point) " + selected[1])
            generated = generated.replace("@DRIVE_FIRE@", "void DriveFire(CUnit* u,const FVector& aim,bool haveAim,int state,bool held) " + selected[2])
            cpp, exe = work / (label + ".cpp"), work / label
            cpp.write_text(generated)
            result = subprocess.run(["g++", "-std=c++11", "-O0", "-Wall", "-Wextra",
                                     "-I", str(ROOT / "port/game"), str(cpp), "-o", str(exe)],
                                    text=True, capture_output=True)
            (parent / (label + "-compile.private.log")).write_text(result.stdout + result.stderr)
            if result.returncode:
                print("FAIL fixture compilation: " + label + "; details retained privately")
                return 1
            mode = "baseline" if args.baseline else "human"
            result = subprocess.run([str(exe), mode], text=True, capture_output=True)
            (parent / (label + "-result.private.log")).write_text(result.stdout + result.stderr)
            expected = result.returncode == 0 if label == "native" else result.returncode == 1
            records.append({"case": label, "compile_exit": 0, "run_exit": result.returncode,
                            "expected": expected, "checks_passed": result.stdout.count("PASS "),
                            "checks_failed": result.stdout.count("FAIL ")})
            if label == "native":
                print(result.stdout, end="")
            else:
                print(("PASS" if expected else "FAIL") + " rejected compiled control: " + label)
        (parent / "result.private.json").write_text(json.dumps({"cases": records, "skipped": 0}, indent=2) + "\n")
        return 0 if all(record["expected"] for record in records) else 1


if __name__ == "__main__":
    raise SystemExit(main())
