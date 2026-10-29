#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Test CE music refill publication using the actual patched worker and mixer with authored PCM.

SDL, reader and thread scheduling are controlled boundaries: this neither opens a device nor runs a game. The
predecessor must expose a partial refill, and ordinary CE must submit whole old samples until publication. Replay,
recording and faithful/private routes retain the predecessor's path. This is not Windows or physical-audio evidence.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PREDECESSOR_SKIPS = {"0131-ce-music-refill-publication.patch", "0132-ce-music-refill-order-diagnostics.patch"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(text, signature):
    begin = text.index(signature)
    end = text.index("\n}", begin) + 2
    return text[begin:end] + "\n"


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/audio-refill-overlap-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    common = Path(subprocess.check_output(
        ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT, text=True).strip()).parent
    deps = common / "local-data/port"
    env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "SDL_AUDIO", "LD_PRELOAD"))}
    env.update(TMPDIR=str(out), OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1")
    driver = ROOT / "port/tools/tests/audio_refill_overlap_driver.cpp"
    fixture = ROOT / "port/tools/tests/audio_refill_driver.cpp"
    mixer = ROOT / "port/dsound/dsound.cpp"
    inputs = (driver, fixture, mixer, ROOT / "src/PCMusicStream.cpp", ROOT / "port/platform/common/portlog.cpp",
              ROOT / "port/platform/common/portlog.h", ROOT / "port/platform/common/ce.cpp", Path(__file__))
    receipt = {"scope": __doc__, "pins": {str(p.relative_to(ROOT)): sha(p) for p in inputs},
               "patches": {}, "commands": [], "cases": []}
    try:
        binaries = {}
        commands = {}
        for variant in ("fixed", "predecessor", "private", "lock-failure", "lock-short",
                        "publish-paused", "publish-unlocked"):
            folder = out / variant
            folder.mkdir()
            music = folder / "PCMusicStream.cpp"
            music.write_bytes((ROOT / "src/PCMusicStream.cpp").read_bytes())
            for patch in sorted((ROOT / "port/patches").glob("*.patch")):
                if variant == "predecessor" and patch.name in PREDECESSOR_SKIPS:
                    continue
                sections = [s for s in re.split(r"(?=^--- a/)", patch.read_text(), flags=re.M)
                            if s.startswith("--- a/PCMusicStream.cpp\n")]
                if sections:
                    receipt["patches"][patch.name] = sha(patch)
                    result = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1"], cwd=folder,
                                            input="".join(sections), capture_output=True, text=True, timeout=10)
                    if result.returncode:
                        raise RuntimeError("music patch failed: " + result.stdout + result.stderr)
            (folder / "music.inc").write_text(re.sub(r"^#include[^\n]*\n", "", music.read_text(), flags=re.M))
            (folder / "admission.inc").write_text(function((ROOT / "port/platform/common/portlog.cpp").read_text(),
                                                           "bool PortSessionEnabled()"))
            (folder / "ce-admission.inc").write_text(function((ROOT / "port/platform/common/ce.cpp").read_text(),
                                                              "bool PortCeLayer()"))
            source = mixer
            if variant in ("lock-failure", "lock-short", "publish-paused", "publish-unlocked"):
                # Fault-inject Lock or pause the actual publisher at its midpoint in a disposable mixer copy.
                source = folder / "port/dsound/dsound.cpp"
                source.parent.mkdir(parents=True)
                header = folder / "port/platform/common/portlog.h"
                header.parent.mkdir(parents=True)
                header.write_bytes((ROOT / "port/platform/common/portlog.h").read_bytes())
                text = mixer.read_text()
                if variant == "lock-failure":
                    old = "\t\tDWORD reqOff = off, reqN = n;"
                    new = "\t\tif (off == 0 && n == 0x40000) return DSERR_INVALIDCALL;\n" + old
                elif variant == "lock-short":
                    old = "\t\t*n1 = first;"
                    new = "\t\t*n1 = (off == 0 && n == 0x40000) ? first - 4 : first;"
                else:
                    old = "\tmemcpy(destination, decoded, bytes);"
                    new = ("\tmemcpy(destination, decoded, 256);\n\tFixturePublishBoundary();\n"
                           "\tmemcpy((unsigned char *)destination + 256, (const unsigned char *)decoded + 256, bytes - 256);")
                if text.count(old) != 1:
                    raise RuntimeError("mixer mutation is not unique: " + variant)
                text = text.replace(old, new)
                if variant == "publish-unlocked":
                    old = "\tSDL_LockMutex(gMixer.lock);\n" + new + "\n\tSDL_UnlockMutex(gMixer.lock);"
                    if text.count(old) != 1:
                        raise RuntimeError("publisher lock mutation is not unique")
                    text = text.replace(old, new)
                source.write_text(text)
                receipt["pins"][variant + "/mixer"] = sha(source)
            binary = folder / "driver"
            command = ["clang++", "-std=c++17", "-O1", "-g", "-pthread", "-DNOMINMAX", "-DINITGUID",
                       *(['-DCE_VERSION="fixture"'] if variant != "private" else []),
                       *(["-DFIXTURE_PRODUCTION"] if variant == "fixed" else []),
                       "-fms-extensions", "-fno-autolink", "-w", "-I" + str(folder),
                       "-I" + str(ROOT / "port/d3dx/include"), "-I" + str(ROOT / "port/include"),
                       "-I" + str(ROOT / "port/include/linux"), "-I" + str(deps / "dxvk-native/usr/include/dxvk"),
                       "-I" + str(deps / "prefix-sniper/include"), '-DMIXER_SOURCE="' + str(source) + '"',
                       "-include", str(ROOT / "port/platform/common/portlog.h"), str(driver), "-o", str(binary)]
            receipt["commands"].append(command)
            build = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)
            (folder / "build.log").write_text(build.stdout + build.stderr)
            if build.returncode:
                raise RuntimeError(variant + " compile failed: " + build.stderr[-4000:])
            binaries[variant] = binary
            commands[variant] = command
            receipt["pins"][variant + "/patched-worker"] = sha(music)
        cases = [
            ("fixed", "ordinary", {}, mode, 0, "") for mode in
            ("overlap-fixed", "safe-half", "late-dispatch", "normal", "short-eof", "empty-initial", "shutdown", "publication-latency")
        ] + [
            ("predecessor", "ordinary", {}, "overlap-old", 0, ""),
            ("predecessor", "causal-negative", {}, "overlap-fixed", 2, "callback sees whole old samples"),
            ("predecessor", "ordinary", {}, "normal", 0, ""),
            ("predecessor", "ordinary", {}, "short-eof", 0, ""),
            ("predecessor", "ordinary", {}, "empty-initial", 0, ""),
            ("private", "ordinary", {}, "overlap-old", 0, ""),
            ("private", "ordinary", {}, "normal", 0, ""),
            ("lock-failure", "fault", {}, "lock-error", 0, ""),
            ("lock-short", "fault", {}, "lock-error", 0, ""),
            ("publish-paused", "instrumented", {}, "publication-fixed", 0, ""),
            ("publish-unlocked", "instrumented", {}, "publication-unlocked", 0, ""),
            ("publish-unlocked", "causal-negative", {}, "publication-fixed", 2, "callback is blocked during publication"),
        ] + [
            ("fixed", gate, variables, "overlap-old", 0, "") for gate, variables in (
                ("faithful", {"BEA_PORT_CE_LAYER": "0"}),
                ("clock-forced", {"BEA_PORT_CLOCK": "shim", "BEA_PORT_CE_LAYER": "1"}),
                ("replay-forced", {"BEA_PORT_TAPE": "fixture", "BEA_PORT_CE_LAYER": "1"}),
                ("record-forced", {"BEA_PORT_RECORD": "fixture", "BEA_PORT_CE_LAYER": "1"}),
                ("probes-forced", {"BEA_PORT_PROBES": "fixture", "BEA_PORT_CE_LAYER": "1"}),
            )
        ] + [
            ("fixed", gate, variables, "normal", 0, "") for gate, variables in (
                ("faithful", {"BEA_PORT_CE_LAYER": "0"}),
                ("clock-forced", {"BEA_PORT_CLOCK": "shim", "BEA_PORT_CE_LAYER": "1"}),
                ("replay-forced", {"BEA_PORT_TAPE": "fixture", "BEA_PORT_CE_LAYER": "1"}),
                ("record-forced", {"BEA_PORT_RECORD": "fixture", "BEA_PORT_CE_LAYER": "1"}),
                ("probes-forced", {"BEA_PORT_PROBES": "fixture", "BEA_PORT_CE_LAYER": "1"}),
            )
        ]
        for variant, gate, variables, mode, expected, reason in cases:
            run = subprocess.run([str(binaries[variant]), mode], cwd=ROOT, env=dict(env, **variables),
                                 capture_output=True, text=True, timeout=15)
            passed = run.returncode == expected and (not reason or reason in run.stderr)
            entry = {"variant": variant, "gate": gate, "mode": mode, "exit": run.returncode,
                     "expected_exit": expected, "passed": passed, "stdout": run.stdout, "stderr": run.stderr}
            if expected == 0 and passed:
                entry["metrics"] = json.loads(run.stdout)
            receipt["cases"].append(entry)
            (out / variant / (gate + "-" + mode + ".log")).write_text(run.stdout + run.stderr)
            print(variant, gate, mode, "PASS" if passed else "FAIL", flush=True)
            if not passed:
                raise RuntimeError(variant + " " + mode + " failed: " + run.stderr[-2000:])
        normal = [c["metrics"] for c in receipt["cases"] if c["mode"] == "normal"]
        fields = ("frames", "mismatched_samples", "refill_dispatches", "pcm_fnv64")
        if any(any(row[k] != normal[0][k] for k in fields) for row in normal[1:]):
            raise RuntimeError("normal-playback metrics differ from predecessor/private")
        # Relaxed-atomic scheduling preserves a real race in the predecessor. The fixed path and the
        # different-half control must be clean; the report must identify the actual reader and mixer sample.
        for variant, modes in (("fixed", ("race-fixed", "race-safe")),
                               ("predecessor", ("race-old", "race-safe"))):
            command = commands[variant].copy()
            command[1:1] = ["-fsanitize=thread", "-fno-omit-frame-pointer"]
            command[-1] = str(out / variant / "tsan-driver")
            receipt["commands"].append(command)
            build = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)
            (out / variant / "tsan-build.log").write_text(build.stdout + build.stderr)
            if build.returncode:
                raise RuntimeError(variant + " TSAN compile failed: " + build.stderr[-4000:])
            for mode in modes:
                run = subprocess.run([command[-1], mode], cwd=ROOT,
                                     env=dict(env, TSAN_OPTIONS="halt_on_error=1:exitcode=66"),
                                     capture_output=True, text=True, timeout=15)
                is_race = variant == "predecessor" and mode == "race-old"
                passed = ((run.returncode == 66 and "ThreadSanitizer: data race" in run.stderr and
                           "CBaseWaveFileRead::Read" in run.stderr and "Buffer::Sample" in run.stderr) if is_race
                          else (run.returncode == 0 and "ThreadSanitizer:" not in run.stderr))
                receipt["cases"].append({"variant": variant, "gate": "tsan", "mode": mode,
                                         "exit": run.returncode, "passed": passed,
                                         "stdout": run.stdout, "stderr": run.stderr})
                (out / variant / ("tsan-" + mode + ".log")).write_text(run.stdout + run.stderr)
                print(variant, "tsan", mode, "PASS" if passed else "FAIL", flush=True)
                if not passed:
                    raise RuntimeError(variant + " " + mode + " TSAN failed: " + run.stderr[-4000:])
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
