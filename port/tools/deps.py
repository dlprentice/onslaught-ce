#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fetch, verify and build the native port's pinned third-party inputs (port/deps.lock) for Linux i686 and
Windows i686.

    python3 port/tools/deps.py fetch     # download into local-data/port/downloads; refuse a SHA-256 mismatch
    python3 port/tools/deps.py build [linux|windows]   # fetch, unpack, build and install (default: both)
    python3 port/tools/deps.py toolchain  # fetch and unpack only the pinned Linux compiler (local-data/port/llvm)

Everything lands in the canonical checkout's ignored local-data/port, deliberately shared by every worktree (the inputs
are pinned and identical, and about 1.6 GB): downloads/, src/,
build/, tools/ (cmake, meson), llvm/ (the pinned LLVM release the Linux target compiles with; the system clang
changed from 22.1.8 to 23.1.3 on 2026-10-20 and with it the game's behaviour), llvm-mingw/ (the Windows cross toolchain), dxvk-native/ (upstream's prebuilt release,
whose usr/lib32 holds the i686 libraries), prefix/ (Linux i686: lib, include, lib/pkgconfig) and prefix-win32/
(Windows i686: bin with the DLLs, lib with import libraries, include). Linux builds use llvm/'s clang -m32; Windows builds use
llvm-mingw's i686-w64-mingw32-clang. Nothing here touches the system.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(subprocess.run(["git", "-C", str(PORT), "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                capture_output=True, text=True, check=True).stdout.strip()).parent
DATA = Path(os.environ.get("BEA_PORT_DATA", REPO_ROOT / "local-data/port"))
# What derives from a checkout's own src/ (the mirror, objects, probe output) lives in that checkout's local-data/port.
WORK = PORT.parent / "local-data/port"
DOWNLOADS, SRC, BUILD, TOOLS, PREFIX, PREFIX_WIN = (DATA / n for n in (
    "downloads", "src", "build", "tools", "prefix", "prefix-win32"))

CROSS = """[binaries]
c = '{llvm}/clang'
cpp = '{llvm}/clang++'
ar = '{llvm}/llvm-ar'
strip = '{llvm}/llvm-strip'
pkg-config = 'pkgconf'

[built-in options]
c_args = ['-m32', '-ffile-prefix-map={data}=deps']
cpp_args = ['-m32', '-ffile-prefix-map={data}=deps']
c_link_args = ['-m32']
cpp_link_args = ['-m32']

[host_machine]
system = 'linux'
cpu_family = 'x86'
cpu = 'i686'
endian = 'little'
"""


def patch_pins():
    """port/deps-patches' pinned SHA-256s ("patch:NAME<TAB>sha256" lines in deps.lock)."""
    pins = {}
    for line in (PORT / "deps.lock").read_text().splitlines():
        if line.startswith("patch:"):
            name, sha = line[len("patch:"):].split("\t")
            pins[name] = sha.strip()
    return pins


def licence_pins():
    """Licence texts the release packages carry that no pinned input contains ("licence:NAME<TAB>version<TAB>licence
    <TAB>sha256<TAB>url" lines in deps.lock): package.py fetches and checks them; the build never reads them."""
    pins = {}
    for line in (PORT / "deps.lock").read_text().splitlines():
        if line.startswith("licence:"):
            name, version, licence, sha, url = line[len("licence:"):].split("\t")
            pins[name] = dict(name=name, version=version, licence=licence, sha=sha, url=url)
    return pins


def lock():
    rows = {}
    for line in (PORT / "deps.lock").read_text().splitlines():
        if line.strip() and not line.startswith("#") and not line.startswith(("patch:", "licence:")):
            name, version, licence, sha, url = line.split("\t")
            rows[name] = dict(name=name, version=version, licence=licence, sha=sha, url=url,
                              file=DOWNLOADS / url.rsplit("/", 1)[1])
    return rows


def fetch(rows):
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    for name, r in rows.items():
        f = r["file"]
        if not f.exists():
            print(f"fetch {name} {r['version']}", flush=True)
            tmp = f.with_suffix(f.suffix + ".part")
            with urllib.request.urlopen(r["url"]) as resp, open(tmp, "wb") as out:
                shutil.copyfileobj(resp, out)
            tmp.rename(f)
        h = hashlib.sha256()
        with open(f, "rb") as data:
            for block in iter(lambda: data.read(1 << 20), b""):
                h.update(block)
        got = h.hexdigest()
        if got != r["sha"]:
            raise SystemExit(f"{f}: SHA-256 {got} differs from deps.lock's {r['sha']}")


