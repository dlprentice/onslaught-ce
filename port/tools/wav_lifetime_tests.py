#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Public WAV-reader lifetime checks with the real reader, Linux MMIO and path implementations.

Needs the generated port headers and pinned compiler. No game, audio device, display, retail inputs or network.
Fixtures are authored RIFF bytes under a private HOME. Tracking wrappers observe real MMIO opens/closes; invalid
handles are counted and refused so a pre-fix negative control cannot crash. Valid handles always reach real MMIO.
--reader-source permits a frozen earlier public reader; no byte-matching source is changed or used as the fix.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def chunk(tag, data):
    return tag + struct.pack("<I", len(data)) + data + bytes(len(data) & 1)


def wave(parts):
    data = b"WAVE" + b"".join(parts)
    return b"RIFF" + struct.pack("<I", len(data)) + data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mirror", type=Path, default=ROOT / "local-data/port/mirror/src")
    parser.add_argument("--reader-source", type=Path, default=ROOT / "port/platform/public/wavread.cpp")
    args = parser.parse_args()
    if not args.reader_source.is_file() or not (args.mirror / "common.h").is_file():
        parser.error("the public reader and generated port headers are required")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    build.check_compiler("linux")
    parent = ROOT / "local-data/port/wav-lifetime-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    mirror = out / "src"
    mirror.mkdir()
    pins = {}
    for path in sorted(args.mirror.rglob("*")):
        if path.is_file() and path.suffix.lower() in {".h", ".hpp", ".inl"}:
            relative = path.relative_to(args.mirror)
            destination = mirror / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(path.read_bytes())
            pins[str(relative)] = digest(destination)
    shutil.copyfile(ROOT / "port/platform/public/WavRead.h", mirror / "WavRead.h")
    pins["WavRead.h"] = digest(mirror / "WavRead.h")
    reader = mirror / "wavread.cpp"
    shutil.copyfile(args.reader_source, reader)
    driver = out / "wav_lifetime_driver.cpp"
    shutil.copyfile(ROOT / "port/tools/tests/wav_lifetime_driver.cpp", driver)
    sources = [driver, ROOT / "port/platform/linux/mmio.cpp", ROOT / "port/platform/linux/paths.cpp"]
    target = build.TARGETS["linux"]
    includes = [build.PORT / "d3dx/include", build.PORT / "include", *target["includes"],
                build.PORT / "platform/linux", mirror, mirror / "MissionScript"]
    flags = [*build.COMMON, "-ffunction-sections", "-fdata-sections", *["-I" + str(p) for p in includes]]
    receipt = {"reader_sha256": digest(reader), "driver_sha256": digest(driver), "header_sha256": pins,
               "sources": {}, "commands": [], "cases": []}
    env = dict(os.environ, TMPDIR=str(out))
    objects = []
    with (out / "build.log").open("w") as log:
        for i, source in enumerate(sources):
            if i:
                copied = out / source.name
                shutil.copyfile(source, copied)
                source = copied
            receipt["sources"][source.name] = digest(source)
            obj = out / f"{i}.o"
            extra = [*build.MSCXX, *build.X87, *build.NO_MATH_BUILTINS,
                     "-include", str(build.PORT / "include/port.h"),
                     '-DWAV_READER_SOURCE="' + str(reader) + '"'] if not i else []
            command = [*target["cxx"], "-std=gnu++14", *flags, *extra, "-c", str(source), "-o", str(obj)]
            receipt["commands"].append(command)
            result = subprocess.run(command, stdout=log, stderr=log, env=env, timeout=120)
            if result.returncode:
                print(f"Build failed: {out / 'build.log'}")
                return 1
            objects.append(obj)
        binary = out / "wav-lifetime"
        command = [*target["cxx"], *build.deps.sysroot_link_flags(), *map(str, objects), "-Wl,--gc-sections",
                   *["-Wl,--wrap=" + name for name in ("mmioOpenA", "mmioClose", "mmioSeek", "mmioGetInfo")],
                   "-o", str(binary)]
        receipt["commands"].append(command)
        result = subprocess.run(command, stdout=log, stderr=log, env=env, timeout=120)
        if result.returncode:
            print(f"Link failed: {out / 'build.log'}")
            return 1
    receipt["binary_sha256"] = digest(binary)
    game, user, home = out / "game", out / "user", out / "home"
    for path in (game, user, home):
        path.mkdir()
    first, second = bytes(range(256)) * 40, bytes(reversed(range(63)))
    pcm = struct.pack("<HHIIHH", 1, 1, 22050, 22050, 1, 8)
    other = struct.pack("<HHIIHH", 2, 1, 22050, 22050, 1, 8)
    fixtures = {"first.wav": wave([chunk(b"fmt ", pcm), chunk(b"data", first)]),
                "second.wav": wave([chunk(b"fmt ", pcm), chunk(b"data", second)]),
                "extra.wav": wave([chunk(b"fmt ", other + struct.pack("<H", 3) + b"abc"), chunk(b"data", second)]),
                "no-data.wav": wave([chunk(b"fmt ", pcm)]),
                "bad.wav": b"not a RIFF file",
                "short-extra.wav": wave([chunk(b"fmt ", other + struct.pack("<H", 4))])}
    for name, data in fixtures.items():
        (game / name).write_bytes(data)
    expected_a, expected_b = out / "first.samples", out / "second.samples"
    expected_a.write_bytes(first)
    expected_b.write_bytes(second)
    fixture_pins = {name: digest(game / name) for name in fixtures}
    env.update(HOME=str(home), XDG_DATA_HOME=str(home / "data"), XDG_CONFIG_HOME=str(home / "config"),
               XDG_CACHE_HOME=str(home / "cache"), BEA_GAME_DIR=str(game), BEA_USER_DIR=str(user))
    cases = ("unopened", "unopened-close-read", "missing", "bad-format", "missing-data", "truncated-extra",
             "destructor-open", "repeat-close", "reopen-valid", "reopen-missing", "reopen-no-data",
             "recover-after-failure", "non-pcm")
    for case in cases:
        proc = subprocess.run([str(binary), case, str(expected_a), str(expected_b)], env=env, cwd=out,
                              capture_output=True, text=True, timeout=20)
        try:
            actual = json.loads(proc.stdout)
        except json.JSONDecodeError:
            actual = {}
        passed = proc.returncode == 0 and actual.get("checks", 0) > 0 and actual.get("failures") == 0
        receipt["cases"].append({"case": case, "exit": proc.returncode, "actual": actual,
                                 "pass": passed, "stderr": proc.stderr})
        print(f"{'PASS' if passed else 'FAIL'} {case}")
    receipt["fixtures_unchanged"] = all(digest(game / n) == value for n, value in fixture_pins.items())
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if receipt["fixtures_unchanged"] and all(c["pass"] for c in receipt["cases"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
