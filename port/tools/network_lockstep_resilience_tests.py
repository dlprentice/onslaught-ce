#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline mid-match wait/recovery controls for the real lockstep controller.

World, transport delivery/status and time are authored fixtures. No socket, game,
device input, save, service or credential file is used. This establishes the
controller's failure boundary, not physical network recovery or ICE renewal.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import tempfile

import deps
from network_world_tests import actual_body

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "local-data/port/lockstep-resilience-tests"
CONTROL_SOURCE = "port/game/ce_network_lockstep.cpp"
HARNESS = "port/tools/tests/network_lockstep_terminal_driver.cpp"
DRIVER = "port/tools/tests/network_lockstep_resilience_driver.cpp"
CRYPTO_PINS = {
    "monocypher.h": "f78bb31255cfb7beba66afd2137f5194c8a025cf40488b6cc1e295234d43f374",
    "monocypher.c": "afe2b098c8569577a84488e0b98d276d1fba6506adea68bb9241a52111734c59",
}
CONTROLS = (
    ("allow-missing-controls", "bool have = t < L.delay || (L.theirs[t % kRing].present && L.theirs[t % kRing].tick == t);",
     "bool have = true;", "missing controls keep the update gate closed"),
    ("restart-wait-each-poll", "if (!L.waiting_since) L.waiting_since = now;", "L.waiting_since = now;",
     "waiting notice appears at half a second"),
    ("delay-wait-notice", "if (waited >= kWaitNoticeMs) {", "if (waited > kWaitNoticeMs) {",
     "waiting notice appears at half a second"),
    ("ignore-control-deadline", "if (waited >= kGiveUpMs) { Fail(\"the other player stopped sending controls\"); return 0; }",
     "if (false) { Fail(\"the other player stopped sending controls\"); return 0; }",
     "missing controls at thirty seconds abort without a voluntary leave or result"),
    ("retain-old-wait", "L.waiting_since = 0;\n\tRestoreRandom();", "/* control: retain the previous wait */\n\tRestoreRandom();",
     "controls before failure resume the same tick and restore simulation random state"),
    ("admit-update-after-failure", "if (L.stage != RUNNING) return 0;",
     "if (L.stage == FAILED) return 1; if (L.stage != RUNNING) return 0;",
     "late controls cannot reopen a failed update gate"),
    ("discard-relay-reason", "if (status.phase == CE_NET_FAILED && CE_NetGameProblem(L.net)[0]) return Fail(CE_NetGameProblem(L.net));",
     "/* control: discard relay-specific failure */", "relay expiry reason survives to the frontend before another tick"),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_command(command, log, env, seconds=60):
    with log.open("wb") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                env=env, timeout=seconds)
    if log.stat().st_size > 1_048_576:
        raise RuntimeError("fixture log exceeded its bound")
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("native", "linux32"), default="native")
    parser.add_argument("--causal", action="store_true")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    WORK.mkdir(parents=True, exist_ok=True, mode=0o700)
    out = Path(tempfile.mkdtemp(prefix=args.target + "-", dir=WORK))
    snapshot = out / "source"
    snapshot.mkdir()
    paths = {CONTROL_SOURCE, HARNESS, DRIVER, "port/game/ce_network_world.cpp",
             "port/game/ce_network_campaign.cpp", "port/game/ce_network_result_exchange.cpp",
             "port/network/codec.cpp", "port/network/prerun_flags.cpp"}
    for directory in ("port/game", "port/network", "port/platform/common", "port/tools/tests/lockstep_stubs"):
        paths.update(str(path.relative_to(ROOT)) for path in (ROOT / directory).glob("*.h"))
    pins = {}
    for relative in sorted(paths):
        source, dest = ROOT / relative, snapshot / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        pins[relative] = sha(source)
        if sha(dest) != pins[relative]:
            raise RuntimeError("source snapshot changed during copy")
    source = snapshot / CONTROL_SOURCE
    production = source.read_text()
    harness = snapshot / HARNESS
    validator = out / "world-input-validator.inc"
    world = (snapshot / "port/game/ce_network_world.cpp").read_text()
    start = world.index("const uint32_t native_actions[]=")
    validator.write_text(world[start:world.index(";", start) + 1] + "\n" +
                         actual_body(world, "unsigned ActionRank(") + "\n" +
                         actual_body(world, "bool ValidInput(") + "\nextern \"C\" " +
                         actual_body(world, "int CE_WorldLockstepValidInput(") + "\n")
    # No native slot-getter scenario is run. This unrelated harness boundary
    # receives an inert authored definition, without reading retail source.
    getter = out / "unused-slot-getter.inc"
    getter.write_text("int NativeGame::GetSlot(int) { return 0; }\n")
    crypto = out / "crypto"
    crypto.mkdir()
    for name, digest in CRYPTO_PINS.items():
        existing = deps.SRC / "monocypher-4.0.2/src" / name
        if sha(existing) != digest:
            raise RuntimeError("pinned Monocypher input changed")
        shutil.copyfile(existing, crypto / name)
        if sha(crypto / name) != digest:
            raise RuntimeError("Monocypher snapshot changed during copy")
    if args.target == "linux32":
        cc = str(deps.DATA / "llvm/bin/clang")
        cxx = str(deps.DATA / "llvm/bin/clang++")
        compile_flags = ["--target=i686-pc-linux-gnu", *deps.sysroot_flags(), "-m32"]
        link_flags = [*deps.sysroot_link_flags(), "-m32"]
    else:
        cc, cxx = "clang", "clang++"
        compile_flags, link_flags = [], []
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("BEA_") and key not in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS")}
    env.update(TMPDIR=str(out), HOME=str(out), XDG_CONFIG_HOME=str(out / "config"),
               XDG_DATA_HOME=str(out / "data"), XDG_CACHE_HOME=str(out / "cache"))
    crypto_obj = out / "monocypher.o"
    command = [cc, *compile_flags, "-std=c99", "-O1", "-I", str(crypto), "-c",
               str(crypto / "monocypher.c"), "-o", str(crypto_obj)]
    if run_command(command, out / "crypto-build.log", env):
        raise RuntimeError("crypto compile failed; inspect the private output directory")
    cases = [("actual", None, None, None), *(CONTROLS if args.causal else ())]
    records = []
    for label, before, after, expected in cases:
        # Each compilation and execution is complete before this owned snapshot
        # receives the next controlled change. Production inputs stay read-only.
        if before is not None and production.count(before) != 1:
            raise RuntimeError("control anchor is not unique: " + label)
        source.write_text(production if before is None else production.replace(before, after, 1))
        exe = out / (label + "-test")
        command = [cxx, *compile_flags, "-std=c++17", "-fwrapv", "-O1",
                   '-DRESILIENCE_HARNESS_INC="' + str(harness) + '"',
                   '-DLOCKSTEP_VALIDATOR_INC="' + str(validator) + '"',
                   '-DNATIVE_SLOT_GETTER_INC="' + str(getter) + '"',
                   "-I", str(snapshot / "port/tools/tests/lockstep_stubs"), "-I", str(crypto),
                   str(snapshot / DRIVER), str(snapshot / "port/game/ce_network_campaign.cpp"),
                   str(snapshot / "port/game/ce_network_result_exchange.cpp"),
                   str(snapshot / "port/network/codec.cpp"), str(snapshot / "port/network/prerun_flags.cpp"),
                   str(crypto_obj), *link_flags, "-o", str(exe)]
        if run_command(command, out / (label + "-build.log"), env):
            raise RuntimeError("fixture compile failed: " + label + "; inspect " + str(out))
        log = out / (label + "-result.log")
        status = run_command([str(exe)], log, env, 10)
        lines = log.read_text().splitlines()
        complete = any(line.startswith("RESILIENCE_COMPLETE failures=") for line in lines)
        passed = complete and (status == 0 if expected is None else status == 1 and "FAIL " + expected in lines)
        records.append({"name": label, "passed": passed, "exit_status": status,
                        "expected_failure": expected, "completed": complete,
                        "executable_sha256": sha(exe), "source_sha256": sha(source), "command": command})
        print(("PASS " if passed else "FAIL ") + label, flush=True)
    source.write_text(production)
    unchanged = all(sha(ROOT / path) == digest for path, digest in pins.items())
    passed = unchanged and all(record["passed"] for record in records)
    receipt = {"target": args.target, "passed": passed, "source_sha256": pins,
               "production_inputs_unchanged": unchanged, "cases": records,
               "limits": "Actual lockstep controller; fabricated world, transport delivery/status and clock. No socket, game, device input, save, provider, credential renewal or physical/cross-platform recovery acceptance."}
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(("PASS " if unchanged else "FAIL ") + "production-inputs-unchanged")
    print("evidence:", out / "result.json")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
