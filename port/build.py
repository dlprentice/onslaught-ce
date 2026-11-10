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
import ice_deps  # noqa: E402
import network_source_id  # noqa: E402

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
    # Match the source-identity helper's explicit-root index authority. A caller's alternate index or
    # worktree must not select different compiler inputs from those bound into network compatibility.
    env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    env.update(GIT_OPTIONAL_LOCKS="0", GIT_NO_LAZY_FETCH="1", GIT_CONFIG_NOSYSTEM="1",
               GIT_CONFIG_GLOBAL=os.devnull, GIT_LITERAL_PATHSPECS="1")
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z", "src", "linklib"],
                         env=env, capture_output=True, check=True).stdout
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


def network_view(text, off=False):
    """The text with its CE_NETWORK_OFF conditionals resolved: off=True as a --public build sees it (the network's hooks gone, the
    original code kept), off=False as a build with the network sees it. Other conditionals are left alone. Tests read patched source
    through this when they look for the text a hook produces."""
    out, stack = [], []        # None: an ordinary conditional; [kept]: a CE_NETWORK_OFF one
    for line in text.split("\n"):
        s = line.strip()
        if re.match(r"#\s*ifndef\s+CE_NETWORK_OFF\b", s):
            stack.append([not off])
            continue
        if re.match(r"#\s*ifdef\s+CE_NETWORK_OFF\b", s):
            stack.append([off])
            continue
        if re.match(r"#\s*if", s):
            stack.append(None)
        elif re.match(r"#\s*else\b", s) and stack and stack[-1] is not None:
            stack[-1][0] = not stack[-1][0]
            continue
        elif re.match(r"#\s*endif\b", s) and stack:
            if stack.pop() is not None:
                continue
        if all(e is None or e[0] for e in stack):
            out.append(line)
    if stack:
        raise ValueError("unbalanced conditionals")
    return "\n".join(out)


NETWORK_CODE = (".c", ".cpp", ".h", ".hpp", ".inl", ".inc")


def resolve_network_off(paths):
    """The default public build (no network): each of these mirror files that holds a CE_NETWORK_OFF conditional is rewritten with it resolved
    (network_view, off=True), so the compiler is given main's text itself, line for line. Defining CE_NETWORK_OFF alone would leave the
    network's guarded lines in the file: the preprocessor skips them but still counts them, which moves every __LINE__ after a hook (the
    game's new(...) passes one to the allocator) and every line table in the debug information."""
    for path in paths:
        if path.suffix.lower() not in NETWORK_CODE or not path.is_file():
            continue
        raw = path.read_bytes()
        if b"CE_NETWORK_OFF" in raw:
            write_if_changed(path, network_view(raw.decode("latin-1"), off=True).encode("latin-1"))


RESOLVED = BUILD / "resolved"
VIEW = RESOLVED / "port"       # a default public build's copy of the port's code (build_port_view)
VIEW_SKIPPED = {"tools", "tests", "network", "nettest-kit", "patches", "deps-patches"}


def default_flavour():
    """build.py --public without --with-network: the build that must be main's (nothing of the network in it)."""
    return PUBLIC and not WITH_NETWORK


