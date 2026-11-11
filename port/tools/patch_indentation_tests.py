#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""No patch introduces misleading indentation into the game's sources, by the compiler's own judgement.

The game's translation units compile with every warning off (build.py COMMON ends with -w), so clang's
-Wmisleading-indentation never shows: patch 0208 once put a counter call between an unbraced `if` and its body, and patch
0206 an overlay call after one, indented as if guarded. This check builds two scratch mirrors with the build's own mirror
builder (premirror.scratch_mirror), in the public build's form (build.py --public): the unpatched one and the one with
every patch applied. It compiles each game
translation unit of both syntax-only with the build's compiler, flags, include paths and the shipped builds' defines, but
with -w replaced by -Wno-everything -Wmisleading-indentation, so that warning is the only one. The original's own
indentation oddities are in both; a warning is named by its file and the text of its two lines (the statement and the
control statement it seems to belong to), not by line numbers, which patches move. Any warning the patched mirror has
more often than the unpatched one is a finding.

Flavours compiled from the patched mirror: the default public build (Linux and Windows targets: CE_VERSION, the public
graphics host, CE_NETWORK_OFF) and the public build with the network and text chat compiled in (Linux). The unpatched
mirror is compiled once per target.

Controls (each applied to a copy of the queue in a third mirror, which must then build at fuzz 0): patch 0208 without
the braces round its tree imposter counter, and patch 0206 without the braces round the commander map and editor overlay
calls (each mutant is byte for byte the patch as it was before its fix). Each must give exactly one finding, in its own
file. Run on the queue before those fixes, the main pass reports the same finding.

    python3 port/tools/patch_indentation_tests.py [-j N] [--keep DIR] [--no-controls]

Needs the dependency cache (the pinned compilers, BEA_PORT_DATA) and the game data the mirror pins (BEA_GAME_DIR). A
finding names the file and line numbers only; no source text is printed.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import types
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parent
sys.path.insert(0, str(PORT))
sys.path.insert(0, str(PORT / "tools"))
import build  # noqa: E402
import premirror  # noqa: E402

ONLY = ["-Wno-everything", "-Wmisleading-indentation", "-ferror-limit=0", "-fno-color-diagnostics",
        "-fno-caret-diagnostics", "-fdiagnostics-absolute-paths"]
PUBLIC = ['-DCE_VERSION="check"', "-DBEA_PUBLIC_GRAPHICS_HOST=1"]
FLAVOURS = {          # name: (target, defines)
    "linux-public": ("linux", PUBLIC + ["-DCE_NETWORK_OFF=1"]),
    "windows-public": ("windows", PUBLIC + ["-DCE_NETWORK_OFF=1"]),
    "linux-network": ("linux", PUBLIC + ["-DCE_NETWORK_TEXT=1"]),
}
WARNING = re.compile(r"^(?P<file>/[^:]+):(?P<line>\d+):\d+: warning: misleading indentation;[^\n]*\n"
                     r"(?:(?!\S+:\d+:\d+: (?:warning|error|note):).*\n)*?"
                     r"(?P<nfile>/[^:]+):(?P<nline>\d+):\d+: note: previous statement is here", re.M)


def flags(target, mirror, defines):
    """The cxx rule's command (build.ninja_file) for one target, as a list, with only the one warning on."""
    t = build.TARGETS[target]
    inc = [PORT / "d3dx/include", PORT / "include", PORT / "game"] + t["includes"] + [mirror, mirror / "MissionScript"]
    common = [f for f in build.COMMON if f not in ("-w",) and not f.startswith("-ferror-limit")]
    common += build.X87 + build.NO_MATH_BUILTINS + defines + ["-include", str(PORT / "include/port.h")]
    common += [f"-I{p}" for p in inc]
    return t, common + ONLY


