#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""dump_diff.py on authored per-update dumps (port/game/updatedump.cpp's format): no game data, no build.

Cases: two equal dumps agree (exit 0); the first difference is reported at the right update and field for the event time, a
thing's axis and a thing gone (exit 1), and an earlier difference wins over a later one; dumps with no update in common are
NOT COMPARABLE (exit 2, never a pass); a journey's result.json is followed to its dump."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

TOOL = Path(__file__).resolve().parent / "dump_diff.py"


def block(update, event="3f800000", things=(("1571", "4345f903", "43a74291", "c18eb3f6"),)):
    lines = [f"U {update} F {update + 50} E {event} R 12345 678 D 3 N {len(things)}"]
    lines += [f"H {n} 1a 10CComponent {x} {y} {z}" for n, x, y, z in things]
    return "\n".join(lines) + "\n"


def write(folder, name, blocks):
    path = folder / name
    path.write_text("".join(blocks))
    return str(path)


def run(*args):
    done = subprocess.run([sys.executable, str(TOOL), *args], capture_output=True, text=True)
    return done.returncode, done.stdout


def main():
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        base = [block(u) for u in range(1, 6)]
        a = write(d, "a.dump", base)
        same = write(d, "same.dump", base)
        rc, out = run(a, same)
        if rc != 0 or "same (5 common" not in out:
            failures.append(f"equal dumps: rc {rc}, {out!r}")

        event = base[:3] + [block(4, event="3f800001")] + base[4:]
        rc, out = run(a, write(d, "event.dump", event))
        if rc != 1 or "update 4: event time" not in out:
            failures.append(f"event time: rc {rc}, {out!r}")

        moved = base[:2] + [block(3, things=(("1571", "4345f903", "43a74291", "c18eb3f7"),))] + [block(4, event="0")] + base[4:]
        rc, out = run(a, write(d, "moved.dump", moved))
        if rc != 1 or "update 3: thing 1571 (10CComponent) z" not in out or "update 4" in out:
            failures.append(f"thing axis (and the earlier difference first): rc {rc}, {out!r}")

        gone = base[:4] + [block(5, things=())]
        rc, out = run(a, write(d, "gone.dump", gone))
        if rc != 1 or "update 5: thing count" not in out:
            failures.append(f"thing gone: rc {rc}, {out!r}")

        disjoint = write(d, "late.dump", [block(u) for u in range(10, 12)])
        rc, out = run(a, disjoint)
        if rc != 2 or "NOT COMPARABLE" not in out:
            failures.append(f"no common update: rc {rc}, {out!r}")

        # a dump cut short agrees over the part both hold, and must still not pass, in either order
        short = write(d, "short.dump", base[:3])
        for x, y in ((a, short), (short, a)):
            rc, out = run(x, y)
            if rc != 1 or "differ in length" not in out:
                failures.append(f"a truncated dump: rc {rc}, {out!r}")

        result = d / "result.json"
        result.write_text(json.dumps({"update_dump": a}))
        rc, out = run(str(result), same)
        if rc != 0:
            failures.append(f"result.json followed to its dump: rc {rc}, {out!r}")

        # a stored base as hashes: equal, then the moved thing's update found without the full base dump
        done = subprocess.run([sys.executable, str(TOOL), "--hashes", a], capture_output=True, text=True)
        hashes = d / "base.hashes"
        hashes.write_text(done.stdout)
        if done.returncode or len(done.stdout.splitlines()) != 5:
            failures.append(f"--hashes: rc {done.returncode}, {done.stdout!r}")
        rc, out = run(str(hashes), same)
        if rc != 0:
            failures.append(f"hashes against an equal dump: rc {rc}, {out!r}")
        rc, out = run(str(hashes), str(d / "moved.dump"))
        if rc != 1 or "update 3:" not in out:
            failures.append(f"hashes against a changed dump: rc {rc}, {out!r}")
        rc, out = run(str(hashes), short)
        if rc != 1 or "differ in length" not in out:
            failures.append(f"hashes against a truncated dump: rc {rc}, {out!r}")
    print(json.dumps({"passed": not failures, "failures": failures}))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