def build_port_view():
    """The default public build compiles a copy of the port's code with its CE_NETWORK_OFF conditionals resolved, under RESOLVED/port: every
    file holds main's text line for line, which the preprocessor alone cannot give (it skips the network's lines but still counts them: every
    __LINE__ after a hook and every line table in the debug information moves, and so does the linker's hash of the image that the stripped
    program still carries). Files that hold no conditional are copied as they are. RESOLVED is mapped to "." like the checkout, and last in
    the list, because the compiler's debug-information map takes the last entry that matches (the checkout's own entry would rename the view's
    include directories in the line tables), so __FILE__ and the debug names read as the original files'. The network's own directory and the tools are not copied: a
    default build that reached for one would not find it."""
    keep = set()
    for path in sorted(PORT.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in NETWORK_CODE:
            continue
        rel = path.relative_to(PORT)
        if set(rel.parts[:-1]) & VIEW_SKIPPED:
            continue
        raw = path.read_bytes()
        if b"CE_NETWORK_OFF" in raw:
            raw = network_view(raw.decode("latin-1"), off=True).encode("latin-1")
        write_if_changed(VIEW / rel, raw)
        keep.add(VIEW / rel)
    for stale in sorted(VIEW.rglob("*"), reverse=True):
        if stale.is_file() and stale not in keep:
            stale.unlink()


def cc_path(path):
    """A port file or include directory as the compiler is given it: in a default public build the copy under VIEW."""
    path = Path(path)
    if default_flavour() and path.is_relative_to(PORT) and not (set(path.relative_to(PORT).parts) & VIEW_SKIPPED):
        return VIEW / path.relative_to(PORT)
    return path


def mirror_text(name, root=ROOT):
    """root's src/NAME as the mirror holds it before any patch: include names and their case normalised as build_mirror does. The
    patches' context lines are written against that text ("common.h", not the original's "Common.h"), so a test that applies them to a
    copy of src/ starts from this and not from the raw file."""
    SYSTEM_NAMES.update(system_header_names())
    rows = [l.split(maxsplit=1) for l in (ROOT / "config/game-data.sha256").read_text().splitlines() if l.strip()]
    targets = ([MIRROR_ROOT / f.relative_to(ROOT) for f in tracked_sources()] + [MIRROR / r for _, r in rows] +
               [MIRROR / "data/NVShaderFragments.inc"])
    raw = (Path(root) / "src" / name).read_bytes().decode("latin-1")
    return normalise(raw, MIRROR_ROOT / "src", CaseIndex(targets), [MIRROR, MIRROR / "MissionScript"]).encode("latin-1")


def patched_text(names, skip=(), network=None):
    """{name: text} of src/NAME in the mirror's form with every patch's section for it applied in the build's order at fuzz 0 (skip:
    patch file names left out). network None leaves the CE_NETWORK_OFF conditionals in; False resolves them as a --public build sees the
    text, True as a build with the network sees it (network_view). Raises RuntimeError naming the patch that does not apply."""
    import tempfile
    names = list(names)
    scratch = ROOT / "local-data"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ce-patched-", dir=scratch) as tmp:
        for name in names:
            (Path(tmp) / name).write_bytes(mirror_text(name))
        for patch in sorted((PORT / "patches").glob("*.patch")):
            if patch.name in skip:
                continue
            text = patch.read_text(encoding="latin-1")
            sections = [part for part in re.split(r"(?=^--- a/)", text, flags=re.M)
                        if any(part.startswith(f"--- a/{name}\n") for name in names)]
            if not sections:
                continue
            done = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", tmp], input="".join(sections).encode("latin-1"),
                                  capture_output=True)
            out = (done.stdout + done.stderr).decode("latin-1")
            if done.returncode or "fuzz" in out.lower():
                raise RuntimeError(f"{patch.name} does not apply to {', '.join(names)}:\n{out}")
        texts = {name: (Path(tmp) / name).read_bytes().decode("latin-1") for name in names}
    return texts if network is None else {name: network_view(text, off=not network) for name, text in texts.items()}


def network_module(source):
    """The files that exist only for the network (their callers' hooks are guarded by CE_NETWORK_OFF)."""
    path = Path(source)
    return (path.parent == PORT / "network" or path.name in ("monocypher.c", "ce_netlobby.cpp", "ce_netcode.cpp",
                                                           "ce_netpages.cpp", "typeinfo_bridge.cpp", "private_ui_input.cpp")
            or (path.parent == PORT / "game" and path.name.startswith("ce_network_")))


ICE_BINARY_TAG = "CE-NET-FEATURE:ICE-LIBJUICE:1"
NATIVE_SIGNALING_TAG = "CE-NATIVE-SIGNALING-V1"
NATIVE_SIGNALING_SOURCES = frozenset(("signaling_provider.cpp", "signaling_https.cpp"))
TEXT_CHAT_SOURCES = ("text_chat.cpp", "text_chat_owner.cpp", "media_profile.cpp")


