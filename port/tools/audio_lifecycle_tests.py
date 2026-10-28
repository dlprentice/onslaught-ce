#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Controlled production-mixer lifecycle and actual music poll checks, without devices or retail inputs.

The full DirectSound source is compiled against the pinned SDK headers and controlled SDL function boundaries.
The frozen pre-fix reverse patch is a negative control. Timer cancellation deliberately permits an in-flight
callback, as SDL3 does. A monitor boundary bounds the known stale-clock underflow after 16 blocks, rather than
letting a broken control loop indefinitely. No production function is replaced. --tsan instruments the actual
music worker handler plus the actual CPCMusic completion poll; both CE and faithful admissions are exercised.
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


def checked(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, timeout=kwargs.pop("timeout", 45), **kwargs)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}\n{result.stderr}")
    return result


def music_sources(out, fixed):
    out.mkdir()
    names = ("PCMusicStream.cpp", "PCMusic.cpp")
    for name in names:
        shutil.copyfile(ROOT / "src" / name, out / name)
    for patch in sorted((ROOT / "port/patches").glob("*.patch")):
        # The negative control leaves out 0062, and 0103 (the CE playlist), which is written on 0062's poll.
        if not fixed and patch.name in ("0062-music-finished-poll.patch", "0103-ce-music-playlist.patch"):
            continue
        sections = [part for part in re.split(r"(?=^--- a/)", patch.read_text(), flags=re.M)
                    if any(part.startswith("--- a/" + name + "\n") for name in names)]
        if sections:
            checked(["patch", "--batch", "--fuzz=0", "-p1"], input="".join(sections), cwd=out)
    code = re.sub(r"^#include[^\n]*\n", "", (out / names[0]).read_text(), flags=re.M)
    (out / "music.inc").write_text(code)
    text = (out / names[1]).read_text()
    begin = text.index("BOOL CPCMusic::DeviceGetTrackFinished()")
    end = text.index("\n}", begin) + 2
    (out / "poll.inc").write_text(text[begin:end] + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--compiler", default="clang++")
    ap.add_argument("--tsan", action="store_true")
    ap.add_argument("--tsan-compiler", default="g++")
    args = ap.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/audio-lifecycle-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    common = Path(checked(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT).stdout.strip()).parent
    deps = common / "local-data/port"
    env = dict(os.environ, TMPDIR=str(out), OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1")
    for key in tuple(env):
        if key.startswith("BEA_") or key.startswith("SDL_AUDIO"):
            env.pop(key)
    receipt = {"commands": [], "cases": [], "source_sha256": sha(ROOT / "port/dsound/dsound.cpp"),
               "mixer_driver_sha256": sha(ROOT / "port/tools/tests/audio_lifecycle_driver.cpp"),
               "music_driver_sha256": sha(ROOT / "port/tools/tests/music_finished_driver.cpp"),
               "worker_driver_sha256": sha(ROOT / "port/tools/tests/music_stream_stop_driver.cpp"),
               "runner_sha256": sha(Path(__file__)),
               "baseline_patch_sha256": sha(ROOT / "port/tools/tests/audio_lifecycle_baseline.patch"),
               "music_patch_sha256": sha(ROOT / "port/patches/0062-music-finished-poll.patch"),
               "compiler": checked([args.compiler, "--version"]).stdout,
               "scope": "host production functions with controlled SDL/reader boundaries; not device/Windows ABI/audio-quality acceptance"}
    flags = ["-std=c++17", "-O1", "-g", "-pthread", "-DNOMINMAX", "-DINITGUID", "-fms-extensions", "-fno-autolink", "-w",
             "-I" + str(ROOT / "port/d3dx/include"), "-I" + str(ROOT / "port/include"),
             "-I" + str(ROOT / "port/include/linux"), "-I" + str(deps / "dxvk-native/usr/include/dxvk"),
             "-I" + str(deps / "prefix-sniper/include")]
    cases = ("normal-callback", "normal-timer", "virtual-reopen", "timer-reopen", "stale-catchup", "stale-timer", "stop-inflight",
             "resume-failed", "init-failed", "open-failed", "publish")
    for variant in ("before", "fixed"):
        folder = out / variant
        source = folder / "port/dsound/dsound.cpp"
        source.parent.mkdir(parents=True)
        shutil.copyfile(ROOT / "port/dsound/dsound.cpp", source)
        header = folder / "port/platform/common/portlog.h"
        header.parent.mkdir(parents=True)
        shutil.copyfile(ROOT / "port/platform/common/portlog.h", header)
        if variant == "before":
            checked(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(ROOT / "port/tools/tests/audio_lifecycle_baseline.patch")], cwd=source.parent)
            if sha(source) != "7ff6346a53fedc7ed6c7496fde083117fb8b21e3fefa583f32c0651684fba036":
                raise RuntimeError("negative control no longer matches the frozen production mixer")
        binary = folder / "driver"
        command = [args.compiler, *flags, '-DMIXER_SOURCE="' + str(source) + '"',
                   str(ROOT / "port/tools/tests/audio_lifecycle_driver.cpp"), "-o", str(binary)]
        receipt["commands"].append(command)
        build = subprocess.run(command, env=env, capture_output=True, text=True, timeout=45)
        (folder / "build.log").write_text(build.stdout + build.stderr)
        if build.returncode:
            raise RuntimeError("build failed: " + str(folder / "build.log"))
        receipt[variant + "_sha256"] = sha(source)
        receipt[variant + "_binary_sha256"] = sha(binary)
        for case in cases:
            run = subprocess.run([str(binary), case, "fixed" if variant == "fixed" else "before"], env=env,
                                 capture_output=True, text=True, timeout=8)
            (folder / (case + ".log")).write_text(run.stdout + run.stderr)
            row = {"variant": variant, "case": case, "exit": run.returncode, "stdout": run.stdout,
                   "stderr": run.stderr, "passed": run.returncode == 0}
            receipt["cases"].append(row)
            print(variant, case, "PASS" if row["passed"] else "FAIL", flush=True)
    ce = (ROOT / "port/platform/common/ce.cpp").read_text()
    start = ce.index("bool PortCeLayer()\n{")
    admission = out / "ce.inc"
    admission.write_text(ce[start:ce.index("\n}\n", start) + 3])
    controls = {}
    worker_cases = ("normal", "stop-then-play", "restart-same", "restart-different", "virtual-normal",
                    "empty-start", "no-sound", "reinit", "no-sound-recover", "read-failed", "format-failed")
    for fixed in (False, True):
        folder = out / ("music-fixed" if fixed else "music-before")
        music_sources(folder, fixed)
        binary = folder / "driver"
        command = [args.tsan_compiler if args.tsan else args.compiler, "-std=c++17", "-O1", "-g", "-pthread", "-w",
                   '-DCE_VERSION="test"', '-DMUSIC_SOURCE="' + str(folder / "music.inc") + '"',
                   '-DPOLL_SOURCE="' + str(folder / "poll.inc") + '"', '-DCE_ADMISSION_SOURCE="' + str(admission) + '"',
                   "-include", str(ROOT / "port/platform/common/portlog.h"),
                   str(ROOT / "port/tools/tests/music_finished_driver.cpp"), "-o", str(binary)]
        if args.tsan:
            command[1:1] = ["-fsanitize=thread", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        receipt["commands"].append(command)
        build = checked(command, env=env)
        (folder / "build.log").write_text(build.stdout + build.stderr)
        for mode in ("ce", "faithful"):
            run_env = dict(env, TSAN_OPTIONS="halt_on_error=1:exitcode=66")
            run = subprocess.run([str(binary), mode], env=run_env, capture_output=True, text=True, timeout=15)
            (folder / (mode + ".log")).write_text(run.stdout + run.stderr)
            expected_race = args.tsan and not fixed
            passed = (run.returncode == 66 and "ThreadSanitizer: data race" in run.stderr) if expected_race else run.returncode == 0
            row = {"variant": folder.name, "case": mode, "exit": run.returncode, "stdout": run.stdout,
                   "stderr": run.stderr, "expected_race": expected_race, "passed": passed}
            receipt["cases"].append(row)
            print(folder.name, mode, "PASS" if passed else "FAIL", flush=True)
        # Existing successful worker semantics must be identical after changing only completion publication.
        # Run these without TSan: the separate poll trial above is the deliberately concurrent race falsifier.
        for public in (True, False):
            worker_binary = folder / ("worker-public" if public else "worker-private")
            command = [args.compiler, "-std=c++17", "-O1", "-g", "-pthread", "-w",
                       *(['-DCE_VERSION="test"'] if public else []),
                       '-DMUSIC_SOURCE="' + str(folder / "music.inc") + '"',
                       '-DPOLL_SOURCE="' + str(folder / "poll.inc") + '"',
                       '-DCE_ADMISSION_SOURCE="' + str(admission) + '"',
                       "-include", str(ROOT / "port/platform/common/portlog.h"),
                       str(ROOT / "port/tools/tests/music_finished_driver.cpp"), "-o", str(worker_binary)]
            receipt["commands"].append(command)
            build = checked(command, env=env)
            (folder / (worker_binary.name + "-build.log")).write_text(build.stdout + build.stderr)
            for mode in (("ce", "faithful") if public else ("private",)):
                for case in worker_cases:
                    run = subprocess.run([str(worker_binary), case, "ce" if mode == "ce" else "faithful"], env=env,
                                         capture_output=True, text=True, timeout=8)
                    (folder / (mode + "-" + case + ".log")).write_text(run.stdout + run.stderr)
                    observed = json.loads(run.stdout) if run.returncode == 0 else {}
                    key = (mode, case)
                    unchanged = not fixed or observed == controls.get(key)
                    if not fixed:
                        controls[key] = observed
                    passed = run.returncode == 0 and observed.get("checks", 0) > 0 and unchanged
                    receipt["cases"].append({"variant": folder.name, "case": mode + ":" + case,
                                             "exit": run.returncode, "observed": observed,
                                             "unchanged": unchanged, "passed": passed, "stderr": run.stderr})
                    if not passed:
                        print(folder.name, mode, case, "FAIL", flush=True)
        print(folder.name, "worker controls complete", flush=True)
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if all(case["passed"] for case in receipt["cases"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
