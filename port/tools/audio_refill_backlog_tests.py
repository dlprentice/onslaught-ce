#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare on-time, differently ordered, and coalesced music wakes with authored PCM.

The complete patched music worker and DirectSound mixer are compiled, with authored reader, SDL and event boundaries.
This is an explicitly scheduled host control, not Windows execution, device output or reporter causality evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MODES = ("on-time", "backlog", "backlog-ordered", "collapsed", "boundary", "equality-recover", "spanning",
         "slow-read", "reader-open", "restart", "short-eof")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(text, signature):
    begin = text.index(signature)
    end = text.index("\n}", begin) + 2
    return text[begin:end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", action="append", choices=MODES, help="run a selected case (repeatable)")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/audio-refill-backlog-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    common = Path(subprocess.check_output(
        ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT, text=True).strip()).parent
    deps = common / "local-data/port"
    env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "SDL_AUDIO", "LD_PRELOAD"))}
    env.update(TMPDIR=str(out), OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1")
    driver = ROOT / "port/tools/tests/audio_refill_backlog_driver.cpp"
    fixture = ROOT / "port/tools/tests/audio_refill_driver.cpp"
    mixer = ROOT / "port/dsound/dsound.cpp"
    inputs = (driver, fixture, mixer, ROOT / "src/PCMusicStream.cpp", ROOT / "port/platform/common/portlog.cpp",
              ROOT / "port/platform/common/portlog.h", ROOT / "port/platform/common/ce.cpp", Path(__file__))
    receipt = {"scope": __doc__, "pins": {str(p.relative_to(ROOT)): sha(p) for p in inputs},
               "patches": {}, "cases": []}
    try:
        music = out / "PCMusicStream.cpp"
        music.write_bytes((ROOT / "src/PCMusicStream.cpp").read_bytes())
        for patch in sorted((ROOT / "port/patches").glob("*.patch")):
            sections = [s for s in re.split(r"(?=^--- a/)", patch.read_text(), flags=re.M)
                        if s.startswith("--- a/PCMusicStream.cpp\n")]
            if sections:
                receipt["patches"][patch.name] = sha(patch)
                result = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1"], cwd=out,
                                        input="".join(sections), capture_output=True, text=True, timeout=10)
                if result.returncode:
                    raise RuntimeError("music patch failed: " + result.stdout + result.stderr)
        receipt["pins"]["patched-worker"] = sha(music)
        (out / "music.inc").write_text(re.sub(r"^#include[^\n]*\n", "", music.read_text(), flags=re.M))
        (out / "admission.inc").write_text(function((ROOT / "port/platform/common/portlog.cpp").read_text(),
                                                   "bool PortSessionEnabled()"))
        (out / "ce-admission.inc").write_text(function((ROOT / "port/platform/common/ce.cpp").read_text(),
                                                      "bool PortCeLayer()"))
        binary = out / "driver"
        command = ["clang++", "-std=c++17", "-O1", "-g", "-pthread", "-DNOMINMAX", "-DINITGUID",
                   '-DCE_VERSION="fixture"', "-fms-extensions", "-fno-autolink", "-w", "-I" + str(out),
                   "-I" + str(ROOT / "port/d3dx/include"), "-I" + str(ROOT / "port/include"),
                   "-I" + str(ROOT / "port/include/linux"), "-I" + str(deps / "dxvk-native/usr/include/dxvk"),
                   "-I" + str(deps / "prefix-sniper/include"), '-DMIXER_SOURCE="' + str(mixer) + '"',
                   "-include", str(ROOT / "port/platform/common/portlog.h"), str(driver), "-o", str(binary)]
        receipt["command"] = command
        build = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)
        (out / "build.log").write_text(build.stdout + build.stderr)
        if build.returncode:
            raise RuntimeError("compile failed: " + build.stderr[-4000:])
        for mode in args.case or MODES:
            run = subprocess.run([str(binary), mode], cwd=ROOT, env=env, capture_output=True, text=True, timeout=15)
            (out / (mode + ".log")).write_text(run.stdout + run.stderr)
            if run.returncode:
                raise RuntimeError(mode + " failed: " + run.stderr[-3000:])
            metrics = json.loads(run.stdout)
            if metrics.get("case") != mode:
                raise RuntimeError("fixture returned the wrong case")
            receipt["cases"].append(metrics)
            print(json.dumps(metrics), flush=True)
        receipt["pass"] = True
    except Exception as error:
        receipt["pass"] = False
        receipt["failure"] = str(error)
        raise
    finally:
        (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(out / "result.json", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
