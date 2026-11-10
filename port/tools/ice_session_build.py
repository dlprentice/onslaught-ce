#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build optional ICE and unchanged direct CE session fixtures from pinned archives.

No download, service access or credential read occurs. Output must be new and
inside ignored local-data. --run requires the caller's isolated network namespace.
Windows builds compile/link only; native Windows execution is a separate gate.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

import ice_bridge_build as bridge

ROOT = Path(__file__).resolve().parents[2]
MONOCYPHER_SHA256 = "38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c"
MONOCYPHER_URL = "https://monocypher.org/download/monocypher-4.0.2.tar.gz"
MONOCYPHER_LICENSE = "BSD-2-Clause OR CC0-1.0"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--juice-archive", type=Path, required=True)
    parser.add_argument("--enet-archive", type=Path, required=True)
    parser.add_argument("--monocypher-archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target", choices=("native", "linux32", "windows32"), default="native")
    parser.add_argument("--toolchain-data", type=Path)
    parser.add_argument("--run", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to((ROOT / "local-data").resolve()):
        parser.error("output must be a new directory below this checkout's local-data")
    if args.target != "native" and not args.toolchain_data:
        parser.error("cross builds require the existing pinned toolchain data directory")
    if args.run and args.target == "windows32":
        parser.error("Windows execution is a separate acceptance step")
    output.mkdir(parents=True)
    command = [sys.executable, str(ROOT / "port/tools/ice_bridge_build.py"),
               "--juice-archive", str(args.juice_archive.resolve()),
               "--enet-archive", str(args.enet_archive.resolve()),
               "--output", str(output / "bridge"), "--target", args.target]
    if args.toolchain_data:
        command += ["--toolchain-data", str(args.toolchain_data.resolve())]
    subprocess.run(command, check=True, timeout=180)
    bridge.unpack(args.monocypher_archive, MONOCYPHER_SHA256, output)
    mc = output / "monocypher-4.0.2/src"
    enet = output / "bridge" / ("enet-" + bridge.ENET_COMMIT)
    juice = output / "bridge" / ("libjuice-" + bridge.JUICE_COMMIT)
    data = args.toolchain_data.resolve() if args.toolchain_data else None
    windows = args.target == "windows32"
    if windows:
        cc = str(data / "llvm-mingw/bin/i686-w64-mingw32-clang")
        cxx = str(data / "llvm-mingw/bin/i686-w64-mingw32-clang++")
        flags, libraries = [], ["-lws2_32", "-lwinmm", "-lbcrypt"]
    else:
        cc = str(data / "llvm/bin/clang") if data else shutil.which("cc")
        cxx = str(data / "llvm/bin/clang++") if data else shutil.which("c++")
        flags = ["-m32", "--sysroot=" + str(data / "sysroot-sniper")] if args.target == "linux32" else []
        libraries = ["-pthread"]
    if not cc or not cxx:
        parser.error("C and C++ compilers are required")
    common = [*flags, "-O2", "-pthread", "-I" + str(enet / "include"), "-I" + str(mc)]
    enet_objects = sorted((output / "bridge").glob("enet-*.o"))
    juice_objects = sorted((output / "bridge").glob("juice-*.o"))
    mc_object = output / "monocypher.o"
    network = ROOT / "port/network"
    binaries = []
    with (output / "build.log").open("w") as log:
        def run(command: list[str | Path]) -> None:
            subprocess.run(list(map(str, command)), check=True, stdout=log,
                           stderr=subprocess.STDOUT, timeout=90)
        run([cc, *common, "-std=gnu11", "-c", mc / "monocypher.c", "-o", mc_object])
        for enabled in (False, True):
            mode = "ice" if enabled else "direct"
            optional = ["-DCE_NETWORK_ICE=1", "-DJUICE_STATIC", "-I" + str(juice / "include")] if enabled else []
            cxx_flags = [*common, *optional, "-std=c++17", "-Wall", "-Wextra", "-Werror"]
            objects = [*enet_objects, mc_object]
            sources = ["codec.cpp", "transport.cpp"] + (["ice_bridge.cpp"] if enabled else [])
            for source in sources:
                obj = output / (mode + "-" + Path(source).stem + ".o")
                run([cxx, *cxx_flags, "-c", network / source, "-o", obj])
                objects.append(obj)
            if enabled:
                objects += juice_objects
            suffix = ".exe" if windows else ""
            binary = output / (mode + "-session-tests" + suffix)
            run([cxx, *cxx_flags, network / "tests/ice_session_tests.cpp", *objects, *libraries, "-o", binary])
            binaries.append(binary)
            protocol = output / (mode + "-protocol-tests" + suffix)
            # The existing protocol fixture includes transport.cpp for its internal controls.
            without_transport = [p for p in objects if p.name != mode + "-transport.o"]
            run([cxx, *cxx_flags, network / "tests/protocol_tests.cpp", *without_transport,
                 *libraries, "-o", protocol])
            binaries.append(protocol)
    print("Built", args.target, "CE session and protocol fixtures:", output)
    if args.run:
        for binary in binaries:
            subprocess.run([str(binary)], check=True, timeout=40)


if __name__ == "__main__":
    main()