def ice_marker_source():
    """A loadable feature tag retained by the compiler and an explicit linker root, including under LTO/strip."""
    source = BUILD / "ice-feature.c"
    write_if_changed(source, ('__attribute__((used)) const char ce_network_ice_feature_marker[] = "' +
                             ICE_BINARY_TAG + '";\n').encode("ascii"))
    return source


def native_signaling_marker_source():
    """Only the explicit profile gets this retained, loadable binary tag."""
    source = BUILD / "native-signaling-feature.c"
    write_if_changed(source, ('__attribute__((used)) const char ce_network_native_signaling_feature_marker[] = "' +
                             NATIVE_SIGNALING_TAG + '";\n').encode("ascii"))
    return source


def native_signaling_backend(target):
    return {"linux": "libcurl-gnutls", "windows": "winhttp"}[target]


def native_signaling_dependency(target):
    """Use only the existing pinned SDK/import library; never a host curl fallback."""
    if target == "linux":
        if deps.SYSROOT_NAME != "sniper" or deps.SYSROOT is None:
            raise SystemExit("native signaling requires the pinned sniper SDK for Linux")
        base = deps.SYSROOT
        path = base / "usr/lib/i386-linux-gnu/libcurl-gnutls.so"
    elif target == "windows":
        base = deps.DATA / "llvm-mingw"
        path = base / "i686-w64-mingw32/lib/libwinhttp.a"
    else:
        raise ValueError("unsupported native signaling target")
    if not path.is_file() or not path.resolve().is_relative_to(base.resolve()):
        raise SystemExit("native signaling pinned HTTPS dependency is unavailable")
    return path


def write_ice_link_receipt(target):
    """Record the successful optional game/launcher link without private paths or repository identifiers."""
    if not (PUBLIC and WITH_NETWORK and WITH_ICE):
        raise ValueError("an ICE link receipt requires the public network ICE profile")
    library = ice_deps.require(target)
    row, install = ice_deps.pin(), ice_deps.prefix(target)

    def digest(path):
        with Path(path).open("rb") as stream:
            return hashlib.file_digest(stream, "sha256").hexdigest()

    def artifact(name):
        path = BUILD / target / name
        return dict(name=name, size=path.stat().st_size, sha256=digest(path))

    game = "bea.exe" if target == "windows" else "bea"
    launcher = "onslaught-ce.exe" if target == "windows" else "onslaught-ce-launch"
    game_bytes = (BUILD / target / game).read_bytes()
    if ICE_BINARY_TAG.encode("ascii") + b"\0" not in game_bytes:
        raise SystemExit("optional ICE game is missing its retained feature marker")
    native_marker = NATIVE_SIGNALING_TAG.encode("ascii") + b"\0" in game_bytes
    if native_marker != WITH_NATIVE_SIGNALING:
        raise SystemExit("native signaling game marker does not match the requested profile")
    del game_bytes
    dependency = json.loads(deps.read_cache_file(install / ".bea-ice-build.json"))
    receipt = dict(schema=2, target=target, profile="public-network-ice", source_id=NETWORK_SOURCE_ID,
                   source_patches=dependency["source_patches"], source_tree_sha256=dependency["source_tree_sha256"],
                   build_recipe_sha256=digest(Path(__file__)), marker=ICE_BINARY_TAG,
                   system_clang=SYSTEM_CLANG, sysroot_name=deps.SYSROOT_NAME,
                   test_build=any(flag.startswith(("-DCE_NET_TEST_AUTOMATION", "-DCE_COMMANDER_TEST", "-DCE_PLAYUNIT_TEST"))
                                  for flag in CE_DEFINES),
                   libjuice=dict(name=row["name"], version=row["version"], licence=row["licence"],
                                 source_sha256=row["sha"], source_url=row["url"],
                                 build_receipt_sha256=digest(install / ".bea-ice-build.json"),
                                 static_library_sha256=digest(library),
                                 header_sha256=digest(install / "include/juice/juice.h"),
                                 licence_text_sha256=digest(install / "licenses/MPL-2.0.txt")),
                   binaries=dict(game=artifact(game), launcher=artifact(launcher)))
    if WITH_NATIVE_SIGNALING:
        receipt["native_signaling"] = dict(enabled=True, marker=NATIVE_SIGNALING_TAG,
                                           https_backend=native_signaling_backend(target))
    deps.write_cache_file(BUILD / target / ".bea-ice-link.json",
                          (json.dumps(receipt, sort_keys=True) + "\n").encode())


