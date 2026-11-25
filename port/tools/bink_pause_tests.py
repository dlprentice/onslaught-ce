#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""port/bink's pause (PortBinkPause) and video clock on the host, with mutants of the production source.

port/bink/bink.cpp is compiled whole into port/tools/tests/bink_pause_driver.cpp (the host's own FFmpeg headers and
libraries satisfy its decoder; nothing is opened or decoded) and driven with a fake DirectSound buffer that ends as
port/dsound's does and a fake clock. Cases: a pause mid-film, a pause after the sound has ended (the Museum's
held-then-replayed fault: the ended buffer's cursor is 0, so the old code held the picture at 0 and played the sound
again on resume), a silent film, and repeated calls. Each mutant of bink.cpp below must make a case fail.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/bink/bink.cpp"
DRIVER = ROOT / "port/tools/tests/bink_pause_driver.cpp"

MUTANTS = [
    # the old behaviour, piece by piece
    ("resume-plays-an-ended-sound", "\t\tif (v->pausedPlaying)\n\t\t\tv->sound->Play(0, 0, 0);", "\t\tif (v->sound)\n\t\t\tv->sound->Play(0, 0, 0);"),
    ("held-clock-reads-the-ended-cursor", "(v->paused && v->pausedPlaying)", "v->paused"),
    ("pause-stops-nothing", "\t\tif (v->pausedPlaying)\n\t\t\tv->sound->Stop();", "\t\tif (false)\n\t\t\tv->sound->Stop();"),
    ("resume-plays-nothing", "\t\tif (v->pausedPlaying)\n\t\t\tv->sound->Play(0, 0, 0);", "\t\tif (false)\n\t\t\tv->sound->Play(0, 0, 0);"),
    ("held-clock-runs-on", "\tif (v->paused)\n\t{\n\t\tLARGE_INTEGER f;", "\tif (false)\n\t{\n\t\tLARGE_INTEGER f;"),
    ("resume-forgets-the-hold", "\t\tv->start.QuadPart += now.QuadPart - v->pausedAt.QuadPart;", ""),
    ("pause-not-remembered", "\t\tv->pausedPlaying = (status & DSBSTATUS_PLAYING) != 0;", "\t\tv->pausedPlaying = false;"),
    ("pause-repeats", "if (!v || !v->started || (pause != 0) == v->paused)", "if (!v || !v->started)"),
    ("wait-ignores-the-clock", "\treturn Clock(v) < (waiting - 1) / v->fps ? 1 : 0;", "\treturn 0;"),
]


def run(command, expect=0, cwd=ROOT):
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=0", UBSAN_OPTIONS="halt_on_error=1")
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True, timeout=120)
    return result.returncode == expect, result.stdout + result.stderr


def main():
    failures = []

    def check(name, passed, detail=""):
        print(("ok   " if passed else "FAIL ") + name + ("" if passed else ("\n" + detail if detail else "")))
        if not passed:
            failures.append(name)

    common = Path(subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                 capture_output=True, text=True, check=True).stdout.strip()).parent
    candidates = []
    if os.environ.get("BEA_PORT_DATA"):
        candidates.append(Path(os.environ["BEA_PORT_DATA"]) / "dxvk-native/usr/include/dxvk")
    candidates.extend([
        common / "local-data/port/dxvk-native/usr/include/dxvk",
        common / "local-data/port-deps-canonical/dxvk-native/usr/include/dxvk",
    ])
    dxvk = next((p for p in candidates if p.is_dir()), candidates[0])
    flags = ["clang++", "-std=c++17", "-O1", "-g", "-w", "-pthread",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
             "-DNOMINMAX", "-DINITGUID", "-fms-extensions",
             "-I" + str(ROOT / "port/d3dx/include"), "-I" + str(ROOT / "port/include"), "-I" + str(ROOT / "port/include/linux"),
             "-I" + str(dxvk), "-I" + str(ROOT / "port/bink"), "-include", str(ROOT / "port/platform/common/portlog.h")]
    libs = ["-lavformat", "-lavcodec", "-lavutil"]
    check("the dxvk-native headers are present", dxvk.is_dir(), str(dxvk))
    out = ROOT / "local-data" / "bink-pause-tests"
    out.mkdir(parents=True, exist_ok=True)
    cases = ("mid-film", "after-end", "no-sound", "twice")
    hardening_cases = ("loop-restart", "zero-frames", "copy-bounds", "make-sound-safety", "dsound-thread-safety")
    with tempfile.TemporaryDirectory(dir=out) as folder:
        folder = Path(folder)

        def build(source, binary):
            return run(flags + ['-DBINK_SOURCE="' + str(source) + '"', str(DRIVER)] + libs + ["-o", str(binary)])

        good, detail = build(SOURCE, folder / "driver")
        check("bink.cpp and the driver build on the host", good, detail)
        if good:
            for case in cases + hardening_cases:
                passed, detail = run([str(folder / "driver"), case])
                check("case " + case + " holds on bink.cpp", passed, detail)
        text = SOURCE.read_text()
        for label, old, new in MUTANTS:
            if text.count(old) != 1:
                check("mutant " + label + " applies", False, "source text not found once: " + old)
                continue
            source = folder / (label + ".cpp")
            source.write_text(text.replace(old, new))
            built, detail = build(source, folder / label)
            if not built:
                check("mutant " + label + " is caught", False, detail)
                continue
            caught = [case for case in cases if run([str(folder / label), case], 2)[0]]
            check("mutant " + label + " is caught (" + (", ".join(caught) or "by no case") + ")", bool(caught))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
