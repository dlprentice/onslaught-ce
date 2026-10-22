#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Builds libogg 1.0 and libvorbis 1.0 (Xiph.Org, BSD) as the retail game's ogg.dll and vorbis.dll were built: the
pinned Visual C++ 6 with the Release settings of Xiph's own win32/ogg_dynamic.dsp and vorbis_dynamic.dsp. Retail's
DLLs say "Xiph.Org libVorbis I 20020717" and were linked by VC6 on 2002-07-19; these objects decode every Ogg the game
plays to the same PCM, bit for bit (README.md). Outputs, in this checkout's local-data/port/vorbis/:
  lib/windows/libvorbis_vc6.a   the VC6 COFF objects, runtime references renamed to vc6_* (vc6crt.c serves them)
  lib/linux/libvorbis_vc6.a     the same objects converted to ELF for the i686 Linux build
Needs: wine, objcopy, ar, llvm-ar (from the port's llvm-mingw), and the decomp's pinned VC6 (BEA_VC6, as
tools/decomp.py; checked against config/toolchain.sha256 and config/vc6-include.sha256).
  python3 port/vorbis/build.py
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parent
CONFIG = ROOT / "config"
WORK = ROOT / "local-data/port/vorbis"
VC6 = Path(os.environ.get("BEA_VC6", "/nonexistent-set-BEA_VC6"))  # the pinned Visual C++ 6 SP5 toolchain (config/toolchain.sha256)
WINEPREFIX = Path(os.environ.get("BEA_WINEPREFIX", WORK / "wine"))
sys.path.insert(0, str(PORT / "tools"))
import deps  # noqa: E402  (the port's dependency data: llvm-mingw lives there)

MINGW = deps.DATA / "llvm-mingw/bin"

TARBALLS = {
    "libogg-1.0": ("https://downloads.xiph.org/releases/ogg/libogg-1.0.tar.gz",
                   "920fa2a0924d66884825d36a2e843de069cfdf1af01945d05da25999bbd6396c"),
    "libvorbis-1.0": ("https://downloads.xiph.org/releases/vorbis/libvorbis-1.0.tar.gz",
                      "8ef7507c22aaddb9d99a4341c5b3e720932541f4b75b08f19fce473f054c620f"),
}
# win32/ogg_dynamic.dsp and win32/vorbis_dynamic.dsp, "Win32 Release" (/YX /FD dropped: automatic precompiled headers
# and minimal-rebuild bookkeeping, no effect on the code). barkmel.c, in the project's list, is a program with its own
# main and is left out.
OGG = ("/nologo /MT /W2 /GX /O2 /D NDEBUG /D WIN32 /D _WINDOWS /D _MBCS /D _USRDLL".split(), ["bitwise", "framing"])
VORBIS = ("/nologo /MD /W3 /GX /O2 /D WIN32 /D NDEBUG /D _WINDOWS /D _MBCS /D _USRDLL".split(),
          ["analysis", "bitrate", "block", "codebook", "envelope", "floor0", "floor1", "info", "lpc", "lsp",
           "mapping0", "mdct", "psy", "registry", "res0", "sharedbook", "smallft", "synthesis", "vorbisenc", "window"])

# What the objects take from the C runtime (MSVCRT.DLL for vorbis, static LIBCMT for ogg), renamed so vc6crt.c serves
# it on both targets without colliding with the program's own runtime. COFF names; C names drop the leading _.
RUNTIME = ["__alloca_probe", "__chkstk", "__allshl", "__allshr", "__ftol", "__CIpow", "__CIacos", "__fltused",
           "_free", "_malloc", "_realloc", "_memchr", "_memmove"]
IMPORTS = ["calloc", "ceil", "fclose", "floor", "fopen", "fprintf", "free", "frexp", "ldexp", "malloc", "memmove",
           "perror", "qsort", "realloc", "sprintf", "toupper", "_iob"]
RENAMES = {**{s: "_vc6_" + s.lstrip("_") for s in RUNTIME}, **{f"__imp__{f}": f"_vc6_imp_{f.lstrip('_')}" for f in IMPORTS}}


def run(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors="replace", **kw)
    if r.returncode:
        raise SystemExit(f"{cmd[0]}: failed\n{r.stdout}{r.stderr}")
    return r.stdout


def sha256(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


# The sources, objects and VC6 itself are reached through their own Wine drives (S:, O:, V:), so the paths VC6 records
# in each object (its source, output and include names) are S:\libvorbis-1.0\lib\block.c and O:\block.obj, not this machine's.
DRIVES = {"s:": WORK / "src", "o:": WORK / "obj", "v:": VC6}


def winpath(p: Path) -> str:
    p = p.resolve()
    for letter, root in DRIVES.items():
        root = root.resolve()
        if p == root or root in p.parents:
            rel = p.relative_to(root)
            return letter.upper() + "\\" + ("" if str(rel) == "." else str(rel).replace("/", "\\"))
    return "Z:" + str(p).replace("/", "\\")


def map_drives():
    dev = WINEPREFIX / "dosdevices"
    dev.mkdir(parents=True, exist_ok=True)
    for letter, root in DRIVES.items():
        root.mkdir(parents=True, exist_ok=True)
        link = dev / letter
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(root.resolve())


def check_vc6():
    for line in (CONFIG / "toolchain.sha256").read_text().splitlines():
        digest, rel = line.split(maxsplit=1)
        if rel.startswith("os/"):		# the decomp's runtime DLL pin, not part of the compiler
            continue
        if sha256(VC6 / rel) != digest:
            raise SystemExit(f"{VC6 / rel}: differs from the pinned toolchain")
    root = VC6 / "VC98/Include"
    manifest = "".join(f"{sha256(f)}  ./{f.relative_to(root)}\n" for f in sorted(p for p in root.rglob("*") if p.is_file()))
    if hashlib.sha256(manifest.encode()).hexdigest() != (CONFIG / "vc6-include.sha256").read_text().split()[0]:
        raise SystemExit(f"{root}: differs from the pinned Visual C++ 6 headers")


def sources() -> Path:
    src = WORK / "src"
    for name, (url, digest) in TARBALLS.items():
        tar = WORK / "downloads" / url.rsplit("/", 1)[1]
        if not tar.exists():
            tar.parent.mkdir(parents=True, exist_ok=True)
            urllib.request.urlretrieve(url, tar.with_suffix(".part"))
            tar.with_suffix(".part").rename(tar)
        if sha256(tar) != digest:
            raise SystemExit(f"{tar}: SHA-256 differs from the pin")
        if not (src / name).exists():
            with tarfile.open(tar) as t:
                t.extractall(src, filter="data")
    return src


def compile_all(src: Path, out: Path) -> list[Path]:
    env = dict(os.environ, WINEPREFIX=str(WINEPREFIX), WINEDEBUG="-all", WINEDLLOVERRIDES="mscoree,mshtml=",
               WINEPATH=f"{winpath(VC6 / 'VC98/Bin')};{winpath(VC6 / 'Common/MSDev98/Bin')}",
               INCLUDE=winpath(VC6 / "VC98/Include"), LIB=winpath(VC6 / "VC98/Lib"))
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    ogg, vorbis = src / "libogg-1.0", src / "libvorbis-1.0"
    includes = {"ogg": [ogg / "include"], "vorbis": [ogg / "include", vorbis / "include", vorbis / "lib"]}
    objs = []
    for kind, (flags, names), folder in (("ogg", OGG, ogg / "src"), ("vorbis", VORBIS, vorbis / "lib")):
        for n in names:
            obj = out / f"{n}.obj"
            obj.unlink(missing_ok=True)
            inc = [a for d in includes[kind] for a in ("/I", winpath(d))]
            run(["wine", VC6 / "VC98/Bin/CL.EXE", *flags, *inc, "/c", f"/Fo{winpath(obj)}", winpath(folder / f"{n}.c")],
                env=env, cwd=folder)
            if not obj.exists():
                raise SystemExit(f"{n}.c: no object")
            data = bytearray(obj.read_bytes())
            data[4:8] = bytes(4)		# the COFF header's time stamp: the only field that changes between builds
            obj.write_bytes(data)
            objs.append(obj)
    return objs


def fix_pc32(path: Path):
    """objcopy's pe-i386 to elf32-i386 conversion keeps a COFF REL32's in-place addend (0: COFF implies the -4),
    which ELF's R_386_PC32 takes literally. Subtract 4 at every PC32 site."""
    d = bytearray(path.read_bytes())
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", d, 0x2e)
    sh = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    for _name, typ, _f, _a, off, size, _link, info, _al, _es in sh:
        if typ != 9:			# SHT_REL
            continue
        target = sh[info][4]
        for r in range(size // 8):
            roff, rinfo = struct.unpack_from("<II", d, off + 8 * r)
            if rinfo & 0xff == 2:		# R_386_PC32
                v, = struct.unpack_from("<i", d, target + roff)
                struct.pack_into("<i", d, target + roff, v - 4)
    path.write_bytes(d)


def main():
    check_vc6()
    map_drives()
    src = sources()
    out = WORK / "obj"
    out.mkdir(parents=True, exist_ok=True)
    objs = compile_all(src, out)
    redefine = WORK / "renames.txt"
    redefine.write_text("".join(f"{a} {b}\n" for a, b in RENAMES.items()))
    coff, elf = WORK / "coff", WORK / "elf"
    for d in (coff, elf):
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir()
    for obj in objs:
        c = coff / obj.name
        # the linker directives name VC6's runtime libraries (/DEFAULTLIB:MSVCRT, LIBC); the program brings its own
        run(["objcopy", "-I", "pe-i386", "-O", "pe-i386", "--remove-section", ".drectve", f"--redefine-syms={redefine}",
             obj, c])
        e = elf / (obj.stem + ".o")
        # VC6 emits each floating constant as a COMDAT (_real@...), which ELF cannot express: make them weak
        run(["objcopy", "-I", "pe-i386", "-O", "elf32-i386", "--remove-leading-char", "--wildcard",
             "--weaken-symbol=_real@*", c, e])
        fix_pc32(e)
    for target, folder, ar in (("windows", coff, MINGW / "llvm-ar"), ("linux", elf, "ar")):
        lib = WORK / "lib" / target / "libvorbis_vc6.a"
        lib.parent.mkdir(parents=True, exist_ok=True)
        lib.unlink(missing_ok=True)
        run([ar, "rcs", lib, *sorted(folder.iterdir())])
        print(f"vorbis: {lib} ({lib.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