def unpack(r, dest):
    """Unpack r's archive into dest (once) and apply its port/deps-patches/<name>-*.patch; return its top directory."""
    with tarfile.open(r["file"]) as t:
        top = dest / t.getnames()[0].split("/")[0]
    stamp = dest / f".{r['file'].name}.unpacked"
    if not stamp.exists():
        dest.mkdir(parents=True, exist_ok=True)
        with tarfile.open(r["file"]) as t:
            t.extractall(dest, filter="data")
        pins = patch_pins()
        for patch in sorted((PORT / "deps-patches").glob(f"{r['name']}-*.patch")):
            got = hashlib.sha256(patch.read_bytes()).hexdigest()
            if pins.get(patch.name) != got:
                raise SystemExit(f"{patch}: SHA-256 {got} is not deps.lock's pin ({pins.get(patch.name, 'none')})")
            subprocess.run(["patch", "-s", "-p1", "-d", str(top), "-i", str(patch)], check=True)
        stamp.touch()
    return top


def run(cmd, env=None, cwd=None):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, env=env, cwd=cwd)


def cmake_project(cm, cmake, name, src, opts, env):
    # CMake keeps the compiler a build folder was first configured with (CMakeCache.txt), whatever CC says now: a
    # folder configured with another compiler is configured again from scratch.
    want = next((o.split("=", 1)[1] for o in cm if o.startswith("-DCMAKE_C_COMPILER=")), env.get("CC"))
    cache = BUILD / name / "CMakeCache.txt"
    if want and cache.exists():
        cc = re.search(r"^CMAKE_C_COMPILER:[A-Z]+=(.*)$", cache.read_text(errors="replace"), re.M)
        if cc and cc.group(1) != want:
            shutil.rmtree(BUILD / name)
    run(cm + ["-S", src, "-B", BUILD / name] + opts, env)
    run([cmake, "--build", BUILD / name], env)
    run([cmake, "--install", BUILD / name], env)


