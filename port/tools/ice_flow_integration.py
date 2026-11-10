#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build authored separate-process CE/courier/store integration from pinned inputs.

No download, game launch, external provider or real identity is supported.
--run requires a separately prepared loopback-only network namespace whose
private hosts file maps the fixed TURN hostname to 127.0.0.1. Existing endpoint
policies and production sources are not weakened for this fixture.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

import ice_bridge_build as bridge
import ice_session_build as session

ROOT = Path(__file__).resolve().parents[2]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(args) -> Path:
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to((ROOT / "local-data").resolve()):
        raise ValueError("output must be new and below this checkout's local-data")
    if args.target == "linux32" and not args.toolchain_data:
        raise ValueError("linux32 requires existing pinned toolchain data")
    output.mkdir(mode=0o700, parents=True)
    command = [sys.executable, str(ROOT / "port/tools/ice_bridge_build.py"),
               "--juice-archive", str(args.juice_archive.resolve()),
               "--enet-archive", str(args.enet_archive.resolve()),
               "--output", str(output / "bridge"), "--target", args.target]
    if args.toolchain_data:
        command += ["--toolchain-data", str(args.toolchain_data.resolve())]
    with (output / "build.log").open("w") as log:
        def compile(command):
            subprocess.run(list(map(str, command)), check=True, stdout=log,
                           stderr=subprocess.STDOUT, timeout=180)
        compile(command)
        bridge.unpack(args.monocypher_archive, session.MONOCYPHER_SHA256, output)
        mono = output / "monocypher-4.0.2/src"
        enet = output / "bridge" / ("enet-" + bridge.ENET_COMMIT)
        juice = output / "bridge" / ("libjuice-" + bridge.JUICE_COMMIT)
        data = args.toolchain_data.resolve() if args.toolchain_data else None
        cc = str(data / "llvm/bin/clang") if data else shutil.which("cc")
        cxx = str(data / "llvm/bin/clang++") if data else shutil.which("c++")
        native_cc = shutil.which("cc")
        if not cc or not cxx or not native_cc:
            raise ValueError("C and C++ compilers required")
        flags = ["-m32", "--sysroot=" + str(data / "sysroot-sniper")] if args.target == "linux32" else []
        network = ROOT / "port/network"
        common = [*flags, "-O2", "-pthread", "-DCE_NETWORK_ICE=1", "-DJUICE_STATIC",
                  "-I" + str(juice / "include"), "-I" + str(enet / "include"),
                  "-I" + str(mono), "-I" + str(network)]
        objects = [*sorted((output / "bridge").glob("enet-*.o")),
                   *sorted((output / "bridge").glob("juice-*.o"))]
        mono_object = output / "monocypher.o"
        compile([cc, *common, "-std=gnu11", "-c", mono / "monocypher.c", "-o", mono_object])
        objects.append(mono_object)
        sources = ["codec.cpp", "transport.cpp", "ice_bridge.cpp", "ice_exchange.cpp", "ice_game.cpp"]
        for source in sources:
            obj = output / (Path(source).stem + ".o")
            compile([cxx, *common, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                     "-c", network / source, "-o", obj])
            objects.append(obj)
        binary = output / "ice-flow-peer"
        compile([cxx, *common, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 network / "tests/ice_flow_peer.cpp", *objects,
                 "-Wl,--wrap=juice_create", "-o", binary])
        # Python's ctypes needs its own host-width build even for i686 CE peers.
        library = output / "parcel-crypto.so"
        compile([native_cc, "-O2", "-fPIC", "-shared", mono / "monocypher.c", "-o", library])
    tracked = ["port/network/" + name for name in sources]
    tracked += ["port/network/tests/ice_flow_peer.cpp", "port/network/ice_credentials.h",
                "port/network/ice_exchange.h", "port/network/ice_session.h",
                "port/network/network.h", "port/network/ice_game.h", "port/network/ice_bridge.h",
                "port/network/tests/fixture_guard.h", "port/tools/ice_bridge_build.py",
                "port/tools/ice_session_build.py",
                "port/tools/ice_flow_integration.py", "port/tools/ice_flow_integration_tests.py",
                "port/tools/ice_pilot_courier.py", "port/tools/turn_operator_issue.py",
                "port/tools/signaling_session_store.py", "port/tools/signaling_parcel_crypto.py"]
    receipt = {"schema": 1, "target": args.target, "game_process": False,
               "binary_sha256": sha256(binary), "library_sha256": sha256(library),
               "inputs": {name: sha256(ROOT / name) for name in tracked},
               "archives": {"libjuice": bridge.JUICE_SHA256, "enet": bridge.ENET_SHA256,
                            "monocypher": session.MONOCYPHER_SHA256},
               "relay_patch_sha256": bridge.JUICE_PATCH_SHA256}
    (output / "build-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("juice-archive", "enet-archive", "monocypher-archive", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--target", choices=("native", "linux32"), default="native")
    parser.add_argument("--toolchain-data", type=Path)
    parser.add_argument("--run", action="store_true")
    args = parser.parse_args()
    try:
        output = build(args)
        if args.run:
            import ice_flow_integration_tests as checks
            checks.run(output, output / "checks")
    except (ValueError, OSError, subprocess.SubprocessError):
        raise SystemExit("offline CE flow check refused; private diagnostics retained") from None
    print("Built authored CE flow fixture:", args.target)


if __name__ == "__main__":
    main()
