#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The test kit's checks (port/journeys/README.md).

    python3 port/tools/test_kit_tests.py [--kit-bin BEA --plain-bin BEA ...]

1. Host: compiles the kit's core (port/platform/common/ce_testkit.cpp, CE_TEST_KIT=1) with an authored two-page menu
   (tests/ce_testkit_driver.cpp) and runs scenarios: select and go by text with real key presses, waits, asserts, state
   keys, the TESTWRITE mark, budgets, ambiguous and missing rows, an unknown verb, and that two runs are byte-identical.
2. Binaries (when given): a --kit-bin carries the marker "ce-testkit-v1" and the symbol CeTestKitFrame; every --plain-bin
   (a public or network build without --test-kit) carries neither. No game data is read."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "port/platform/common/ce_testkit.cpp"
DRIVER = ROOT / "port/tools/tests/ce_testkit_driver.cpp"

SCENARIOS = {
    "pass": ("""wait ticks 5
go Settings
wait page t:settings
assert test.volume=7
wait notice "saved"
dump inside
back
wait page t:menu
assert page t:menu
focus row "Load Game"
assert file seed.txt sha256=@SEED@
key DOWN hold 4
""", {"end": "pass", "testwrite": False, "steps": 12}),
    "ambiguous": ('select row "Sett"\n', {"end": "fail", "why_has": "more than one row starts with that text"}),
    "missing": ('select row "Options"\n', {"end": "fail", "why_has": "no such row"}),
    "budget": ("wait page t:never within 30\n", {"end": "fail", "why_has": "not done within 30 ticks"}),
    "testwrite": ("poke\n", {"end": "pass", "testwrite": True}),
    "unknown": ("fly to the moon\n", {"end": "fail", "why_has": "no verb"}),
    # a bad line anywhere refuses the journey before its first step runs
    "unknown-late": ("go Settings\nwait ticks 3\nfly to the moon\n", {"end": "fail", "why_has": "line 3 of the steps", "steps": 0}),
    "go-last-stays": ("go Settings > Sound\nassert page t:settings\n", {"end": "pass"}),
    "quoted": ('select row "Settings Extra"\nassert page t:menu\n', {"end": "pass"}),
    "wait-any": ("wait state test.volume=*\n", {"end": "pass", "steps": 1}),
    "wait-missing-key": ("wait state test.nothing=* within 20\n", {"end": "fail", "why_has": "not done within 20 ticks"}),
    "row-number": ("select row #3\nwait page t:settings\n", {"end": "pass"}),
    "require-met": ("require test.volume=0\ngo Settings\n", {"end": "pass", "steps": 2}),
    "require-unmet": ("require test.volume=9\ngo Settings\n", {"end": "skip", "steps": 0}),
    "dirty": ("test-dirty seed.txt\nassert file seed.txt sha256=@SEED@\n", {"end": "fail", "testwrite": True}),
    "chord": ("key LCTRL+Z hold 4\nkey W\n", {"end": "pass", "steps": 2}),
    "bad-key": ("key LCTRL+NOPE\n", {"end": "fail", "why_has": "no key"}),
    "hold": ("press DOWN\nwait ticks 5\nrelease DOWN\nassert test.volume=0\n", {"end": "pass", "steps": 4}),
    "release-unheld": ("release DOWN\n", {"end": "fail", "why_has": "is not held"}),
    "held-at-end": ("press LCTRL\n", {"end": "pass", "steps": 1}),
    "mouse-stream": ("mouse-move 100 0 over 7\nassert test.relx=100\nmouse-down\nmouse-up\nassert test.clicks=1\n",
                     {"end": "pass", "steps": 5}),
    "until": ("until test.clicks=3 every 10 do click 5 5\nassert test.clicks=3\n", {"end": "pass", "steps": 2}),
    "until-budget": ("until test.clicks=99 every 10 do click 5 5 within 60\n", {"end": "fail", "why_has": "not done within 60"}),
    "until-bad-inner": ("until test.clicks=1 every 10 do fly away\n", {"end": "fail", "why_has": "to repeat", "steps": 0}),
    # 2 counts a tick from tick 0; wait ticks 10 ends at tick 11 and mouse-stop runs at tick 12, after that tick's part: 13 parts
    "background": ("mouse-move 100 0 over 50 background\nwait ticks 10\nmouse-stop\nwait ticks 5\nassert test.relx=26\n",
                   {"end": "pass", "steps": 5}),
    "repeat": ("repeat 4 every 5 do click 1 1\nassert test.clicks=4\n", {"end": "pass", "steps": 2}),
    "compare": ("assert test.pose[3]<0.6\nclick 1 1\nassert test.pose[3]>1.2\nassert test.pose[2]>=90\nassert test.pose[0]<=1.5\n"
                "wait state test.pose[1]=2.25\n", {"end": "pass", "steps": 6}),
    "compare-fails": ("assert test.pose[3]>0.6\n", {"end": "fail", "why_has": "test.pose[3]=0.5"}),
    "no-field": ("assert test.pose[9]=1\n", {"end": "fail", "why_has": "no field 9"}),
    "until-compare": ("until test.pose[3]>=2 every 10 do click 1 1\nassert test.clicks=2\n", {"end": "pass", "steps": 2}),
    "mouse": ("click 10 20\ndrag 1 1 50 50 over 5\nassert test.clicks=2\n", {"end": "pass", "steps": 3}),
}


