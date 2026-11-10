#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Object-level check of the museum's host/game boundary in a finished build tree (both targets).

    python3 port/tools/ce_museum_visit_objects_check.py [BUILD_DIR] [--json OUT]

BUILD_DIR holds linux/ and windows/ (default: this checkout's local-data/port/build). It reads the generated build.ninja and the
compiled objects with llvm-nm and llvm-objdump; it runs nothing, loads no game data and prints only verdicts and counts:

  - rules      the cache provider and the room writer were built by the host rule (pcxx), the engine adapter and the gallery by
               the game rule (qcxx);
  - renames    the host objects carry none of the game rule's symbol renames (allocator, x87 maths, retail C runtime);
  - allocator  the host objects bind the host operator new and delete;
  - names      every CeMuseumVisit / PortCeMuseumVisit symbol any object of the tree references is defined by exactly one museum
               object, under the same mangled name;
  - returns    no function that crosses the boundary (the CeMuseumVisit and PortCeMuseumVisit functions the museum objects define,
               and the engine adapter's two callbacks) returns with a callee-pops immediate: they all keep the default caller-pops
               convention the other side calls them with.
Exit status 0 only when every check holds on both targets."""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build  # noqa: E402

HOST = ("ce_museum_visit.cpp", "ce_museum_visit_generate.cpp")
GAME = ("ce_museum_visit_engine.cpp", "ce_museum.cpp")
UNITS = HOST + GAME
CALLBACKS = ("_GLOBAL__N_1", ("7Receive", "10ReadSource"))
RENAMES = set(build.GAME_ALLOCATOR.values()) | set(build.GAME_MATH.values()) | set(build.GAME_CRT.values())


def symbols(path):
    """[(name, type)] of an object, with the i686 COFF leading underscore removed."""
    out = subprocess.run(["llvm-nm", "--format=posix", str(path)], capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            rows.append((parts[0], parts[1]))
    return rows


def functions(path):
    """{symbol: [return instructions' operand text]} for every function in the object's code."""
    out = subprocess.run(["llvm-objdump", "-d", "--no-show-raw-insn", str(path)], capture_output=True, text=True, check=True).stdout
    found, current = {}, None
    for line in out.splitlines():
        header = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if header:
            current = found.setdefault(header.group(1), [])
            continue
        ret = re.match(r"^\s*[0-9a-f]+:\s+ret[lwq]?\s*(.*)$", line)
        if ret and current is not None:
            current.append(ret.group(1).strip())
    return found


def boundary(name):
    return "CeMuseumVisit" in name or "PortCeMuseumVisit" in name


def callback(name):
    return CALLBACKS[0] in name and any(token in name for token in CALLBACKS[1])


def check(build_dir):
    report = {}
    for target in ("linux", "windows"):
        root = build_dir / target
        strip = "_" if target == "windows" else ""
        ninja = (root / "build.ninja").read_text()
        objects = {unit: root / "port/game" / (unit + ".o") for unit in UNITS}
        verdict = {"objects": all(path.is_file() for path in objects.values())}
        edges = {}
        for line in ninja.splitlines():
            found = re.match(r"build \S+/port/game/(ce_museum\w*\.cpp)\.o: (\w+) ", line)
            if found:
                edges[found.group(1)] = found.group(2)
        verdict["rules"] = edges == {**{u: "pcxx" for u in HOST}, **{u: "qcxx" for u in GAME}}
        if not verdict["objects"]:
            report[target] = verdict
            continue
        table = {unit: [(name[len(strip):] if strip and name.startswith(strip) else name, kind)
                        for name, kind in symbols(path)] for unit, path in objects.items()}
        verdict["renames"] = not any(name in RENAMES for unit in HOST for name, _ in table[unit])
        referenced = {name for unit in HOST for name, kind in table[unit] if kind == "U"}
        verdict["allocator"] = "_Znwj" in referenced and bool(referenced & {"_ZdlPv", "_ZdlPvj"})
        defined = {}
        for unit in UNITS:
            for name, kind in table[unit]:
                if kind in "TtWwDdBbRr" and boundary(name):
                    defined.setdefault(name, []).append(unit)
        wanted = set()
        for path in sorted(root.rglob("*.o")):
            wanted |= {name[len(strip):] if strip and name.startswith(strip) else name
                       for name, kind in symbols(path) if kind == "U" and boundary(name)}
        verdict["names"] = bool(wanted) and all(len(defined.get(name, [])) == 1 for name in wanted)
        verdict["names_checked"] = len(wanted)
        checked, returns_seen, popped = 0, 0, []
        for unit in UNITS:
            for name, returns in functions(objects[unit]).items():
                bare = name[len(strip):] if strip and name.startswith(strip) else name
                if (boundary(bare) and defined.get(bare) == [unit]) or (unit == "ce_museum_visit_engine.cpp" and callback(bare)):
                    checked += 1
                    returns_seen += len(returns)
                    popped += [unit for operand in returns if operand]
        verdict["returns"] = checked > 0 and returns_seen > 0 and not popped
        verdict["functions_checked"] = checked
        verdict["returns_inspected"] = returns_seen
        report[target] = verdict
    report["passed"] = all(v.get(k) is True for t in ("linux", "windows") for v in [report[t]]
                           for k in ("objects", "rules", "renames", "allocator", "names", "returns"))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build_dir", nargs="?", type=Path, default=build.BUILD)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    report = check(args.build_dir)
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.json:
        args.json.write_text(text)
    print(text, end="")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