# libvorbis 1.3.7 declares cmake_minimum_required 2.8, which CMake 4 refuses without this policy floor.
XIPH = (("libogg", []), ("libvorbis", ["-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]))


def build_linux(rows, cmake, meson):
    unpack(rows["dxvk-native"], DATA / "dxvk-native")
    # The pinned LLVM (deps.lock "llvm"), not the system's clang: the bundled libraries must not change when the
    # system compiler does (SDL3 built by the system's 22.1.8 and later its 23.1.3 differed), and the source and build
    # folders are mapped out so a rebuild elsewhere gives the same bytes.
    llvm = DATA / "llvm/bin"
    cross = DATA / "i686-linux.ini"
    cross.write_text(CROSS.replace("{llvm}", str(llvm)).replace("{data}", str(DATA)))
    maps = f"-ffile-prefix-map={SRC}=src -ffile-prefix-map={BUILD}=build -ffile-prefix-map={DATA}=deps"
    env = dict(os.environ, CC=str(llvm / "clang"), CXX=str(llvm / "clang++"), AR=str(llvm / "llvm-ar"), RANLIB=str(llvm / "llvm-ranlib"),
               CFLAGS=f"-m32 {maps}", CXXFLAGS=f"-m32 {maps}", LDFLAGS="-m32",
               PKG_CONFIG_LIBDIR=f"{PREFIX}/lib/pkgconfig:/usr/lib32/pkgconfig:/usr/share/pkgconfig")
    cm = [cmake, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={PREFIX}",
          "-DCMAKE_INSTALL_LIBDIR=lib", f"-DCMAKE_PREFIX_PATH={PREFIX}", "-DBUILD_SHARED_LIBS=ON"]

    # libxkbcommon: SDL3's Wayland backend needs it, and multilib's 32-bit copy is not installed.
    xkb = unpack(rows["libxkbcommon"], SRC)
    # meson --reconfigure keeps the compiler a folder was first set up with (one set up before the cross file named
    # the pinned clang shipped a library built by the system's): such a folder is set up again from scratch.
    info = BUILD / "xkbcommon/meson-info/intro-compilers.json"
    if (BUILD / "xkbcommon").exists() and (not info.exists() or
                                           json.loads(info.read_text())["host"]["c"]["exelist"][:1] != [str(llvm / "clang")]):
        shutil.rmtree(BUILD / "xkbcommon")
    run(meson + ["setup", "--reconfigure" if (BUILD / "xkbcommon/build.ninja").exists() else "--wipe",
                 BUILD / "xkbcommon", xkb, "--cross-file", cross, f"--prefix={PREFIX}", "--libdir=lib",
                 "--buildtype=release", "-Denable-x11=false", "-Denable-wayland=false", "-Denable-xkbregistry=false",
                 "-Denable-docs=false", "-Denable-tools=false", "-Denable-bash-completion=false",
                 "-Dxkb-config-root=/usr/share/X11/xkb", "-Dx-locale-root=/usr/share/X11/locale",
                 # its default (PREFIX/etc/xkb) would put this machine's path in the shipped library
                 "-Dxkb-config-extra-path=/etc/xkb"], env)
    run(meson + ["install", "-C", BUILD / "xkbcommon"], env)

    cmake_project(cm, cmake, "sdl3", unpack(rows["SDL3"], SRC),
                  ["-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF", "-DSDL_STATIC=OFF"], env)
    for name, opts in XIPH:
        cmake_project(cm, cmake, name, unpack(rows[name], SRC), opts, env)
    print(f"deps: Linux i686 installed into {PREFIX}; dxvk-native in {DATA / 'dxvk-native'}")


def linux_toolchain(rows):
    """Unpack the pinned LLVM release (deps.lock "llvm") and point local-data/port/llvm at it."""
    top = unpack(rows["llvm"], DATA)
    link = DATA / "llvm"
    if not link.exists():
        link.symlink_to(top.name)
    return link


def build_windows(rows, cmake):
    mingw = unpack(rows["llvm-mingw"], DATA)
    link = DATA / "llvm-mingw"
    if not link.exists():
        link.symlink_to(mingw.name)
    bin_ = link / "bin"
    env = dict(os.environ, PATH=f"{bin_}:{os.environ['PATH']}")
    env.pop("PKG_CONFIG_PATH", None)
    env["PKG_CONFIG_LIBDIR"] = f"{PREFIX_WIN}/lib/pkgconfig"
    cm = [cmake, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_SYSTEM_NAME=Windows",
          "-DCMAKE_SYSTEM_PROCESSOR=x86", f"-DCMAKE_C_COMPILER={bin_}/i686-w64-mingw32-clang",
          f"-DCMAKE_CXX_COMPILER={bin_}/i686-w64-mingw32-clang++", f"-DCMAKE_RC_COMPILER={bin_}/i686-w64-mingw32-windres",
          f"-DCMAKE_FIND_ROOT_PATH={PREFIX_WIN}", "-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER",
          "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY", "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY",
          f"-DCMAKE_INSTALL_PREFIX={PREFIX_WIN}", "-DCMAKE_INSTALL_LIBDIR=lib", "-DBUILD_SHARED_LIBS=ON",
          # deterministic PE files: no link time in the headers, and source and build folders mapped out
          "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--no-insert-timestamp", "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-insert-timestamp",
          f"-DCMAKE_C_FLAGS=-ffile-prefix-map={SRC}=src -ffile-prefix-map={BUILD}=build -ffile-prefix-map={DATA}=deps"]
    cmake_project(cm, cmake, "win32-sdl3", unpack(rows["SDL3"], SRC),
                  ["-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF", "-DSDL_STATIC=OFF"], env)
    for name, opts in XIPH:
        cmake_project(cm, cmake, f"win32-{name}", unpack(rows[name], SRC), opts, env)
    # zlib: the Linux build links the system's lib32-zlib (the same 1.3.2); Windows builds it.
    cmake_project(cm, cmake, "win32-zlib", unpack(rows["zlib"], SRC), ["-DZLIB_BUILD_EXAMPLES=OFF"], env)
    # libogg's win32/ogg.def names its library "ogg", so its import library refers to ogg.dll, while CMake installs
    # libogg.dll; install it under the name the import library (and so every importer) uses.
    shutil.copy2(PREFIX_WIN / "bin/libogg.dll", PREFIX_WIN / "bin/ogg.dll")
    print(f"deps: Windows i686 installed into {PREFIX_WIN}; toolchain {link}")


# FFmpeg for port/bink: the Bink demuxer and its video and audio decoders only, LGPL (no --enable-gpl, no
# --enable-nonfree); static here for private builds, shared libraries for public builds (build_ffmpeg_shared below,
# the configuration the release packages ship).
FFMPEG_COMMON = ["--enable-static", "--disable-shared", "--disable-autodetect", "--disable-everything", "--disable-programs",
                 "--disable-doc", "--disable-network", "--disable-avdevice", "--disable-avfilter", "--disable-swscale",
                 "--disable-swresample", "--disable-x86asm", "--enable-decoder=bink,binkaudio_dct,binkaudio_rdft",
                 "--enable-demuxer=bink", "--enable-protocol=file", "--arch=x86_32", "--enable-cross-compile"]


def build_ffmpeg(rows, target):
    src = unpack(rows["FFmpeg"], SRC)
    out = BUILD / f"ffmpeg-{target}"
    if target == "linux":
        prefix, extra = PREFIX, [f"--cc={DATA / 'llvm/bin/clang'} --target=i686-pc-linux-gnu", "--target-os=linux", "--enable-pic"]
    else:
        mingw = DATA / "llvm-mingw/bin"
        prefix = PREFIX_WIN
        extra = [f"--cross-prefix={mingw}/i686-w64-mingw32-", f"--cc={mingw}/i686-w64-mingw32-clang", f"--ar={mingw}/llvm-ar",
                 f"--ranlib={mingw}/llvm-ranlib", f"--nm={mingw}/llvm-nm", "--target-os=mingw32", "--enable-w32threads"]
    if (prefix / "lib/libavcodec.a").exists():
        return
    out.mkdir(parents=True, exist_ok=True)
    run([src / "configure", f"--prefix={prefix}"] + FFMPEG_COMMON + extra, dict(os.environ, PWD=str(out)), cwd=out)
    run(["make", f"-j{os.cpu_count()}"], None, cwd=out)
    run(["make", "install"], None, cwd=out)


# The public build links FFmpeg dynamically (the player can replace the LGPL libraries): the same configuration as shared
# libraries, installed under FFMPEG_SHARED/<target>. Configured with neutral names (--prefix=/usr through DESTDIR, the
# compilers by name from PATH) because libavutil exports its configure line (avutil_configuration).
FFMPEG_SHARED = DATA / "ffmpeg-shared"


def build_ffmpeg_shared(rows, target):
    src = unpack(rows["FFmpeg"], SRC)
    out = BUILD / f"ffmpeg-shared-{target}"
    dest = FFMPEG_SHARED / target
    marker = dest / ("usr/lib/libavcodec.so" if target == "linux" else "usr/lib/libavcodec.dll.a")
    if marker.exists():
        return
    out.mkdir(parents=True, exist_ok=True)
    # The compiler as a wrapper on PATH (ffcc) that maps the source and build directories in __FILE__ and debug
    # information, so neither the configure line nor the libraries name this machine's paths.
    wrap = out / "wrap"
    wrap.mkdir(exist_ok=True)
    real = DATA / ("llvm/bin/clang" if target == "linux" else "llvm-mingw/bin/i686-w64-mingw32-clang")
    tgt = " --target=i686-pc-linux-gnu" if target == "linux" else ""
    # Windows: no link time in the PE headers (deterministic DLLs); ffcc also links.
    # (lld also writes a per-link GUID into a CodeView record for these DLLs, which differed between two clean builds)
    det = " -Wl,--no-insert-timestamp -Wl,--build-id=none" if target == "windows" else ""
    (wrap / "ffcc").write_text(f'#!/bin/sh\nexec {real}{tgt} -ffile-prefix-map={src}=ffmpeg-{rows["FFmpeg"]["version"]} '
                               f'-ffile-prefix-map={out}=build{det} "$@"\n')
    (wrap / "ffcc").chmod(0o755)
    tools = DATA / ("llvm/bin" if target == "linux" else "llvm-mingw/bin")
    env = dict(os.environ, PWD=str(out), PATH=f"{wrap}:{tools}:{os.environ['PATH']}")
    if target == "linux":
        extra = ["--cc=ffcc", "--target-os=linux", "--enable-pic", "--ar=llvm-ar", "--nm=llvm-nm", "--ranlib=llvm-ranlib",
                 "--strip=llvm-strip"]
    else:
        extra = ["--cross-prefix=i686-w64-mingw32-", "--cc=ffcc", "--ar=llvm-ar", "--ranlib=llvm-ranlib", "--nm=llvm-nm",
                 "--strip=llvm-strip", "--target-os=mingw32", "--enable-w32threads"]
    common = [f for f in FFMPEG_COMMON if f not in ("--enable-static", "--disable-shared")]
    run([src / "configure", "--prefix=/usr", "--enable-shared", "--disable-static"] + common + extra, env, cwd=out)
    run(["make", f"-j{min(8, os.cpu_count())}"], env, cwd=out)
    run(["make", "install", f"DESTDIR={dest}"], env, cwd=out)


def build(rows, targets):
    fetch(rows)
    cmake = unpack(rows["cmake"], TOOLS) / "bin/cmake"
    meson = [sys.executable, unpack(rows["meson"], TOOLS) / "meson.py"]
    if "linux" in targets:
        linux_toolchain(rows)
        build_linux(rows, cmake, meson)
        build_ffmpeg(rows, "linux")
        build_ffmpeg_shared(rows, "linux")
    if "windows" in targets:
        build_windows(rows, cmake)
        build_ffmpeg(rows, "windows")
        build_ffmpeg_shared(rows, "windows")


def main():
    args = sys.argv[1:]
    targets = args[1:] or ["linux", "windows"]
    if not args or args[0] not in ("fetch", "build", "toolchain") or (args[0] in ("fetch", "toolchain") and len(args) > 1) \
            or any(t not in ("linux", "windows") for t in targets):
        raise SystemExit(__doc__)
    rows = lock()
    if args[0] == "toolchain":
        fetch({"llvm": rows["llvm"]})
        print(f"deps: Linux toolchain {linux_toolchain(rows)}")
    else:
        fetch(rows) if args[0] == "fetch" else build(rows, targets)


if __name__ == "__main__":
    main()