def compile_all(mirror, target, defines, jobs):
    """(Counter of (file under the mirror, statement text, control text), the number of units with a fatal error, the
    number of units, {key: [statement line numbers]}) over every game translation unit of the mirror."""
    t, common = flags(target, mirror, defines)
    # A #line directive (the sources keep __LINE__ as the original's) makes clang report a presumed line: blank each one
    # (the line count stays) so that a warning names the line the file holds.
    for path in mirror.parent.rglob("*"):
        if path.is_file() and path.suffix.lower() in (".c", ".cpp", ".h", ".inc", ".hpp", ".inl"):
            text = path.read_bytes()
            if b"#line" in text or b"# line" in text:
                blanked = re.sub(rb"(?m)^[ \t]*#[ \t]*line\b[^\r\n]*", b"", text)
                if blanked != text:
                    path.write_bytes(blanked)
    units = sorted(p for p in mirror.parent.rglob("*") if p.suffix.lower() in (".c", ".cpp") and p.is_file())

    def one(unit):
        if unit.suffix.lower() == ".c":
            cmd = t["cc"] + ["-std=gnu99"] + common + ["-fsyntax-only", str(unit)]
        else:
            cmd = t["cxx"] + ["-std=gnu++14"] + build.MSCXX + common + ["-fsyntax-only", str(unit)]
        r = subprocess.run(cmd, capture_output=True, text=True, errors="replace", cwd=mirror)
        return r.stderr

    found, fatal = {}, 0
    with ThreadPoolExecutor(jobs) as pool:
        for log in pool.map(one, units):
            fatal += "fatal error:" in log
            for m in WARNING.finditer(log):
                found[(m["file"], int(m["line"]), m["nfile"], int(m["nline"]))] = True   # a header's warning, once
    root = mirror.parent.resolve()
    keys, where = Counter(), {}
    for file, line, nfile, nline in sorted(found):
        path = Path(file).resolve()
        if not path.is_relative_to(root):
            continue                   # the port's own headers are not game text
        lines = re.split(r"\r\n|\r|\n", path.read_text(encoding="latin-1"))      # as clang counts lines
        key = (str(path.relative_to(root)), lines[line - 1].strip(), lines[nline - 1].strip() if Path(nfile).resolve() == path else "")
        keys[key] += 1
        where.setdefault(key, []).append(line)
    return keys, fatal, len(units), where


def findings(base, patched):
    """[(file, statement text, control text, extra count)] for every warning the patched mirror has more often."""
    return sorted((k[0], k[1], k[2], n - base.get(k, 0)) for k, n in patched.items() if n > base.get(k, 0))


HEADER = re.compile(r"^@@ -(\d+)(,\d+)? \+(\d+)(?:,(\d+))? @@")


def unbrace(patch_text, marker):
    """The patch with the added '{' and '}' lines round the added line that holds MARKER taken away, or None when there is
    no such pair: the hunk keeps as many context lines before its first change as after its last (patch reads unequal
    context as fuzz), and its header and the later hunks of the same file move to match."""
    lines = patch_text.split("\n")
    at = [i for i, line in enumerate(lines) if line.startswith("+") and marker in line]
    if len(at) != 1:
        return None
    i = at[0]
    opening = next((k for k in range(i - 1, max(i - 6, 0), -1) if lines[k][:1] == "+" and lines[k][1:].strip() == "{"), None)
    closing = next((k for k in range(i + 1, min(i + 6, len(lines))) if lines[k][:1] == "+" and lines[k][1:].strip() == "}"), None)
    if opening is None or closing is None:
        return None
    header = next(k for k in range(opening, -1, -1) if HEADER.match(lines[k]))
    end = next((k for k in range(header + 1, len(lines)) if not lines[k][:1] in (" ", "-", "+", "\\") or HEADER.match(lines[k])
                or lines[k].startswith(("--- ", "+++ "))), len(lines))
    body = [line for k, line in enumerate(lines[header + 1:end], header + 1) if k not in (opening, closing)]
    m = HEADER.match(lines[header])
    old_start, new_start = int(m[1]), int(m[3])
    changed = [k for k, line in enumerate(body) if line[:1] in "+-"]
    lead, trail = changed[0], len(body) - 1 - changed[-1]
    while lead > trail:
        body, lead, old_start, new_start = body[1:], lead - 1, old_start + 1, new_start + 1
    while trail > lead:
        body, trail = body[:-1], trail - 1
    old = sum(line[:1] in " -" for line in body)
    new = sum(line[:1] in " +" for line in body)
    out = lines[:header] + [f"@@ -{old_start},{old} +{new_start},{new} @@" + lines[header][m.end():]] + body
    rest = lines[end:]
    for k, line in enumerate(rest):
        if line.startswith("--- "):
            break
        n = HEADER.match(line)
        if n:
            rest[k] = f"@@ -{n[1]}{n[2] or ''} +{int(n[3]) - 2}{',' + n[4] if n[4] else ''} @@" + line[n.end():]
    return "\n".join(out + rest)


CONTROLS = [   # (patch, marker of the guarded added line, the file the finding must be in)
    ("0208-ce-full-models.patch", "CE_MODEL_TREE_IMPOSTOR", "src/RTTree.cpp"),
    ("0206-native-editor-lifecycle.patch", "PortCeEditorRender();", "src/DXEngine.cpp"),
]


