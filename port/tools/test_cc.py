#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Shared host C++ compiler wrapper and cached weak-stub archive for port/tools/*_tests.py drivers.

Instead of duplicating subprocess g++/clang++ invocations and hand-maintained hook stubs across dozens
of standalone test drivers, suites can call compile_driver(...) here to link against a content-hashed
libbea_test_stubs.a built from port/tools/tests/ce_weak_stubs.cpp. Strong definitions in any driver
cleanly override the weak defaults.
"""
from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
from pathlib import Path
from typing import Iterable, Mapping, Sequence

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
GAME_INC = PORT / "game"
STUBS_SRC = PORT / "tools/tests/ce_weak_stubs.cpp"
HOOKS_HDR = GAME_INC / "ce_hooks.h"
DEFAULT_CACHE = ROOT / "local-data/port/test-cc-cache"


def compiler(cxx: str | None = None) -> str:
    """Resolve the host C++ compiler."""
    cand = cxx or os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++") or "c++"
    resolved = shutil.which(cand) if os.sep not in cand else cand
    if not resolved or not Path(resolved).is_file():
        raise RuntimeError(f"host C++ compiler not found: {cand}")
    return resolved


def archiver() -> str:
    """Resolve ar for packing the cached weak-stub archive."""
    ar = shutil.which("ar") or shutil.which("llvm-ar")
    if not ar:
        raise RuntimeError("archiver (ar or llvm-ar) not found")
    return ar


def _stubs_key(cxx_path: str, std: str, m32: bool, stubs_src: Path, hooks_hdr: Path) -> str:
    h = hashlib.sha256()
    h.update(f"{cxx_path}\0{std}\0{int(m32)}\0".encode())
    h.update(stubs_src.read_bytes())
    h.update(b"\0")
    h.update(hooks_hdr.read_bytes())
    return h.hexdigest()[:16]


def stubs_archive(
    cache_dir: Path | None = None,
    *,
    cxx: str | None = None,
    std: str = "gnu++14",
    m32: bool = False,
    stubs_src: Path = STUBS_SRC,
    hooks_hdr: Path = HOOKS_HDR,
    timeout: int = 120,
) -> Path:
    """Build (or reuse from cache) libbea_test_stubs-<key>.a containing weak hook & port defaults."""
    cxx_path = compiler(cxx)
    cache = Path(cache_dir) if cache_dir is not None else DEFAULT_CACHE
    cache.mkdir(parents=True, exist_ok=True)
    key = _stubs_key(cxx_path, std, m32, stubs_src, hooks_hdr)
    archive = cache / f"libbea_test_stubs-{key}.a"
    if archive.is_file():
        return archive
    pid = os.getpid()
    obj = cache / f".ce_weak_stubs-{key}-{pid}.o"
    tmp_ar = cache / f".libbea_test_stubs-{key}-{pid}.a"
    cmd = [
        cxx_path, f"-std={std}", *(["-m32"] if m32 else []),
        "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-I", str(hooks_hdr.parent), "-c", str(stubs_src), "-o", str(obj),
    ]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        if res.returncode != 0:
            raise RuntimeError(f"weak stubs compile failed:\n{res.stdout}{res.stderr}")
        pack = subprocess.run([archiver(), "rcs", str(tmp_ar), str(obj)], capture_output=True, text=True, timeout=timeout)
        if pack.returncode != 0:
            raise RuntimeError(f"weak stubs archive failed:\n{pack.stdout}{pack.stderr}")
        os.replace(tmp_ar, archive)
    finally:
        obj.unlink(missing_ok=True)
        tmp_ar.unlink(missing_ok=True)
    return archive


def compile_driver(
    sources: Iterable[Path | str],
    out: Path | str,
    *,
    include_dirs: Iterable[Path | str] = (),
    defines: Sequence[str] = (),
    extra_flags: Sequence[str] = (),
    cxx: str | None = None,
    std: str = "gnu++14",
    m32: bool = False,
    with_stubs: bool = True,
    cache_dir: Path | None = None,
    timeout: int = 120,
) -> Path:
    """Compile and link a host C++ test driver, optionally linking the cached weak-stub archive."""
    cxx_path = compiler(cxx)
    out_path = Path(out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    cmd = [cxx_path, f"-std={std}", *(["-m32"] if m32 else []), "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter"]
    cmd += [d if d.startswith("-D") else f"-D{d}" for d in defines]
    cmd += list(extra_flags)
    seen_inc = set()
    for inc in (*include_dirs, GAME_INC):
        s = str(inc)
        if s not in seen_inc:
            seen_inc.add(s)
            cmd += ["-I", s]
    cmd += [str(s) for s in sources]
    if with_stubs:
        cmd.append(str(stubs_archive(cache_dir=cache_dir, cxx=cxx_path, std=std, m32=m32, timeout=timeout)))
    cmd += ["-o", str(out_path)]
    res = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if res.returncode != 0:
        raise RuntimeError(f"driver compile failed ({out_path.name}):\n{res.stdout}{res.stderr}")
    return out_path


def run_driver(
    binary: Path | str,
    args: Sequence[str] = (),
    *,
    env: Mapping[str, str] | None = None,
    timeout: int = 30,
) -> subprocess.CompletedProcess[str]:
    """Run a compiled host test driver and return CompletedProcess."""
    return subprocess.run([str(binary), *args], env=dict(env) if env is not None else None,
                          capture_output=True, text=True, timeout=timeout)
