#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The lockstep controller in a build that has the network but not test automation (the shape the experimental co-op release ships): it compiles, it takes its configuration
from the lobby's admission and handoff (BEA_NET_*, the admission's content digest), and it reads no test switch.

The test-automation build drives the controller from BEA_LOCKSTEP_* variables and carries the traces, stalls, skews, poison and dump. None of that may be reachable in a shipped
build, so the suite preprocesses the controller both ways with the same stand-in headers the fixtures use: without CE_NET_TEST_AUTOMATION no BEA_LOCKSTEP_ name, no dump,
no stall, skew, poison or trace code remains, the configuration is read from the admission, CE_LockstepRequested comes from the boot, and the test hooks of the patched game code
are empty stand-ins; with it the switches are there (so the check can fail). Both shapes must compile. Mutants of the source show each requirement can fail.
No game data, device or network is used."""
import re
import subprocess
import sys
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/game/ce_network_lockstep.cpp"
INCLUDES = ["-I", str(ROOT / "port/tools/tests/lockstep_stubs"), "-I", str(ROOT / "port/game"), "-I", str(ROOT / "port/network")]
# What must not exist in the production text: a switch name, any way to SET a test field (reads of fields that stay zero are inert and remain), the dump opened, the poison, the probe,
# the simulation-identity digest of the test-automation admission.
TEST_ONLY_WORDS = re.compile(r"BEA_LOCKSTEP_|\bL\.(?:dump|skew_at|quit_at|stall_every|stall_at|stall_from|stall_ms|flip_cw_at|poison_stack|test_uninit_read)\s*=(?!=)(?!\s*(?:NULL|__null|nullptr)\b)"
                             r"|fopen\(path|PoisonStack|StackProbe|CE_CompatibilityLockstepDigest")


def check(good, name, results):
    print(("PASS " if good else "FAIL ") + name)
    results.append(bool(good))


def preprocess(path, *defines):
    proc = subprocess.run(["clang++", "-std=c++17", "-E", "-P", "-fwrapv", *defines, *INCLUDES, str(path)], capture_output=True, text=True, timeout=60)
    if proc.returncode:
        raise RuntimeError("preprocess failed: " + proc.stderr[:600])
    return proc.stdout


def compiles(path, *defines):
    proc = subprocess.run(["clang++", "-std=c++17", "-fsyntax-only", "-fwrapv", *defines, *INCLUDES, str(path)], capture_output=True, text=True, timeout=120)
    return proc.returncode == 0, proc.stderr[:400]


def function_text(text, name):
    m = re.search(r"\b" + re.escape(name) + r"\([^)]*\)\s*\{", text)
    if not m:
        return ""
    depth, i = 0, m.end() - 1
    while i < len(text):
        depth += (text[i] == "{") - (text[i] == "}")
        i += 1
        if depth == 0:
            break
    return text[m.start():i]


def owned_mutant_path():
    parent = ROOT / "local-data" / "port" / "lockstep-production-shape-tests"
    parent.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix="run-", dir=parent)) / "_mutant_lockstep.cpp"


def relocated_mutant_headers(text):
    def include(match):
        header = (SOURCE.parent / match.group(1)).resolve()
        return '#include "' + str(header) + '"' if header.is_file() else match.group(0)
    return re.sub(r'^#include "([^"]+)"', include, text, flags=re.M)


def main():
    results = []
    production = preprocess(SOURCE)
    test_automation = preprocess(SOURCE, "-DCE_NET_TEST_AUTOMATION=1")
    check(len(re.findall(r"BEA_LOCKSTEP_", test_automation)) >= 15, "the test-automation shape carries the BEA_LOCKSTEP_ switches (%d names), so the next check can fail" %
          len(re.findall(r"BEA_LOCKSTEP_", test_automation)), results)
    check(not re.search(r"BEA_LOCKSTEP_", production), "the production shape reads no BEA_LOCKSTEP_ variable and names none", results)
    leftovers = sorted(set(TEST_ONLY_WORDS.findall(production)))
    check(not leftovers, "the production shape can set no test field, open no dump, and has no poison, probe or test digest (found: %s)" % (leftovers or "none"), results)
    configure = function_text(production, "CE_LockstepConfigureLevel")
    check("BEA_NET_HOST" in configure and "BEA_NET_JOIN" in configure and "BEA_NET_SECRET" in configure, "the production configuration reads the lobby's BEA_NET_ roles and secret", results)
    check("CE_BootPrepared(" in configure and "memcpy(config.compatibility, admission.digest" in configure,
          "the production compatibility is the admission's content digest", results)
    check("CE_BootLockstep()" in function_text(production, "CE_LockstepRequested"), "CE_LockstepRequested comes from the boot's admission", results)
    check("co-op (experimental)" in production and "lockstep co-op prototype" not in production, "a shipped build calls the session experimental, not a prototype", results)
    for hook in ("CE_LockstepUpdateEntry", "CE_LockstepTraceEvent", "CE_LockstepTestNoLoss", "CE_LockstepTestNoParticles", "CE_LockstepTestExtraRender"):
        body = function_text(production, hook)
        check(bool(body) and len(body) < 60, "%s is an empty stand-in in the production shape" % hook, results)
    ok, err = compiles(SOURCE)
    check(ok, "the production shape compiles" + ("" if ok else ": " + err), results)
    ok, err = compiles(SOURCE, "-DCE_NET_TEST_AUTOMATION=1")
    check(ok, "the test-automation shape compiles" + ("" if ok else ": " + err), results)

    # mutants of the source: each requirement, lost, must be caught
    text = SOURCE.read_text()
    mutants = (
        ("a test switch read outside test automation",
         text.replace("\tL.test_uninit_read = Env(\"BEA_LOCKSTEP_TEST_UNINIT_READ\");\n#endif\n", "#endif\n\tL.test_uninit_read = Env(\"BEA_LOCKSTEP_TEST_UNINIT_READ\");\n", 1)),
        ("the dump opened outside test automation",
         text.replace("#ifdef CE_NET_TEST_AUTOMATION\n\tif (const char *path = getenv(\"BEA_LOCKSTEP_DUMP\")) L.dump = fopen(path, \"w\");\n#endif\n",
                      "\tif (const char *path = getenv(\"BEA_LOCKSTEP_DUMP\")) L.dump = fopen(path, \"w\");\n", 1)),
        ("the requested flag taken from a test variable only",
         text.replace("\tif (CE_BootLockstep()) return 1;\n", "", 1)),
    )
    for label, mutated in mutants:
        check(mutated != text, "mutant source differs from the original: %s" % label, results)
        path = owned_mutant_path()
        path.write_text(relocated_mutant_headers(mutated))
        try:
            production_mutant = preprocess(path)
        finally:
            path.unlink()
        check(not shape_ok_text(production_mutant), "mutant: %s is caught" % label, results)

    ok = all(results)
    print("lockstep production-shape controls: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


def shape_ok_text(production):
    """True when the preprocessed production text meets the shape (test-only code absent, boot-driven request)."""
    return (not TEST_ONLY_WORDS.search(production)) and "CE_BootLockstep()" in function_text(production, "CE_LockstepRequested")


if __name__ == "__main__":
    sys.exit(main())
