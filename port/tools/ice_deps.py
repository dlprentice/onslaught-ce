#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the optional pinned ICE client library, separate from normal CE dependencies.

    python3 port/tools/ice_deps.py build [linux|windows|all]

Requires the existing pinned compiler, CMake and (on Linux) sysroot from deps.py.
Only this explicit command fetches libjuice. No service or game process is run.
Use a private BEA_PORT_DATA overlay when sharing the normal dependency cache.
"""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess

import deps


def pin():
    rows = [line.split("\t") for line in (deps.PORT / "ice-deps.lock").read_text().splitlines()
            if line.strip() and not line.startswith(("#", "patch:"))]
    if len(rows) != 1 or len(rows[0]) != 5 or rows[0][0] != "libjuice":
        raise SystemExit("ice-deps.lock must contain exactly one libjuice pin")
    name, version, licence, sha, url = rows[0]
    return dict(name=name, version=version, licence=licence, sha=sha, url=url,
                file=deps.DATA / "ice-downloads" / (url.rsplit("/", 1)[1] + ".tar.gz"))


def source_patches():
    """Verify the exact modification before using any cached source or binary."""
    rows = [line.split("\t") for line in (deps.PORT / "ice-deps.lock").read_text().splitlines()
            if line.startswith("patch:")]
    if len(rows) != 1 or len(rows[0]) != 3:
        raise SystemExit("ice-deps.lock requires exactly one relay-policy patch pin")
    name, digest, licence = rows[0]
    name = name.removeprefix("patch:")
    if name != "libjuice-relay-only.patch" or not re.fullmatch(r"[0-9a-f]{64}", digest) or licence != "MPL-2.0":
        raise SystemExit("invalid optional ICE relay-policy patch pin")
    path = deps.PORT / "network" / name
    if path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit("optional ICE relay-policy patch is missing or differs from its pin")
    return [dict(name=name, sha256=digest, licence=licence)]


def same_json(left, right):
    """JSON types are part of identity: true/2.0 cannot stand in for schema 2."""
    try:
        return json.dumps(left, sort_keys=True, allow_nan=False) == json.dumps(right, sort_keys=True, allow_nan=False)
    except (TypeError, ValueError):
        return False


def variant():
    return source_patches()[0]["sha256"]


def target_name(target):
    if target not in ("linux", "windows"):
        raise ValueError(f"unknown ICE target: {target}")
    return target + (f"-{deps.SYSROOT_NAME}" if target == "linux" and deps.SYSROOT else "")


def prefix(target):
    return deps.DATA / "ice-relay" / variant() / target_name(target)


def source_root():
    return deps.DATA / "ice-src-relay" / variant()


def build_root(target):
    return deps.DATA / "ice-build-relay" / variant() / target_name(target)


@contextmanager
def install_lock(target, exclusive=False):
    folder = deps.DATA / "ice-locks"
    folder.mkdir(parents=True, exist_ok=True)
    with os.fdopen(os.open(folder / (target_name(target) + ".lock"),
                          os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600), "r+") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH)
        yield


def checked_source():
    row, patches = pin(), source_patches()
    identity, ordinary_patches = deps.patch_identity(row)
    if ordinary_patches:
        raise SystemExit("libjuice modifications must be declared only in ice-deps.lock")
    top, _ = deps.checked_stamp(row, source_root(), identity)
    try:
        known = json.loads(deps.read_cache_file(source_root() / ".bea-ice-source.json"))
    except (OSError, ValueError):
        raise SystemExit("optional ICE patched-source receipt missing; use a fresh BEA_PORT_DATA overlay")
    wanted = dict(schema=2, input={k: row[k] for k in ("name", "version", "licence", "sha", "url")},
                  source_patches=patches, source_tree_sha256=deps.source_tree_sha256(top), top=top.name)
    if not same_json(known, wanted):
        raise SystemExit("optional ICE patched source is stale or changed; use a fresh BEA_PORT_DATA overlay")
    return top, wanted


def prepare_source(row):
    if deps.patch_identity(row)[1]:
        raise SystemExit("libjuice modifications must be declared only in ice-deps.lock")
    folder = deps.DATA / "ice-source-locks"
    folder.mkdir(parents=True, exist_ok=True)
    with os.fdopen(os.open(folder / (variant() + ".lock"),
                          os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600), "r+") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        if (source_root() / ".bea-ice-source.json").exists():
            return checked_source()[0]
        if source_root().exists():
            raise SystemExit("incomplete optional ICE source; use a fresh BEA_PORT_DATA overlay")
        patches = source_patches()
        top = deps.unpack(row, source_root())
        for patch in patches:
            data = (deps.PORT / "network" / patch["name"]).read_bytes()
            if hashlib.sha256(data).hexdigest() != patch["sha256"]:
                raise SystemExit("optional ICE patch changed during source preparation")
            subprocess.run(["patch", "-s", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(top)],
                           input=data, check=True)
        receipt = dict(schema=2, input={k: row[k] for k in ("name", "version", "licence", "sha", "url")},
                       source_patches=patches, source_tree_sha256=deps.source_tree_sha256(top), top=top.name)
        deps.write_cache_file(source_root() / ".bea-ice-source.json", (json.dumps(receipt, sort_keys=True) + "\n").encode())
        return checked_source()[0]


def recipe(target):
    """Use pinned tools and a fixed environment, with separate patch-bound output folders."""
    dest, out = prefix(target), build_root(target)
    tools = deps.DATA / ("llvm/bin" if target == "linux" else "llvm-mingw/bin")
    compiler = tools / ("clang" if target == "linux" else "i686-w64-mingw32-clang")
    flags = deps.prefix_maps_arg(source_root(), out)
    cmd = [str(deps.sdl_cmake()), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
           f"-DCMAKE_INSTALL_PREFIX={dest}", "-DCMAKE_INSTALL_LIBDIR=lib",
           f"-DCMAKE_C_COMPILER={compiler}", f"-DCMAKE_AR={tools / 'llvm-ar'}",
           f"-DCMAKE_RANLIB={tools / 'llvm-ranlib'}", "-DBUILD_SHARED_LIBS=OFF",
           "-DNO_TESTS=ON", "-DNO_SERVER=ON", "-DUSE_NETTLE=OFF",
           # LLVM's deterministic archive mode is explicit even if its default changes.
           "-DCMAKE_C_ARCHIVE_CREATE=<CMAKE_AR> qcD <TARGET> <LINK_FLAGS> <OBJECTS>"]
    if target == "linux":
        flags = " ".join(["-m32", *deps.sysroot_flags(), flags])
        cmd += ["-DCMAKE_C_COMPILER_TARGET=i686-pc-linux-gnu",
                "-DCMAKE_EXE_LINKER_FLAGS=" + " ".join(["-m32", *deps.sysroot_link_flags()])]
        if deps.SYSROOT:
            cmd += [f"-DCMAKE_SYSROOT={deps.SYSROOT}"]
    else:
        # This pinned Clang otherwise writes wall time into each COFF object, even with SOURCE_DATE_EPOCH.
        flags += " -mno-incremental-linker-compatible"
        cmd += ["-DCMAKE_SYSTEM_NAME=Windows", "-DCMAKE_SYSTEM_PROCESSOR=x86",
                "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-insert-timestamp"]
    cmd += [f"-DCMAKE_C_FLAGS={flags}", "-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER"]
    env = {"PATH": "/usr/bin:/bin", "HOME": str(deps.DATA / ".ice-home"),
           "TMPDIR": str(deps.DATA / ".ice-tmp"), "LC_ALL": "C", "TZ": "UTC",
           "SOURCE_DATE_EPOCH": "0", "CMAKE_BUILD_PARALLEL_LEVEL": "2"}
    return cmd, env


def expected(target):
    row = pin()
    top, source = checked_source()
    cmd, env = recipe(target)
    tools = deps.lock()
    names = ["cmake", "llvm" if target == "linux" else "llvm-mingw"]
    if target == "linux" and deps.SYSROOT:
        names += ["sysroot-" + deps.SYSROOT_NAME]
    return dict(schema=2, target=target, input={k: row[k] for k in ("name", "version", "licence", "sha", "url")},
                source_patches=source["source_patches"], source_tree_sha256=source["source_tree_sha256"],
                toolchain={name: tools[name]["sha"] for name in names},
                recipe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                configure_sha256=hashlib.sha256(json.dumps([cmd, env], sort_keys=True).encode()).hexdigest())


def artifacts(target):
    dest = prefix(target)
    names = ("lib/libjuice.a", "include/juice/juice.h", "licenses/MPL-2.0.txt")
    result = {}
    for name in names:
        path = dest / name
        if path.is_symlink() or not path.is_file():
            raise SystemExit(f"{path}: optional ICE artifact missing or not a regular file")
        with path.open("rb") as stream:
            result[name] = hashlib.file_digest(stream, "sha256").hexdigest()
    return result


def require(target):
    """Refuse missing, stale or modified optional inputs. Caller keeps install_lock while using them."""
    receipt = prefix(target) / ".bea-ice-build.json"
    try:
        known = json.loads(deps.read_cache_file(receipt))
    except (OSError, ValueError):
        raise SystemExit(f"{receipt}: optional ICE receipt missing or invalid; run port/tools/ice_deps.py build")
    wanted = dict(expected(target), artifacts=artifacts(target))
    if not same_json(known, wanted):
        raise SystemExit(f"{receipt}: optional ICE receipt is stale or its artifacts changed; use a fresh BEA_PORT_DATA overlay")
    return prefix(target) / "lib/libjuice.a"


def build(target):
    with install_lock(target, exclusive=True):
        dest, out, row = prefix(target), build_root(target), pin()
        if dest.exists():
            require(target)
            return
        row["file"].parent.mkdir(parents=True, exist_ok=True)
        deps.fetch({"libjuice": row})
        src = prepare_source(row)
        cmd, env = recipe(target)
        for tool in (cmd[0], next(x.split("=", 1)[1] for x in cmd if x.startswith("-DCMAKE_C_COMPILER="))):
            if not Path(tool).is_file():
                raise SystemExit(f"{tool}: pinned tool missing; prepare the ordinary deps.py toolchain first")
        if out.exists():
            raise SystemExit(f"{out}: incomplete optional ICE build; use a fresh BEA_PORT_DATA overlay")
        for name in ("HOME", "TMPDIR"):
            Path(env[name]).mkdir(parents=True, exist_ok=True)
        deps.run(cmd + ["-S", src, "-B", out], env)
        deps.run([cmd[0], "--build", out, "--parallel", "2"], env)
        deps.run([cmd[0], "--install", out], env)
        (dest / "licenses").mkdir(exist_ok=True)
        shutil.copyfile(src / "LICENSE", dest / "licenses/MPL-2.0.txt")
        saved = dict(expected(target), artifacts=artifacts(target))
        deps.write_cache_file(dest / ".bea-ice-build.json", (json.dumps(saved, sort_keys=True) + "\n").encode())
        require(target)
        print(f"Optional ICE {target} i686 library: {dest / 'lib/libjuice.a'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("build",))
    parser.add_argument("target", nargs="?", default="all", choices=("linux", "windows", "all"))
    args = parser.parse_args()
    for target in (("linux", "windows") if args.target == "all" else (args.target,)):
        build(target)


if __name__ == "__main__":
    main()
