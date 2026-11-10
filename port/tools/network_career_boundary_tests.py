#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Patched Linux card methods and real options writer, with authored network/file boundaries.

The production card, path and file code is compiled by save_flow_tests.Build. Network boot and lockstep state
are authored stubs; wrappers refuse and record any attempted career mutation before it reaches the filesystem.
The normal options writer is exercised against a disposable user root. No .bes file, game, network or real
player data is created or opened. A guard-removal mutant must reach every intercepted mutation boundary.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "port"), str(ROOT / "port/tools")]
import save_flow_tests as flow

PATCH_CONDITION = "CE_BootRequested() || CE_LockstepRequested()"


def main():
    parent = ROOT / "local-data/port/network-career-boundary-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    copies = out / "authored-test-stubs"
    copies.mkdir()
    originals = ROOT / "port/tools/tests"
    for name in ("save_flow_driver.cpp", "save_replace_faults.cpp", "network_hook_stubs.cpp", "mods_none.cpp"):
        shutil.copyfile(originals / name, copies / name)
    mods = copies / "mods_none.cpp"
    mods.write_text(mods.read_text().replace('"../../platform/common/mods.h"', '"common/mods.h"'))
    # A second, low-level backstop refuses creation or rename of any .bes name even if an upper wrapper changes.
    faults = copies / "save_replace_faults.cpp"
    fault_text = faults.read_text().replace("#include <string.h>", "#include <string.h>\n#include <strings.h>")
    helper = ('extern "C" void NetworkTestMarkCareerBoundary();\n'
              'static bool IsCareerName(const char *name) { size_t n = name ? strlen(name) : 0; '
              'return n >= 4 && !strcasecmp(name + n - 4, ".bes"); }\n')
    fault_text = fault_text.replace('extern "C" int __wrap_openat(', helper + 'extern "C" int __wrap_openat(', 1)
    needle = "    int fd = __real_openat(parent, name, flags, mode);"
    assert fault_text.count(needle) == 1
    fault_text = fault_text.replace(needle, '    if ((flags & O_CREAT) && IsCareerName(name)) '
                                   '{ NetworkTestMarkCareerBoundary(); errno = EACCES; return -1; }\n' + needle)
    needle = 'extern "C" int __wrap_renameat(int from_dir, const char *from, int to_dir, const char *to)\n{'
    assert fault_text.count(needle) == 1
    fault_text = fault_text.replace(needle, needle + '\n    if (IsCareerName(to)) '
                                   '{ NetworkTestMarkCareerBoundary(); errno = EACCES; return -1; }')
    faults.write_text(fault_text)
    hooks = copies / "network_hook_stubs.cpp"
    text = hooks.read_text().replace("#include <stdio.h>", "#include <stdio.h>\n#include <stdlib.h>")
    # the shared stub answers both questions of the fence with 0; the boundary run makes each answer the test's to give
    old_boot, old_lockstep = "int CE_BootRequested(void) { return 0; }", "int CE_LockstepRequested(void) { return 0; }"
    assert text.count(old_boot) == 1 and text.count(old_lockstep) == 1
    text = text.replace(old_boot, 'int CE_BootRequested(void) { return getenv("TEST_NETWORK_BOOT") != NULL; }')
    text = text.replace(old_lockstep, 'int CE_LockstepRequested(void) { return getenv("TEST_NETWORK_LOCKSTEP") != NULL; }')
    hooks.write_text(text + "\n" + (originals / "network_career_boundary_wrappers.cpp").read_text())
    flow.TESTS = copies
    flow.WRAPS += ",--wrap=bea_crt_fopen,--wrap=bea_crt_port_fopen,--wrap=PortReplaceUserFile,--wrap=unlinkat,--wrap=DeleteFileA"
    results = []

    def check(label, good):
        results.append({"name": label, "passed": bool(good)})
        print(("PASS " if good else "FAIL ") + label)
        if not good:
            raise AssertionError(label)

    good = flow.Build(out, True, label="guarded")
    original_patched = flow.patched

    def remove_guards(dest, name, *args, **kw):
        source = original_patched(dest, name, *args, **kw)
        if name == "PCMemoryCard.cpp":
            assert source.count(PATCH_CONDITION) == 3
            source = source.replace(PATCH_CONDITION, "false")
            (dest / name).write_text(source, encoding="latin1")
        return source

    flow.patched = remove_guards
    try:
        mutant = flow.Build(out, True, label="no-guards")
    finally:
        flow.patched = original_patched

    payload = out / "authored-options.bin"
    payload.write_bytes(b"authored options test")

    def run(label, binary, operation, admission, should_mark):
        root = out / label
        game, user = root / "game", root / "user"
        game.mkdir(parents=True)
        (game / "savegames").mkdir()
        (user / "savegames").mkdir(parents=True)
        marker = root / "attempt.txt"
        env = dict(os.environ, HOME=str(root), BEA_GAME_DIR=str(game), BEA_USER_DIR=str(user), NETWORK_TEST_MARKER=str(marker))
        for name in ("TEST_NETWORK_BOOT", "TEST_NETWORK_LOCKSTEP", "SAVE_TEST_FAULT", "SAVE_TEST_N", "SAVE_TEST_SLEEPS"):
            env.pop(name, None)
        if admission == "boot": env["TEST_NETWORK_BOOT"] = "1"
        if admission == "lockstep": env["TEST_NETWORK_LOCKSTEP"] = "1"
        args = {"create-overwrite": ["card", "create", "authored", "1"],
                "create-new": ["card", "create", "authored", "0"],
                "write": ["card", "write", "authored", str(payload)],
                "delete": ["card", "delete", "authored"]}[operation]
        p = subprocess.run([str(binary), *args], env=env, cwd=root, capture_output=True, text=True, timeout=30)
        check(label + " exits", p.returncode == 0)
        check(label + " reaches expected boundary", marker.exists() == should_mark)
        check(label + " leaves no career file", not any(p.is_file() and p.suffix.lower() == ".bes" for p in root.rglob("*")))
        if not should_mark:
            match = re.search(r"(?:create|write|delete)=(-?\d+)", p.stdout)
            check(label + " refuses the operation", bool(match) and int(match.group(1)) != 0)
            if operation.startswith("create-"):
                check(label + " initializes the result", bool(re.search(r"\bsave=-1\b", p.stdout)))

    for operation in ("create-overwrite", "create-new", "write", "delete"):
        for admission in ("boot", "lockstep"):
            run(f"guarded-{operation}-{admission}", good.binary, operation, admission, False)
        run(f"offline-{operation}", good.binary, operation, "none", True)
        run(f"mutant-{operation}", mutant.binary, operation, "boot", True)

    for admission in ("boot", "lockstep", "none"):
        root = out / ("options-" + admission)
        game, user = root / "game", root / "user"
        game.mkdir(parents=True)
        user.mkdir()
        env = dict(os.environ, HOME=str(root), BEA_GAME_DIR=str(game), BEA_USER_DIR=str(user), NETWORK_TEST_MARKER=str(root / "attempt.txt"))
        env.pop("TEST_NETWORK_BOOT", None)
        env.pop("TEST_NETWORK_LOCKSTEP", None)
        if admission == "boot": env["TEST_NETWORK_BOOT"] = "1"
        if admission == "lockstep": env["TEST_NETWORK_LOCKSTEP"] = "1"
        p = subprocess.run([str(good.binary), "options", str(payload)], env=env, cwd=root, capture_output=True, timeout=30)
        options = user / "defaultoptions.bea"
        check("options-" + admission + " exits", p.returncode == 0)
        check("options-" + admission + " preserved", options.is_file() and options.read_bytes() == payload.read_bytes())
        check("options-" + admission + " has no career mutation", not (root / "attempt.txt").exists())

    (out / "result.json").write_text(json.dumps({"cases": results, "passed": all(r["passed"] for r in results), "scope": __doc__}, indent=2) + "\n")
    print("receipt: " + str(out / "result.json"))


if __name__ == "__main__":
    main()
