#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The startup's second music initialisation leaves one stream (patch 0113), on the production music worker.

Saved options that differ from the defaults in a sound setting make WinMain's options load re-initialise the sound
and start the front end's track before the window exists; CSystem::Init then initialises the music again. Before
patch 0113 that second InitMusicStream replaced the buffer, events, worker and reader and left the first stream
looping, with its worker refilling the new buffer: the session log's outstanding_music_starts=2.

src/PCMusicStream.cpp gets every port patch's PCMusicStream.cpp section in number order ("candidate") and the same
without patch 0113 ("predecessor", the frozen negative control), and compiles whole inside a driver that gives each
stream its own buffer, position events, worker and reader; the real session log (portlog.cpp) is linked in. Modes:
ordinary CE (the public build, session log on), faithful (a tape set: the CE layer and log off), the virtual clock
(no worker: the main thread pumps) and the private build. On a tree without patch 0113 the candidate is the
predecessor and fails. This is a host scheduling and ownership check, not a Windows runtime, audio-quality or
game-run claim: the run-level check is port/tools/run_startup_music_tests.py.
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
PATCH = "0113-music-stream-second-init.patch"
MODES = {"public": ("ce", "faithful", "virtual"), "private": ("private",)}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}", start) + 2] + "\n"


def music_source(folder, skip):
    """src/PCMusicStream.cpp with the PCMusicStream.cpp sections of every port patch but `skip`, in number order."""
    folder.mkdir()
    (folder / "PCMusicStream.cpp").write_bytes((ROOT / "src/PCMusicStream.cpp").read_bytes())
    applied = {}
    for patch in sorted((ROOT / "port/patches").glob("*.patch")):
        if patch.name == skip:
            continue
        sections = [s for s in re.split(r"(?=^--- a/)", patch.read_text(), flags=re.M)
                    if s.startswith("--- a/PCMusicStream.cpp\n")]
        if sections:
            result = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1"], cwd=folder, input="".join(sections),
                                    capture_output=True, text=True, timeout=10)
            if result.returncode:
                raise SystemExit(f"{patch.name} does not apply: {result.stdout}{result.stderr}")
            applied[patch.name] = sha(patch)
    code = re.sub(r"^#include[^\n]*\n", "", (folder / "PCMusicStream.cpp").read_text(), flags=re.M)
    (folder / "music.inc").write_text(code)
    return folder / "music.inc", applied


def session_lines(stderr):
    return [line for line in stderr.splitlines() if " session pid=" in line]


