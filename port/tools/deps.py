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
BUILD_WIN = BUILD       # the Windows dependencies' build folders (the Linux ones move with the sysroot below)
# The Linux target builds against the Steam Runtime 3 "sniper" SDK sysroot (deps.lock "sysroot-sniper": Debian 11, glibc
# 2.31, GCC 10 libstdc++, wayland 1.18): every Linux compile and link, the bundled libraries included, uses its headers,
# crt files and libraries instead of the host's, and links with lld (LLD below), so what ships needs nothing newer than
# glibc 2.29 (port/PLAN.md "Old glibc", measured by package.py's ELF gate). BEA_PORT_SYSROOT=host is the earlier build
# against this machine's own glibc and libraries (development only: what it makes runs only on as new a glibc as the
# host's). The sysroot build's Linux outputs have their own names (prefix-sniper, build-sniper, ffmpeg-shared-sniper), so
# a host-built library is never reused; build.py's ninja file names the sysroot in every command, so a checkout's game
# objects rebuild when the choice changes.
SYSROOT_NAME = os.environ.get("BEA_PORT_SYSROOT", "sniper")
if SYSROOT_NAME not in ("host", "sniper"):
    raise SystemExit(f"BEA_PORT_SYSROOT={SYSROOT_NAME!r}: 'sniper' (the default) or 'host'")
if SYSROOT_NAME == "host":
    SYSROOT_NAME = ""
SYSROOT = DATA / f"sysroot-{SYSROOT_NAME}" if SYSROOT_NAME else None
if SYSROOT:
    PREFIX, BUILD = DATA / f"prefix-{SYSROOT_NAME}", DATA / f"build-{SYSROOT_NAME}"

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
        if line.strip() and not line.startswith("#") and not line.startswith(("patch:", "licence:", "testroot:")):
            name, version, licence, sha, url = line.split("\t")
            if name.startswith("sysroot-") and name != f"sysroot-{SYSROOT_NAME}":
                continue    # fetched only for the build that uses it (BEA_PORT_SYSROOT)
            rows[name] = dict(name=name, version=version, licence=licence, sha=sha, url=url,
                              file=DOWNLOADS / url.rsplit("/", 1)[1])
    return rows


def testroot_pins():
    """Root file systems the old-glibc tests run in, never read by the build ("testroot:NAME<TAB>version<TAB>licence<TAB>
    sha256<TAB>url" lines in deps.lock; port/tools/oldroot.py fetches and unpacks them)."""
    rows = {}
    for line in (PORT / "deps.lock").read_text().splitlines():
        if line.startswith("testroot:"):
            name, version, licence, sha, url = line[len("testroot:"):].split("\t")
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
    stamp = dest / f".{r['file'].name}.unpacked"
    # The stamp records the top directory, so an unpacked archive is not read again (the LLVM release is 1.9 GB of xz).
    known = stamp.read_text().strip() if stamp.exists() else ""
    if known:
        return dest / known
    with tarfile.open(r["file"]) as t:
        top = dest / t.getnames()[0].split("/")[0]
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
    stamp.write_text(top.name + "\n")
    return top


# What the build reads from the sysroot tarball (one pinned file, 1.2 GiB, about 62,000 members): the i386 headers,
# libraries, crt files and pkg-config files, GCC 10's i686 files, and wayland-scanner 1.18 (amd64) with the amd64 loader
# and libraries it runs with (SCANNER below). Its Mesa DRI drivers and debug libraries are left out.
SYSROOT_KEEP = ("usr/include/", "lib/ld-linux.so.2", "lib/i386-linux-gnu/", "usr/lib/i386-linux-gnu/", "usr/lib/gcc/i686-linux-gnu/10/",
                "usr/share/pkgconfig/", "usr/share/wayland/", "usr/share/wayland-protocols/", "usr/bin/wayland-scanner",
                "lib64/", "lib/x86_64-linux-gnu/", "usr/lib/x86_64-linux-gnu/libxml2.so", "usr/lib/x86_64-linux-gnu/libicu",
                "usr/lib/x86_64-linux-gnu/libstdc++.so.6")
