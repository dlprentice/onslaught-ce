#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the isolated CE ICE/ENet fixture from verified source archives.

No download, service access, credential read or test run is implicit. Build output
must be a new directory below this checkout's ignored local-data directory.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
JUICE_COMMIT = "b89c792e3612faf2f12cf35bcc56857313a06be3"
JUICE_URL = "https://codeload.github.com/paullouisageneau/libjuice/tar.gz/" + JUICE_COMMIT
JUICE_SHA256 = "713b22174e8cb2e06538fa9bac2650bda21b83c86f5128813ab4dcf8f5bea9ed"
JUICE_LICENSE = "MPL-2.0"
JUICE_PATCH = ROOT / "port/network/libjuice-relay-only.patch"
JUICE_PATCH_SHA256 = "21d2dbecacfff42e61fc16589e26795335600354ca82c45199ba8f26235341e2"
ENET_COMMIT = "5a9c537fd464b3c6d3c55e1d3bd47588faf71b42"
ENET_URL = "https://codeload.github.com/lsalzman/enet/tar.gz/" + ENET_COMMIT
ENET_SHA256 = "537d42f31b0c0d27f5a9635f6c224f2451c1c1484bfde50e94c884e96181c290"
ENET_LICENSE = "MIT"


def unpack(archive: Path, digest: str, output: Path) -> None:
    if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise SystemExit("dependency archive identity mismatch")
    with tarfile.open(archive, "r:gz") as stream:
        stream.extractall(output, filter="data")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--juice-archive", type=Path, required=True)
    parser.add_argument("--enet-archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target", choices=("native", "linux32", "windows32"), default="native")
    parser.add_argument("--toolchain-data", type=Path)
    parser.add_argument("--run", action="store_true", help="run only the authored loopback fixture")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / "local-data").resolve()) or output.exists():
        parser.error("output must be a new directory below this checkout's local-data")
    if args.target != "native" and not args.toolchain_data:
        parser.error("cross builds require the existing pinned toolchain data directory")
    if args.run and args.target == "windows32":
        parser.error("Windows execution is a separate acceptance step")
    output.mkdir(parents=True)
    unpack(args.juice_archive, JUICE_SHA256, output)
    unpack(args.enet_archive, ENET_SHA256, output)
    juice = output / ("libjuice-" + JUICE_COMMIT)
    enet = output / ("enet-" + ENET_COMMIT)
    if hashlib.sha256(JUICE_PATCH.read_bytes()).hexdigest() != JUICE_PATCH_SHA256:
        raise SystemExit("relay policy patch identity mismatch")
    subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(JUICE_PATCH)],
                   cwd=juice, check=True, capture_output=True, timeout=15)
    data = args.toolchain_data.resolve() if args.toolchain_data else None
    if args.target == "windows32":
        cc = str(data / "llvm-mingw/bin/i686-w64-mingw32-clang")
        cxx = str(data / "llvm-mingw/bin/i686-w64-mingw32-clang++")
        target_flags = []
        libraries = ["-lws2_32", "-lwinmm", "-lbcrypt"]
        enet_flags = []
    else:
        cc = str(data / "llvm/bin/clang") if data else shutil.which("cc")
        cxx = str(data / "llvm/bin/clang++") if data else shutil.which("c++")
        target_flags = ["-m32", "--sysroot=" + str(data / "sysroot-sniper")] if args.target == "linux32" else []
        libraries = ["-pthread"]
        enet_flags = ["-DHAS_" + flag + "=1" for flag in (
            "FCNTL", "POLL", "GETADDRINFO", "GETNAMEINFO", "INET_PTON", "INET_NTOP",
            "MSGHDR_FLAGS", "SOCKLEN_T")]
    if not cc or not cxx:
        parser.error("C and C++ compilers are required")
    common = target_flags + ["-O2", "-pthread", "-DCE_NETWORK_ICE=1", "-DJUICE_STATIC", "-DUSE_NETTLE=0",
                             "-I" + str(juice / "include"), "-I" + str(enet / "include")]
    objects = []
    commands = [[cxx, *target_flags, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-c", str(ROOT / "port/network/ice_bridge.cpp"),
                 "-o", str(output / "ice_bridge_disabled.o")]]
    for source in sorted((juice / "src").glob("*.c")):
        obj = output / ("juice-" + source.stem + ".o")
        commands.append([cc, *common, "-std=gnu11", "-I" + str(juice / "include/juice"),
                         "-c", str(source), "-o", str(obj)])
        objects.append(obj)
    for source in sorted(enet.glob("*.c")):
        obj = output / ("enet-" + source.stem + ".o")
        commands.append([cc, *common, *enet_flags, "-std=gnu11", "-c", str(source), "-o", str(obj)])
        objects.append(obj)
    control = output / "ice_relay_policy_control.o"
    commands.append([cc, *common, "-std=gnu11", "-I" + str(juice / "src"),
                     "-I" + str(juice / "include/juice"), "-c",
                     str(ROOT / "port/network/tests/ice_relay_policy_control.c"), "-o", str(control)])
    objects.append(control)
    binary = output / ("ice_bridge_tests.exe" if args.target == "windows32" else "ice_bridge_tests")
    commands.append([cxx, *common, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                     str(ROOT / "port/network/ice_bridge.cpp"),
                     str(ROOT / "port/network/tests/ice_bridge_tests.cpp"),
                     *map(str, objects), *libraries, "-Wl,--wrap=enet_socket_receive,--wrap=conn_send",
                     "-o", str(binary)])
    with (output / "build.log").open("w") as log:
        for command in commands:
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=90)
    record = {"schema": 1, "target": args.target, "upstream_archive_sha256": JUICE_SHA256,
              "patch_sha256": JUICE_PATCH_SHA256, "patch_license": "MPL-2.0",
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
              "patched_files": {p: hashlib.sha256((juice / p).read_bytes()).hexdigest()
                                for p in ("include/juice/juice.h", "src/agent.h", "src/agent.c", "src/juice.c")}}
    (output / "build-receipt.json").write_text(json.dumps(record, indent=2) + "\n")
    print("Built", args.target, "fixture:", binary)
    if args.run:
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
