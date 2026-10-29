#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual DirectSound mixer and music worker diagnostic controls with authored PCM and controlled scheduling.

No game, device, retail data, display or Windows runtime. The delayed-worker control demonstrates stale samples in
one looping buffer; its risk flag is not physical-output or reporter causality evidence. Fresh retained receipts
include before/after source, excluded-route clock checks, exact event order/recovery and causal mutations.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(text, signature):
    begin = text.index(signature)
    end = text.index("\n}", begin) + 2
    return text[begin:end] + "\n"


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/audio-refill-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    common = Path(subprocess.check_output(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT, text=True).strip()).parent
    data = common / "local-data/port"
    env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "SDL_AUDIO", "FIXTURE_"))}
    env.update(TMPDIR=str(out), OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1")
    receipt = {"mixer_sha256": sha(ROOT / "port/dsound/dsound.cpp"),
               "driver_sha256": sha(ROOT / "port/tools/tests/audio_refill_driver.cpp"),
               "runner_sha256": sha(Path(__file__)), "patches_sha256": {}, "cases": [], "commands": [],
               "scope": "actual mixer+complete music worker; authored PCM/events/SDL scheduling; no game/device/Windows runtime/audio-quality acceptance"}
    variants = {}
    for label in ("baseline", "diagnostic"):
        folder = out / label
        folder.mkdir()
        shutil.copyfile(ROOT / "src/PCMusicStream.cpp", folder / "PCMusicStream.cpp")
        for patch in sorted((ROOT / "port/patches").glob("*.patch")):
            # The later publication patch is based on the diagnostics patch and is absent from this baseline.
            if label == "baseline" and patch.name in ("0073-audio-refill-diagnostics.patch",
                                                      "0131-ce-music-refill-publication.patch",
                                                      "0132-ce-music-refill-order-diagnostics.patch"):
                continue
            sections = [s for s in re.split(r"(?=^--- a/)", patch.read_text(), flags=re.M)
                        if s.startswith("--- a/PCMusicStream.cpp\n")]
            if sections:
                receipt["patches_sha256"][patch.name] = sha(patch)
                r = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1"], cwd=folder, input="".join(sections),
                                   capture_output=True, text=True, timeout=10)
                if r.returncode:
                    raise RuntimeError(r.stdout + r.stderr)
        (folder / "music.inc").write_text(re.sub(r"^#include[^\n]*\n", "", (folder / "PCMusicStream.cpp").read_text(), flags=re.M))
        variants[label] = folder
    source = (ROOT / "port/platform/common/portlog.cpp").read_text()
    admission = function(source, "bool PortSessionEnabled()")
    ce = function((ROOT / "port/platform/common/ce.cpp").read_text(), "bool PortCeLayer()")
    mutations = (("no-observation", "music.inc", "sPortRefillDiagnostics = PortSessionEnabled();", "sPortRefillDiagnostics = false;", "starved", "every bounded"),
                 ("wrong-half", "music.inc", "unsigned requested=offset/STREAM_HALF;", "unsigned requested=1-offset/STREAM_HALF;", "normal", "ordinary flag diagnoses only forced starvation risk"))
    for label, name, old, new, _, _ in mutations:
        folder = out / label
        folder.mkdir()
        text = (variants["diagnostic"] / name).read_text()
        if text.count(old) != 1:
            raise RuntimeError("nonunique mutation " + label)
        (folder / name).write_text(text.replace(old, new))
        variants[label] = folder
    for label, folder in variants.items():
        (folder / "admission.inc").write_text(admission)
        (folder / "ce-admission.inc").write_text(ce)
        for public in (True, False) if label in ("baseline", "diagnostic") else (True,):
            binary = folder / ("public" if public else "private")
            cmd = ["clang++", "-std=c++17", "-O1", "-g", "-pthread", "-DNOMINMAX", "-DINITGUID", "-fms-extensions", "-fno-autolink", "-w",
                   *(['-DCE_VERSION="fixture"'] if public else []), *(["-DFIXTURE_BASELINE"] if label == "baseline" else []),
                   "-I" + str(folder), "-I" + str(ROOT / "port/d3dx/include"), "-I" + str(ROOT / "port/include"),
                   "-I" + str(ROOT / "port/include/linux"), "-I" + str(data / "dxvk-native/usr/include/dxvk"), "-I" + str(data / "prefix-sniper/include"),
                   '-DMIXER_SOURCE="' + str(ROOT / "port/dsound/dsound.cpp") + '"',
                   "-include", str(ROOT / "port/platform/common/portlog.h"), str(ROOT / "port/tools/tests/audio_refill_driver.cpp"), "-o", str(binary)]
            receipt["commands"].append(cmd)
            build = subprocess.run(cmd, capture_output=True, text=True, timeout=40, env=env)
            (folder / (binary.name + "-build.log")).write_text(build.stdout + build.stderr)
            if build.returncode:
                raise RuntimeError("compile failed: " + str(folder))
            gates = {"ordinary": {}, "faithful": {"BEA_PORT_CE_LAYER": "0"}, "clock": {"BEA_PORT_CLOCK": "shim"},
                     "replay": {"BEA_PORT_TAPE": "fixture", "BEA_PORT_CE_LAYER": "1"},
                     "record": {"BEA_PORT_RECORD": "fixture", "BEA_PORT_CE_LAYER": "1"},
                     "probes": {"BEA_PORT_PROBES": "fixture", "BEA_PORT_CE_LAYER": "1"}}
            if label not in ("baseline", "diagnostic"):
                control = next(m for m in mutations if m[0] == label)
                trials = [("ordinary", {}, control[4], 2, "bounded refill" if label == "no-observation" else control[5])]
            else:
                trials = [(gate, variables, mode, 0, "") for gate, variables in gates.items() for mode in ("normal", "starved")]
                if label == "diagnostic" and public:
                    trials += [("ordinary", {}, "normal-limit", 0, ""), ("ordinary", {}, "starved-limit", 0, "")]
            for gate, variables, mode, expected, reason in trials:
                r = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=8, env=dict(env, **variables))
                passed = r.returncode == expected and (not reason or reason in r.stderr)
                receipt["cases"].append({"variant": label, "public": public, "gate": gate, "mode": mode,
                    "exit": r.returncode, "expected_exit": expected, "reason": reason, "passed": passed,
                    "stdout": r.stdout, "stderr": r.stderr})
                (folder / f"{binary.name}-{gate}-{mode}.log").write_text(r.stdout + r.stderr)
                print(label, binary.name, gate, mode, "PASS" if passed else "FAIL", flush=True)
    # Serial scheduling: the diagnostic and staged publisher preserve playback/cursor progression and recovery.
    # This test does not cover concurrent access to a half while the decoder is filling it.
    keys = ("frames", "mismatched_frames", "first_bad_frame", "refill_dispatches", "voices", "reader_frames")
    references = {mode: json.loads(next(c["stdout"] for c in receipt["cases"]
        if c["variant"] == "baseline" and c["public"] and c["gate"] == "ordinary" and c["mode"] == mode))
        for mode in ("normal", "starved")}
    for case in receipt["cases"]:
        if case["exit"] == 0 and case["mode"] in references:
            observed = json.loads(case["stdout"])
            case["playback_equals_baseline"] = all(observed[k] == references[case["mode"]][k] for k in keys)
            case["passed"] = case["passed"] and case["playback_equals_baseline"]
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json", flush=True)
    return 0 if all(c["passed"] for c in receipt["cases"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