def compile_driver(work):
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        sys.exit("a C++ compiler is required")
    out = work / "kit-driver"
    cmd = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(CORE), str(DRIVER), "-o", str(out)]
    built = subprocess.run(cmd, capture_output=True, text=True)
    (work / "compile.log").write_text(built.stdout + built.stderr)
    if built.returncode:
        sys.exit(f"the kit's core did not compile: {work / 'compile.log'}")
    return out


def run(driver, work, name, steps):
    folder = work / name
    folder.mkdir()
    user = folder / "user"
    user.mkdir()
    (user / "seed.txt").write_bytes(b"seed\n")
    seed = hashlib.sha256(b"seed\n").hexdigest()
    (folder / "steps.txt").write_text(steps.replace("@SEED@", seed))
    out = folder / "verbs.jsonl"
    env = {"BEA_PORT_VERBS": str(folder / "steps.txt"), "BEA_PORT_VERB_OUT": str(out), "PATH": "/usr/bin:/bin",
           "BEA_PORT_CLOCK": "fixed"}   # the journeys' default clock (on the real clock a page also settles for 0.5 s)
    subprocess.run([str(driver), name, str(user)], env=env, capture_output=True, timeout=60, check=True)
    lines = [json.loads(l) for l in out.read_text().splitlines()]
    return lines, out.read_bytes()


def check_binaries(kit, plain):
    failures = []
    for path in kit:
        data = Path(path).read_bytes()
        syms = subprocess.run(["nm", "-C", path], capture_output=True, text=True).stdout
        if b"ce-testkit-v1" not in data or "CeTestKitFrame" not in syms:
            failures.append(f"{path}: a test-kit build without the kit's marker or symbol")
    for path in plain:
        data = Path(path).read_bytes()
        syms = subprocess.run(["nm", "-C", path], capture_output=True, text=True).stdout
        if b"ce-testkit-v1" in data or "CeTestKit" in syms:
            failures.append(f"{path}: carries the test kit (it must not)")
        for name in (b"BEA_PORT_VERBS", b"BEA_PORT_VERB_OUT", b"BEA_PORT_VERB_RUN_DIR"):
            if name in data:
                failures.append(f"{path}: names {name.decode()}, which only a test-kit build may read")
    return failures


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kit-bin", action="append", default=[])
    ap.add_argument("--plain-bin", action="append", default=[])
    a = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="test-kit-"))
    driver = compile_driver(work)
    failures, report = [], {}
    for name, (steps, want) in SCENARIOS.items():
        lines, _ = run(driver, work, name, steps)
        end = next((l for l in reversed(lines) if "end" in l), {})
        report[name] = end
        if end.get("end") != want["end"]:
            failures.append(f"{name}: ended {end.get('end')}, wanted {want['end']}")
        if "testwrite" in want and end.get("testwrite") != want["testwrite"]:
            failures.append(f"{name}: testwrite {end.get('testwrite')}")
        if "steps" in want and end.get("steps") != want["steps"]:
            failures.append(f"{name}: {end.get('steps')} steps finished, wanted {want['steps']}")
        if "why_has" in want and want["why_has"] not in end.get("why", ""):
            failures.append(f"{name}: why {end.get('why')!r}")
        if want["end"] == "fail" and not any("dump" in l for l in lines):
            failures.append(f"{name}: a failure without its dump")
        ticks = [l["tick"] for l in lines if "step" in l and "tick" in l]
        if ticks != sorted(ticks) or len(ticks) != len(set(ticks)):
            failures.append(f"{name}: two steps finished in one tick, or out of order: {ticks}")
    _, first = run(driver, work, "repeat-a", SCENARIOS["pass"][0])
    _, second = run(driver, work, "repeat-b", SCENARIOS["pass"][0])
    if first != second:
        failures.append("the same steps gave two different outputs")
    # Ensure ce_frontend_testkit.cpp's kCePages stays in lockstep with ECePage (CEP_COUNT) in ce_frontend.cpp.
    import re
    fe_src = (ROOT / "port/game/ce_frontend.cpp").read_text()
    tk_src = (ROOT / "port/game/ce_frontend_testkit.cpp").read_text()
    m_enum = re.search(r"enum ECePage\s*\{(.*?)\};", fe_src, re.S)
    m_names = re.search(r"kCePages\[\]\s*=\s*\{(.*?)\};", tk_src, re.S)
    if not m_enum or not m_names:
        failures.append("could not locate ECePage or kCePages for test-kit parity check")
    else:
        enum_entries = [e for e in re.findall(r"\b(CEP_\w+)\b", m_enum.group(1)) if e != "CEP_COUNT"]
        page_names = re.findall(r'"([^"]+)"', m_names.group(1))
        if len(page_names) != len(enum_entries):
            failures.append(f"kCePages has {len(page_names)} entries, ECePage has {len(enum_entries)}")
        if "labs" not in page_names:
            failures.append("kCePages omits 'labs' (CEP_LABS)")
    failures += check_binaries(a.kit_bin, a.plain_bin)
    (work / "result.json").write_text(json.dumps({"passed": not failures, "failures": failures, "ends": report}, indent=1) + "\n")
    print(json.dumps({"passed": not failures, "scenarios": len(SCENARIOS) + 1, "binaries": len(a.kit_bin) + len(a.plain_bin),
                      "failures": failures, "evidence": str(work)}))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
