#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Packed-reader regressions using the actual generated DXMemBuffer.cpp and real zlib.

Run after building the port mirror, or select an existing mirror with --mirror. This snapshots the reader and
headers into ignored output before compiling with the pinned i686 compiler and normal game flags. Synthetic
fixtures exercise the decoder through fake file/error boundaries; no game, retail data, display or network is used.
--reader-source selects a frozen generated reader for a negative control (its include names must be normalised).
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
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def block(data):
    packed = zlib.compress(data)
    return struct.pack("<I", len(packed)) + packed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mirror", type=Path, default=ROOT / "local-data/port/mirror/src")
    parser.add_argument("--reader-source", type=Path)
    args = parser.parse_args()
    source = args.reader_source or args.mirror / "DXMemBuffer.cpp"
    if not source.is_file() or not (args.mirror / "common.h").is_file():
        parser.error("a generated port mirror and DXMemBuffer.cpp are required; no build is started automatically")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    build.check_compiler("linux")
    parent = ROOT / "local-data/port/packed-buffer-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    mirror = out / "src"
    mirror.mkdir()
    # Snapshot only compilation inputs, never the mirror's retail data or other translation units.
    pins = {}
    for path in sorted(args.mirror.rglob("*")):
        if path.is_file() and path.suffix.lower() in {".h", ".hpp", ".inl"}:
            relative = path.relative_to(args.mirror)
            target = mirror / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            data = path.read_bytes()
            target.write_bytes(data)
            pins[str(relative)] = hashlib.sha256(data).hexdigest()
    reader = mirror / "DXMemBuffer.cpp"
    reader.write_bytes(source.read_bytes())
    driver_source = ROOT / "port/tools/tests/packed_buffer_driver.cpp"
    driver_copy = out / driver_source.name
    shutil.copyfile(driver_source, driver_copy)
    target = build.TARGETS["linux"]
    includes = [build.PORT / "d3dx/include", build.PORT / "include", *target["includes"],
                mirror, mirror / "MissionScript"]
    binary = out / "packed-reader"
    command = [*target["cxx"], "-std=gnu++14", *build.MSCXX, *build.COMMON, *build.X87,
               *build.NO_MATH_BUILTINS, "-ffunction-sections", "-fdata-sections",
               "-include", str(build.PORT / "include/port.h"), *["-I" + str(p) for p in includes],
               '-DPACKED_READER_SOURCE="' + str(reader) + '"', str(driver_copy),
               *build.deps.sysroot_link_flags(), "-Wl,--gc-sections", "-Wl,--wrap=uncompress", "-lz",
               "-o", str(binary)]
    receipt = {"source": str(source.resolve()), "source_sha256": sha(reader), "header_sha256": pins,
               "driver_sha256": sha(driver_copy), "compile": command, "cases": []}
    with (out / "build.log").open("w") as log:
        result = subprocess.run(command, stdout=log, stderr=log, timeout=120,
                                env=dict(os.environ, TMPDIR=str(out)))
    receipt["compile_exit"] = result.returncode
    if result.returncode:
        (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(f"Build failed: {out / 'build.log'}")
        return 1
    receipt["binary_sha256"] = sha(binary)
    mib = 1024 * 1024
    pattern = (bytes(range(256)) * (mib // 256))
    cases = []

    def case(name, raw, expected, capacity, assertions, mode="packed", next_capacity=0, seek=0):
        cases.append((name, raw, expected, capacity, assertions, mode, next_capacity, seek))

    case("exact-capacity", block(pattern), pattern, mib, {"ok": True, "read": mib, "capacities": [mib]})
    case("two-chunks", block(pattern[:100]) + block(pattern[100:300]), pattern[:300], 300,
         {"ok": True, "read": 300, "capacities": [300, 200]})
    case("larger-object-five-chunks", block(pattern) * 5, pattern * 5, 5 * mib,
         {"ok": True, "read": 5 * mib, "capacities": [5 * mib, 4 * mib, 3 * mib, 2 * mib, mib]})
    case("final-short-chunk", block(pattern[:97]), pattern[:97], mib,
         {"ok": True, "read": 97, "capacities": [mib]})
    case("short-destination", block(pattern[:33]), pattern[:32], 32,
         {"ok": False, "read": 0, "error": 13, "capacities": [32]})
    case("short-remaining-destination", block(pattern[:16]) + block(pattern[16:33]), pattern[:32], 32,
         {"ok": False, "read": 16, "error": 13, "capacities": [32, 16]})
    case("valid-empty-stream", block(b""), b"", 32,
         {"ok": True, "read": 0, "capacities": [32]})
    case("plain-bound", pattern[:33], pattern[:32], 32,
         {"ok": True, "read": 32, "position": 32, "max_request": 32, "capacities": []}, mode="plain")
    for packed in (0, 0x102928):
        case(f"invalid-packed-{packed}", struct.pack("<I", packed), b"", 32,
             {"ok": False, "read": 0, "read_calls": 1, "max_request": 4, "error": 13, "capacities": []})
        case(f"seek-invalid-{packed}", struct.pack("<I", packed), b"", 32,
             {"seek": 0, "read_calls": 1, "max_request": 4, "error": 13, "capacities": []},
             mode="seek", seek=mib)
    case("corrupt-compressed-data", struct.pack("<I", 4) + b"oops", b"", 32,
         {"ok": False, "read": 0, "error": 13, "capacities": [32]})
    case("maximum-packed-size-admitted", struct.pack("<I", 0x102927) + bytes(0x102927), b"", 32,
         {"ok": False, "read": 0, "read_calls": 2, "max_request": 0x102927,
          "error": 13, "capacities": [32]})
    case("zero-output-capacity", block(b"x"), b"", 0,
         {"ok": False, "read": 0, "read_calls": 1, "error": 13, "capacities": []})
    case("load-small-object-large-next", block(pattern), pattern, mib,
         {"ok": True, "read": mib, "last": False, "fatal": 0, "capacities": [mib]},
         mode="load", next_capacity=5 * mib)
    case("load-large-object-small-next", block(pattern) * 5, pattern * 5, 5 * mib,
         {"ok": True, "read": 5 * mib, "last": False, "fatal": 0,
          "capacities": [5 * mib, 4 * mib, 3 * mib, 2 * mib, mib]}, mode="load", next_capacity=mib)
    case("load-short-final", block(b"final"), b"final", mib,
         {"ok": True, "read": 5, "last": True, "fatal": 0, "capacities": [mib]}, mode="load")
    case("load-invalid-overrides-stale-eof", struct.pack("<I", 0), b"", mib,
         {"ok": False, "read": 0, "error": 13, "fatal": 1, "last": True, "capacities": []}, mode="load")
    # Existing behavior: a later incomplete chunk returns the earlier valid prefix. This is not a new claim that
    # the reader fully validates malformed containers; the regression makes that limitation visible.
    case("existing-truncated-tail-prefix", block(b"prefix") + struct.pack("<I", 8) + b"short", b"prefix", 32,
         {"ok": True, "read": 6, "capacities": [32]})
    for name, raw, expected, capacity, assertions, mode, next_capacity, seek in cases:
        folder = out / name
        folder.mkdir()
        input_path, expected_path = folder / "input", folder / "expected"
        input_path.write_bytes(raw)
        expected_path.write_bytes(expected)
        proc = subprocess.run([str(binary), mode, str(input_path), str(expected_path), str(capacity),
                               str(next_capacity), str(seek)], capture_output=True, text=True, timeout=20)
        try:
            actual = json.loads(proc.stdout) if proc.returncode == 0 else {}
        except json.JSONDecodeError:
            actual = {}
        assertions = {"canaries": True, "payload": True, **assertions}
        passed = proc.returncode == 0 and all(actual.get(k) == v for k, v in assertions.items())
        receipt["cases"].append({"case": name, "exit": proc.returncode, "actual": actual,
                                 "expected": assertions, "pass": passed, "stderr": proc.stderr})
        print(f"{'PASS' if passed else 'FAIL'} {name}")
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if receipt["cases"] and all(c["pass"] for c in receipt["cases"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
