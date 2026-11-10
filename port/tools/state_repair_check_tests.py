#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only repair admission and unchanged ExpectRepair temporal behavior on actual delivery/codec sources.

Owner snapshots are test observations, never a production copy/rollback. No Session, World, network or game runs.
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

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-ref", required=True, help="frozen source before the additive check API")
    parser.add_argument("--syntax-targets", action="store_true")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/state-repair-check-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    stage = out / "production"
    stage.mkdir()
    inputs = {str(Path(__file__)): sha(Path(__file__)), str(ROOT / "port/tools/deps.py"): sha(ROOT / "port/tools/deps.py"),
              str(ROOT / "port/deps.lock"): sha(ROOT / "port/deps.lock")}
    for name in ("state_delivery.h", "state_delivery.cpp", "state_delta.h", "state_delta.cpp", "network.h"):
        path = ROOT / "port/network" / name
        inputs[str(path)] = sha(path)
        shutil.copyfile(path, stage / name)
    for name in ("state_repair_check_driver.cpp", "state_delivery_driver.cpp"):
        path = ROOT / "port/tools/tests" / name
        inputs[str(path)] = sha(path)
        shutil.copyfile(path, stage / name)
    for name in ("monocypher.c", "monocypher.h"):
        path = deps.SRC / "monocypher-4.0.2/src" / name
        if not path.is_file():
            raise RuntimeError("pinned existing Monocypher required; no download/install")
        inputs[str(path)] = sha(path)
        shutil.copyfile(path, stage / name)
    cc, cxx = (deps.DATA / "llvm/bin" / name for name in ("clang", "clang++"))
    for path in (cc, cxx):
        inputs[str(path)] = sha(path)
    env = dict(os.environ, TMPDIR=str(out), ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:disable_coredump=1")
    for key in tuple(env):
        if key.startswith("BEA_"):
            env.pop(key)
    result = {"scope": __doc__, "inputs": inputs, "commands": [], "stages": [], "cases": [], "mutants": [],
              "limits": ["Actual delivery/codec execute on authored byte images; native WorldApply and Session do not.",
                         "Const check does not prove caller shared WAIT or advance terminal progress.",
                         "No peer, ENet, game, VM, GPU, native Windows runtime or dependency download."]}

    def save():
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")

    def run(label, command, expected=0):
        result["commands"].append([str(x) for x in command])
        p = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
        (out / (label + ".stdout")).write_text(p.stdout)
        (out / (label + ".stderr")).write_text(p.stderr)
        result["stages"].append({"name": label, "exit": p.returncode, "passed": p.returncode == expected})
        save()
        if p.returncode != expected:
            raise RuntimeError(label + " refused; see " + str(out))
        return p

    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    run("crypto-build", [cc, "-std=c99", *flags, "-I" + str(stage), "-c", stage / "monocypher.c", "-o", out / "crypto.o"])

    def build(label, directory, legacy=False, driver="state_repair_check_driver.cpp"):
        binary = out / (label + "-driver")
        sources = [directory / "state_delta.cpp"]
        if driver == "state_delivery_driver.cpp":
            sources.append(directory / "state_delivery.cpp")
        defines = ["-DCE_REPAIR_CHECK_LEGACY"] if legacy else []
        run(label + "-build", [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags, *defines,
                               "-I" + str(directory), *sources, directory / driver, out / "crypto.o", "-o", binary])
        return binary

    binary = build("production", stage)
    for case in ("check-admission", "check-operability", "expect-temporal"):
        result["cases"].append(json.loads(run(case, [binary, case]).stdout))
    before = out / "before"
    before.mkdir()
    for path in stage.iterdir():
        shutil.copyfile(path, before / path.name)
    before_inputs = {}
    for name in ("state_delivery.cpp", "state_delivery.h"):
        path = before / name
        p = subprocess.run(["git", "show", args.before_ref + ":port/network/" + name], cwd=ROOT,
                           capture_output=True, check=True, timeout=10)
        path.write_bytes(p.stdout)
        before_inputs[name] = sha(path)
    old = build("before", before, legacy=True)
    observed = json.loads(run("before-expect-temporal", [old, "expect-temporal"]).stdout)
    if observed != result["cases"][-1]:
        raise RuntimeError("ExpectRepair changed an existing temporal admission/continuation observation")
    result["before"] = {"ref": args.before_ref, "source_pins": before_inputs, "identical_temporal_observations": observed}
    regression = build("regression", stage, driver="state_delivery_driver.cpp")
    for case in ("repair", "repair-lifecycle-overtake", "terminal-pairs"):
        result["cases"].append(json.loads(run("regression-" + case, [regression, case]).stdout))
    source = (stage / "state_delivery.cpp").read_text()
    mutants = (
        ("check-commits-expect", "return ReceiverRepairAdmission(r,c,gate,now).result;",
         "return CE_StateReceiverExpectRepair(const_cast<CE_StateReceiver*>(r),c,gate,now);"),
        ("check-advances-clock", "return ReceiverRepairAdmission(r,c,gate,now).result;",
         "if(r) const_cast<CE_StateReceiver*>(r)->now=now;return ReceiverRepairAdmission(r,c,gate,now).result;"),
        ("skip-known-active-base-identity", "for(const auto &slot:r->slots) if(slot.valid&&slot.key.sequence==c->active_base.sequence&&!Equal(slot.key,c->active_base)) return {CE_DELIVERY_INVALID,true};", ""),
        ("skip-gate-match", "||!Equal(c->gate,*gate)||!GateValid(*gate)||", "||!GateValid(*gate)||"),
    )
    for label, old_text, new_text in mutants:
        if source.count(old_text) != 1:
            raise RuntimeError("mutation seam changed: " + label)
        directory = out / label
        directory.mkdir()
        for path in stage.iterdir():
            shutil.copyfile(path, directory / path.name)
        (directory / "state_delivery.cpp").write_text(source.replace(old_text, new_text))
        mutant = build(label, directory)
        p = run(label, [mutant, "check-admission"], expected=2)
        if not p.stderr.startswith("FAIL:"):
            raise RuntimeError("mutant must be killed by a causal assertion")
        result["mutants"].append({"name": label, "reason": p.stderr.strip(), "killed": True})
    if args.syntax_targets:
        for target in ("linux", "windows"):
            compiler = cxx if target == "linux" else deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang++"
            inputs[str(compiler)] = sha(compiler)
            opts = ["--target=i686-pc-linux-gnu", "--sysroot=" + str(deps.SYSROOT)] if target == "linux" else []
            run(target + "-syntax", [compiler, *opts, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                                     "-I" + str(stage), stage / "state_delivery.cpp"])
    result["inputs_after"] = {path: sha(Path(path)) for path in inputs}
    result["passed"] = result["inputs_after"] == inputs and all(row["passed"] for row in result["stages"])
    save()
    print(json.dumps({"receipt": str(out / "result.json"), "passed": result["passed"], "cases": result["cases"],
                      "mutants": len(result["mutants"]), "before_temporal_equal": True}, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