SYSROOT_SKIP = ("usr/lib/i386-linux-gnu/dri/", "usr/lib/i386-linux-gnu/debug/")


def sysroot(rows):
    """Extract the pinned sysroot's subset (SYSROOT_KEEP) into SYSROOT once and return it. Absolute symbolic links
    become relative ones (libz.so -> /lib/i386-linux-gnu/libz.so.1.2.11 would otherwise name the host's file), and the
    pkg-config files name the sysroot's absolute paths, so no PKG_CONFIG_SYSROOT_DIR is needed (which would also prefix
    the prefix's own .pc paths). Linker scripts (libc.so) keep their absolute paths: lld reads them inside the sysroot."""
    r = rows[f"sysroot-{SYSROOT_NAME}"]
    stamp = SYSROOT / f".{r['version']}.complete"
    if stamp.exists():
        return SYSROOT
    if SYSROOT.exists():
        shutil.rmtree(SYSROOT)
    print(f"sysroot: extracting {r['file'].name} into {SYSROOT}", flush=True)
    links = []
    with tarfile.open(r["file"]) as t:
        for m in t:
            name = m.name.removeprefix("./")
            if not name.startswith(SYSROOT_KEEP) or name.startswith(SYSROOT_SKIP):
                continue
            dest = SYSROOT / name
            if ".." in Path(name).parts:
                raise SystemExit(f"sysroot: refusing member {m.name}")
            if m.isdir():
                dest.mkdir(parents=True, exist_ok=True)
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            if m.issym():
                target = m.linkname
                if target.startswith("/"):
                    target = os.path.relpath(target.lstrip("/"), os.path.dirname(name))
                dest.symlink_to(target)
            elif m.islnk():
                links.append((dest, SYSROOT / m.linkname.removeprefix("./")))
            elif m.isfile():
                with t.extractfile(m) as f, open(dest, "wb") as out:
                    shutil.copyfileobj(f, out)
                dest.chmod(m.mode & 0o755)
    for dest, target in links:
        if target.exists():
            os.link(target, dest)
    # wayland-scanner needs libxml2.so.2, which the host may lack: a wrapper runs it with the sysroot's own amd64 loader
    # and libraries (its generated code does not depend on them).
    scanner = SYSROOT / "wayland-scanner"
    scanner.write_text(f'#!/bin/sh\nexec "{SYSROOT}/lib64/ld-linux-x86-64.so.2" --library-path '
                       f'"{SYSROOT}/lib/x86_64-linux-gnu:{SYSROOT}/usr/lib/x86_64-linux-gnu" "{SYSROOT}/usr/bin/wayland-scanner" "$@"\n')
    scanner.chmod(0o755)
    for pc in [*SYSROOT.glob("usr/lib/i386-linux-gnu/pkgconfig/*.pc"), *SYSROOT.glob("usr/share/pkgconfig/*.pc")]:
        if pc.is_symlink():
            continue
        text = pc.read_text()
        text = re.sub(r"^(\w+)=/", lambda m: f"{m.group(1)}={SYSROOT}/", text, flags=re.M)
        text = re.sub(r"(?<=\s)-([IL])/", lambda m: f"-{m.group(1)}{SYSROOT}/", text)
        pc.write_text(text)
    stamp.touch()
    return SYSROOT


def sysroot_flags():
    """The compile and link flags that select the sysroot and lld ([] without BEA_PORT_SYSROOT)."""
    return [f"--sysroot={SYSROOT}"] if SYSROOT else []


# The linker for the sysroot build: lld from the pinned llvm-mingw release (deps.lock "llvm-mingw", LLD 23.1.2, the
# linker the Windows target already uses; lld links ELF as well as COFF). The pinned LLVM release's own lld needs
# libxml2.so.2, which current distributions (Arch: libxml2.so.16) no longer have. The compiler stays the pinned 22.1.8.
LLD = DATA / "llvm-mingw/bin/ld.lld"