def mirror_with(out, patches=None, below=None):
    """A scratch mirror under OUT (premirror.scratch_mirror); PATCHES, a folder, stands in for port/patches in its patch loop."""
    if patches is None:
        return premirror.scratch_mirror(out, below)
    real = subprocess.run

    def run(cmd, *args, **kwargs):
        if cmd and cmd[0] == "patch":
            cmd = list(cmd)
            cmd[cmd.index("-i") + 1] = str(Path(patches) / Path(cmd[cmd.index("-i") + 1]).name)
        return real(cmd, *args, **kwargs)

    view = types.SimpleNamespace(**{k: getattr(subprocess, k) for k in dir(subprocess) if not k.startswith("_")})
    view.run = run
    saved, premirror.subprocess = premirror.subprocess, view
    try:
        return premirror.scratch_mirror(out, below)
    finally:
        premirror.subprocess = saved


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-j", type=int, default=os.cpu_count())
    ap.add_argument("--keep", type=Path, help="build the mirrors here (a new folder) and leave them")
    ap.add_argument("--no-controls", action="store_true")
    a = ap.parse_args()
    for target in {t for t, _ in FLAVOURS.values()}:
        build.check_compiler(target)
    # The mirrors are the public build's (build.py --public): the SDK sources it replaces are replaced, and the patches only
    # they take are skipped, as in what ships and in the exported tree, which does not hold those sources.
    build.PUBLIC = True
    scratch = ROOT / "local-data"
    scratch.mkdir(exist_ok=True)
    work = a.keep or Path(tempfile.mkdtemp(prefix="patch-indentation-", dir=scratch))
    work.mkdir(parents=True, exist_ok=True)
    failures = 0
    try:
        original = mirror_with(work / "original", below=0)
        patched = mirror_with(work / "patched")
        base = {}
        for target in sorted({t for t, _ in FLAVOURS.values()}):
            keys, fatal, units, _ = compile_all(original, target, FLAVOURS[f"{target}-public"][1], a.j)
            base[target] = keys
            print(f"unpatched {target}: {units} units, {sum(keys.values())} misleading-indentation warnings, {fatal} units with a fatal error")
            if units < 200:
                print(f"FAIL unpatched {target}: only {units} translation units")
                failures += 1
        seen = Counter()
        for name, (target, defines) in FLAVOURS.items():
            keys, fatal, units, where = compile_all(patched, target, defines, a.j)
            got = findings(base[target], keys)
            print(f"patched {name}: {units} units, {sum(keys.values())} warnings, {fatal} units with a fatal error, {len(got)} introduced by a patch")
            if fatal:
                print(f"FAIL patched {name}: {fatal} units did not compile to the end")
                failures += 1
            for file, statement, control, extra in got:
                lines = ", ".join(map(str, where[(file, statement, control)]))
                print(f"FAIL {name}: {file} line {lines} of the patched mirror: a patch adds misleading indentation ({extra}x)")
                failures += 1
        if not a.no_controls:
            for patch, marker, where in CONTROLS:
                text = (PORT / "patches" / patch).read_text(encoding="latin-1")
                mutant = unbrace(text, marker)
                if mutant is None:
                    print(f"FAIL control {patch}: the line with {marker} is not inside an added braced body")
                    failures += 1
                    continue
                queue = work / ("queue-" + patch[:4])
                shutil.rmtree(queue, ignore_errors=True)
                shutil.copytree(PORT / "patches", queue)
                (queue / patch).write_text(mutant, encoding="latin-1")
                try:
                    mirror = mirror_with(work / ("control-" + patch[:4]), patches=queue)
                except SystemExit as e:
                    print(f"FAIL control {patch}: the unbraced queue does not apply ({str(e).splitlines()[0]})")
                    failures += 1
                    continue
                keys, _, _, _ = compile_all(mirror, "linux", FLAVOURS["linux-public"][1], a.j)
                got = findings(base["linux"], keys)
                ok = len(got) == 1 and got[0][0] == where and got[0][3] == 1
                print(("PASS" if ok else "FAIL") + f" control: {patch} without its braces gives {len(got)} finding(s)"
                      f"{' in ' + ', '.join(sorted({g[0] for g in got})) if got else ''} (1 in {where} expected)")
                failures += not ok
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    print("patch indentation check: " + ("OK" if not failures else f"{failures} FAILED"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
