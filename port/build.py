#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the native port of src/ for Linux i686 and Windows i686 (port/README.md, port/PLAN.md).

    python3 port/build.py [--target linux|windows|all] [--public] [--syntax] [-k] [-j N] [FILE ...]

1. Mirror: src/'s tracked files are copied into local-data/port/mirror with two mechanical, line-preserving rewrites
   (include names: `\\` becomes `/`, quoted names take the case of the file they find, angle names are lowercased),
   then port/patches/*.patch apply in name order. The mirror and the build live in this checkout's local-data/port. The game data files src/ includes (config/game-data.sha256) come
   from the player's install (BEA_GAME_DIR, default the Steam folder), hash-checked. NVShaderFragments.inc, which the
   matching build writes from the retail executable's .data and no code reads, is zero-filled here.
2. Compile: a ninja file per target in local-data/port/build/<target>, every game translation unit in the x87 class
   with port/include/port.h forced in. --syntax checks without writing objects. FILE limits to those sources.
-k keeps going after errors and prints a census of the diagnostics.
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from collections import Counter
from contextlib import nullcontext
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))
import deps  # noqa: E402

PORT = deps.PORT
ROOT = PORT.parent                        # this checkout (worktree), whose src/ is ported
SRC = ROOT / "src"
MIRROR_ROOT = deps.WORK / "mirror"
MIRROR = MIRROR_ROOT / "src"              # src/ mirrored; linklib/ beside it, so its "..\src\" includes resolve
BUILD = deps.WORK / "build"
GAME = Path(os.environ.get("BEA_GAME_DIR", Path.home() / ".local/share/Steam/steamapps/common/Battle Engine Aquila"))
NV_FRAGMENTS_SIZE = 0x63C108 - 0x634550   # tools/decomp.py SPECIMEN_DATA

# C++ only: VC6's template lookup (unqualified names in dependent bases, Array.h) needs -fms-compatibility, which stops
# clang defining __GNUC__ that glibc's and MinGW's headers test, so its usual values are restored. The C files (data
# only) keep plain C, where -fms-compatibility would also drop __STDC__.
MSCXX = ["-fms-compatibility", "-fms-compatibility-version=19.20",
         "-D__GNUC__=4", "-D__GNUC_MINOR__=2", "-D__GNUC_PATCHLEVEL__=1", "-D__GNUC_STDC_INLINE__=1"]
# glibc 2.31's sys/cdefs.h (the old-glibc sysroot, deps.py SYSROOT) refuses __GNUC__ without __STDC__ ("You need a ISO C
# conforming compiler"), a check later glibc dropped; no other header the build reads tests __STDC__ (only zconf.h, which
# also accepts __cplusplus), so defining it changes nothing else.
if deps.SYSROOT:
    MSCXX.append("-D__STDC__=1")
# -fno-autolink: the sources' #pragma comment(lib, ...) (d3dx8, nvtristrip, imagehlp) name libraries the port replaces.
# -fno-sized-deallocation: VC6's delete is operator delete(void *), the game's own (port.h, PORT_ARRAY_FORMS).
COMMON = ["-fms-extensions", "-fno-autolink", "-fno-sized-deallocation", "-fno-strict-aliasing", "-fwrapv", "-malign-double", "-mms-bitfields", "-fshort-wchar",
          "-g", "-O2", "-Wno-pragma-pack", "-Wno-c++11-narrowing", "-Wno-reserved-user-defined-literal",
          "-Wno-microsoft", "-Wno-address-of-temporary", "-Wno-writable-strings", "-Wno-invalid-offsetof", "-w", "-ferror-limit=50"]
X87 = ["-mno-sse", "-mfpmath=387"]
# Linux: wchar_t is 2 bytes (-fshort-wchar) but glibc's wide functions take 4-byte units, and LLVM turns a wide-string
# loop into a call to them (the loop-idiom pass made port_wcslen call glibc's wcslen: the tutorial's first message got
# 8,053 characters and never ended). These are not library calls the port may use.
NO_WIDE_LIBCALLS = ["-fno-builtin-" + f for f in ("wcslen", "wcscmp", "wcsncmp", "wcschr", "wcscpy", "wcsncpy", "wcscat",
                                                  "wmemcmp", "wmemchr", "wmemcpy", "wmemmove", "wmemset")]
# VC6 at /O2 computes these as inline x87 instructions (port/platform/common/x87math.cpp). Game files call them as
# functions, renamed in their objects to the port's x87 versions (GAME_MATH); clang must not fold, shrink or inline
# them on its own.
X87_MATH = ["sin", "cos", "tan", "atan", "atan2", "sqrt", "exp", "log", "log10", "asin", "acos"]
GAME_MATH = {f + suffix: f"bea_x87_{f}{suffix}" for f in X87_MATH for suffix in ("", "f")}
NO_MATH_BUILTINS = ["-fno-builtin-" + f for f in GAME_MATH]
# The Linux target compiles with the pinned LLVM release (deps.lock "llvm", unpacked by `deps.py toolchain`), never the
# system clang: the system's 22.1.8 -> 23.1.3 upgrade (2026-10-20) changed the game's behaviour (port/PARITY.md, Compiler).
LINUX_LLVM = deps.DATA / "llvm/bin"
LINUX_CLANG_VERSION = "clang version 22.1.8"
TARGETS = {
    # BEA_PORT_SYSROOT=sniper adds the old-glibc sysroot (deps.py, SYSROOT) to every compile and link.
    "linux": dict(cxx=[str(LINUX_LLVM / "clang++"), "--target=i686-pc-linux-gnu"] + deps.sysroot_flags() + NO_WIDE_LIBCALLS,
                  cc=[str(LINUX_LLVM / "clang"), "--target=i686-pc-linux-gnu"] + deps.sysroot_flags() + NO_WIDE_LIBCALLS,
                  includes=[PORT / "include/linux", deps.DATA / "dxvk-native/usr/include/dxvk"]),
    "windows": dict(cxx=[str(deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang++")],
                    cc=[str(deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang")],
                    includes=[PORT / "include/windows"]),
}

def system_header_names():
    """Lower-cased name -> real name of every header in the port's and the targets' include directories."""
    names = {}
    dirs = [PORT / "include", PORT / "d3dx/include", PORT / "include/linux", PORT / "include/windows",
            deps.DATA / "dxvk-native/usr/include/dxvk", deps.DATA / "llvm-mingw/generic-w64-mingw32/include"]
    for d in dirs:
        if d.is_dir():
            for f in d.iterdir():
                names.setdefault(f.name.lower(), f.name)
    return names


SYSTEM_NAMES = {}
INCLUDE = re.compile(r'^(\s*#\s*include\s*)([<"])([^>"]+)([>"])', re.M)


def tracked_sources():
    """src/'s and linklib/'s tracked files (linklib/ holds the link-only definitions whose owner is still open)."""
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z", "src", "linklib"], capture_output=True, check=True).stdout
    return [ROOT / p for p in out.decode().split("\0") if p]


class CaseIndex:
    """Case-insensitive lookup of mirror paths."""

    def __init__(self, files):
        self.by_lower = {}
        for f in files:
            self.by_lower.setdefault(str(f).lower(), f)

    def find(self, path: Path):
        return self.by_lower.get(str(os.path.normpath(path)).lower())


def normalise(text, here: Path, index: CaseIndex, search):
    def fix(m):
        lead, open_, name, close = m.groups()
        name = name.replace("\\", "/")
        if open_ == "<":
            return f"{lead}<{name.lower()}>"
        for base in [here] + search:
            hit = index.find(base / name)
            if hit is not None:
                rel = os.path.relpath(hit, here) if base == here else os.path.relpath(hit, base)
                return f'{lead}"{rel}"'
        # Not a game file: take the case of the system or port header it names ("Vfw.h" is vfw.h).
        return f'{lead}"{SYSTEM_NAMES.get(name.lower(), name)}"'
    return INCLUDE.sub(fix, text)


def write_if_changed(path: Path, data: bytes):
    if not path.exists() or path.read_bytes() != data:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def build_mirror():
    SYSTEM_NAMES.update(system_header_names())
    files = tracked_sources()
    rel = [f.relative_to(ROOT) for f in files]            # src/... and linklib/...
    if PUBLIC:
        # A checkout without the replaced files (the public export leaves them out) still gets the replacements, under
        # the same src/ names, so includes and the object list resolve.
        for name in PUBLIC_REPLACEMENTS:
            if Path("src") / name not in rel and (PORT / "platform/public" / name).exists():
                files.append(PORT / "platform/public" / name)
                rel.append(Path("src") / name)
    data_rows = [l.split(maxsplit=1) for l in (ROOT / "config/game-data.sha256").read_text().splitlines() if l.strip()]
    targets = [MIRROR_ROOT / r for r in rel] + [MIRROR / r for _, r in data_rows] + [MIRROR / "data/NVShaderFragments.inc"]
    index = CaseIndex(targets)
    search = [MIRROR, MIRROR / "MissionScript"]
    unreplaced = []
    for src, r in zip(files, rel):
        if PUBLIC and r.parent == Path("src") and r.name in PUBLIC_REPLACEMENTS:
            if (PORT / "platform/public" / r.name).exists():
                src = PORT / "platform/public" / r.name    # the public build's own version (PUBLIC_REPLACEMENTS)
            else:
                unreplaced.append(r.name)
        raw = src.read_bytes()
        if src.suffix.lower() in (".c", ".cpp", ".h", ".inc", ".hpp"):
            text = raw.decode("latin-1")
            raw = normalise(text, (MIRROR_ROOT / r).parent, index, search).encode("latin-1")
        write_if_changed(MIRROR_ROOT / r, raw)
    for digest, r in data_rows:
        source = GAME / r
        if not source.is_file():
            source = next((p for p in GAME.rglob("*") if str(p.relative_to(GAME)).lower() == r.lower()), source)
        blob = source.read_bytes()
        if hashlib.sha256(blob).hexdigest() != digest:
            raise SystemExit(f"{source}: differs from config/game-data.sha256 (set BEA_GAME_DIR to the game's folder)")
        write_if_changed(MIRROR / r, blob)
    write_if_changed(MIRROR / "data/NVShaderFragments.inc",
                     (",\n".join(", ".join(["0x00"] * 16) for _ in range(NV_FRAGMENTS_SIZE // 16)) + "\n").encode())
    # Files no longer in src/ leave the mirror.
    keep = {str(t) for t in targets}
    for p in sorted(MIRROR_ROOT.rglob("*"), reverse=True):
        if p.is_file() and str(p) not in keep:
            p.unlink()
    if PUBLIC:
        # package.py refuses a build that still compiled a file the public build must replace.
        if unreplaced:
            print(f"build: --public without replacements for {', '.join(unreplaced)}: not shippable", file=sys.stderr)
        write_if_changed(MIRROR_ROOT / "public-unreplaced.txt", "".join(n + "\n" for n in unreplaced).encode())
    for patch in sorted((PORT / "patches").glob("*.patch")):
        if PUBLIC and PUBLIC_SKIPPED_PATCHES.get(patch.name) not in (None, *unreplaced):
            continue
        r = subprocess.run(["patch", "-p1", "-d", str(MIRROR), "-i", str(patch), "--forward", "-r", "-"],
                           capture_output=True, text=True)
        fuzz = [line for line in r.stdout.splitlines() if "fuzz" in line]
        if r.returncode or fuzz:
            # A hunk applied with fuzz may have landed beside its intended lines: refresh the patch's context.
            raise SystemExit(f"build: {patch.name} does not apply cleanly:\n{r.stdout}{r.stderr}")
    return [MIRROR_ROOT / r for r in rel if r.suffix.lower() in (".c", ".cpp")]


NVTRISTRIP = "NvTriStrip-c40ad04c7fec77be6af9d4ebc0c64da80241984f/NvTriStrip"


def module_flags(module):
    """port/<module>/flags.txt: `file: -flags` lines appended to that file's compile; '#' lines are comments."""
    flags = {}
    f = PORT / module / "flags.txt"
    if f.exists():
        for line in f.read_text().splitlines():
            if line.strip() and not line.lstrip().startswith("#") and ":" in line:
                name, extra = line.split(":", 1)
                flags[name.strip()] = extra.strip()
    return flags


def port_sources(target):
    """(source, rule, extra flags) for the port's own code and third-party sources linked into the program."""
    out = [(s, "dcxx", module_flags("d3dx").get(s.name, "")) for s in sorted((PORT / "d3dx/src").glob("*.cpp"))]
    # The modules port/dsound and port/bink replace the platform's stand-ins once they hold sources.
    replaced = {name for module, name in (("dsound", "dsound.cpp"), ("bink", "bink.cpp"))
                if any((PORT / module).glob("*.cpp"))}
    for module in ("dsound", "bink"):
        flags = module_flags(module)
        out += [(s, "pcxx", flags.get(s.name, "")) for s in sorted((PORT / module).glob("*.cpp"))]
    for d in ("common", target):
        out += [(s, "pcxx", "") for s in sorted((PORT / "platform" / d).glob("*.cpp"))
                if not (d == "common" and s.name in replaced)]
    # Port code that reads game objects through the game's own headers: built as a game file (flags, port.h, renames).
    out += [(s, "qcxx", "") for s in sorted((PORT / "game").glob("*.cpp"))]
    if PUBLIC:
        # The public build (--public): no NvTriStrip (its repository states no licence, port/PLAN.md). With retail data
        # the game never strips at run time; the stub refuses the raw-mesh path instead (port/platform/public).
        out = [o for o in out if o[0].name != "nvtristrip_bridge.cpp"]
        out += [(PORT / "platform/public/nvtristrip_public.cpp", "pcxx", "")]
    else:
        nvt = deps.DATA / "src" / NVTRISTRIP
        if not nvt.is_dir():
            deps.unpack(deps.lock()["NvTriStrip"], deps.DATA / "src")
        # NvTriStrip's own build compiles these two (VertexCache.h defines its class inline; VertexCache.cpp repeats it).
        out += [(nvt / "src" / n, "pcxx", "") for n in ("NvTriStrip.cpp", "NvTriStripObjects.cpp")]
    # The C runtime that the VC6-built libvorbis/libogg 1.0 objects call (port/vorbis).
    out += [(PORT / "vorbis/vc6crt.c", "pcc", "")]
    # SHA-256 for the mod checks (platform/common/mods.cpp), the launcher's own implementation.
    out += [(PORT / "updater/sha256.c", "pcc", "")]
    return out


def vorbis_lib(target):
    """Retail's decoder: libvorbis/libogg 1.0 compiled by the pinned VC6 (port/vorbis/build.py; README there)."""
    lib = ROOT / "local-data/port/vorbis/lib" / target / "libvorbis_vc6.a"
    if not lib.is_file():
        # Building it needs the pinned VC6, which is not redistributable; the same library, built by port/vorbis/build.py
        # and pinned by SHA-256 in deps.lock, is fetched from the release assets instead.
        row = deps.lock()[f"libvorbis_vc6-{target}"]
        deps.fetch({row["name"]: row})
        lib.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(row["file"], lib)
    return lib


def link_flags(target):
    aliases = [l.split("\t")[:2] for l in (PORT / "aliases.tsv").read_text().splitlines()
               if l.strip() and not l.startswith("#")]
    # Private builds may use static archives. Public links name the validated shared inputs explicitly, so a missing
    # import library/shared object cannot silently select a static archive from a later -L directory.
    ffmpeg = ""
    if any((PORT / "bink").glob("*.cpp")):
        ffmpeg = " ".join(shlex.quote(str(path)) for path in deps.require_ffmpeg_shared(target)) if PUBLIC else "-lavformat -lavcodec -lavutil"
    if target == "windows":
        # COFF's weak alias (/alternatename); i686 MinGW symbols carry a leading underscore.
        defsyms = " ".join(f"-Wl,-Xlink=-alternatename:_{a}=_{b}" for a, b in aliases)
    else:
        defsyms = " ".join(f"-Wl,--defsym={a}={b}" for a, b in aliases)
    if target == "linux":
        dxvk = deps.DATA / "dxvk-native/usr/lib32"
        lib = deps.PREFIX / "lib"
        # The public build finds its bundled libraries beside it ($ORIGIN/lib; ninja's $$ and the shell's quotes keep it).
        rpath = "'$$ORIGIN/lib'" if PUBLIC else f"{lib}:{dxvk}"
        lld = "".join(f + " " for f in deps.sysroot_link_flags())
        return (f"-m32 {lld}{defsyms} -L{lib} -L{dxvk} -Wl,-rpath,{rpath} -no-pie -ldxvk_d3d9 -lSDL3 {vorbis_lib(target)} -lz "
                f"{ffmpeg} -lpthread -ldl -lm")
    lib = deps.PREFIX_WIN / "lib"
    return (f"-mwindows -Wl,--no-insert-timestamp {defsyms} -L{lib} -lSDL3 {vorbis_lib(target)} -lz {ffmpeg} {'-lbcrypt' if ffmpeg else ''} -ld3d9 -ldxguid -luuid -lwinmm -lgdi32 -luser32 "
            "-ladvapi32 -lversion -lcomdlg32 -lvfw32 -lole32 -static-libstdc++ -static-libgcc")


# VC6 gave the game its own operator new/delete per translation unit (`static inline`, MemoryManager.h), so game code
# allocated from the game's heap while NvTriStrip, the STL and the runtime used the C runtime's. C++ has no per-file
# operator new; after compiling, each game object's references and definitions are renamed to these private names,
# which reproduces that split exactly. (Global replacements were exported and interposed on C++ libraries.)
# The C runtime's rand/srand as retail's LIBCMT computes them (platform/common/crtrand.cpp) and fopen/fclose as its static
# stream table behaves (crtfile.cpp: a second fclose returns EOF), for game objects only. Game code opens files through
# port.h's fopen -> port_fopen, so port_fopen is renamed too, or no game stream reaches the table.
GAME_CRT = {"rand": "bea_crt_rand", "srand": "bea_crt_srand", "fopen": "bea_crt_fopen", "port_fopen": "bea_crt_port_fopen",
            "fclose": "bea_crt_fclose"}
GAME_ALLOCATOR = {"_Znwj": "bea_game_new", "_Znaj": "bea_game_new_array", "_ZdlPv": "bea_game_delete",
                  "_ZdaPv": "bea_game_delete_array"}


def objcopy(target):
    if target == "windows":
        return str(deps.DATA / "llvm-mingw/bin/llvm-objcopy")
    return "llvm-objcopy" if SYSTEM_CLANG else str(LINUX_LLVM / "llvm-objcopy")


SYSTEM_CLANG = False
PUBLIC = False
CE_DEFINES = []   # the public build's CE_VERSION (game files: the CE layer of patch 0037; unset in private builds)
# The public build replaces these src/ files (derived from Microsoft's DirectX SDK samples, which cannot ship) with the
# port's own versions in port/platform/public/, same names and interfaces, compiled as game files; the patches below
# touch only a replaced file, whose replacement carries the change itself.
PUBLIC_REPLACEMENTS = ("wavread.cpp", "WavRead.h", "dxutil.cpp", "d3dutil.cpp", "DXFont.cpp", "d3dapp.cpp", "d3dapp.h")
PUBLIC_SKIPPED_PATCHES = {"0034-constant-divisions-DXFont.patch": "DXFont.cpp",
                          "0042-widescreen-virtual-ui-DXFont.patch": "DXFont.cpp",
                          "0041-widescreen-screen-shape.patch": "d3dapp.cpp",
                          "0045-widescreen-mode-gate.patch": "d3dapp.cpp",
                          "0046-widescreen-native-default.patch": "d3dapp.cpp"}


def use_system_clang():
    """The cross-compiler check: the Linux target with the system's clang, objcopy and whatever version it is."""
    global SYSTEM_CLANG
    SYSTEM_CLANG = True
    t = TARGETS["linux"]
    t["cxx"][0], t["cc"][0] = "clang++", "clang"


def check_compiler(target):
    """Fail fast unless the Linux target's compiler is the pinned release."""
    if target != "linux":
        return
    if SYSTEM_CLANG:
        first = subprocess.run(["clang++", "--version"], capture_output=True, text=True, check=True).stdout.splitlines()[0]
        print(f"build: Linux target with the system compiler ({first}), not the pinned {LINUX_CLANG_VERSION}", flush=True)
        return
    clang = TARGETS["linux"]["cxx"][0]
    try:
        first = subprocess.run([clang, "--version"], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    except (OSError, subprocess.CalledProcessError, IndexError) as e:
        raise SystemExit(f"{clang}: not usable ({e}); run python3 port/tools/deps.py toolchain")
    if not first.startswith(LINUX_CLANG_VERSION):
        raise SystemExit(f"{clang} reports '{first}', not the pinned {LINUX_CLANG_VERSION}")


def allocator_syms(target, out):
    """The --redefine-syms file for game objects: the allocator and the x87 maths (i686 MinGW symbols carry a leading
    underscore)."""
    prefix = "_" if target == "windows" else ""
    path = out / "game-allocator.syms"
    renames = {**GAME_ALLOCATOR, **GAME_MATH, **GAME_CRT}
    write_if_changed(path, "".join(f"{prefix}{a} {prefix}{b}\n" for a, b in renames.items()).encode())
    return path


def ninja_file(target, sources, syntax, link, sdl_marker=None):
    t = TARGETS[target]
    out = BUILD / target
    inc = [PORT / "d3dx/include", PORT / "include"] + t["includes"] + [MIRROR, MIRROR / "MissionScript"]
    incflags = " ".join(f"-I{p}" for p in inc)
    common = " ".join(COMMON + X87 + NO_MATH_BUILTINS + CE_DEFINES) + f" -include {PORT / 'include/port.h'} {incflags}"
    mode = "-fsyntax-only" if syntax else "-c -o $out"
    out.mkdir(parents=True, exist_ok=True)
    rename = "" if syntax else f" && {objcopy(target)} --redefine-syms={allocator_syms(target, out)} $out"
    sdl = deps.PREFIX if target == "linux" else deps.PREFIX_WIN
    # The port's own code: C++17, struct layout and 2-byte wchar_t as the game (shared structures have one layout).
    portinc = " ".join(f"-I{p}" for p in [PORT / "d3dx/include", PORT / "include"] + t["includes"] +
                       [sdl / "include", deps.DATA / "src" / NVTRISTRIP / "include", deps.DATA / "src" / NVTRISTRIP / "src"])
    # (No -malign-double: libstdc++'s <cmath> rejects it, and no structure shared with the game holds a double.)
    layout = "-DNOMINMAX -fms-extensions -fno-autolink -fno-strict-aliasing -fwrapv -mms-bitfields -fshort-wchar -g -O2 -w"
    lines = [f"builddir = {out}", f"cxx = {' '.join(t['cxx'])}", f"cc = {' '.join(t['cc'])}",
             f"rule cxx\n  command = $cxx -std=gnu++14 {" ".join(MSCXX)} {common} -MD -MF $out.d {mode} $in{rename} && touch $out\n"
             "  depfile = $out.d\n  deps = gcc\n  description = C++ $in",
             # /QIfist (tools/qifist.py): unoptimised IR, float-to-integer conversions rounded as fistp does, then
             # optimised and compiled. Every game C++ file but cframetimer.cpp, which retail built without /QIfist.
             f"rule qcxx\n  command = $cxx -std=gnu++14 {" ".join(MSCXX)} {common} -MD -MF $out.d -S -emit-llvm "
             f"-Xclang -disable-llvm-passes -o $out.ll $in && {sys.executable} {PORT / 'tools/qifist.py'} $out.ll > /dev/null "
             f"&& $cxx {' '.join(COMMON + X87)} -c -o $out $out.ll{rename} && rm -f $out.ll\n"
             "  depfile = $out.d\n  deps = gcc\n  description = C++ /QIfist $in",
             f"rule cc\n  command = $cc -std=gnu99 {common} -MD -MF $out.d {mode} $in && touch $out\n"
             "  depfile = $out.d\n  deps = gcc\n  description = C $in",
             f"rule dcxx\n  command = $cxx -std=gnu++17 {layout} {' '.join(X87)} $extra {portinc} -MD -MF $out.d -c -o $out $in\n"
             "  depfile = $out.d\n  deps = gcc\n  description = D3DX $in",
             f"rule pcxx\n  command = $cxx -std=gnu++17 {layout} {' '.join(CE_DEFINES)} $extra {portinc} -MD -MF $out.d -c -o $out $in\n"
             "  depfile = $out.d\n  deps = gcc\n  description = PORT $in",
             f"rule pcc\n  command = $cc -std=gnu99 -O2 -w -MD -MF $out.d -c -o $out $in\n"
             "  depfile = $out.d\n  deps = gcc\n  description = PORT $in",
             f"rule link\n  command = $cxx -o $out @$out.rsp {link_flags(target)}\n  rspfile = $out.rsp\n"
             "  rspfile_content = $in\n  description = LINK $out"]
    if target == "windows":
        # the application manifest (UTF-8 active code page) as a resource object: RESOURCE below
        lines.append(f"rule res\n  command = {WINDRES} --target=pe-i386 -I {PORT / 'platform/windows'} -O coff $in -o $out\n"
                     "  description = RC $in")
    suffix = ".chk" if syntax else ".o"
    objs = []
    for s in sources:
        obj = out / (str(s.relative_to(MIRROR_ROOT)) + suffix)
        objs.append(obj)
        rule = "cc" if s.suffix.lower() == ".c" else "cxx" if syntax or s.name.lower() == "cframetimer.cpp" else "qcxx"
        # The rename list is an input of every renamed object: a changed list must recompile them (the command line
        # names only its path, so ninja would otherwise keep objects renamed by the old list).
        implicit = [] if syntax or rule == "cc" else [allocator_syms(target, out)]
        if sdl_marker:
            implicit.append(sdl_marker)
        renamed = " | " + " ".join(map(str, implicit)) if implicit else ""
        lines.append(f"build {obj}: {rule} {s}{renamed}")
    if link and not syntax:
        for s, rule, extra in port_sources(target):
            try:
                where = s.relative_to(PORT)				# platform/common/clock.cpp and platform/windows/clock.cpp
            except ValueError:
                where = Path("third-party") / s.name
            obj = out / "port" / (str(where) + ".o")
            objs.append(obj)
            lines.append(f"build {obj}: {rule} {s}" + (f" | {sdl_marker}" if sdl_marker else "") +
                         (f"\n  extra = {extra}" if extra else ""))
        if target == "windows":
            res = out / "port/platform/windows/utf8.rc.o"
            objs.append(res)
            lines.append(f"build {res}: res {RESOURCE} | {RESOURCE.with_name('utf8.manifest')}")
            # bea.exe's version resource (WinMain reads it; platform/windows/version.rc), not the launcher's.
            ver = out / "port/platform/windows/version.rc.o"
            objs.append(ver)
            lines.append(f"build {ver}: res {RESOURCE.with_name('version.rc')}")
        exe = out / ("bea" if target == "linux" else "bea.exe")
        lines.append(f"build {exe}: link {' '.join(str(o) for o in objs)}" + (f" | {sdl_marker}" if sdl_marker else ""))
    out.mkdir(parents=True, exist_ok=True)
    path = out / ("syntax.ninja" if syntax else "build.ninja")
    write_if_changed(path, ("\n".join(lines) + "\n").encode())
    return path


def census(log: str):
    errors = re.findall(r"^(\S+?):(\d+):\d+: (?:fatal )?error: (.*)$", log, re.M)
    sites = {(f, l, m) for f, l, m in errors}
    kinds = Counter(re.sub(r"'[^']*'", "'X'", m) for _, _, m in sites)
    files = Counter(os.path.basename(f) for f, _, _ in sites)
    print(f"census: {len(sites)} unique error sites")
    for k, n in kinds.most_common(40):
        print(f"  {n:5d}  {k}")
    print("  files:", ", ".join(f"{f} {n}" for f, n in files.most_common(25)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", default="linux", choices=["linux", "windows", "all"])
    ap.add_argument("--syntax", action="store_true")
    ap.add_argument("-k", action="store_true")
    ap.add_argument("-j", type=int, default=os.cpu_count())
    ap.add_argument("--system-clang", action="store_true",
                    help="Linux only: compile with the system clang instead of the pinned release (the cross-compiler check;\n                    use a separate worktree, as its objects differ)")
    ap.add_argument("--public", action="store_true",
                    help="the public build: NvTriStrip compiled out (port/platform/public); use a separate worktree, as its\n"
                         "objects differ")
    ap.add_argument("--ce-version", default="dev", help="the public build's version (Onslaught CE VX.Y; the release tag)")
    ap.add_argument("--commander-test", action="store_true",
                    help="the commander order prototype: a test build, never shipped (its source is not in the public tree);\n"
                         "use a separate worktree, as its objects differ")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    if a.system_clang:
        use_system_clang()
    if a.public:
        global PUBLIC
        PUBLIC = True
        if not re.fullmatch(r"[0-9A-Za-z.+-]+", a.ce_version):
            raise SystemExit(f"--ce-version {a.ce_version!r}: letters, digits and . + - only")
        if a.target in ("windows", "all"):
            try:
                launcher_version_words(a.ce_version)
            except ValueError as error:
                raise SystemExit(f"--ce-version {a.ce_version!r}: {error}") from error
        CE_DEFINES.append(f"'-DCE_VERSION=\"{a.ce_version}\"'")
        CE_DEFINES.append("-DBEA_PUBLIC_GRAPHICS_HOST=1")
        # No machine paths in the shipped binary (__FILE__ in assertions and logs, debug information): this checkout,
        # its mirror and build, and the shared dependency directory become relative names.
        maps = [f"-ffile-prefix-map={p}={n}" for p, n in ((MIRROR_ROOT, "mirror"), (BUILD, "build"), (deps.DATA, "deps"),
                                                           (ROOT, "."))]
        for t in TARGETS.values():
            t["cxx"] += maps
            t["cc"] += maps
    if a.commander_test:
        CE_DEFINES.append("-DCE_COMMANDER_TEST=1")
    sources = build_mirror()
    if a.files:
        want = {f.lower().replace("\\", "/").removeprefix("src/") for f in a.files}
        # linklib/ sources are outside MIRROR (src/): compare those by their path under MIRROR_ROOT only.
        sources = [s for s in sources if (s.is_relative_to(MIRROR) and str(s.relative_to(MIRROR)).lower() in want)
                   or str(s.relative_to(MIRROR_ROOT)).lower() in want]
    rc = 0
    for target in (["linux", "windows"] if a.target == "all" else [a.target]):
        check_compiler(target)
        full_public = PUBLIC and not a.syntax and not a.files
        with deps.sdl_install_lock(target) if full_public else nullcontext():
            marker = None
            if full_public:
                deps.require_sdl_receipt(target)
                marker = BUILD / target / ".sdl3-input.sha256"
                deps.write_cache_if_changed(marker, (deps.sdl_receipt_sha256(target) + "\n").encode())
                deps.write_cache_file(BUILD / target / ".bea-sdl3-link.json", b'{"status":"incomplete"}\n')
            nf = ninja_file(target, sources, a.syntax, link=not a.files, sdl_marker=marker)
            r = subprocess.run(["ninja", "-f", str(nf), "-j", str(a.j)] + (["-k", "0"] if a.k else []),
                               capture_output=a.k, text=True)
            if a.k:
                log = r.stdout + r.stderr
                (BUILD / target / "last.log").write_text(log)
                print(f"{target}: ninja exit {r.returncode}; log {BUILD / target / 'last.log'}")
                census(log)
                if r.returncode and not re.search(r"^\S+?:\d+:\d+: (?:fatal )?error:", log, re.M):
                    # A link or ninja failure has no compiler error site; show it rather than a clean census.
                    print("build failed outside the compiler:\n" + "\n".join(
                        l for l in log.strip().splitlines() if "error" in l and "LNK4217" not in l)[-2000:])
            rc = rc or r.returncode
            if full_public and r.returncode == 0:
                launcher_rc = build_launcher(target, a.ce_version)
                rc = rc or launcher_rc
                if launcher_rc == 0:
                    receipt = deps.sdl_link_receipt(BUILD, target)
                    deps.write_cache_file(BUILD / target / ".bea-sdl3-link.json",
                                          (json.dumps(receipt, sort_keys=True) + "\n").encode())
    return rc


def source_commit():
    """The launcher's build name for logs, session tapes and problem reports: this checkout's commit (12 digits), with
    +dirty when tracked files differ from it; "unknown" outside a Git checkout."""
    r = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short=12", "HEAD"], capture_output=True, text=True)
    if r.returncode or not r.stdout.strip():
        return "unknown"
    dirty = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no"],
                           capture_output=True, text=True).stdout.strip()
    return r.stdout.strip() + ("+dirty" if dirty else "")


# Both Windows programs embed one application manifest (port/platform/windows/utf8.manifest): activeCodePage UTF-8,
# so the A functions (the game's file and folder calls, the launcher's environment and paths) take UTF-8 and any
# user or game folder name works, as on Linux. windres writes no time stamp (deterministic).
RESOURCE = PORT / "platform/windows/utf8.rc"
WINDRES = deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-windres"


def manifest_object(out: Path) -> Path:
    """utf8.rc compiled to out (for the launcher; bea.exe's comes from its ninja file)."""
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(WINDRES), "--target=pe-i386", "-I", str(PORT / "platform/windows"), "-O", "coff", str(RESOURCE),
                    "-o", str(out)], check=True)
    return out


def launcher_version_words(version: str) -> tuple[int, int, int, int]:
    """Windows fixed file version: the numeric release core, zero-padded; named development builds use zero.
    The full label, including prerelease/build suffixes, remains in the resource's FileVersion/ProductVersion strings.
    Reject numeric versions that cannot fit four WORDs instead of truncating or wrapping them."""
    if re.fullmatch(r"[A-Za-z][0-9A-Za-z.+-]*", version):
        return (0, 0, 0, 0)
    match = re.fullmatch(r"([0-9]+(?:\.[0-9]+){0,3})(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
                         r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?", version)
    if not match:
        raise ValueError("use a named development build or one to four numeric parts with an optional -prerelease/+build")
    parts = [part.lstrip("0") or "0" for part in match[1].split(".")]
    if any(len(part) > 5 or int(part) > 65535 for part in parts):
        raise ValueError("Windows file-version parts must be between 0 and 65535")
    return tuple([int(part) for part in parts] + [0] * (4 - len(parts)))


def launcher_version_object(out: Path, version: str) -> Path:
    """The launcher's own CE version resource; bea.exe keeps its separate retail 1.0.0.0 resource."""
    words = ",".join(map(str, launcher_version_words(version)))
    out.parent.mkdir(parents=True, exist_ok=True)
    # A generated header preserves the quoted label through windres's preprocessor invocation.
    (out.parent / "launcher-version.h").write_text(f'#define CE_VERSION_WORDS {words}\n#define CE_VERSION_TEXT "{version}"\n')
    subprocess.run([str(WINDRES), "--target=pe-i386", "-O", "coff", "-I", str(out.parent),
                    str(RESOURCE.with_name("launcher-version.rc")),
                    "-o", str(out)], check=True)
    return out


def build_launcher(target, version, out=None, defines=()):
    with deps.sdl_install_lock(target):
        deps.require_sdl_receipt(target)
        return _build_launcher(target, version, out, defines)


def _build_launcher(target, version, out=None, defines=()):
    """The public launcher: game-folder discovery, logging and the signed v2 updater (port/updater/UPDATER-V2.md). It
    links SDL3 (dialogs), Monocypher (Ed25519) and zlib (unpacking), and on Windows WinHTTP and BCrypt. Linux:
    build/linux/onslaught-ce-launch; Windows: build/windows/onslaught-ce.exe. `out` and `defines` are for the updater
    tests' builds only (CE_UPDATER_TESTING with a test key); package.py refuses such a launcher."""
    mc = deps.SRC / "monocypher-4.0.2/src"
    if not mc.is_dir():
        deps.unpack(deps.lock()["monocypher"], deps.SRC)
    src = [PORT / "updater/ce_launch.c", PORT / "updater/sha256.c", mc / "monocypher.c", mc / "optional/monocypher-ed25519.c"]
    flags = ["-std=gnu11", "-O2", "-Wall", f'-DCE_VERSION="{version}"', f'-DCE_COMMIT="{source_commit()}"', f"-I{mc}",
             f"-I{mc / 'optional'}", f"-I{PORT / 'updater'}"] + list(defines)
    if target == "linux":
        out = out or BUILD / "linux/onslaught-ce-launch"
        cmd = TARGETS["linux"]["cc"] + ["-m32"] + flags + [f"-I{deps.PREFIX / 'include'}"] + [str(s) for s in src] + \
              [f"-L{deps.PREFIX / 'lib'}", "-lSDL3", "-lz", "-Wl,-rpath,$ORIGIN/lib", "-o", str(out)] + \
              deps.sysroot_link_flags()
    else:
        out = out or BUILD / "windows/onslaught-ce.exe"
        res = manifest_object(Path(out).parent / "launcher-utf8.rc.o")
        ver = launcher_version_object(Path(out).parent / "launcher-version.rc.o", version)
        cmd = TARGETS["windows"]["cc"] + ["-mwindows"] + flags + [f"-I{deps.PREFIX_WIN / 'include'}"] + [str(s) for s in src] + [str(res), str(ver)] + \
              [f"-L{deps.PREFIX_WIN / 'lib'}", "-lSDL3", "-lz", "-lwinhttp", "-lbcrypt", "-ladvapi32", "-lshell32",
               "-static-libgcc", "-Wl,--no-insert-timestamp", "-o", str(out)]
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(cmd)
    print(f"{target}: launcher {'built' if r.returncode == 0 else 'FAILED'}")
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