def sysroot_link_flags():
    # --no-dependent-libraries: lld, unlike GNU ld, honours an object's .deplibs section, where the game's
    # #pragma comment(lib, "d3dx8.lib") and others still reach through the /QIfist IR step (build.py qcxx).
    return [f"--sysroot={SYSROOT}", f"--ld-path={LLD}", "-Wl,--no-dependent-libraries"] if SYSROOT else []


def run(cmd, env=None, cwd=None):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, env=env, cwd=cwd)


def cmake_project(cm, cmake, name, src, opts, env):
    # CMake keeps the compiler a build folder was first configured with (CMakeCache.txt), whatever CC says now: a
    # folder configured with another compiler is configured again from scratch.
    want = next((str(o).split("=", 1)[1] for o in cm if str(o).startswith("-DCMAKE_C_COMPILER=")), env.get("CC"))
    root = BUILD_WIN if name.startswith("win32-") else BUILD
    cache = root / name / "CMakeCache.txt"
    if want and cache.exists():
        cc = re.search(r"^CMAKE_C_COMPILER:[A-Z]+=(.*)$", cache.read_text(errors="replace"), re.M)
        if cc and cc.group(1) != want:
            shutil.rmtree(root / name)
    run(cm + ["-S", src, "-B", root / name] + opts, env)
    run([cmake, "--build", root / name], env)
    run([cmake, "--install", root / name], env)


