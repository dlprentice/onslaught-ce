#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""shot_diff.py on authored frames: no game data, no build.

Equal frames pass; one changed pixel fails with its box and share; a tolerance above the change passes; --max-changed lets a
small share through; a region away from the change passes; different sizes and a missing frame are NOT COMPARABLE (exit 2);
a journey result is followed to its shot."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

TOOL = Path(__file__).resolve().parent / "shot_diff.py"


def run(*args):
    done = subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True)
    return done.returncode, (json.loads(done.stdout) if done.stdout.strip().startswith("{") else {"raw": done.stdout + done.stderr})


def main():
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        base = Image.new("RGB", (64, 48), (40, 80, 120))
        base.save(d / "a.png")
        base.save(d / "same.png")
        one = base.copy()
        one.putpixel((10, 20), (40, 80, 125))
        one.save(d / "one.png")
        Image.new("RGB", (32, 48), (40, 80, 120)).save(d / "small.png")

        rc, r = run(d / "a.png", d / "same.png")
        if rc != 0 or r.get("changed_pixels") != 0:
            failures.append(f"equal: {rc} {r}")
        rc, r = run(d / "a.png", d / "one.png")
        if rc != 1 or r.get("changed_pixels") != 1 or r.get("changed_box") != [10, 20, 11, 21] or r.get("largest_change") != 5:
            failures.append(f"one pixel: {rc} {r}")
        rc, r = run(d / "a.png", d / "one.png", "--tolerance", 5)
        if rc != 0:
            failures.append(f"within tolerance: {rc} {r}")
        rc, r = run(d / "a.png", d / "one.png", "--max-changed", 0.001)
        if rc != 0:
            failures.append(f"within max-changed: {rc} {r}")
        rc, r = run(d / "a.png", d / "one.png", "--region", "30,0,20,20")
        if rc != 0:
            failures.append(f"region away from the change: {rc} {r}")
        rc, r = run(d / "a.png", d / "small.png")
        if rc != 2:
            failures.append(f"different sizes: {rc} {r}")
        rc, r = run(d / "a.png", d / "missing.png")
        if rc != 2:
            failures.append(f"missing frame: {rc} {r}")
        for name, img in (("ra", base), ("rb", one)):
            (d / name).mkdir()
            img.save(d / name / "hud.png")
            (d / name / "result.json").write_text("{}")
        rc, r = run(d / "ra" / "result.json", d / "rb" / "result.json", "--label", "hud")
        if rc != 1 or r.get("changed_pixels") != 1:
            failures.append(f"journey results: {rc} {r}")
    print(json.dumps({"passed": not failures, "failures": failures}))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
