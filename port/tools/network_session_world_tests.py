#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Couple current Session/delivery to a source-verified World10 native-lifecycle fixture, without game or network.

The accepted fixture contains actual World lifecycle codecs/API and native explosion/factory caller bodies.
All pinned function bodies are verified against current source or the selected actual typed mirror before use.
Full-frame serialization/commit, base geometry, particles/audio and transport remain controlled boundaries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import deps
from network_lifecycle_native_tests import body

ROOT = Path(__file__).resolve().parents[2]


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fixture", required=True, type=Path, help="frozen successful network-lifecycle-native run")
    ap.add_argument("--case", action="append", choices=("normal", "factory-failure", "gap", "late-state"),
                    help="run selected focused cases; default includes all four, including valid delayed STATE")
    ap.add_argument("--session-source", type=Path, default=ROOT / "port/game/ce_network_session.cpp",
                    help="explicit actual Session for before/after source controls")
    ap.add_argument("--world-source", type=Path, default=ROOT / "port/game/ce_network_world.cpp",
                    help="explicit World lifecycle source basis, identical to the selected native fixture")
    ap.add_argument("--world-header", type=Path, default=ROOT / "port/game/ce_network_world.h")
    ap.add_argument("--readiness-source", type=Path, default=ROOT / "port/game/ce_network_world.cpp",
                    help="explicit World source for its two actual readiness functions; other lifecycle bodies stay pinned")
    ap.add_argument("--compat-header", type=Path, default=ROOT / "port/network/compatibility.h")
    ap.add_argument("--boot-header", type=Path, default=ROOT / "port/game/ce_network_boot.h")
    ap.add_argument("--loaded-header", type=Path, default=ROOT / "port/game/ce_network_loaded_admission.h")
    args = ap.parse_args()
    fixture = args.fixture.resolve()
    prior = json.loads((fixture / "result.json").read_text())
    world_fixture = Path(prior["fixture"])
    if not prior["passed"] or sha(world_fixture / "result.json") != prior["fixture_receipt_sha256"]:
        raise ValueError("native fixture receipt is not the successful pinned World source basis")
    world_path = args.world_source
    if sha(world_path) != sha(world_fixture / "production/game/ce_network_world.cpp"):
        raise ValueError("selected World source changed: reselect a native lifecycle fixture")
    if sha(args.world_header) != sha(world_fixture / "production/game/ce_network_world.h"):
        raise ValueError("selected World header changed")
    if sha(fixture / "source-pins.json") != prior["source_pins_sha256"]:
        raise ValueError("native source function pins changed")
    source = world_path.read_text()
    mirror = world_fixture / "mirror"
    sources = [source, (ROOT / "src/InitThing.cpp").read_text()]
    sources += [(mirror / name).read_text() for name in ("thing.cpp", "explosion.cpp", "WorldPhysicsManager.cpp", "game.h", "InitThing.h",
                "Round.cpp", "actor.cpp", "collisionseekingthing.cpp", "weapon.cpp", "maths.h", "WorldPhysicsManager.h")]
    pins = json.loads((fixture / "source-pins.json").read_text())
    for signature, expected in pins.items():
        matched = False
        for text in sources:
            try:
                fragment = body(text, signature)
            except ValueError:
                continue
            if hashlib.sha256(fragment.encode()).hexdigest() == expected:
                matched = True
                break
        if not matched:
            raise ValueError("actual pinned function changed: " + signature)
    witness = (fixture / "witness.cpp").read_text()
    # Verify effectful lifecycle/API/codec bodies are present unmodified in the generated fixture too.
    for signature, expected in pins.items():
        if signature == "int CE_WorldFrameReadiness(":
            continue  # the native fixture pinned this guard but did not consume it; append it below
        if signature.startswith(("int CE_World", "bool Life", "void Life", "bool EncodeLife", "bool DecodeLife")):
            if hashlib.sha256(body(witness, signature).encode()).hexdigest() != expected:
                raise ValueError("generated actual lifecycle body changed: " + signature)
    parent = ROOT / "local-data/port/network-session-world-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    production = out / "production"
    for folder, names in (("game", ("ce_network_session.cpp", "ce_network_session.h", "ce_network_world.h")),
                          ("network", ("network.h", "ice_game.h", "codec.cpp", "state_delivery.h", "state_delivery.cpp", "state_delta.h", "state_delta.cpp"))):
        (production / folder).mkdir(parents=True)
        for name in names:
            selected = args.session_source if name == "ce_network_session.cpp" else args.world_header if name == "ce_network_world.h" else ROOT / "port" / folder / name
            shutil.copyfile(selected, production / folder / name)
    for selected,dest in ((args.compat_header,production/"network/compatibility.h"),
                          (args.boot_header,production/"game/ce_network_boot.h"),
                          (args.loaded_header,production/"game/ce_network_loaded_admission.h"),
                          (ROOT/"port/game/ce_network_lockstep.h",production/"game/ce_network_lockstep.h")):
        if not selected.is_file():
            raise ValueError("explicit owning API header missing: "+str(selected))
        shutil.copyfile(selected,dest)
    crypto=deps.SRC/"monocypher-4.0.2/src"
    for name in ("monocypher.h","monocypher.c"):
        shutil.copyfile(crypto/name,production/"network"/name)
    # Strip only the authored old test main and replace its explicitly controlled full-frame encoder boundary.
    old_main = body(witness, "int main()")
    old_encode = body(witness, "int CE_WorldEncode(")
    witness = witness.replace(old_main, "").replace(old_encode, "")
    readiness_source = args.readiness_source.read_text()
    readiness = body(readiness_source, "int LifeReadiness(") + "\n" + body(readiness_source, "int CE_WorldFrameReadiness(")
    bridge = ROOT / "port/tools/tests/network_session_world_bridge.cpp.in"
    world_tu = out / "world.cpp"
    world_tu.write_text(witness + "\n" + readiness + "\n" + bridge.read_text())
    driver = out / "driver.cpp"
    shutil.copyfile(ROOT / "port/tools/tests/network_session_world_driver.cpp", driver)
    command = ["clang++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
               "-I"+str(production/"network"),
               '-DCE_WORLD_HEADER="' + str(production / "game/ce_network_world.h") + '"',
               '-DCE_NET_HEADER="' + str(production / "network/network.h") + '"',
               '-DNATIVE_THING_TYPES="' + str(mirror / "ThingType.h") + '"',
               '-DSESSION_SOURCE="' + str(production / "game/ce_network_session.cpp") + '"',
               str(world_tu), str(driver), str(production / "network/codec.cpp"),
               str(production/"network/state_delta.cpp"),str(production/"network/state_delivery.cpp"),
               str(out/"crypto.o"),"-o", str(out / "driver")]
    receipt = {"limits": __doc__, "runner_sha256": sha(Path(__file__)), "arguments": sys.argv[1:], "fixture": str(fixture), "fixture_receipt_sha256": sha(fixture / "result.json"),
               "fixture_witness_sha256": sha(fixture / "witness.cpp"), "world_source_sha256": sha(world_path), "world_header_sha256": sha(args.world_header),
               "readiness_source": str(args.readiness_source.resolve()), "readiness_source_sha256": sha(args.readiness_source),
               "readiness_functions_sha256": hashlib.sha256(readiness.encode()).hexdigest(),
               "function_pins": pins, "bridge_sha256": sha(bridge), "world_tu_sha256": sha(world_tu),
               "session_sha256": sha(production / "game/ce_network_session.cpp"), "driver_sha256": sha(driver), "compile": command,
               "delivery_inputs":{str(p.relative_to(production)):sha(p) for p in production.rglob("*") if p.is_file()}}
    env = dict(os.environ, TMPDIR=str(out), ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:disable_coredump=1")
    env = {k: v for k, v in env.items() if not k.startswith("BEA_")}
    crypto_command=["clang","-std=c99","-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer",
                    "-I"+str(production/"network"),"-c",str(production/"network/monocypher.c"),"-o",str(out/"crypto.o")]
    crypto_build=subprocess.run(crypto_command,env=env,capture_output=True,text=True,timeout=60)
    (out/"crypto-build.log").write_text(crypto_build.stdout+crypto_build.stderr)
    receipt["crypto_compile"]=crypto_command;receipt["crypto_compile_exit"]=crypto_build.returncode
    if crypto_build.returncode:
        (out/"result.json").write_text(json.dumps(receipt,indent=2)+"\n");print(out/"result.json");return 1
    compiled = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
    (out / "build.log").write_text(compiled.stdout + compiled.stderr)
    receipt["compile_exit"] = compiled.returncode
    receipt["cases"] = []
    if not compiled.returncode:
        receipt["binary_sha256"] = sha(out / "driver")
        for case in (args.case or ("normal", "factory-failure", "gap", "late-state")):
            proc = subprocess.run([str(out / "driver"), case], env=env, capture_output=True, text=True, timeout=10)
            (out / (case + ".log")).write_text(proc.stdout + proc.stderr)
            receipt["cases"].append({"case": case, "exit": proc.returncode, "stdout": proc.stdout, "stderr": proc.stderr, "passed": not proc.returncode})
            print(case, "PASS" if not proc.returncode else "FAIL", flush=True)
    receipt["passed"] = not compiled.returncode and all(row["passed"] for row in receipt["cases"])
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json", flush=True)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
