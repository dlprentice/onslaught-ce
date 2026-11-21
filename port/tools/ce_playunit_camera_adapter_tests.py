#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile actual possession-camera adapter regions against an authored host world."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/game/ce_playunit.cpp"
DRIVER = ROOT / "port/tools/tests/ce_playunit_camera_adapter_driver.cpp"


def function(text, name):
    match = re.search(r"(?m)^.*\b" + re.escape(name) + r"\([^;]*?\)\s*\{", text)
    if not match:
        raise ValueError("missing adapter function: " + name)
    start = text.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end]


def fixture(source):
    start = source.index("CePlayUnitCameraRig gUnitViewRig")
    end = source.index("FVector FootAimEye(", start)
    adapter = source[start:end]
    ground = function(adapter, "ClampUnitViewGround")
    adapter = adapter.replace(ground, "FVector ClampUnitViewGround(FVector eye);", 1)
    drive = function(source, "Drive")
    start = drive.index("    if (gViewMode == kViewLook && zoomIn != zoomOut)")
    opening = drive.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (drive[end] == "{") - (drive[end] == "}")
        end += 1
    zoom = drive[start:end]
    fire = function(source, "FireTick")
    match = re.search(r"FVector eye\s*=\s*([^;]+);", fire)
    if not match:
        raise ValueError("missing production fire origin")
    return DRIVER.read_text().replace("@ADAPTER@", adapter).replace("@ZOOM@", zoom).replace("@FIRE_EYE@", match.group(1))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=ROOT / "local-data/ce-playunit-camera-adapter")
    ap.add_argument("--causal-controls", action="store_true")
    ap.add_argument("--sanitize", action="store_true")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=args.out))
    source = SOURCE.read_text()
    cpp = fixture(source)
    cases = [("normal", cpp, False, False)]
    if args.causal_controls:
        mutations = [
            ("ignore-scale", "const float scale = unit->GetRenderScale();", "const float scale = 1.0f;"),
            ("stale-owner", "gUnitViewMade && gUnitViewState.identity == identity", "gUnitViewMade"),
            ("advance-per-query", "frame != gUnitViewFrame || !gUnitViewNs", "true"),
            ("advance-paused", "GAME.IsPaused() ? 0.0f : dt", "dt"),
            ("hide-at-request", "? gUnitViewHideBody : gFootFirstPerson", "? gFootFirstPerson : gFootFirstPerson"),
            ("omit-vehicle-eye", "gOnFoot ? CePlayUnitCameraFootEye(lo, hi, eye) : CePlayUnitCameraVehicleEye(lo, hi, eye)", "gOnFoot && CePlayUnitCameraFootEye(lo, hi, eye)"),
            ("vehicle-never-hides", "gUnitViewHideBody = pose.hideBody;", "gUnitViewHideBody = gOnFoot && pose.hideBody;"),
            ("narrow-foot-pitch", "if (gOnFoot) { law.pitchUp = -1.30f; law.pitchDown = 1.30f; }", ""),
            ("old-foot-fire-origin", "(gOnFoot && gViewMode != kViewLook) ? FootAimEye(u->GetPos()) : gCamera->GetPos()", "gOnFoot ? FootAimEye(u->GetPos()) : gCamera->GetPos()"),
            ("retain-rig-on-return", "gUnitViewMade = gUnitViewHideBody = false;", "gUnitViewHideBody = false;"),
        ]
        for name, old, new in mutations:
            if cpp.count(old) != 1:
                raise ValueError("mutation must have one operative anchor: " + name)
            cases.append((name, cpp.replace(old, new), True, False))
    if args.sanitize:
        cases.append(("sanitized", cpp, False, True))
    result = []
    for name, text, negative, sanitize in cases:
        case = out / name
        case.mkdir()
        inp, exe = case / "adapter.cpp", case / "adapter"
        inp.write_text(text)
        command = ["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "port/game"), str(inp), "-o", str(exe)]
        if sanitize:
            command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        with (case / "compile.log").open("w") as log:
            compiled = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        rc, checks, failed = None, 0, 0
        if compiled.returncode == 0:
            with (case / "run.log").open("w") as log:
                run = subprocess.run([str(exe)], stdout=log, stderr=subprocess.STDOUT)
            rc = run.returncode
            match = re.search(r"checks=(\d+) failed=(\d+)", (case / "run.log").read_text())
            if match:
                checks, failed = map(int, match.groups())
        passed = compiled.returncode == 0 and checks > 250 and ((rc == 1 and failed > 0) if negative else (rc == 0 and failed == 0))
        result.append(dict(name=name, compile_exit=compiled.returncode, run_exit=rc, checks=checks, failed=failed, negative=negative, passed=passed))
    receipt = {"source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(), "driver_sha256": hashlib.sha256(DRIVER.read_bytes()).hexdigest(), "cases": result, "skipped": 0, "limits": ["Authored host world; native terrain result is a declared seam.", "No native renderer, animation, image, asset or whole-build claim."]}
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": sum(r["passed"] for r in result), "cases": len(result), "skipped": 0, "result": str(out / "result.json")}))
    return 0 if all(r["passed"] for r in result) else 1


if __name__ == "__main__":
    raise SystemExit(main())
