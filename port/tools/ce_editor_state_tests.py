#!/usr/bin/env python3
"""Compile the real editor state labels and layout against authored UI inputs."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    if args.out:
        out = args.out.resolve()
        out.mkdir(parents=True, exist_ok=False)
    else:
        parent = ROOT / "local-data/native-editor-state-tests"
        parent.mkdir(parents=True, exist_ok=True)
        out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++14 host compiler is required.")
    ui = ROOT / "port/game/ce_editor_ui.cpp"
    driver = ROOT / "port/tools/tests/ce_editor_state_driver.cpp"
    source = ui.read_text()
    changes = [
        ("dirty-read-as-saved", 'dirty ? " / Draft" : " / Saved"',
         'dirty ? " / Saved" : " / Draft"'),
        ("hold-read-as-reach", 'holdArea ? "Hold Area" : "Reach Point"',
         'holdArea ? "Reach Point" : "Reach Point"'),
        ("selection-hides-placement", 'if (placing) {', 'if (placing && !m.card) {'),
        ("menu-focus-arms-placement", 'if (placing) {',
         'if (placing || m.toolFocus == 0) {'),
        ("move-read-as-select", 'moving ? "Move" : "Select"', 'moving ? "Select" : "Select"'),
        ("missing-selection-identity", 'moving ? "Move" : "Select", m.cardName',
         'moving ? "Move" : "Select", ""'),
        ("readback-clears-selection", 'm.state = dirty ?', 'm.card = false; m.state = dirty ?'),
        ("tool-text-click-through", 'l.mode.Contains(x, y) || l.objective.Contains(x, y)',
         'l.objective.Contains(x, y)'),
        ("marker-over-tool-text", 'Cross(r, l.mode)',
         'false'),
        ("tool-and-route-overlap", 'l.objective.y = l.mode.y + l.mode.h + 3.f * k;',
         'l.objective.y += 0.f;'),
        ("height-row-unmeasured", ' + (lh + 2.f) * k;', ';'),
    ]
    rows = []
    cases = [("production", source)]
    for name, before, after in changes:
        if source.count(before) != (2 if name == 'move-read-as-select' else 1):
            raise SystemExit(f"Control needs exactly one production seam: {name}")
        cases.append((name, source.replace(before, after)))
    for name, text in cases:
        folder = out / name
        folder.mkdir()
        src = folder / "ce_editor_ui.cpp"
        src.write_text(text)
        exe = folder / "state-check"
        command = [compiler, "-std=c++14", "-Wall", "-Wextra", "-Werror",
                   "-I", str(ROOT / "port/game"), str(src), str(driver), "-o", str(exe)]
        built = subprocess.run(command, capture_output=True, text=True, timeout=120)
        (folder / "compile.stdout").write_text(built.stdout)
        (folder / "compile.stderr").write_text(built.stderr)
        code = None
        if built.returncode == 0:
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            (folder / "run.stdout").write_text(result.stdout)
            (folder / "run.stderr").write_text(result.stderr)
            code = result.returncode
        rows.append(dict(case=name, compiled=built.returncode == 0, exit=code,
                         accepted=(built.returncode == 0 and code == 0) if name == "production"
                         else (built.returncode == 0 and code is not None and code != 0)))
    files = [ui, driver, ROOT/"port/game/ce_editor_ui.h", Path(__file__).resolve()]
    receipt = dict(scope="Real state formatter and UI layout only; not a native Play simulation.",
                   passed=all(row["accepted"] for row in rows), controls=rows,
                   inputs={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
                           for f in files})
    (out / "result.json").write_text(json.dumps(receipt, indent=2)+"\n")
    print("editor state clarity:", "PASS" if receipt["passed"] else "FAIL",
          "|", sum(row["compiled"] for row in rows), "compiled cases",
          "|", len(changes), "negative controls", "|", out)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