def port_sources(target):
    """(source, rule, extra flags) for the port's own code and third-party sources linked into the program."""
    out = [(s, "dcxx", module_flags("d3dx").get(s.name, "")) for s in sorted((PORT / "d3dx/src").glob("*.cpp"))]
    # The modules port/dsound and port/bink replace the platform's stand-ins once they hold sources.
    replaced = {name for module, name in (("dsound", "dsound.cpp"), ("bink", "bink.cpp"))
                if any((PORT / module).glob("*.cpp"))}
    for module in ("dsound", "bink"):
        flags = module_flags(module)
        out += [(s, "pcxx", flags.get(s.name, "")) for s in sorted((PORT / module).glob("*.cpp"))]
    # Transport initialization remains explicit. Its buffers use the host allocator, not the game's private heap.
    mc = deps.SRC / "monocypher-4.0.2/src"
    if not (mc / "monocypher.c").is_file():
        deps.unpack(deps.lock()["monocypher"], deps.SRC)
    # Native OS ABI bridges override only their own layout flags; shared network structures keep pcxx's ABI.
    network_flags = module_flags("network")
    ice_include = f" -I{ice_deps.prefix(target) / 'include'}" if WITH_ICE else ""
    out += [(s, "pcxx", f"-I{mc}" + ice_include + network_identity_flag(s) + " " + network_flags.get(s.name, ""))
            for s in sorted((PORT / "network").glob("*.cpp"))
            if WITH_NATIVE_SIGNALING or s.name not in NATIVE_SIGNALING_SOURCES]
    if WITH_TEXT_CHAT:
        out += [(PORT / "network/optional" / name, "pcxx", f"-I{mc} -I{PORT / 'network'}" + ice_include)
                for name in TEXT_CHAT_SOURCES]
    out += [(mc / "monocypher.c", "pcc", "")]
    for d in ("common", target):
        out += [(s, "pcxx", network_identity_flag(s).strip()) for s in sorted((PORT / "platform" / d).glob("*.cpp"))
                if not (d == "common" and s.name in replaced)]
    # Game-object hooks keep the game's flags and allocator renames. HOST_BUFFER_GAME_MODULES (below) are the standalone
    # units beside them that own host STL buffers and native handles: they are compiled as port code and never see those renames.
    # The chat overlay is a game-object hook that also handles SDL events; the Windows toolchain has no SDL of its own.
    game_extra = {"ce_network_chat_ui.cpp": f"-I{cc_path(deps.PREFIX_WIN / 'include')}"} if target == "windows" else {}
    # The museum's room writer takes two class identifiers from the game's oids.h, a header that holds only an enumeration.
    # -iquote lets the mirror answer quoted includes and nothing else: as -I it would also stand before the system directories
    # for every <angle> include of the standard library.
    host_extra = {"ce_museum_visit_generate.cpp": f"-iquote{MIRROR}"}
    out += [(s, 'pcxx' if s.name in HOST_BUFFER_GAME_MODULES else 'qcxx', host_extra.get(s.name, game_extra.get(s.name, "")))
            for s in sorted((PORT / "game").glob("*.cpp"))]
    if PUBLIC:
        # The default public build has no network (CE_NETWORK_OFF, defined below): none of its modules or hooks are compiled.
        # --with-network leaves those modules and hooks in, as a private build has them.
        if not WITH_NETWORK:
            out = [o for o in out if not network_module(o[0])]
            out = [(cc_path(src), rule, extra) for src, rule, extra in out]
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
    # SHA-256 for the mod checks (platform/common/mods.cpp) and, in a build with the network, the join code and handoff
    # (platform/common/ce_netcode.cpp): the launcher's own implementation, compiled once for both.
    out += [(PORT / "updater/sha256.c", "pcc", "")]
    if WITH_ICE:
        out += [(ice_marker_source(), "pcc", "")]
    if WITH_NATIVE_SIGNALING:
        out += [(native_signaling_marker_source(), "pcc", "")]
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
    network = ("-lenet " if target == "linux" else "-lenet -lws2_32 ") if network_compiled_in() else ""
    if WITH_ICE:
        # Name the verified static archive explicitly: a host library cannot satisfy a missing optional input.
        network += shlex.quote(str(ice_deps.require(target))) + " " + ("-lbcrypt " if target == "windows" else "")
        network += "-Wl,--undefined=" + ("_" if target == "windows" else "") + "ce_network_ice_feature_marker "
    if WITH_NATIVE_SIGNALING:
        dependency = native_signaling_dependency(target)
        # Link the checked file itself: an earlier -L prefix must not replace it.
        network += shlex.quote(str(dependency)) + " "
        network += "-Wl,--undefined=" + ("_" if target == "windows" else "") + "ce_network_native_signaling_feature_marker "
    if target == "linux":
        dxvk = deps.DATA / "dxvk-native/usr/lib32"
        lib = deps.PREFIX / "lib"
        # The public build finds its bundled libraries beside it ($ORIGIN/lib; ninja's $$ and the shell's quotes keep it).
        rpath = "'$$ORIGIN/lib'" if PUBLIC else f"{lib}:{dxvk}"
        lld = "".join(f + " " for f in deps.sysroot_link_flags())
        return (f"-m32 {lld}{defsyms} -L{lib} -L{dxvk} -Wl,-rpath,{rpath} -no-pie -ldxvk_d3d9 -lSDL3 {vorbis_lib(target)} -lz "
                f"{ffmpeg} {network}-lpthread -ldl -lm")
    lib = deps.PREFIX_WIN / "lib"
    return (f"-mwindows -Wl,--no-insert-timestamp {defsyms} -L{lib} -lSDL3 {vorbis_lib(target)} -lz {ffmpeg} {'-lbcrypt ' if ffmpeg or not PUBLIC else ''}"
            f"-ld3d9 -ldxguid -luuid -lwinmm -lgdi32 -luser32 {network}"
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
# The port/game units compiled as port code (pcxx), not with the game's flags and renames (qcxx). Each owns host STL buffers, exceptions
# or native handles, so the operator new its containers instantiate must stay the host's: the game rule renames it to the game's private
# allocator, which only a translation unit that includes MemoryManager.h defines, and the link then finds no definition. The museum's
# room writer and cache provider are two of them; its engine adapter (ce_museum_visit_engine.cpp) reads game objects and keeps the game
# rule. Only plain records and callbacks cross between them, and ce_museum_visit.h asserts their layout under both rules.
HOST_BUFFER_GAME_MODULES = frozenset(("ce_network_boot_authority.cpp", "ce_network_invitation.cpp", "ce_project.cpp",
                                      "ce_museum_visit.cpp", "ce_museum_visit_generate.cpp"))


def network_compiled_in():
    """The network's modules, hooks and libraries belong to this build: every private build, and the public build made with --with-network. The default
    public build has none of it (CE_NETWORK_OFF)."""
    return not PUBLIC or WITH_NETWORK


def objcopy(target):
    if target == "windows":
        return str(deps.DATA / "llvm-mingw/bin/llvm-objcopy")
    return "llvm-objcopy" if SYSTEM_CLANG else str(LINUX_LLVM / "llvm-objcopy")


SYSTEM_CLANG = False
PUBLIC = False
WITH_NETWORK = False  # build.py --public --with-network: the public build with the network compiled in (the experimental co-op build)
WITH_ICE = False  # explicit --public --with-network --with-ice only; never an ordinary dependency or package
WITH_NATIVE_SIGNALING = False  # explicit HTTPS profile layered on the optional public network ICE build
WITH_TEXT_CHAT = False  # optional bounded network text transport and UI; never part of the default build
NETWORK_SOURCE_ID = None
TEST_AUTOMATION = False  # build.py --test-automation (private network test builds; never with --public)
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


def configure_network_identity():
    """Bind the fixed public-source profile; other builds remain usable offline, with networking refused."""
    global NETWORK_SOURCE_ID
    NETWORK_SOURCE_ID = None
    if (PUBLIC or TEST_AUTOMATION) and not SYSTEM_CLANG and deps.SYSROOT_NAME == "sniper":
        identity = (network_source_id.compute_source_id(ROOT, with_ice=True) if WITH_ICE else
                    network_source_id.compute_source_id(ROOT))
        if not re.fullmatch(r"[0-9a-f]{64}", identity):
            raise ValueError("network source identity must be a SHA-256 hex digest")
        if TEST_AUTOMATION:
            # A test-automation build admits only its own kind: never a public or release build of the same source.
            identity = hashlib.sha256(b"CE_NET_TEST_AUTOMATION\0" + identity.encode()).hexdigest()
        NETWORK_SOURCE_ID = identity


def network_identity_flag(source):
    # Only the owning translation units consume the ID (the content admission, and the lobby's build check,
    # platform/common/ce_netlobby.cpp). Updating it need not rebuild unrelated game objects.
    if source.name in ("compatibility.cpp", "ce_netlobby.cpp") and NETWORK_SOURCE_ID is not None:
        return f" '-DCE_NETWORK_SIMULATION_ID=\"{NETWORK_SOURCE_ID}\"'"
    return ""


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
    inc = [cc_path(p) for p in [PORT / "d3dx/include", PORT / "include", PORT / "game"] + t["includes"]] + [MIRROR, MIRROR / "MissionScript"]
    incflags = " ".join(f"-I{p}" for p in inc)
    common = " ".join(COMMON + X87 + NO_MATH_BUILTINS + CE_DEFINES) + f" -include {cc_path(PORT / 'include/port.h')} {incflags}"
    mode = "-fsyntax-only" if syntax else "-c -o $out"
    out.mkdir(parents=True, exist_ok=True)
    rename = "" if syntax else f" && {objcopy(target)} --redefine-syms={allocator_syms(target, out)} $out"
    sdl = deps.PREFIX if target == "linux" else deps.PREFIX_WIN
    # The port's own code: C++17, struct layout and 2-byte wchar_t as the game (shared structures have one layout).
    portinc = " ".join(f"-I{cc_path(p)}" for p in [PORT / "d3dx/include", PORT / "include"] + t["includes"] +
                       [sdl / "include", deps.DATA / "src" / NVTRISTRIP / "include", deps.DATA / "src" / NVTRISTRIP / "src"])
    # (No -malign-double: libstdc++'s <cmath> rejects it, and no structure shared with the game holds a double.)
    layout = "-DNOMINMAX -fms-extensions -fno-autolink -fno-strict-aliasing -fwrapv -mms-bitfields -fshort-wchar -g -O2 -w"
    lines = [f"builddir = {out}", f"cxx = {' '.join(t['cxx'])}", f"cc = {' '.join(t['cc'])}",
             f"rule cxx\n  command = $cxx -std=gnu++14 {" ".join(MSCXX)} {common} -MD -MF $out.d {mode} $in{rename} && touch $out\n"
             "  depfile = $out.d\n  deps = gcc\n  description = C++ $in",
             # /QIfist (tools/qifist.py): unoptimised IR, float-to-integer conversions rounded as fistp does, then
             # optimised and compiled. Every game C++ file but cframetimer.cpp, which retail built without /QIfist.
             f"rule qcxx\n  command = $cxx -std=gnu++14 {" ".join(MSCXX)} {common} $extra -MD -MF $out.d -S -emit-llvm "
             f"-Xclang -disable-llvm-passes -o $out.ll $in && {sys.executable} {PORT / 'tools/qifist.py'} $out.ll > /dev/null "
             f"&& $cxx {' '.join(COMMON + X87)} -c -o $out $out.ll{rename} && rm -f $out.ll\n"
             "  depfile = $out.d\n  deps = gcc\n  description = C++ /QIfist $in",
             f"rule cc\n  command = $cc -std=gnu99 {common} -MD -MF $out.d {mode} $in && touch $out\n"
             "  depfile = $out.d\n  deps = gcc\n  description = C $in",
             # (d3dx takes none of CE_DEFINES but the network's: its Windows texture loader holds a guarded hook)
             f"rule dcxx\n  command = $cxx -std=gnu++17 {layout} {' '.join(X87)} {' '.join(d for d in CE_DEFINES if d.startswith('-DCE_NETWORK_OFF'))} "
             f"$extra {portinc} -MD -MF $out.d -c -o $out $in\n"
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
            if s.is_relative_to(PORT):
                where = s.relative_to(PORT)				# platform/common/clock.cpp and platform/windows/clock.cpp
            elif s.is_relative_to(VIEW):
                where = s.relative_to(VIEW)				# a default build's copy of the port file (build_port_view): the same object name
            else:
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
        link_inputs = ([sdl_marker] if sdl_marker else []) + ([ice_deps.require(target)] if WITH_ICE else [])
        if WITH_NATIVE_SIGNALING:
            link_inputs.append(native_signaling_dependency(target))
        lines.append(f"build {exe}: link {' '.join(str(o) for o in objs)}" +
                     (" | " + " ".join(map(str, link_inputs)) if link_inputs else ""))
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
    ap.add_argument("--with-network", action="store_true",
                    help="with --public: the public build with the network compiled in (Host / Join and the lockstep co-op, experimental); the\n"
                         "default public build has none of it; use a separate worktree, as its objects differ")
    ap.add_argument("--with-ice", action="store_true",
                    help="with --public --with-network: compile the optional ICE session bridge; requires ice_deps.py build;\n"
                         "use a separate worktree; packaging requires package.py --with-ice and exact build receipts;\n"
                         "Windows ICE requests remain refused")
    ap.add_argument("--with-native-signaling", action="store_true",
                    help="with --public --with-network --with-ice: compile the optional native HTTPS signaling client;\n"
                         "does not select a provider or load credentials; packaging requires separate explicit opt-in")
    ap.add_argument("--with-text-chat", action="store_true",
                    help="with networking compiled in: compile the optional bounded in-game text chat")
    ap.add_argument("--test-automation", action="store_true",
                    help="private network test build only: authored BEA_PORT_FAKE_KEYS/FAKE_CLICK input counts as idle at\n"
                         "network admission (CE_NET_TEST_AUTOMATION); refused with --public")
    ap.add_argument("--commander-test", action="store_true",
                    help="adds the commander mode's test machinery (an order tape, a probe, a proof switch and diagnostics) to a build: for tests, never shipped;\n"
                         "use a separate worktree, as its objects differ")
    ap.add_argument("--play-unit-test", action="store_true",
                    help="adds the play-as-a-unit option's test machinery (a probe, a proof switch, god mode for the Battle Engine and a control) to a build: for tests, never shipped;\n"
                         "use a separate worktree, as its objects differ")
    ap.add_argument("--mods-windows-test", action="store_true",
                    help="the Windows build with the mod loader switched ON (PORT_MODS_WINDOWS_ON): for the gate's mods cases and the Wine tests, never shipped while mods are\n"
                         "off on Windows; use a separate worktree, as its objects differ")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    if a.with_native_signaling and not (a.public and a.with_network and a.with_ice):
        raise SystemExit("--with-native-signaling requires --public --with-network --with-ice")
    if a.with_network and not a.public:
        raise SystemExit("--with-network is for the public build: a private build has the network already")
    if a.with_ice and not (a.public and a.with_network):
        raise SystemExit("--with-ice requires --public --with-network")
    if a.test_automation:
        if a.public:
            raise SystemExit("--test-automation is for private network test builds; it cannot be combined with --public")
        global TEST_AUTOMATION
        TEST_AUTOMATION = True
        CE_DEFINES.append("-DCE_NET_TEST_AUTOMATION=1")
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
        # The network (Host / Join, the engineering session, the lockstep co-op) is compiled out of the default public build:
        # CE_NETWORK_OFF removes its hooks from the game's own code and port_sources leaves out its modules (network_module).
        # --with-network is the build that offers them.
        if a.with_network:
            global WITH_NETWORK
            WITH_NETWORK = True
        else:
            CE_DEFINES.append("-DCE_NETWORK_OFF=1")
        if a.with_ice:
            global WITH_ICE
            WITH_ICE = True
            CE_DEFINES.extend(["-DCE_NETWORK_ICE=1", "-DJUICE_STATIC=1"])
        if a.with_native_signaling:
            global WITH_NATIVE_SIGNALING
            WITH_NATIVE_SIGNALING = True
            CE_DEFINES.append("-DCE_NETWORK_SIGNALING=1")
        # No machine paths in the shipped binary (__FILE__ in assertions and logs, debug information): this checkout,
        # its mirror and build, and the shared dependency directory become relative names.
        maps = [f"-ffile-prefix-map={p}={n}" for p, n in ((MIRROR_ROOT, "mirror"), (BUILD, "build"), (deps.DATA, "deps"),
                                                           (ROOT, "."), (RESOLVED, "."), *deps.real_prefix_maps())]
        for t in TARGETS.values():
            t["cxx"] += maps
            t["cc"] += maps
    if a.commander_test:
        CE_DEFINES.append("-DCE_COMMANDER_TEST=1")
    if a.play_unit_test:
        CE_DEFINES.append("-DCE_PLAYUNIT_TEST=1")
    if a.mods_windows_test:
        CE_DEFINES.append("-DPORT_MODS_WINDOWS_ON=1")
    if a.with_text_chat:
        if not network_compiled_in():
            raise SystemExit("--with-text-chat requires networking compiled in")
        global WITH_TEXT_CHAT
        WITH_TEXT_CHAT = True
        CE_DEFINES.append("-DCE_NETWORK_TEXT=1")
    if WITH_NATIVE_SIGNALING:
        for target in (["linux", "windows"] if a.target == "all" else [a.target]):
            native_signaling_dependency(target)
    sources = build_mirror()
    if default_flavour():
        resolve_network_off(sorted(MIRROR_ROOT.rglob("*")))
        build_port_view()
    configure_network_identity()
    if PUBLIC and not a.syntax and not a.files:
        # which public flavour this build folder holds: the packager reads it to carry (or refuse) ENet's licence and notice
        write_if_changed(BUILD / "flavour.txt", b"public-network-ice\n" if WITH_ICE else
                         b"public-network\n" if WITH_NETWORK else b"public\n")
    if a.files:
        want = {f.lower().replace("\\", "/").removeprefix("src/") for f in a.files}
        # linklib/ sources are outside MIRROR (src/): compare those by their path under MIRROR_ROOT only.
        sources = [s for s in sources if (s.is_relative_to(MIRROR) and str(s.relative_to(MIRROR)).lower() in want)
                   or str(s.relative_to(MIRROR_ROOT)).lower() in want]
    rc = 0
    for target in (["linux", "windows"] if a.target == "all" else [a.target]):
        check_compiler(target)
        full_public = PUBLIC and not a.syntax and not a.files
        with deps.sdl_install_lock(target) if full_public else nullcontext(), \
             ice_deps.install_lock(target) if WITH_ICE else nullcontext():
            marker = None
            if full_public:
                deps.require_sdl_receipt(target)
                marker = BUILD / target / ".sdl3-input.sha256"
                deps.write_cache_if_changed(marker, (deps.sdl_receipt_sha256(target) + "\n").encode())
                deps.write_cache_file(BUILD / target / ".bea-sdl3-link.json", b'{"status":"incomplete"}\n')
                if WITH_ICE:
                    deps.write_cache_file(BUILD / target / ".bea-ice-link.json", b'{"status":"incomplete"}\n')
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
                    if WITH_ICE:
                        write_ice_link_receipt(target)
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
