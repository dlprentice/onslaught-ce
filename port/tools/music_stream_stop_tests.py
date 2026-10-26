#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Controlled scheduling checks of the production music worker, without game/device/retail inputs.

Apply the production stream patch to a fresh source copy, remove only its platform includes, then compile the
entire translation unit inside a driver supplying event, reader, and buffer boundaries. The worker, play, stop,
and virtual pump are production code. A separate typo-only control changes precisely the bad ResetEvent argument.
This is a host concurrency/ownership check, not a Windows ABI, audio-quality, or reporter reproduction claim.
"""
import argparse
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/music-stream-stop-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    source_dir = out / "production"
    source_dir.mkdir()
    for name in ("PCMusicStream.cpp",):
        shutil.copyfile(ROOT / "src" / name, source_dir / name)
    patch = ROOT / "port/patches/0035-music-stream-virtual-pump.patch"
    stream_patch = out / "stream.patch"
    stream_patch.write_text(patch.read_text().split("--- a/PCMusic.cpp\n", 1)[0])
    applied = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(stream_patch)],
                             cwd=source_dir, capture_output=True, text=True, timeout=10)
    if applied.returncode:
        raise SystemExit(applied.stdout + applied.stderr)
    actual = source_dir / "PCMusicStream.cpp"
    code = re.sub(r"^#include[^\n]*\n", "", actual.read_text(), flags=re.M)
    typo = "ResetEvent((HANDLE)PlayMusicStream);"
    if code.count(typo) != 1:
        raise SystemExit("expected exactly one production stop typo")
    correction = ROOT / "port/patches/0058-ce-music-stream-stop.patch"
    applied = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(correction)],
                             cwd=source_dir, capture_output=True, text=True, timeout=10)
    if applied.returncode:
        raise SystemExit(applied.stdout + applied.stderr)
    fixed = re.sub(r"^#include[^\n]*\n", "", actual.read_text(), flags=re.M)
    variants = {"released": (code, True, False), "event-only": (code.replace(typo, "ResetEvent(sPlayEvent);"), True, False),
                "fixed-ce": (fixed, True, True), "fixed-faithful": (fixed, True, False), "fixed-private": (fixed, False, False)}
    diagnostics = ROOT / "port/patches/0061-session-diagnostics.patch"
    sections = [part for part in re.split(r"(?=^--- a/)", diagnostics.read_text(), flags=re.M)
                if part.startswith("--- a/PCMusicStream.cpp\n+++ b/PCMusicStream.cpp\n")]
    if len(sections) != 1:
        raise SystemExit("expected one complete diagnostics section for the music worker")
    applied = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(source_dir), "-r", "-"],
                             input=sections[0], capture_output=True, text=True, timeout=10)
    if applied.returncode:
        raise SystemExit(applied.stdout + applied.stderr)
    logged = re.sub(r"^#include[^\n]*\n", "", actual.read_text(), flags=re.M)
    variants.update({"diagnostic-ce": (logged, True, True), "diagnostic-faithful": (logged, True, False),
                     "diagnostic-private": (logged, False, False)})
    (out / "SDL3").mkdir()
    (out / "SDL3/SDL.h").write_text("#include <cstdint>\nuint64_t SDL_GetTicks();\n")
    clock = out / "log-clock.cpp"
    clock.write_text("#include <cstdint>\nuint64_t SDL_GetTicks() { return 0; }\n")
    cases = ("normal", "queued-stop", "consumed-stop", "inflight-stop", "stop-then-play", "restart-same",
             "restart-different", "replace-inflight", "stopped-refill", "refill-new-track", "shutdown-fill",
             "virtual-normal", "empty-start", "no-sound", "reinit", "no-sound-recover")
    ce_path = ROOT / "port/platform/common/ce.cpp"
    ce_source = ce_path.read_text()
    ce_start = ce_source.index("bool PortCeLayer()\n{")
    ce_end = ce_source.index("\n}\n", ce_start) + 3
    admission = out / "ce-admission.inc"
    admission.write_text(ce_source[ce_start:ce_end])
    receipt = {"source_sha256": sha(ROOT / "src/PCMusicStream.cpp"), "patch_sha256": sha(patch),
               "corrected_sha256": sha(actual), "correction_sha256": sha(correction),
               "driver_sha256": sha(ROOT / "port/tools/tests/music_stream_stop_driver.cpp"),
               "diagnostics_patch_sha256": sha(diagnostics),
               "ce_source_sha256": sha(ce_path), "ce_admission_sha256": sha(admission),
               "compiler": subprocess.run([args.compiler, "--version"], capture_output=True, text=True, check=True).stdout,
               "cases": [], "commands": []}
    for variant, (text, public, serialized) in variants.items():
        included = out / f"{variant}.inc"
        included.write_text(text)
        binary = out / variant
        command = [args.compiler, "-std=c++14", "-pthread", "-O0", "-g", "-Wall", "-Wextra",
                   "-DEXPECT_SERIALIZED=" + str(int(serialized)), *(["-DCE_VERSION=\"test\""] if public else []),
                   '-DMUSIC_SOURCE="' + str(included) + '"',
                   '-DCE_ADMISSION_SOURCE="' + str(admission) + '"',
                   str(ROOT / "port/tools/tests/music_stream_stop_driver.cpp"), "-o", str(binary)]
        if variant.startswith("diagnostic-"):
            command += ["-I" + str(out), "-include", str(ROOT / "port/platform/common/portlog.h"),
                        str(ROOT / "port/platform/common/portlog.cpp"), str(clock)]
        receipt[variant + "_sha256"] = sha(included)
        receipt[variant + "_binary_sha256"] = None
        receipt["commands"].append(command)
        result = subprocess.run(command, capture_output=True, text=True, timeout=30, env=dict(os.environ, TMPDIR=str(out)))
        (out / f"{variant}-build.log").write_text(result.stdout + result.stderr)
        if result.returncode:
            raise SystemExit(f"Build failed: {out / (variant + '-build.log')}")
        receipt[variant + "_binary_sha256"] = sha(binary)
        env = dict(os.environ)
        for name in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_CE_LAYER"):
            env.pop(name, None)
        extra = ("read-failed", "format-failed") if variant.startswith("diagnostic-") else ()
        for case in (*cases, *(("failed-buffer", "failed-buffer-recover") if serialized else ()), *extra):
            proc = subprocess.run([str(binary), case, "ce" if serialized else "faithful"], capture_output=True, text=True, timeout=10, env=env)
            try:
                observed = json.loads(proc.stdout)
            except json.JSONDecodeError:
                observed = {}
            # A demonstrated defect is expected only in these frozen negative controls.
            expect = not serialized and (case in ("inflight-stop", "consumed-stop") or
                                         (variant != "event-only" and case == "queued-stop"))
            passed = proc.returncode == 0 and observed.get("after_stop_playing") == expect and observed.get("checks", 0) > 0
            if variant == "diagnostic-ce" and case in ("read-failed", "format-failed"):
                passed = passed and ("initial read failed" if case == "read-failed" else "missing or unsupported format") in proc.stderr
            if variant in ("diagnostic-faithful", "diagnostic-private"):
                passed = passed and "session pid=" not in proc.stderr
            receipt["cases"].append(dict(variant=variant, case=case, exit=proc.returncode,
                                         expected_after_stop_playing=expect, observed=observed, passed=passed, stderr=proc.stderr))
            if not passed:
                print(f"FAIL {variant}: {case} {observed} {proc.stderr}", flush=True)
        selected = [case for case in receipt["cases"] if case["variant"] == variant]
        print(f"{variant}: {sum(case['passed'] for case in selected)}/{len(selected)} expected outcomes", flush=True)
    indexed = {(case["variant"], case["case"]): case["observed"] for case in receipt["cases"]}
    receipt["unchanged_controls"] = []
    for newer, older in (("diagnostic-ce", "fixed-ce"), ("diagnostic-faithful", "fixed-faithful"),
                         ("diagnostic-private", "fixed-private")):
        for variant, case in indexed:
            if variant != newer or (older, case) not in indexed:
                continue
            same = indexed[newer, case] == indexed[older, case]
            receipt["unchanged_controls"].append({"variant": newer, "case": case, "passed": same})
            if not same:
                print(f"FAIL changed complete worker result: {newer} {case}")
    print(f"{sum(c['passed'] for c in receipt['unchanged_controls'])}/{len(receipt['unchanged_controls'])} complete unchanged worker controls")
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if all(case["passed"] for case in [*receipt["cases"], *receipt["unchanged_controls"]]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