# libvorbis 1.3.7 declares cmake_minimum_required 2.8, which CMake 4 refuses without this policy floor.
XIPH = (("libogg", []), ("libvorbis", ["-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]))


def build_linux(rows, cmake, meson):
    unpack(rows["dxvk-native"], DATA / "dxvk-native")
    # The pinned LLVM (deps.lock "llvm"), not the system's clang: the bundled libraries must not change when the
    # system compiler does (SDL3 built by the system's 22.1.8 and later its 23.1.3 differed), and the source and build
    # folders are mapped out so a rebuild elsewhere gives the same bytes.
    llvm = DATA / "llvm/bin"
    cross = DATA / (f"i686-linux-{SYSROOT_NAME}.ini" if SYSROOT else "i686-linux.ini")
    text = CROSS.replace("{llvm}", str(llvm)).replace("{data}", str(DATA))
    if SYSROOT:
        sysroot(rows)
        mingw_toolchain(rows)
        cflag, lflag = ", ".join(f"'{f}'" for f in sysroot_flags()), ", ".join(f"'{f}'" for f in sysroot_link_flags())
        text = (text.replace("_args = ['-m32', '-ffile", f"_args = ['-m32', {cflag}, '-ffile")
                .replace("_link_args = ['-m32']", f"_link_args = ['-m32', {lflag}]"))
    cross.write_text(text)
    maps = f"-ffile-prefix-map={SRC}=src -ffile-prefix-map={BUILD}=build -ffile-prefix-map={DATA}=deps"
    if SYSROOT:
        pkg = f"{PREFIX}/lib/pkgconfig:{SYSROOT}/usr/lib/i386-linux-gnu/pkgconfig:{SYSROOT}/usr/share/pkgconfig"
    else:
        pkg = f"{PREFIX}/lib/pkgconfig:/usr/lib32/pkgconfig:/usr/share/pkgconfig"
    sflags, lflags = " ".join(sysroot_flags()), " ".join(sysroot_link_flags())
    env = dict(os.environ, CC=str(llvm / "clang"), CXX=str(llvm / "clang++"), AR=str(llvm / "llvm-ar"), RANLIB=str(llvm / "llvm-ranlib"),
               CFLAGS=f"-m32 {sflags} {maps}".replace("  ", " "), CXXFLAGS=f"-m32 {sflags} {maps}".replace("  ", " "),
               LDFLAGS=f"-m32 {lflags}".strip(), PKG_CONFIG_LIBDIR=pkg)
    env.pop("PKG_CONFIG_PATH", None)
    env.pop("PKG_CONFIG_SYSROOT_DIR", None)
    cm = [cmake, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={PREFIX}",
          "-DCMAKE_INSTALL_LIBDIR=lib", f"-DCMAKE_PREFIX_PATH={PREFIX}", "-DBUILD_SHARED_LIBS=ON"]
    if SYSROOT:
        # find_library and find_path look in the sysroot (then the prefix), never the host's /usr/lib32; the sysroot's
        # wayland-scanner (1.18) generates SDL's protocol code, so SDL needs only the libwayland-client 1.18 interface.
        # (ONLY: find_path otherwise falls back to the host's /usr/include, and SDL compiled its io_uring backend
        # against the host's liburing.h, which the sysroot lacks.)
        cm += [f"-DCMAKE_SYSROOT={SYSROOT}", f"-DCMAKE_FIND_ROOT_PATH={PREFIX}", "-DCMAKE_LIBRARY_ARCHITECTURE=i386-linux-gnu",
               "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY", "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY",
               "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY", "-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER",
               f"-DWAYLAND_SCANNER={SYSROOT}/wayland-scanner"]

    # libxkbcommon: SDL3's Wayland backend needs it, and multilib's 32-bit copy is not installed.
    xkb = unpack(rows["libxkbcommon"], SRC)
    # meson --reconfigure keeps the compiler a folder was first set up with (one set up before the cross file named
    # the pinned clang shipped a library built by the system's): such a folder is set up again from scratch.
    info = BUILD / "xkbcommon/meson-info/intro-compilers.json"
    if (BUILD / "xkbcommon").exists() and (not info.exists() or
                                           json.loads(info.read_text())["host"]["c"]["exelist"][:1] != [str(llvm / "clang")]):
        shutil.rmtree(BUILD / "xkbcommon")
    # Every search path xkbcommon compiles in is a player's system path, never this machine's: its defaults derive from
    # the prefix (PREFIX/etc/xkb, PREFIX/share/xkeyboard-config.d) and from xkeyboard-config.pc's xkb_base (the legacy
    # root and the versioned extensions folder), which in the sysroot names the sysroot's absolute path. So the extension
    # folders are given (the values the host-built 0.1.2 library had), and xkbcommon alone reads a neutral
    # xkeyboard-config.pc (xkb_base /usr/share/X11/xkb, the sysroot's version) placed first on its pkg-config path.
    xenv = env
    if SYSROOT:
        neutral = BUILD / "xkbcommon-pkgconfig"
        neutral.mkdir(parents=True, exist_ok=True)
        ver = re.search(r"^Version: (.*)$", (SYSROOT / "usr/share/pkgconfig/xkeyboard-config.pc").read_text(), re.M)[1]
        (neutral / "xkeyboard-config.pc").write_text(
            "prefix=/usr\ndatarootdir=${prefix}/share\ndatadir=${datarootdir}\nxkb_base=/usr/share/X11/xkb\n\n"
            f"Name: XKeyboardConfig\nDescription: X Keyboard configuration data\nVersion: {ver}\n")
        xenv = dict(env, PKG_CONFIG_LIBDIR=f"{neutral}:{env['PKG_CONFIG_LIBDIR']}")
    run(meson + ["setup", "--reconfigure" if (BUILD / "xkbcommon/build.ninja").exists() else "--wipe",
                 BUILD / "xkbcommon", xkb, "--cross-file", cross, f"--prefix={PREFIX}", "--libdir=lib",
                 "--buildtype=release", "-Denable-x11=false", "-Denable-wayland=false", "-Denable-xkbregistry=false",
                 "-Denable-docs=false", "-Denable-tools=false", "-Denable-bash-completion=false",
                 "-Dxkb-config-root=/usr/share/X11/xkb", "-Dx-locale-root=/usr/share/X11/locale",
                 "-Dxkb-config-extra-path=/etc/xkb",
                 "-Dxkb-config-unversioned-extensions-path=/usr/share/xkeyboard-config.d",
                 "-Dxkb-config-versioned-extensions-path=/usr/share/xkeyboard-config-2.d"], xenv)
    run(meson + ["install", "-C", BUILD / "xkbcommon"], xenv)

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


def mingw_toolchain(rows):
    """Unpack the pinned llvm-mingw release (deps.lock "llvm-mingw") and point local-data/port/llvm-mingw at it."""
    mingw = unpack(rows["llvm-mingw"], DATA)
    link = DATA / "llvm-mingw"
    if not link.exists():
        link.symlink_to(mingw.name)
    return link


def build_windows(rows, cmake):
    link = mingw_toolchain(rows)
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
          f"-DCMAKE_C_FLAGS=-ffile-prefix-map={SRC}=src -ffile-prefix-map={BUILD_WIN}=build -ffile-prefix-map={DATA}=deps"]
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
    out = (BUILD if target == "linux" else BUILD_WIN) / f"ffmpeg-{target}"
    if target == "linux":
        sflags = "".join(" " + f for f in sysroot_flags())
        prefix, extra = PREFIX, [f"--cc={DATA / 'llvm/bin/clang'} --target=i686-pc-linux-gnu{sflags}", "--target-os=linux", "--enable-pic"]
        if SYSROOT:
            extra += [f"--extra-ldflags={' '.join(sysroot_link_flags())}", f"--ar={DATA / 'llvm/bin/llvm-ar'}",
                      f"--nm={DATA / 'llvm/bin/llvm-nm'}", f"--ranlib={DATA / 'llvm/bin/llvm-ranlib'}"]
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
# libraries, installed under ffmpeg_shared_prefix(target). Configured with neutral names (--prefix=/usr through DESTDIR, the
# compilers by name from PATH) because libavutil exports its configure line (avutil_configuration).
FFMPEG_ABI = (("avformat", 61), ("avcodec", 61), ("avutil", 59))


def ffmpeg_shared_prefix(target):
    """Installed shared FFmpeg for this target. Linux sysroots never rename the Windows dependency tree."""
    if target not in ("linux", "windows"):
        raise ValueError(f"unknown FFmpeg target: {target}")
    suffix = f"-{SYSROOT_NAME}" if target == "linux" and SYSROOT else ""
    return DATA / f"ffmpeg-shared{suffix}" / target


def ffmpeg_shared_files(target, runtime=False):
    """The three pinned link inputs or runtime files; one naming contract for build and package consumers."""
    prefix = ffmpeg_shared_prefix(target) / "usr"
    if target == "windows":
        return tuple(prefix / ("bin" if runtime else "lib") / (f"{name}-{abi}.dll" if runtime else f"lib{name}.dll.a")
                     for name, abi in FFMPEG_ABI)
    return tuple(prefix / "lib" / (f"lib{name}.so.{abi}" if runtime else f"lib{name}.so") for name, abi in FFMPEG_ABI)


def require_ffmpeg_shared(target):
    """Refuse an incomplete public dependency tree before a linker can fall through to private static archives."""
    inputs = ffmpeg_shared_files(target)
    missing = [path for path in (*inputs, *ffmpeg_shared_files(target, runtime=True)) if not path.is_file()]
    if missing:
        raise SystemExit(f"public {target} FFmpeg shared inputs missing:\n  " + "\n  ".join(map(str, missing)))
    return inputs


def build_ffmpeg_shared(rows, target):
    src = unpack(rows["FFmpeg"], SRC)
    out = (BUILD if target == "linux" else BUILD_WIN) / f"ffmpeg-shared-{target}"
    dest = ffmpeg_shared_prefix(target)
    if all(path.is_file() for path in (*ffmpeg_shared_files(target), *ffmpeg_shared_files(target, runtime=True))):
        return
    out.mkdir(parents=True, exist_ok=True)
    # The compiler as a wrapper on PATH (ffcc) that maps the source and build directories in __FILE__ and debug
    # information, so neither the configure line nor the libraries name this machine's paths.
    wrap = out / "wrap"
    wrap.mkdir(exist_ok=True)
    real = DATA / ("llvm/bin/clang" if target == "linux" else "llvm-mingw/bin/i686-w64-mingw32-clang")
    tgt = " --target=i686-pc-linux-gnu" + "".join(" " + f for f in sysroot_link_flags()) if target == "linux" else ""
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
    require_ffmpeg_shared(target)


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
