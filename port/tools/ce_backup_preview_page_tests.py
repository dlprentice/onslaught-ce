#!/usr/bin/env python3
"""Compile the actual game page and check its authored row/action contract."""
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    parent = root / "local-data/port/ce-backup-preview-page-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    (out / "common.h").write_text(
        "#pragma once\n#include <cstddef>\n#include <cstdint>\n"
        "using WCHAR = wchar_t;\nusing BOOL = int;\n"
        "#define TRUE 1\n#define FALSE 0\n"
    )
    source = root / "port/tools/tests/ce_backup_preview_page_driver.cpp"
    exe = out / "page-test"
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    command = ["c++", "-std=c++17", "-fshort-wchar", '-DCE_VERSION="test"', "-O1", "-g",
               "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
               "-I", str(out), str(source), "-o", str(exe)]
    with (out / "build.private.log").open("w") as log:
        built = subprocess.run(command, stdout=log, stderr=log)
    receipt = {"ok": False, "compile_exit": built.returncode,
               "scope": "actual game page; authored controller/platform stubs; no rendering or storage",
               "sources": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in [source, root / "port/game/ce_backup_preview.cpp"]}}
    if built.returncode == 0:
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        (out / "run.private.log").write_text(run.stdout + run.stderr)
        receipt["run_exit"] = run.returncode
        if run.returncode == 0:
            receipt.update(json.loads(run.stdout))
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"ok": receipt["ok"], "receipt": str(out / "result.json")}))
    return 0 if receipt["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
