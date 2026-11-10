#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pinned i686 MS-caller/native-stat bridge controls on authored metadata only; no game or network.

Windows checks are compilation only. Sparse bound fixtures are never hashed or read as content.
"""
import ast
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import subprocess
import tempfile

import deps


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    owner = deps.WORK / "network-native-stat-tests"
    owner.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=owner))
    records = []
    print(out, flush=True)

    def run(label, argv, expected=0):
        r = subprocess.run(list(map(str, argv)), text=True, capture_output=True, timeout=90)
        (out / (label + ".log")).write_text(r.stdout + r.stderr)
        records.append({"label": label, "argv": list(map(str, argv)), "exit": r.returncode})
        (out / "result.json").write_text(json.dumps({"pass": False, "cases": records, "stage": "checks in progress or failed"}, indent=2) + "\n")
        if r.returncode != expected:
            raise RuntimeError(label + ": " + r.stdout + r.stderr)
        return r.stdout

    source = (deps.PORT / "build.py").read_text()
    layouts = [n.value.value for n in ast.walk(ast.parse(source)) if isinstance(n, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == "layout" for t in n.targets)
               and isinstance(n.value, ast.Constant) and isinstance(n.value.value, str)]
    if len(layouts) != 1 or "-mms-bitfields" not in shlex.split(layouts[0]):
        raise RuntimeError("production pcxx layout could not be resolved")
    compiler = deps.DATA / "llvm/bin/clang++"
    base = [str(compiler), "--target=i686-pc-linux-gnu", f"--sysroot={deps.SYSROOT}", "-std=gnu++17"]
    profile = [*base, *shlex.split(layouts[0]), "-ffunction-sections", "-fdata-sections"]
    bridge = deps.PORT / "network/native_posix_stat.cpp"
    flags = (deps.PORT / "network/flags.txt").read_text()
    if "native_posix_stat.cpp: -mno-ms-bitfields" not in flags:
        raise RuntimeError("native ABI override missing")
    obj = out / "native.o"
    run("native-bridge", [*profile, "-mno-ms-bitfields", "-c", bridge, "-o", obj])
    bad = subprocess.run([*profile, "-c", str(bridge), "-o", str(out / "bad.o")], capture_output=True, text=True)
    (out / "wrong-layout-compile.log").write_text(bad.stdout + bad.stderr)
    if bad.returncode == 0 or "POSIX stat bridge requires native i686" not in bad.stderr:
        raise RuntimeError("wrong-layout bridge must fail compilation")
    records.append({"label": "wrong-layout-bridge-causal-refusal", "exit": bad.returncode})
    driver = out / "driver"
    mc = deps.SRC / "monocypher-4.0.2/src"
    run("mixed-profile-driver", [*profile, f"-I{mc}", deps.PORT / "network/tests/native_posix_stat_driver.cpp",
                                  obj, "-Wl,--gc-sections", "-o", driver])
    with tempfile.TemporaryDirectory(prefix="authored-", dir=out) as temp:
        root = Path(temp); data = root / "data"; data.mkdir(); file = data / "sentinel.dat"
        file.write_bytes(b"authored metadata sentinel")
        stat = file.stat()
        expected = [stat.st_dev, stat.st_ino, 0, stat.st_size,
                    ((stat.st_mtime_ns // 10**9) << 32) | (stat.st_mtime_ns % 10**9),
                    ((stat.st_ctime_ns // 10**9) << 32) | (stat.st_ctime_ns % 10**9)]
        actual = json.loads(run("actual-path-fd", [driver, "stamp", file]))
        if actual != {"path": expected, "fd": expected, "offset_preserved": True, "api": [100, 208, 48, 40]}:
            raise RuntimeError("native path/fd metadata or public caller ABI differs")
        legacy = int(run("legacy-ms-layout-causal-mismatch", [driver, "legacy", file]))
        if legacy == stat.st_size:
            raise RuntimeError("legacy wrong-layout contrast did not fail")
        run("actual-scan", [driver, "scan", root])
        run("missing", [driver, "refuse", root / "missing"])
        run("directory", [driver, "refuse", data])
        link = root / "link"; link.symlink_to(file); run("symlink", [driver, "refuse", link])
        huge = data / ("long-name-" + "x" * 90 + ".dat")
        with huge.open("wb") as f:
            f.truncate(4 * 1024**3 + 1)
        text = run("actual-bound-short-buffers", [driver, "bound", root])
        if not all(s in text for s in ("observed=4294967297", "limit=4294967296", "path=data/", "...")) or str(root) in text:
            raise RuntimeError("bounded data-relative diagnostic differs")
    # All existing shared struct layouts remain the production MS layout on both selected targets.
    assertions = out / "public-layout.cpp"
    driver_source = (deps.PORT / "network/tests/native_posix_stat_driver.cpp").read_text()
    first = driver_source.index("static_assert(sizeof(CE_CompatibilityPreset)")
    last = driver_source.index('extern "C" FILE *port_fopen')
    assertions.write_text('#include "compatibility.h"\n#include <cstddef>\n' + driver_source[first:last])
    mingw = deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang++"
    for name, cmd in (("linux", profile), ("windows", [str(mingw), "-std=gnu++17", *shlex.split(layouts[0])])):
        run(name + "-shared-layout", [*cmd, f"-I{deps.PORT / 'network'}", "-fsyntax-only", assertions])
    for name in ("native_posix_stat.cpp", "compatibility.cpp"):
        run("windows-" + name, [str(mingw), "-std=c++17", "-fsyntax-only", f"-I{mc}", deps.PORT / "network" / name])
    result = {"pass": True, "cases": records, "production_layout": layouts[0], "source_sha256": {
        str(p.relative_to(deps.PORT)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (bridge, deps.PORT / "network/native_posix_stat.h", deps.PORT / "network/compatibility.cpp",
                  deps.PORT / "network/compatibility.h", deps.PORT / "network/flags.txt")},
        "limits": __doc__, "native_path_fd_expected": expected, "legacy_wrong_size": legacy}
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"pass": True, "cases": len(records), "receipt": str(out / "result.json")}))


if __name__ == "__main__":
    main()
