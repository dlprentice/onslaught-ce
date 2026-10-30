#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Test the bounded, worker-only handled-refill-order observation with authored PCM.

The complete patched music worker and mixer are compiled with controlled SDL, event, reader and clock boundaries.
No game, device or Windows runtime is used. A missing anomaly cannot exclude coalesced events or exhausted budgets.
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
PATCH = "0132-ce-music-refill-order-diagnostics.patch"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}", start) + 2] + "\n"


def replace_one(text, old, new, label):
    if text.count(old) != 1:
        raise RuntimeError(label + " mutation is not unique")
    return text.replace(old, new, 1)


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/audio-refill-order-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    common = Path(subprocess.check_output(
        ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT, text=True).strip()).parent
    deps = common / "local-data/port"
    env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "SDL_AUDIO", "LD_PRELOAD", "FIXTURE_"))}
    env.update(TMPDIR=str(out), OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1")
    mixer = ROOT / "port/dsound/dsound.cpp"
    driver = ROOT / "port/tools/tests/audio_refill_backlog_driver.cpp"
    fixture = ROOT / "port/tools/tests/audio_refill_driver.cpp"
    pins = (mixer, driver, fixture, ROOT / "src/PCMusicStream.cpp", ROOT / "port/platform/common/portlog.cpp",
            ROOT / "port/platform/common/portlog.h", ROOT / "port/platform/common/ce.cpp", Path(__file__))
    receipt = {"scope": __doc__, "pins": {str(p.relative_to(ROOT)): sha(p) for p in pins},
               "patches": {}, "commands": [], "cases": [], "comparisons": []}
    variants = ("candidate", "predecessor", "private", "no-observation", "reset-on-open", "no-stop-clear",
                "no-voice", "no-voice-early-arm", "failed-lock", "before-lock", "before-eof",
                "no-forget-suppressed", "no-forget-read", "failed-unlock", "no-forget-unlock", "no-forget-wait", "no-forget-quit")
    try:
        binaries = {}
        for variant in variants:
            folder = out / variant
            folder.mkdir()
            music = folder / "PCMusicStream.cpp"
            music.write_bytes((ROOT / "src/PCMusicStream.cpp").read_bytes())
            for patch in sorted((ROOT / "port/patches").glob("*.patch")):
                if variant == "predecessor" and patch.name in (PATCH, "0187-audio-observation-counters.patch"):
                    continue
                sections = [s for s in re.split(r"(?=^--- a/)", patch.read_text(), flags=re.M)
                            if s.startswith("--- a/PCMusicStream.cpp\n")]
                if sections:
                    receipt["patches"][patch.name] = sha(patch)
                    applied = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1"], cwd=folder,
                                             input="".join(sections), capture_output=True, text=True, timeout=10)
                    if applied.returncode:
                        raise RuntimeError("music patch failed: " + applied.stdout + applied.stderr)
            code = re.sub(r"^#include[^\n]*\n", "", music.read_text(), flags=re.M)
            observe = "\t\t\t\t\tPortRefillOrder(offset);\n"
            if variant == "no-observation":
                code = replace_one(code, observe, "", variant)
            elif variant == "reset-on-open":
                old = "\t\tcase 0:\t\t// a new track\n\t\t\tsReader->Close();\n"
                code = replace_one(code, old, old + "\t\t\tif (sPortRefillDiagnostics && sPortRefillOrderKnown) sPortRefillExpectedHalf = 0;\n", variant)
            elif variant == "no-stop-clear":
                old = "\tPortRefillForget(); // stop invalidates this observation while the stream guard is held\n"
                code = replace_one(code, old, "", variant)
            elif variant == "no-voice-early-arm":
                old = "FAILED(portRead) || FAILED(portUnlock) || !portRefillBefore.playing"
                code = replace_one(code, old, "FAILED(portRead) || FAILED(portUnlock)", variant)
            elif variant == "before-lock":
                old = "\t\t\tHRESULT portLock = sBuffer->Lock(offset, STREAM_HALF, &data1, &size1, &data2, &size2, 0);\n"
                code = replace_one(code, old, "\t\t\tPortRefillOrder(offset);\n" + old, variant)
            elif variant == "before-eof":
                old = "\t\tcase 2:\n\t\t{\n\t\t\tif (sSilentHalves > 2)\n"
                new = ("\t\tcase 2:\n\t\t{\n\t\t\tif (sSilentHalves > 2) PortRefillOrder(result == 1 ? 0 : STREAM_HALF);\n"
                       "\t\t\tif (sSilentHalves > 2)\n")
                code = replace_one(code, old, new, variant)
            elif variant == "no-forget-suppressed":
                old = "\t\t\tPortRefillForget(); // this consumed wake is not handled, so subsequent order is unknown\n"
                code = replace_one(code, old, "", variant)
            elif variant == "no-forget-read":
                old = "FAILED(portRead) || FAILED(portUnlock) || !portRefillBefore.playing"
                code = replace_one(code, old, "FAILED(portUnlock) || !portRefillBefore.playing", variant)
            elif variant == "no-forget-unlock":
                old = "FAILED(portRead) || FAILED(portUnlock) || !portRefillBefore.playing"
                code = replace_one(code, old, "FAILED(portRead) || !portRefillBefore.playing", variant)
            elif variant == "no-forget-wait":
                old = "\t\t\tif (sPortRefillDiagnostics) { CPortMusicStreamGuard guard; PortRefillForget(); }\n"
                code = replace_one(code, old, "", variant)
            elif variant == "no-forget-quit":
                old = "\t\t\tPortRefillForget(); // quit also retires the guarded observation\n"
                code = replace_one(code, old, "", variant)
            (folder / "music.inc").write_text(code)
            (folder / "admission.inc").write_text(function((ROOT / "port/platform/common/portlog.cpp").read_text(),
                                                           "bool PortSessionEnabled()"))
            (folder / "ce-admission.inc").write_text(function((ROOT / "port/platform/common/ce.cpp").read_text(),
                                                              "bool PortCeLayer()"))
            source = folder / "port/dsound/dsound.cpp"
            source.parent.mkdir(parents=True)
            header = folder / "port/platform/common/portlog.h"
            header.parent.mkdir(parents=True)
            header.write_bytes((ROOT / "port/platform/common/portlog.h").read_bytes())
            mix = mixer.read_text()
            marker = "\tif (!gSessionAudio) return false;\n\tint savedError = errno;"
            mix = replace_one(mix, marker, "\t++fixtureSnapshotCalls; // fixture-only call witness\n" + marker, "snapshot witness")
            if variant in ("no-voice", "no-voice-early-arm"):
                old = "if (!playing && Bytes() && !(flags & DSBCAPS_PRIMARYBUFFER) && gMixer.numVoices < kMaxVoices)"
                mix = replace_one(mix, old, "if (false && !playing && Bytes() && !(flags & DSBCAPS_PRIMARYBUFFER) && gMixer.numVoices < kMaxVoices)", variant)
            if variant in ("failed-lock", "before-lock"):
                old = "\t\tDWORD reqOff = off, reqN = n;"
                fault = "\t\tstatic unsigned fixtureHalfLocks;\n\t\tif (off == 0 && n == 0x40000 && ++fixtureHalfLocks == 2) return DSERR_INVALIDCALL;\n"
                mix = replace_one(mix, old, fault + old, variant)
            if variant in ("failed-unlock", "no-forget-unlock"):
                old = "\t\t// DirectSound rejects an unlock larger than the buffer"
                fault = "\t\tstatic unsigned fixtureHalfUnlocks;\n\t\tif (n1 == 0x40000 && ++fixtureHalfUnlocks == 1) return DSERR_INVALIDCALL;\n"
                mix = replace_one(mix, old, fault + old, variant)
            source.write_text(mix)
            receipt["pins"][variant + "/worker"] = sha(folder / "music.inc")
            receipt["pins"][variant + "/instrumented-mixer"] = sha(source)
            binary = folder / "driver"
            public = variant != "private"
            command = ["clang++", "-std=c++17", "-O1", "-g", "-pthread", "-DNOMINMAX", "-DINITGUID",
                       *(['-DCE_VERSION="fixture"'] if public else []),
                       *(["-DFIXTURE_HAS_ORDER"] if public and variant != "predecessor" else []),
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
        gates = {
            "faithful": {"BEA_PORT_CE_LAYER": "0"},
            "clock-forced": {"BEA_PORT_CLOCK": "shim", "BEA_PORT_CE_LAYER": "1"},
            "replay-forced": {"BEA_PORT_TAPE": "fixture", "BEA_PORT_CE_LAYER": "1"},
            "record-forced": {"BEA_PORT_RECORD": "fixture", "BEA_PORT_CE_LAYER": "1"},
            "probes-forced": {"BEA_PORT_PROBES": "fixture", "BEA_PORT_CE_LAYER": "1"},
        }
        modes = ("on-time", "backlog", "backlog-ordered", "collapsed", "reader-open", "restart", "short-eof", "gate")
        cases = [(v, "ordinary", {}, m, 0, "") for v in ("candidate", "predecessor", "private") for m in modes]
        cases += [(v, g, e, m, 0, "") for v in ("candidate", "predecessor") for g, e in gates.items() for m in ("backlog", "gate")]
        cases += [("candidate", "ordinary", {}, m, 0, "") for m in
                  ("rearm", "budget", "stop-guard", "eof-gate", "suppressed-wake", "suppressed-before-arm",
                   "failed-read", "failed-wait", "quit-guard")]
        cases += [("no-voice", "ordinary", {}, "no-voice", 0, ""),
                  ("failed-lock", "ordinary", {}, "failed-lock", 0, ""),
                  ("failed-unlock", "ordinary", {}, "failed-unlock", 0, "")]
        cases += [("no-observation", "negative", {}, "backlog", 2, "unexpected handled-wake observation count"),
                  ("reset-on-open", "negative", {}, "reader-open", 2, "unexpected handled-wake observation count"),
                  ("no-stop-clear", "negative", {}, "stop-guard", 2, "stop invalidates ordering state"),
                  ("no-voice-early-arm", "negative", {}, "no-voice", 2, "unexpected handled-wake observation count"),
                  ("before-lock", "negative", {}, "failed-lock", 2, "unexpected handled-wake observation count"),
                  ("before-eof", "negative", {}, "eof-gate", 2, "unexpected handled-wake observation count"),
                  ("no-forget-suppressed", "negative", {}, "suppressed-wake", 2, "unexpected handled-wake observation count"),
                  ("no-forget-suppressed", "negative", {}, "suppressed-before-arm", 2, "unexpected handled-wake observation count"),
                  ("no-forget-read", "negative", {}, "failed-read", 2, "unexpected handled-wake observation count"),
                  ("no-forget-unlock", "negative", {}, "failed-unlock", 2, "unexpected handled-wake observation count"),
                  ("no-forget-wait", "negative", {}, "failed-wait", 2, "failed wait invalidates ordering state"),
                  ("no-forget-quit", "negative", {}, "quit-guard", 2, "quit invalidates ordering state")]
        for variant, gate, overrides, mode, expected, reason in cases:
            detect = variant not in ("predecessor", "private") and gate in ("ordinary", "negative")
            variables = dict(env, FIXTURE_ORDER_VALIDATE="1", **overrides)
            if detect:
                variables["FIXTURE_ORDER_DETECT"] = "1"
            run = subprocess.run([str(binaries[variant]), mode], cwd=ROOT, env=variables,
                                 capture_output=True, text=True, timeout=15)
            passed = run.returncode == expected and (not reason or reason in run.stderr)
            row = {"variant": variant, "gate": gate, "mode": mode, "exit": run.returncode,
                   "expected_exit": expected, "passed": passed, "stdout": run.stdout, "stderr": run.stderr}
            if expected == 0 and passed:
                row["metrics"] = json.loads(run.stdout)
            receipt["cases"].append(row)
            (out / variant / (gate + "-" + mode + ".log")).write_text(run.stdout + run.stderr)
            print(variant, gate, mode, "PASS" if passed else "FAIL", flush=True)
            if not passed:
                raise RuntimeError(variant + " " + gate + " " + mode + ": " + run.stderr[-3000:])
        def metrics(variant, gate, mode):
            return next(c["metrics"] for c in receipt["cases"] if c["variant"] == variant and c["gate"] == gate and c["mode"] == mode)
        fields = ("worker_order", "half0", "half1", "crossing_sample", "submitted_sample", "pcm_fnv64", "voices")
        for mode in ("on-time", "backlog", "backlog-ordered"):
            before, after = metrics("predecessor", "ordinary", mode), metrics("candidate", "ordinary", mode)
            same = all(before[k] == after[k] for k in fields)
            receipt["comparisons"].append({"gate": "ordinary", "mode": mode, "fields": list(fields), "same": same})
            if not same:
                raise RuntimeError("authored playback changed: " + mode)
        for gate in ("ordinary", *gates):
            before, after = metrics("predecessor", gate, "gate"), metrics("candidate", gate, "gate")
            fields = ("clock_reads", "snapshot_calls", "order_calls", "order_lines")
            same = all(before[k] == after[k] for k in fields)
            receipt["comparisons"].append({"gate": gate, "mode": "gate", "fields": list(fields), "same": same})
            if not same or (gate != "ordinary" and after["snapshot_calls"]) or after["order_calls"] or after["order_lines"]:
                raise RuntimeError("excluded route added observation work: " + gate)
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
