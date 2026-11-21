#!/usr/bin/env python3
"""Compile actual authored squad dispatch bodies with synthetic native setters.

The setters observe token state at the first native write. This tests ordering,
including the normal-squad path that does not call SetMembers. It does not claim
native movement or full-game acceptance.
"""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/game/ce_commander.cpp"
DRIVER = ROOT / "port/tools/tests/ce_cmdunit_scope_driver.cpp"


def body(text, name):
    match = re.search(r"^(?:bool|void) " + name + r"\([^;]*?\)\s*\{", text, re.M)
    if not match:
        raise ValueError("missing authored function: " + name)
    opening = text.index("{", match.start())
    depth = 0
    for end in range(opening, len(text)):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if not depth:
                return text[match.start():end + 1]
    raise ValueError("unfinished authored function: " + name)


def extract(text):
    scope = re.search(r"bool gExplicitUnitReplacement = false;\n"
                      r"struct ExplicitUnitReplacement \{.*?\n\};", text, re.S)
    if not scope:
        raise ValueError("missing explicit scope")
    for name in ("Apply", "CeCmdIssue"):
        if not re.search(r"\{\s*ExplicitUnitReplacement replacingUnits;", body(text, name)):
            raise ValueError("explicit entry is unscoped: " + name)
    for name in ("Maintain", "Follow"):
        current = body(text, name)
        if "ExplicitUnitReplacement" in current or "CeCmdUnitOrderRelease" in current:
            raise ValueError("routine entry releases ownership: " + name)
    return scope.group(0) + "\n" + "\n".join(
        body(text, name) for name in ("ReleaseMemberOrders", "SetMembers", "Plant", "Hold")) + "\n"


def replace_body(text, name, old, new):
    original = body(text, name)
    if original.count(old) != 1:
        raise ValueError("mutation needle is not unique: " + name)
    return text.replace(original, original.replace(old, new), 1)


def main():
    parent = ROOT / "local-data/ce-cmdunit-scope-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    text = SOURCE.read_text()
    variants = {"control": text}
    variants["normal_orders_keep_token"] = replace_body(
        text, "ReleaseMemberOrders", "if (!gExplicitUnitReplacement) return;",
        "if (!gExplicitUnitReplacement || r.normal) return;")
    variants["routine_follow_releases_token"] = replace_body(
        text, "ReleaseMemberOrders", "if (!gExplicitUnitReplacement) return;", "(void)0;")
    for name in ("Apply", "CeCmdIssue"):
        variants["unscoped_" + name] = replace_body(
            text, name, "ExplicitUnitReplacement replacingUnits;", "(void)0;")
    results = {}
    compiler = shutil.which("c++")
    if not compiler:
        raise RuntimeError("C++ compiler required")
    for name, source in variants.items():
        folder = out / name
        folder.mkdir()
        try:
            selected = extract(source)
        except ValueError as error:
            results[name] = {"structural_rejection": str(error)}
            continue
        (folder / "commander_scope.inc").write_text(selected)
        command = [compiler, "-std=c++17", "-O1", "-include", "initializer_list",
                   "-I" + str(folder), str(DRIVER), "-o", str(folder / "checks")]
        compiled = subprocess.run(command, capture_output=True, text=True)
        (folder / "compile.private.log").write_text(compiled.stdout + compiled.stderr)
        record = {"compiled": compiled.returncode == 0}
        if compiled.returncode == 0:
            ran = subprocess.run([str(folder / "checks")], capture_output=True, text=True)
            (folder / "run.private.log").write_text(ran.stdout + ran.stderr)
            record["exit"] = ran.returncode
            record["failure_label"] = next((line[5:] for line in ran.stderr.splitlines() if line.startswith("FAIL ")), None)
            match = re.search(r"checks=(\d+) failures=(\d+) skips=(\d+)", ran.stdout)
            record["counts"] = list(map(int, match.groups())) if match else None
        results[name] = record
    passed = results["control"].get("exit") == 0 and results["control"].get("counts") == [76, 0, 0] and all(
        record.get("structural_rejection") or
        (record.get("compiled") and record.get("exit") == 1 and record.get("failure_label") == {"normal_orders_keep_token": "release precedes squad state", "routine_follow_releases_token": "routine follow does not release temporary destinations"}.get(name))
        for name, record in results.items() if name != "control")
    report = {"passed": bool(passed), "results": results,
              "inputs": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in (SOURCE, DRIVER)},
              "scope": "Actual authored dispatch bodies and synthetic native setters; two named compiled ordering negatives and two lexical entry-scope negatives"}
    (out / "RESULT.private.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": bool(passed), "checks": results["control"].get("counts"),
                      "negative_controls": len(variants) - 1,
                      "receipt": str(out / "RESULT.private.json")}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
