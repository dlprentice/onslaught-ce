#!/usr/bin/env python3
"""Compile the current selection adapter against an authored live registry."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/game/ce_playunit.cpp"
DRIVER = ROOT / "port/tools/tests/ce_playunit_selection_driver.cpp"

def definition(text, name):
    spec = importlib.util.spec_from_file_location("camera_extract", ROOT / "port/tools/ce_commander_camera_tests.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.definition(text, name)

def main():
    parent = ROOT / "local-data/ce-playunit-selection-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    source = SOURCE.read_text()
    names = ["SelectionWorldEnded", "CePlayUnitSelectionWorld", "SelectionUnit", "SelectionReady", "CePlayUnitCanTakeSelection", "CePlayUnitTakeSelection"]
    actual = "\n\n".join(definition(source, name) for name in names)
    level = definition(source, "CePlayUnitLevelEnd")
    assert re.search(r"\{\s*SelectionWorldEnded\(\);", level)
    (out / "selection_facts.inc").write_text(definition((ROOT / "port/tools/ce_playunit_driver.cpp").read_text(), "Tank"))
    variants = {"control": actual}
    changes = {
        "stale-world": ("world != gSelectionWorld", "false"),
        "wrong-number": ("thing->GetThingNumber() != expectedNumber", "(expectedNumber, false)"),
        "missing-registry": ("if (thing != candidate) continue;", "if (false) continue;"),
        "wrong-type": ("!(thing->GetThingType() & THING_TYPE_UNIT)", "false"),
        "missing-ready": ("if (!Ready(player, engine, controller))", "if (Ready(player, engine, controller) && false)"),
        "missing-gate": ("!gWanted || !Allowed(gWanted)", "!gWanted"),
        "human-owner": ("if (CePlayUnitOwnsPilot())", "if (false)"),
        "unsupported": ("if (*kind < 0)", "if (false)"),
        "simultaneous-owner": ("if (PortCeCmdMapOpen())", "if (false)"),
        "reused-world": ("if (++gSelectionWorld == 0) ++gSelectionWorld;", "(void)gSelectionWorld;")
    }
    for name, (old, new) in changes.items():
        assert actual.count(old) == 1, name
        variants[name] = actual.replace(old, new)
    results = {}
    env = os.environ.copy()
    env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
    env["UBSAN_OPTIONS"] = "halt_on_error=1"
    for name, body in variants.items():
        folder = out / name
        folder.mkdir()
        (folder / "selection_actual.inc").write_text(body)
        binary = folder / "checks"
        command = ["g++", "-std=c++17", "-DCE_VERSION=1", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(folder), "-I", str(out), "-I", str(ROOT / "port/game"), str(DRIVER), "-o", str(binary)]
        compile_result = subprocess.run(command, capture_output=True, text=True)
        (folder / "compile.private.log").write_text(compile_result.stdout + compile_result.stderr)
        if compile_result.returncode:
            results[name] = {"compiled": False}
            break
        run = subprocess.run([str(binary)], env=env, capture_output=True, text=True)
        (folder / "run.private.log").write_text(run.stdout + run.stderr)
        results[name] = {"compiled": True, "exit": run.returncode, "sanitizer_error": "Sanitizer" in run.stderr or "runtime error:" in run.stderr}
        if name == "control":
            match = re.search(r"checks=(\d+) failures=(\d+) skips=(\d+)", run.stdout)
            results[name]["counts"] = list(map(int, match.groups())) if match else None
            if run.returncode:
                break
    passed = len(results) == len(variants) and results["control"].get("exit") == 0 and all(x["compiled"] and x.get("exit", 0) != 0 and not x.get("sanitizer_error") for n, x in results.items() if n != "control")
    receipt = {"passed": passed, "scope": "source-bound adapter with authored world; no rendered or physical-input claim", "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(), "driver_sha256": hashlib.sha256(DRIVER.read_bytes()).hexdigest(), "results": results}
    (out / "RESULT.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": passed, "checks": results.get("control", {}).get("counts"), "mutants": len(changes), "receipt": str(out / "RESULT.private.json")}))
    return 0 if passed else 1

if __name__ == "__main__":
    raise SystemExit(main())