def judge(variant, mode, observed, stderr):
    """The expected outcome of one run, as (passed, reasons)."""
    reasons = []

    def want(condition, text):
        if not condition:
            reasons.append(text)

    sessions = session_lines(stderr)
    starts = [int(n) for n in re.findall(r"outstanding_music_starts=(\d+)", "\n".join(sessions))]
    threaded = mode != "virtual"
    want(observed.get("checks", 0) > 0 and observed.get("buffers") == 2, "the scenario did not run to the end")
    want(observed.get("workers") == (2 if threaded else 0), "one worker per stream (none on the virtual clock)")
    want(observed.get("second_plays") == 1 and observed.get("second_playing_at_front_end"), "the front end's start")
    want(observed.get("readers_created") == 2, "one reader per stream")
    if variant == "candidate":
        want(observed.get("reader_deletes") == 2, "both readers deleted by exit")
        want(not observed.get("first_playing_after_second_init"), "the first stream still plays after the second init")
        want(observed.get("first_released_at_second_init") == 1, "the first buffer is released at the second init")
        want(not observed.get("first_worker_alive_at_second_init"), "the first worker outlives the second init")
        want(observed.get("max_playing") == 1, "more than one music buffer played at once")
        want(observed.get("playing_at_front_end") == 1, "the front end plays more than one music buffer")
        want(observed.get("foreign_locks") == 0 and observed.get("foreign_reads") == 0,
             "a worker touched another stream's buffer or reader")
        want(observed.get("locks_reinitialized") == 0, "a live stream lock was initialised again")
        want(observed.get("locks_initialized") == observed.get("locks_destroyed"), "stream locks unbalanced")
        want(observed.get("first_releases") == 1 and observed.get("second_releases") == 1, "each buffer released once")
        want(observed.get("buffer_released_live_worker") == 0, "a buffer was released while its worker still ran")
        want(observed.get("reader_deleted_live_worker") == 0, "a reader was deleted while its worker still ran")
        want(observed.get("closed_event_uses") == 0, "an event was signalled or reset after it was closed")
        want(observed.get("events_open") == 0, "an event was left open")
    else:
        want(observed.get("first_playing_after_second_init") and observed.get("max_playing") == 2,
             "negative control: the predecessor no longer leaves the first stream playing")
        want(observed.get("playing_at_front_end") == 2, "negative control: two music buffers at the front end")
        want(observed.get("first_releases") == 0 and observed.get("reader_deletes") == 1,
             "negative control: the first buffer and reader are no longer leaked")
        if threaded:
            want(observed.get("foreign_locks", 0) > 0 and observed.get("foreign_reads", 0) > 0,
                 "negative control: the orphaned worker no longer refills the new stream")
    if mode == "ce":
        want(bool(starts), "the session log has no music start lifetimes")
        want(max(starts or [0]) == (1 if variant == "candidate" else 2),
             f"outstanding_music_starts reached {max(starts or [0])}")
        replaced = sum("stream re-initialised: the earlier stream is shut down first" in line for line in sessions)
        want(replaced == (1 if variant == "candidate" else 0), f"{replaced} re-initialisation lines")
        want(sum("music: buffer start" in line for line in sessions) == 2, "two buffer starts: startup and front end")
    else:
        want(not sessions, "a faithful, virtual-clock or private run wrote session lines")
    return not reasons, reasons


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/music-stream-reinit-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    driver = ROOT / "port/tools/tests/music_stream_reinit_driver.cpp"
    portlog = ROOT / "port/platform/common/portlog.cpp"
    header = ROOT / "port/platform/common/portlog.h"
    ce = ROOT / "port/platform/common/ce.cpp"
    (out / "SDL3").mkdir()
    (out / "SDL3/SDL.h").write_text("#include <cstdint>\nuint64_t SDL_GetTicks();\n")
    clock = out / "log-clock.cpp"
    clock.write_text("#include <cstdint>\nuint64_t SDL_GetTicks() { return 0; }\n")
    admission = out / "ce-admission.inc"
    admission.write_text(function(ce.read_text(), "bool PortCeLayer()\n{"))
    receipt = {"scope": __doc__, "pins": {str(p.relative_to(ROOT)): sha(p) for p in
                                          (ROOT / "src/PCMusicStream.cpp", driver, portlog, header, ce, Path(__file__))},
               "patch_present": (ROOT / "port/patches" / PATCH).is_file(), "variants": {}, "commands": [], "cases": [],
               "compiler": subprocess.run([args.compiler, "--version"], capture_output=True, text=True,
                                          check=True).stdout.splitlines()[0]}
    env = {k: v for k, v in os.environ.items()
           if k not in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_CE_LAYER")}
    env["TMPDIR"] = str(out)
    for variant, skip in (("candidate", None), ("predecessor", PATCH)):
        source, applied = music_source(out / variant, skip)
        receipt["variants"][variant] = {"patches": applied, "source_sha256": sha(source)}
        for build, modes in MODES.items():
            binary = out / f"{variant}-{build}"
            command = [args.compiler, "-std=c++17", "-pthread", "-O0", "-g", "-Wall", "-Wno-unused-function",
                       *(['-DCE_VERSION="test"'] if build == "public" else []), "-I" + str(out),
                       "-include", str(header), f'-DMUSIC_SOURCE="{source}"', f'-DCE_ADMISSION_SOURCE="{admission}"',
                       str(driver), str(portlog), str(clock), "-o", str(binary)]
            receipt["commands"].append(command)
            built = subprocess.run(command, capture_output=True, text=True, timeout=120, env=env)
            (out / f"{variant}-{build}-build.log").write_text(built.stdout + built.stderr)
            if built.returncode:
                raise SystemExit(f"build failed: {out / (variant + '-' + build + '-build.log')}")
            for mode in modes:
                proc = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=60, env=env)
                try:
                    observed = json.loads(proc.stdout)
                except json.JSONDecodeError:
                    observed = {}
                passed, reasons = judge(variant, mode, observed, proc.stderr)
                passed = passed and proc.returncode == 0
                receipt["cases"].append(dict(variant=variant, build=build, mode=mode, exit=proc.returncode,
                                             observed=observed, passed=passed, reasons=reasons,
                                             session=session_lines(proc.stderr),
                                             stderr_other=[l for l in proc.stderr.splitlines() if " session pid=" not in l]))
                starts = re.findall(r"outstanding_music_starts=(\d+)", proc.stderr)
                print(f"{'ok  ' if passed else 'FAIL'} {variant:11} {mode:8} exit {proc.returncode}; buffers playing at "
                      f"once {observed.get('max_playing')}, at the front end {observed.get('playing_at_front_end')}; "
                      f"foreign locks/reads {observed.get('foreign_locks')}/{observed.get('foreign_reads')}"
                      + (f"; outstanding_music_starts max {max(map(int, starts))}" if starts else "")
                      + ("" if passed else f" -- {'; '.join(reasons) or 'exit ' + str(proc.returncode)}"), flush=True)
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    total = len(receipt["cases"])
    good = sum(case["passed"] for case in receipt["cases"])
    print(f"{good}/{total} expected outcomes" + ("" if receipt["patch_present"] else f" ({PATCH} is not in this tree)"))
    return 0 if good == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
