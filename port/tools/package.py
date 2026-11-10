#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the public build (build.py --public) for release: a Linux i686 tarball and a Windows i686 zip.

    python3 port/tools/package.py VERSION [--out DIR]

Reads this checkout's local-data/port/build/{linux/bea,windows/bea.exe}, which must come from `build.py --public`
(no NvTriStrip symbols, RUNPATH $ORIGIN/lib). Writes, into DIR (default local-data/port/release/VERSION):
  onslaught-ce-VERSION-linux-i686.tar.gz   bea (stripped), lib/ (dxvk-native's d3d9, SDL3, libxkbcommon, FFmpeg), the launcher
                                           `onslaught-ce` with its preflight, licences and notices
  onslaught-ce-VERSION-windows-i686.zip    bea.exe (stripped), SDL3.dll, libz.dll, FFmpeg's DLLs, licences and notices
  ffmpeg-7.1.1.tar.xz                      FFmpeg's exact pinned source (deps.lock), whose shared libraries both bundle
  symbols/                                 the unstripped binaries, for symbolizing crash reports (not for upload)
No game files go in either package: the launcher reads the player's own install. Refuses a binary that still names
this machine's paths: strings of each stripped file matched against the user's home, the repository and the private
locations listed in BEA_PRIVATE_PATHS (colon-separated; set it to this machine's scratch and archive folders).
ICE/libjuice requires the explicit --with-ice path, verified build receipts, and the pinned source and licence inputs.
"""
import argparse
import gzip
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import textwrap
import time
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import deps  # noqa: E402

PORT = deps.PORT
ROOT = PORT.parent
WORK = deps.WORK
LINUX_LIBS = [(deps.DATA / "dxvk-native/usr/lib32", "libdxvk_d3d9.so.0"), (deps.PREFIX / "lib", "libSDL3.so.0"),
              (deps.PREFIX / "lib", "libxkbcommon.so.0")]
LINUX_LIBS += [(path.parent, path.name) for path in deps.ffmpeg_shared_files("linux", runtime=True)]
WINDOWS_DLLS = [deps.PREFIX_WIN / "bin/SDL3.dll", deps.PREFIX_WIN / "bin/libz.dll", *deps.ffmpeg_shared_files("windows", runtime=True)]
# The newest glibc symbol version the Linux package's programs and libraries need (their .gnu.version_r, checked by
# glibc_needed when packaging): README.txt, preflight.sh, public/README.md and the release notes state it.
GLIBC = "2.44"
GLIBC_TEXT = (f"glibc {GLIBC} or newer, which for now means Arch and other rolling-release distributions (Debian 13 "
              "and Ubuntu 26.04 and older have an older glibc; ldd --version shows yours)")
if deps.SYSROOT:
    # The Steam Runtime 3 sysroot build (the default, port/PLAN.md "Old glibc"): measured 2.29 (pow, exp and log in bea,
    # SDL3 and libavutil); the sysroot's own glibc is 2.31.
    GLIBC = "2.29"
    GLIBC_TEXT = f"glibc {GLIBC} or newer (ldd --version shows yours)"
# The graphics floor, which on older distributions is what stops the game (port/PLAN.md "Old glibc"): dxvk 3.1.1
# (dxvk_instance.h, dxvk_device_info.cpp checkDeviceCompatibility) takes a device with Vulkan 1.3, its required
# extensions and 256 bytes of push constants. Seen refused: Intel's Mesa 24.1 driver (128 bytes, Fedora 40).
VULKAN_TEXT = ("a 32-bit Vulkan 1.3 graphics driver that offers what the game's graphics layer (dxvk 3.1) requires, "
               "among them VK_KHR_load_store_op_none, VK_KHR_maintenance5 and VK_KHR_maintenance6, and 256 bytes of push "
               "constants. The Mesa drivers of Ubuntu 22.04 (23.2) and Debian 12 (22.3) lack those extensions (Debian 12 "
               "has a newer Mesa in bookworm-backports), and Intel's Mesa 24.1 driver (Fedora 40) offers only 128 bytes "
               "of push constants")
# What the Linux package uses when it is there and does without when it is not (preflight.sh notes the X11 libraries on an X11
# session and libdecor on a Wayland one, and does not stop; port/updater/ce_update_v2.h runs curl; ce_launch.c's message boxes
# use zenity, or SDL's own X11 window).
LINUX_OPTIONAL = ("- Optional: curl (the update check and download use it), zenity (SDL shows the launcher's message boxes "
                  "with it, or in its own window without it), on an X11 session the 32-bit libXrandr, libXcursor, libXi and "
                  "libXfixes (SDL works without them, with fewer display features) and on a Wayland session the 32-bit "
                  "libdecor (without it the windows have no title bar on some desktops). The launcher only notes a missing "
                  "library.")
# dxvk's licence is not in the dxvk-native tarball; fetched from the release tag and checked.
DXVK_LICENSE = ("https://raw.githubusercontent.com/doitsujin/dxvk/v3.1.1/LICENSE",
                "a5cb1a6ded7d2d7e92d550ba28edd21be2d1d4044662b399887351023e30ce64")
# port/d3dx/src/bc.cpp is adapted from DirectXTex (MIT): its licence at the commit the file names.
DIRECTXTEX = ("https://raw.githubusercontent.com/microsoft/DirectXTex/1acf4ebe6330e06b7eb52e8db63d4f824ad77427/LICENSE",
              "c2cfccb812fe482101a8f04597dfc5a9991a6b2748266c47ac91b6a5aae15383")
# dxvk-native's libdxvk_d3d9.so.0 compiles in two MIT-licensed submodules of the dxvk v3.1.1 tree: their licences
# (deps.lock's "licence:" lines pin each by URL and SHA-256) at the submodule commits that tag records.
# Its other submodules, Khronos's SPIRV-Headers and Vulkan-Headers, are header-only (declarations and constants): no
# copy of them ships, so their notices are not carried (public/THIRD-PARTY.md records the decision).
# The port's licence: the FSF's official GPL-3.0 text (FFmpeg's COPYING.GPLv3 differs in its URLs).
GPL3 = ("https://www.gnu.org/licenses/gpl-3.0.txt", "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986")
# The Windows build links the MinGW-w64 runtime statically; its notices must ship (llvm-mingw 20260922 carries
# mingw-w64 15.0 headers, newer than the last release tag, so the file at a pinned master commit).
MINGW_RUNTIME = ("https://raw.githubusercontent.com/mingw-w64/mingw-w64/62891fa4bf507d674c8c48822b596560b7cb506d/"
                 "COPYING.MinGW-w64-runtime/COPYING.MinGW-w64-runtime.txt",
                 "1db8da07b436c68833c0673ffee3d9fcb2526047f3820b81661865dfedc79a1f")


ALLOW_UNREPLACED = False
# The public build made with build.py --public --with-network (the experimental co-op release) links ENet and has the game-side Monocypher: its package carries ENet's
# licence and notice, which the default public build, having neither, must not claim. package() sets this from the build folder's flavour.txt.
NETWORK = False
# The optional ICE build retains this tag in executable data, including after LTO and stripping. Check the binary,
# not only flavour.txt: a one-target build can overwrite that shared marker while retaining the other executable.
ICE_FEATURE_MARKER = b"CE-NET-FEATURE:ICE-LIBJUICE:1"
# While mods are off on Windows (port/platform/common/mods.cpp, PlatformOff), every public Windows program holds the line below, and a Windows build made with
# build.py --mods-windows-test (the mod loader switched on, for the gate's mods cases) does not: flavour.txt cannot tell the two apart. Every public program
# holds the mod layer's --check-mods command (mods.h), so a program that has the command and lacks the line is the test build; a test's authored stand-in,
# which is no CE program, has neither and is judged by the other checks. The commit that enables mods on Windows removes the line from mods.cpp, and this
# check with it.
MODS_CPP = PORT / "platform/common/mods.cpp"
MODS_WINDOWS_OFF = "off on Windows until the loader's checks on NTFS have passed"
MODS_CHECK_COMMAND = "--check-mods"


def mods_windows_switch_problem(program: bytes, mods_source: str):
    """Why a Windows program must not be packaged as a release, or None: the mod loader switched on while the source keeps mods off on Windows."""
    if MODS_WINDOWS_OFF in mods_source and MODS_CHECK_COMMAND.encode() in program and MODS_WINDOWS_OFF.encode() not in program:
        return ("the mod loader is switched on (a build.py --mods-windows-test build), and mods stay off on Windows until the loader's checks on NTFS "
                "have passed; rebuild without --mods-windows-test, or make a local test package with --test")
    return None


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch_text(name: str, pin) -> bytes:
    path = deps.DOWNLOADS / name
    if not path.exists():
        subprocess.run(["curl", "-fsSL", "-o", str(path), pin[0]], check=True)
    data = path.read_bytes()
    if sha256(data) != pin[1]:
        sys.exit(f"{path}: SHA-256 differs from the pinned {pin[1]}")
    return data


def xiph_tarballs() -> Path:
    """libogg 1.0 and libvorbis 1.0, the sources of libvorbis_vc6 (their COPYING files ship): fetched as
    port/vorbis/build.py pins them (URL and SHA-256) into WORK/vorbis/downloads when absent, and checked."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("vorbis_build", PORT / "vorbis/build.py")
    vb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(vb)
    folder = WORK / "vorbis/downloads"
    folder.mkdir(parents=True, exist_ok=True)
    for name, (url, digest) in vb.TARBALLS.items():
        tar = folder / url.rsplit("/", 1)[1]
        if not tar.exists():
            subprocess.run(["curl", "-fsSL", "-o", str(tar), url], check=True)
        if sha256(tar.read_bytes()) != digest:
            sys.exit(f"{tar}: SHA-256 differs from port/vorbis/build.py's pin")
    return folder


def from_tarball(tar: Path, member_suffix: str) -> bytes:
    """The member member_suffix (from the archive's top folder, for example "/LICENSE" or "/src/hidapi/LICENSE-bsd.txt")."""
    depth = member_suffix.count("/")
    with tarfile.open(tar) as t:
        for m in t.getmembers():
            if m.name.endswith(member_suffix) and m.name.count("/") == depth:
                return t.extractfile(m).read()
    sys.exit(f"{tar}: no {member_suffix}")


def leading_comment(tar: Path, member_suffix: str) -> bytes:
    """The first /* ... */ comment of a source file in the archive (its licence notice), as the file has it."""
    data = from_tarball(tar, member_suffix)
    start = data.index(b"/*")
    return data[start:data.index(b"*/", start) + 2] + b"\n"


def comment_with(tar: Path, member_suffix: str, text: bytes, last: bool = False) -> bytes:
    """The /* ... */ comment of a source file in the archive that holds text (the last such comment when last), as the
    file has it: a licence notice that is not the file's first comment."""
    data = from_tarball(tar, member_suffix)
    at = data.rindex(text) if last else data.index(text)
    start = data.rindex(b"/*", 0, at)
    return data[start:data.index(b"*/", at) + 2] + b"\n"


def hwdata_notice() -> bytes:
    """hwdata's pnp.ids, which libdisplay-info 275e6459 carries as its PNP ID table (compiled into libdxvk_d3d9.so.0):
    hwdata's licence statement and the text of the XFree86 1.0 licence it offers, as The XFree86 Project publishes it
    (both pinned on deps.lock's "licence:" lines)."""
    import html
    import re
    statement = pinned_licence("hwdata")
    page = pinned_licence("XFree86-1.0").decode("latin-1")
    m = re.search(r"<h2>(Version 1\.0 of XFree86.*?)</h2>\s*<blockquote>(.*?)</blockquote>", page, re.S)
    if not m:
        sys.exit("XFree86-1.0: the licence page has no version 1.0 section")
    def plain(fragment: str) -> str:   # the words of an HTML fragment, without its tags and the registered-mark sign
        return " ".join(html.unescape(re.sub(r"<[^>]+>", " ", fragment)).replace("\u00ae", "").split())
    paras = ["\n".join(textwrap.fill(plain(line), 76) for line in re.split(r"<br\s*/>", p) if plain(line))
             for p in m.group(2).split("</p>")]
    text = [plain(m.group(1)), ""] + [p + "\n" for p in paras if p]
    return (b"The PNP ID table in lib/libdxvk_d3d9.so.0 comes from hwdata's pnp.ids, as libdisplay-info (commit\n"
            b"275e6459, dxvk 3.1.1's submodule) carries it. hwdata's licence statement:\n\n" + statement +
            b"\nThis program takes the XFree86 1.0 option. Its text, from " + deps.licence_pins()["XFree86-1.0"]["url"].encode() +
            b":\n\n" + "\n".join(text).encode())


def wayland_notices(tar: Path) -> bytes:
    """The copyright blocks of the Wayland protocol files SDL3 generates its Wayland code from (wayland-scanner copies
    each into its generated *-protocol.c, which libSDL3.so.0 compiles in), in file-name order."""
    import xml.etree.ElementTree as ET
    out = [b"Wayland protocol code in lib/libSDL3.so.0: generated by wayland-scanner from SDL3's wayland-protocols/*.xml.",
           b"Each protocol file's copyright notice, as the file has it:", b""]
    with tarfile.open(tar) as t:
        members = sorted((m for m in t.getmembers() if m.name.count("/") == 2 and m.name.split("/")[1] == "wayland-protocols"
                          and m.name.endswith(".xml")), key=lambda m: m.name)
        for m in members:
            root = ET.fromstring(t.extractfile(m).read())
            head = f"== {m.name.split('/', 1)[1]} (protocol {root.get('name')})".encode()
            c = root.find("copyright")
            if c is None:
                out += [head, b"    The file carries no copyright notice; it is part of the wayland-protocols project (MIT).", b""]
            else:
                out += [head, c.text.strip("\n").rstrip().encode(), b""]
    if not members:
        sys.exit(f"{tar}: no wayland-protocols/*.xml")
    return b"\n".join(out)


def pinned_licence(name: str) -> bytes:
    """A licence text deps.lock pins on a "licence:" line, fetched once into the downloads folder and checked."""
    pin = deps.licence_pins()[name]
    return fetch_text(f"{name}-{pin['version']}-LICENSE", (pin["url"], pin["sha"]))


def licences(windows: bool) -> dict:
    """Licence texts by package path (COPYING is the port's own licence, GPL-3.0)."""
    dl = deps.DOWNLOADS
    lock = deps.lock()
    ff = lock["FFmpeg"]["file"]
    vorbis = xiph_tarballs()
    out = {
        "COPYING": fetch_text("gpl-3.0.txt", GPL3),
        "licenses/FFmpeg-COPYING.LGPLv2.1": from_tarball(ff, "/COPYING.LGPLv2.1"),
        "licenses/FFmpeg-LICENSE.md": from_tarball(ff, "/LICENSE.md"),
        "licenses/libogg-1.0-COPYING": from_tarball(vorbis / "libogg-1.0.tar.gz", "/COPYING"),
        "licenses/libvorbis-1.0-COPYING": from_tarball(vorbis / "libvorbis-1.0.tar.gz", "/COPYING"),
        "licenses/DirectXTex-LICENSE": fetch_text("DirectXTex-1acf4ebe-LICENSE", DIRECTXTEX),
        "licenses/Monocypher-LICENCE.md": from_tarball(lock["monocypher"]["file"], "/LICENCE.md"),
        **({"licenses/ENet-LICENSE": from_tarball(lock["enet"]["file"], "/LICENSE")} if NETWORK else {}),
        "licenses/SDL3-LICENSE.txt": from_tarball(dl / Path(lock["SDL3"]["url"]).name, "/LICENSE.txt"),
        # SDL3 compiles in its copy of hidapi (libSDL3.so.0 and SDL3.dll alike), which offers three licences; this
        # program takes the BSD-style one (src/hidapi/LICENSE.txt names the choice).
        "licenses/hidapi-LICENSE": from_tarball(dl / Path(lock["SDL3"]["url"]).name, "/src/hidapi/LICENSE-bsd.txt"),
        # and its copy of yuv2rgb (the YUV to RGB conversion), BSD-3-Clause
        "licenses/yuv2rgb-LICENSE": from_tarball(dl / Path(lock["SDL3"]["url"]).name, "/src/video/yuv2rgb/LICENSE"),
        # and its copy of fdlibm (src/libm, Sun Microsystems: "provided that this notice is preserved"); the notice
        # as e_pow.c has it (every Sun file in src/libm carries the same one)
        "licenses/fdlibm-NOTICE": leading_comment(dl / Path(lock["SDL3"]["url"]).name, "/src/libm/e_pow.c"),
        # and stb_image (its image loader, src/video/stb_image.h), which offers MIT or public domain: the file's closing
        # licence comment, both options as it has them
        "licenses/stb_image-LICENSE": comment_with(dl / Path(lock["SDL3"]["url"]).name, "/src/video/stb_image.h",
                                                   b"This software is available under 2 licenses", last=True),
    }
    if windows:
        out["licenses/zlib-LICENSE"] = from_tarball(dl / Path(lock["zlib"]["url"]).name, "/LICENSE")
        out["licenses/MinGW-w64-runtime-COPYING.txt"] = fetch_text(
            "mingw-w64-62891fa4bf507d674c8c48822b596560b7cb506d-COPYING.MinGW-w64-runtime.txt", MINGW_RUNTIME)
        out["licenses/LLVM-LICENSE.TXT"] = (deps.DATA / "llvm-mingw/LICENSE.TXT").read_bytes()
    else:
        out["licenses/dxvk-LICENSE"] = fetch_text("dxvk-3.1.1-LICENSE", DXVK_LICENSE)
        out["licenses/dxbc-spirv-LICENSE"] = pinned_licence("dxbc-spirv")
        out["licenses/libdisplay-info-LICENSE"] = pinned_licence("libdisplay-info")
        # The Linux SDL3 also compiles in X11 and Wayland code under MIT-style notices that ask to be in all copies:
        # each notice as its source file in the pinned SDL3 tarball has it.
        sdl = dl / Path(lock["SDL3"]["url"]).name
        out["licenses/edid-parse-NOTICE"] = leading_comment(sdl, "/src/video/x11/edid-parse.c")
        out["licenses/xsettings-client-NOTICE"] = leading_comment(sdl, "/src/video/x11/xsettings-client.c")
        out["licenses/imKStoUCS-NOTICE"] = leading_comment(sdl, "/src/events/imKStoUCS.c")
        out["licenses/wayland-protocols-NOTICES"] = wayland_notices(sdl)
        # xdg-user-dirs' lookup functions, copied into SDL3's Unix filesystem code (MIT, (c) 2007 Red Hat)
        out["licenses/xdg-user-dir-lookup-NOTICE"] = comment_with(sdl, "/src/filesystem/unix/SDL_sysfilesystem.c",
                                                                  b"Copyright (c) 2007 Red Hat")
        out["licenses/hwdata-pnp.ids-LICENSE"] = hwdata_notice()
        out["licenses/libxkbcommon-LICENSE"] = from_tarball(dl / Path(lock["libxkbcommon"]["url"]).name, "/LICENSE")
    return out


def third_party(windows: bool) -> list:
    """(component, version, licence, how it is bundled, its licence files in the package): THIRD-PARTY-NOTICES.txt's rows
    (a name indented by two spaces is a part of the row above it); main() checks every file is in the archive."""
    lock = deps.lock()
    ffl = "avformat-61.dll, avcodec-61.dll, avutil-59.dll" if windows else "lib/libavformat.so.61, libavcodec.so.61, libavutil.so.59"
    mono = ("compiled into the launcher (the v2 update feed's signature check) and the game (the network's authenticated encryption)" if NETWORK
            else "compiled into the launcher: the v2 update feed's signature check")
    rows = ([("ENet", lock["enet"]["version"], "MIT", "statically linked direct-IP game transport", ["licenses/ENet-LICENSE"])] if NETWORK else [])
    rows += [("DirectXTex", "1acf4ebe", "MIT", "the DXT block encoder (port/d3dx/src/bc.cpp) is adapted from it", ["licenses/DirectXTex-LICENSE"]),
            ("Monocypher", lock["monocypher"]["version"], "BSD-2-Clause OR CC0-1.0", mono,
             ["licenses/Monocypher-LICENCE.md"]),
            ("FFmpeg", lock["FFmpeg"]["version"], "LGPL-2.1-or-later", f"{ffl}: shared libraries, decoders and demuxer only (Bink video and audio)",
             ["licenses/FFmpeg-COPYING.LGPLv2.1", "licenses/FFmpeg-LICENSE.md"]),
            ("libvorbis, libogg", "1.0", "BSD-3-Clause", "statically linked (libvorbis_vc6.a: Xiph's 1.0 source built with Visual C++ 6, as the "
             "original game's DLLs; vendor string \"Xiph.Org libVorbis I 20020717\")", ["licenses/libvorbis-1.0-COPYING", "licenses/libogg-1.0-COPYING"]),
            ("SDL3", lock["SDL3"]["version"], "Zlib", ("SDL3.dll" if windows else "lib/libSDL3.so.0") +
             ", patched: port/deps-patches/SDL3-offscreen-fullscreen-modes.patch and SDL3-x11-optional-extensions.patch in the source", ["licenses/SDL3-LICENSE.txt"]),
            ("  hidapi", "(SDL3 " + lock["SDL3"]["version"] + "'s copy)", "BSD-3-Clause, the BSD option of its three licences",
             "compiled into " + ("SDL3.dll" if windows else "lib/libSDL3.so.0") + " (game controllers)", ["licenses/hidapi-LICENSE"]),
            ("  yuv2rgb", "(SDL3 " + lock["SDL3"]["version"] + "'s copy)", "BSD-3-Clause, (c) 2016 Adrien Descamps",
             "compiled into " + ("SDL3.dll" if windows else "lib/libSDL3.so.0") + " (YUV to RGB conversion)", ["licenses/yuv2rgb-LICENSE"]),
            ("  fdlibm", "(SDL3 " + lock["SDL3"]["version"] + "'s copy, src/libm)", "Sun Microsystems' fdlibm notice, (c) 1993",
             "compiled into " + ("SDL3.dll" if windows else "lib/libSDL3.so.0") + " (maths functions)", ["licenses/fdlibm-NOTICE"]),
            ("  stb_image", "(SDL3 " + lock["SDL3"]["version"] + "'s copy, src/video/stb_image.h)",
             "MIT OR Unlicense (public domain), (c) 2017 Sean Barrett",
             "compiled into " + ("SDL3.dll" if windows else "lib/libSDL3.so.0") + " (image loading)", ["licenses/stb_image-LICENSE"])]
    if not windows:
        sdl_copy = "(SDL3 " + lock["SDL3"]["version"] + "'s copy)"
        rows += [("  edid-parse", sdl_copy, "MIT, (c) 2007 Red Hat", "compiled into lib/libSDL3.so.0 (X11 monitor EDID parsing)",
                  ["licenses/edid-parse-NOTICE"]),
                 ("  xsettings-client", sdl_copy, "HPND-style, (c) 2001, 2007 Red Hat and 2024 Igalia",
                  "compiled into lib/libSDL3.so.0 (X11 XSETTINGS)", ["licenses/xsettings-client-NOTICE"]),
                 ("  imKStoUCS", sdl_copy, "MIT, (c) Jamey Sharp, Josh Triplett, Red Hat, Oracle",
                  "compiled into lib/libSDL3.so.0 (X11 and Wayland keysym to Unicode)", ["licenses/imKStoUCS-NOTICE"]),
                 ("  Wayland protocols", sdl_copy.replace("copy", "wayland-protocols"), "MIT, many holders",
                  "code generated from them is compiled into lib/libSDL3.so.0", ["licenses/wayland-protocols-NOTICES"]),
                 ("  xdg-user-dir-lookup", sdl_copy, "MIT, (c) 2007 Red Hat",
                  "compiled into lib/libSDL3.so.0 (XDG user folders)", ["licenses/xdg-user-dir-lookup-NOTICE"])]
    rows += ([("zlib", lock["zlib"]["version"], "Zlib", "libz.dll", ["licenses/zlib-LICENSE"]),
              ("MinGW-w64 runtime", "15.0 (llvm-mingw " + lock["llvm-mingw"]["version"] + ")", "ZPL-2.1 and others", "statically linked",
               ["licenses/MinGW-w64-runtime-COPYING.txt"]),
              ("libc++, libc++abi, libunwind", "llvm-mingw " + lock["llvm-mingw"]["version"], "Apache-2.0 WITH LLVM-exception",
               "statically linked", ["licenses/LLVM-LICENSE.TXT"])] if windows else
             [("dxvk-native", lock["dxvk-native"]["version"], "Zlib", "lib/libdxvk_d3d9.so.0 (Direct3D 9 over Vulkan)", ["licenses/dxvk-LICENSE"])] +
             [(f"  {n}", f"{p['version']} (in dxvk-native)", f"MIT, (c) {who}", "compiled into lib/libdxvk_d3d9.so.0",
               [f"licenses/{n}-LICENSE"]) for n, who in (("dxbc-spirv", "2025 Philip Rebohle"),
                                                         ("libdisplay-info", "2022 The libdisplay-info Contributors"))
              for p in (deps.licence_pins()[n],)] +
             [("  hwdata pnp.ids", "(libdisplay-info 275e6459's copy, in dxvk-native)", "GPL-2.0-or-later OR XFree86 1.0, the XFree86 1.0 option",
               "its PNP ID table is compiled into lib/libdxvk_d3d9.so.0 (monitor maker names)", ["licenses/hwdata-pnp.ids-LICENSE"])] +
             [("libxkbcommon", lock["libxkbcommon"]["version"], "MIT", "lib/libxkbcommon.so.0", ["licenses/libxkbcommon-LICENSE"])])
    return rows


PUBLIC_REPO = "https://github.com/dlprentice/onslaught-ce"


def release_tag(version: str) -> str:
    """The release's tag: v and the version with three numbers (0.1 -> v0.1.0, 0.1.1 -> v0.1.1)."""
    parts = version.split(".")
    if all(p.isdigit() for p in parts) and len(parts) < 3:
        parts += ["0"] * (3 - len(parts))
    return "v" + ".".join(parts)


def source_commit() -> str:
    """The commit these packages are built from: this checkout's HEAD (in a clone of the public repository, the public
    commit the release tag names)."""
    r = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True)
    if r.returncode or not r.stdout.strip():
        sys.exit("no git commit to name as the corresponding source")
    return r.stdout.strip()


def source_url(version: str) -> str:
    return f"{PUBLIC_REPO}/tree/{release_tag(version)} (commit {source_commit()})"


# The public build compiles the authored graphics host, not the inherited SDK framework.
D3DAPP = ["The public graphics host (port/platform/public/d3dapp.cpp and d3dapp.h) is GPL-3.0-only code by",
          "the Onslaught CE contributors. The inherited SDK sample framework is not included in this package."]


def notices(version: str, windows: bool) -> bytes:
    lock = deps.lock()
    ff = lock["FFmpeg"]
    text = [f"Onslaught CE {version}: third-party components", ""]
    text += [f"{'  - ' if n.startswith('  ') else '- '}{n.strip()} {v} ({l}): {how}. Licence: {', '.join(files)}."
             for n, v, l, how, files in third_party(windows)]
    text += ["", "FFmpeg (LGPL-2.1-or-later), unmodified: the exact source is the release tarball (also attached to this",
             "release)", f"  {ff['url']}", f"  SHA-256 {ff['sha']}",
             "configured as port/tools/deps.py's build_ffmpeg_shared records in the corresponding source (decoders bink,",
             "binkaudio_dct, binkaudio_rdft, the bink demuxer and the file protocol only),",
             f"  {source_url(version)}",
             "The program loads FFmpeg's shared libraries from this folder, so",
             "they can be replaced with a modified build of the same version. FFmpeg gives no patent assurances; the Bink",
             "formats are RAD Game Tools'. The program decodes only the player's own game files.",
             "The Xiph.Org Foundation's name is not used to endorse or promote this program.", "", *D3DAPP, ""]
    if windows:
        text += ["Source paths from llvm-mingw's own build machine (folders named llvm-mingw, llvm-project and mingw-w64) remain",
                 "in three files: bea.exe, inside its statically linked libc++abi and libunwind, and SDL3.dll and libz.dll,",
                 "inside the MinGW-w64 runtime objects linked into them. They are llvm-mingw's folders, not this program's.", ""]
    return "\n".join(text).encode()


# Report a problem's one network action, in the same words as public/README.md and SECURITY.md.
REPORT_PRIVACY = ["Report a problem opens a GitHub page in your browser with your version and OS in its address;",
                  "nothing is posted until you submit it."]


# README.txt's line width (the hand-wrapped lines below stay within it; generated ones wrap at it).
README_WIDTH = 114


def readme(version: str, windows: bool) -> bytes:
    launcher = "onslaught-ce.exe" if windows else "onslaught-ce"
    common = ["It finds your Battle Engine Aquila install in Steam's libraries, or asks you once for its folder and",
              "remembers it (BEA_GAME_DIR overrides). By default, careers and settings go to a separate user folder:",
              ("%APPDATA%\\Onslaught CE." if windows else "$XDG_DATA_HOME/onslaught-ce, or ~/.local/share/onslaught-ce when unset or empty."),
              "BEA_USER_DIR overrides that location. Keep it separate from the game folder and avoid linked paths.",
              "Updates: at start it checks a signed feed (ce-update-v2.json) and asks before installing. A new version",
              "goes into versions/ inside this folder; this folder's own files and everything beside it are left alone.",
              "--update-only checks now, --rollback returns to the previous version, BEA_NO_UPDATE_CHECK=1 or",
              "Check for Updates at Start: No in CE Settings (update_check=0 in ce.ini) turns checking off. --apply is refused.",
              "Do not start an Onslaught CE 0.1.0-0.1.6 launcher at all, not even to check its version or update it: its",
              "startup cleanup can delete neighbouring <program folder>.old-* paths. On Linux it has followed a matching",
              "directory link into its target; Windows reparse-point behaviour is unmeasured. Keep the old installation",
              "and every .old-*, .download-* and .unpack-* path untouched.",
              "Locate and preserve both launcher and game user-data folders. A relative BEA_USER_DIR can put them in",
              "different locations. Copy each whole folder to an independent backup; verify the copies and careers.",
              "If your data was in the default folder and its backup is verified, this release can use it as it is.",
              "Otherwise use a separate working copy of the game's user-data folder outside the original game and old CE",
              "program/staging folders; set BEA_USER_DIR to its absolute path. Check careers; keep originals and backups.",
              "If the locations or conflicting files are unclear, do not merge or overwrite them; seek help first.",
              "This release's launcher removes no neighbouring paths: verify its package and extract it into a new empty",
              "folder. See the project README for full migration steps. No telemetry; the update check sends requests",
              "for the public feed and its signature.",
              *REPORT_PRIVACY]
    run = (["Start onslaught-ce.exe. (bea.exe also runs directly, without the install search.)",
            "It needs Windows 10 or newer, for its built-in UCRT, and Windows 10 version 1903 or newer",
            "for user or game folder names with characters outside your Windows language's character set (earlier",
            "versions ignore the UTF-8 setting the programs ask for)."] + common +
           ["Windows builds have been checked under Wine on Linux and on one Windows 11 virtual machine using software",
            "graphics; this version's release notes list the routes checked for it. See also Known limitations."] if windows else
           textwrap.wrap("Run ./onslaught-ce [game arguments]. It first checks for the 32-bit system libraries it needs "
                         "and names the packages to install when one is missing. It needs " + GLIBC_TEXT + ".",
                         README_WIDTH, break_on_hyphens=False) + common)
    exe = "bea.exe" if windows else "bea"
    return "\n".join([f"Onslaught CE V{version}", ""] + quick_start(windows) + ["",
                      "A native source port of Battle Engine Aquila (2003). It contains no game files: you need your own",
                      "copy of the game (for example from Steam).", f"Project page, source and releases: {PUBLIC_REPO}",
                      ""] + run +
                     ["", f"{exe} is Onslaught CE's game program; {launcher} is the program to start.", ""] +
                     in_the_game() + help_lines(windows) + limitations(windows) + NOTICE_LINES +
                     ["", "Licence: GPL-3.0-only (COPYING). Third-party components: THIRD-PARTY-NOTICES.txt and licenses/.", ""]).encode()


# The build with the network compiled in (flavour public-network) offers the Network game page; its README.txt says what the page says (experimental, what a
# mismatch does, no matchmaking server, what was not tested, what the game contacts). package_readme_tests.py checks the sentences against the page's own text.
NETWORK_GAME_LINES = [
    "- Network game (experimental): Main menu > Onslaught CE > Network Game hosts or joins a two-player co-op game on",
    "  the building defence or the tutorial. It is new and unfinished, so expect problems. Both players need the same",
    "  Onslaught CE version and their own copy of the game. If the two games ever disagree, the game ends with a",
    "  message. A network game does not change your careers. Its HUD scales and sits as in single-player play",
    "  (BEA_PORT_HUD_SCALE=0 turns the scaling off). There is no pause in a network game and Escape does nothing; to",
    "  leave, close the window (the other player is told). There is no matchmaking server: play on one network, or the",
    "  host forwards a UDP port on its router (net_port in ce.ini, default 27652) and gives the other player its",
    "  public address; the Network game page's row \"How to connect over the Internet\" walks the host through the",
    "  router, the firewall prompt and the public address. Not tested: play through routers over the real internet,",
    "  more than two players, Windows on real hardware. Running the Windows build under Wine or Proton, keep the game",
    "  and user folders on a C: path (the user folder's default is): a folder reached through Wine's Z: drive can be",
    "  refused at network admission. The game sends data only to the other player's address and contacts no server.",
    "  The host's page lists this PC's addresses; on Windows that asks the system to look up this PC's own name.",
]


def in_the_game() -> list[str]:
    """The "In the game" list; a build with the network compiled in adds the Network game paragraph before the closing console and log bullets."""
    if not NETWORK:
        return IN_THE_GAME
    at = IN_THE_GAME.index("- A quiet console: the developers' messages are hidden unless BEA_PORT_VERBOSE=1 is set.")
    return IN_THE_GAME[:at] + NETWORK_GAME_LINES + IN_THE_GAME[at:]


# README.txt's "In the game" and "Screen modes and widescreen" (public/README.md says the same).
IN_THE_GAME = [
    "In the game",
    "- Main menu > Onslaught CE: Bonus Levels, CE Settings, Cheats, Help & Info and Report a Problem. Each row has a",
    "  hint under the list. The Options page is the original's. The game's Credits remain separate.",
    "- Bonus Levels: Aquila Prototype, Building Defence and Race Challenge 1 to 5. None of them advances career",
    "  progress, and each comes back to this list with its row highlighted.",
    "  Aquila Prototype is the original playable-demo level (201).",
    "  Building Defence is a two-player local split-screen defence map (level 856) on this PC; the original ships it",
    "  twice, as levels 856 and 858, which are the same map, so the list has it once.",
    "  Choose each player's Battle Engine on the Multiplayer start page; player 2 uses the second set of keys in",
    "  Controller Options or a second gamepad. Plug in both gamepads before starting if using two.",
    "  Settings can still be saved during play, including into a career file; bonus-level progress is left out.",
    "- Race Challenge 1 to 5 are the original's five one-player time trials (levels 901 to 905): three laps against the",
    "  clock in the Racer Battle Engine. The original opens them from the Goodies page: Race Challenge 1 after grades",
    "  of C or better in 26 campaign levels, and each later race when the one before it is finished fast enough (under",
    "  145, 195, 150 and 105 seconds for races 1 to 4; the fifth opens nothing more), or all five at once with Unlock",
    "  All Goodies in Onslaught CE > Cheats; those routes save your career as the original does. The five rows on the",
    "  Bonus Levels list start the races with none of that; a race does not advance career progress and, when it ends",
    "  (finished, lost or quit from the pause menu), saves nothing and returns to the Bonus Levels list.",
    "- CE Settings: a list of choices, each written to ce.ini in your user folder when you make it: HUD Size and HUD",
    "  Layout (see Screen modes and widescreen, below), Sharper Text (experimental), End-of-Level Stats, Controller",
    "  Rumble, Dodge Button (see Walker dodging, below), Developer Information, Your",
    "  Music (shows the folder ce.ini sets: see Your",
    "  own music, below), Music Order, Console Text, Check for Updates at Start and Record Play for Bug Reports (see",
    "  Help make Onslaught CE more faithful, below). Select opens a row's list of choices, and so do Left and Right,",
    "  which also move to the choice before or after; in the list they move as Up and Down do. Nothing is written",
    "  until Select picks a choice other than the one shown, and Escape closes the list unchanged. The row's hint says",
    "  when a change is used: from the next level for the two HUD rows and Dodge Button, the next time the console",
    "  opens for Console Text (the developer information text keeps its colour until the next start), otherwise the",
    "  next time you start the game. If ce.ini cannot be written, a message says so and the row keeps its old choice.",
    "  The page is not on the menu while a session is recorded.",
    "- Your own music: a music_dir= line in ce.ini, with the absolute path of a folder (on Linux ~/ is your home",
    "  folder), plays the folder's tracks in place of the level music; the front end and the credits keep the game's",
    "  own. A track must be Ogg Vorbis, stereo, 44100 Hz, with a full path of at most 255 bytes. Up to 256 tracks are",
    "  used, subfolders are not searched, and the folder is only read. Music Order in CE Settings (music_playtype) sets",
    "  how they play: in order, shuffled, or one track and then quiet. Both lines are read once a session, so a change",
    "  applies the next time you start the game. Your Music in CE Settings shows the folder ce.ini sets, and the log",
    "  says how many tracks were found there. For one session, the console command music adddir <folder> adds a",
    "  folder's tracks without changing ce.ini. Your own music is off in recordings, replays, network games and demos.",
    "- Help & Info: Controls & Tips (dodge, zoom, photo mode and screenshots, the console and the free",
    "  camera), What's Coming (planned features and their limits; it does not start those features), Updates &",
    "  Rollback (how updating works and how to turn the checks off), About Onslaught CE (version, licence and links)",
    "  and Special Thanks.",
    "- Video Options on a new profile: the first time you start with no saved options of your own (a new install, or",
    "  a profile that only has the old game folder's file), every quality row starts at its highest choice except",
    "  Full models, anti-aliasing starts at the highest level your graphics device lists, and extra graphical",
    "  features start On where the device passes the capability test. Full models starts Off: it draws every model",
    "  in full detail at any distance and is the largest cost in frame rate, so turn it On if your computer has the",
    "  room. Terrain detail distance starts at Extended, which draws the terrain further away; choose Normal for the",
    "  original's distance. Anything you saved before is kept as it was: the new defaults apply once, to a profile",
    "  with no options of its own, and every row can be changed afterwards.",
    "- Unfinished features: the first start adds an [Experimental] block to ce.ini that lists them, each set to 0",
    "  (off). They are unsupported and may change or break; a line set to 1 turns that feature on from the next",
    "  start. None of them is on a menu until its line is on, and none is needed to play. Leave the lines at 0",
    "  unless you want to try one. A line you already set in an earlier version (0.1.13 or before) is kept",
    "  as it is.",
    "- Console: during an eligible solo level, press the grave key (`; left of 1 on a US keyboard), type help and",
    "  press Enter. Esc or grave closes it; the game does not pause. It is unavailable in multiplayer, demos,",
    "  recordings and replays.",
    "- Onslaught CE > Cheats: the game's own cheats as On/Off switches, kept in ce.ini so they apply to every career;",
    "  the line under the list says what the highlighted one does:",
    "  - Unlock All Levels (original code TURKEY): every level opens in the level select",
    "  - Unlock All Goodies (MALLOY): every goodie opens on the Goodies page",
    "  - Goodie Hints (lat\u00eate): the Goodies page shows how to unlock each goodie; while it is on, goodies",
    "    cannot be opened",
    "  - God Mode Option (Maladim): adds a God switch to Controller Options, in the pause menu (applies at once)",
    "    and in Options (applies from the next level), for no damage and endless energy; turning the cheat off",
    "    ends god mode. God mode set this way is kept in ce.ini, not in your careers.",
    "  - Free Camera (F key, Aurore): in a level, F lets you fly the camera; F again returns it",
    "  These switches are off in recordings and replays. Typing a code into a career's name still works as in the",
    "  original.",
    "- Keyboard: held keys are released on focus loss, and delayed releases are repaired when possible. Typed",
    "  characters come from key presses; letter and digit keys use their physical positions across layouts.",
    "- Walker dodging (on the ground, not already dodging): with the default US keyboard, tap A then D for right,",
    "  or D then A for left.",
    "  Dodge Button: hold Left Ctrl and press A or D, or, on a gamepad, hold B (the right face button on an",
    "  Xbox-style pad; the tutorial cue calls it the right button) and push the left stick left or right. The",
    "  game then makes the two taps itself, through its own controls, and the dodge is the original one. For",
    "  about 0.15 seconds your own left and right are held back. If you pushed the same direction 0.1 to 0.2",
    "  seconds before, the first tap waits a moment. It follows whatever inputs you use to strafe. For the",
    "  next dodge, let go and press again, or change direction with the button still held; a press made early",
    "  in a dodge (it lasts about three quarters of a second) is dropped, one made in its last 0.35 seconds",
    "  follows it at once. A key or button you have bound to a control of your own is not used; flight sticks",
    "  have none. It is on by default (CE Settings, Dodge Button; dodge_button=0 in ce.ini turns it off, from",
    "  the next level) and off in recordings, replays, demos and network games.",
    "- Zoom: with default mouse bindings, wheel down zooms in and wheel up zooms out. In eligible ordinary play,",
    "  the default gamepad's right bumper zooms in and left bumper out. Manual zoom works only with some weapons;",
    "  some zoom while charging. Check Controller Options for changed bindings.",
    "- Controllers: gamepad and flight-stick defaults cover menus, solo play, two-player split screen (one pad for each",
    "  player) and bonus co-op when joystick controls have not been customized. Only virtual devices were tested.",
    "- Photo mode: F11 (or the console's photo command) hides the HUD and cockpit for pictures, and F12 saves a",
    "  screenshot. Both work in single-player levels, but not while a session is recorded or replayed, in multiplayer,",
    "  a network game or a demo. Tested on Linux; Windows testing of it is limited (see the release notes).",
    "  In a single-player level the pause menu also has a Photo Mode row, so a gamepad can reach it: it closes the",
    "  menu and turns photo mode on, as F11 does, and writes nothing. The row then reads End Photo Mode.",
    "- Mods (experimental): on Linux, Onslaught CE can read replacement game files, such as textures, from mods in the",
    "  mods folder of your user folder, named by a mods= line in ce.ini. Every file is checked against the mod's",
    "  mod.json when the game starts, and a mod with any problem is not loaded (the log says why). There is no mod",
    "  menu yet. On Windows mods are not active in this release: a mods= line is ignored and the log says so. See",
    "  Mods (experimental) in the project README.",
    "- A save that fails now says so: after the pause menu's Continue, the game's own save error message (\"An error",
    "  occurred during saving.\" in English, and in the language you chose) appears for a few seconds at the bottom of",
    "  the screen, and after a language change in the front end it appears in the usual message box. The career you",
    "  had is unchanged. Recordings and replays show neither.",
    "- A quiet console: the developers' messages are hidden unless BEA_PORT_VERBOSE=1 is set.",
    "- A log file for reports in your user folder, and session recording (both below).",
    "",
    "Screen modes and widescreen",
    "Video Options > Window mode offers Windowed or Fullscreen. Choose Apply to switch without restarting.",
    "If saving fails, the menu says the change is for this session only.",
    "An explicit -forcewindowed game argument takes priority.",
    "The saved display-mode choice is ignored while recording or replaying.",
    "By default, Onslaught CE starts at your desktop's resolution; a saved or explicit screen mode can change it.",
    "Video Options lists your monitor's sizes, including widescreen ones. On a widescreen monitor the 3D view shows",
    "more to the sides (the original's vertical view is kept). Menus, loading pictures and films keep their original",
    "4:3 shape, centred with black bars. To get the original's 4:3-only list and 640 x 480 start back, set",
    "BEA_PORT_WIDESCREEN=0. Ordinarily, replays and recordings use the original 4:3 behaviour. The original game shares",
    "one random number source between visual effects and play, so a wider view can change the details of a fight.",
    "In eligible ordinary single-player play with widescreen modes enabled, the HUD automatically scales up when",
    "both screen dimensions exceed 640 x 480, keeping the size it has at 640 x 480 relative to the screen (at 3840 x",
    "2160 it is 4.5 times as large), and so do the markers it draws on targets and objectives, each about its own",
    "target. Choose HUD Size: Original in CE Settings (hud_scale=0 in ce.ini) for the original fixed-size HUD, from",
    "the next level on (a hud_scale line written into ce.ini by hand is read once, when your first level starts:",
    "restart the game after changing it); or set BEA_PORT_HUD_SCALE=0, which also keeps the corner groups below in",
    "the 4:3 columns. A BEA_PORT_HUD_SCALE that is set and not empty decides the size instead of ce.ini: a value that",
    "starts with 0 (0, 00, 05 ...) gives the original's, with the corner groups in the 4:3 columns, and any other",
    "value the larger HUD; the HUD Size row still shows what ce.ini says. Scaling is off in recordings and replays,",
    "split-screen multiplayer, and when BEA_PORT_WIDESCREEN=0.",
    "In the same play the HUD's corner groups (the target panel, objectives, radar, weapon and ammunition readouts and",
    "the radio portrait) sit in the corners of the screen, where the original's HUD sits at 4:3, while the radio",
    "messages, warnings, compass and reticle stay in the middle. On a very wide screen the corners are far apart:",
    "choose HUD Layout: 4:3 Columns in CE Settings (hud_anchor=0 in ce.ini) to keep them in the columns a 4:3",
    "picture would have, as BEA_PORT_HUD_SCALE=0 does. In split-screen multiplayer the HUD stays in the 4:3 columns.",
    "Sharper text is off by default: the menus' and HUD's text keeps the original's soft look, as the rest of the",
    "picture does. Choose Sharper Text: Yes in CE Settings (sharp_ui=1 in ce.ini) to turn it on from the next start:",
    "with widescreen modes enabled on a non-4:3 picture,",
    "bitmap text drawn at least twice its original size horizontally or vertically (for example, menu text at 1920 x",
    "1080) can use a sharper copy of the game's letters, so it can look crisp instead of soft; the letters keep their",
    "shapes, sizes and places.",
    "The text is then deliberately sharper than the original's soft pixels and looks brighter. A font's sharper copy",
    "is made the first time it is used, which can pause the game for a moment (about a tenth of a second). In ordinary",
    "use it does not apply in recordings and replays, on 4:3 screens or when BEA_PORT_WIDESCREEN=0.",
    "For testing, a valid BEA_PORT_WIDE_TEST=WIDTHxHEIGHT forces wide modes even during recording or replay and when",
    "BEA_PORT_WIDESCREEN=0. Unset it before making a faithful reference tape.",
    "",
]


def help_lines(windows: bool) -> list:
    """README.txt's "Report a problem" and "Help make Onslaught CE more faithful" (public/README.md says the same)."""
    launcher = "onslaught-ce.exe" if windows else "./onslaught-ce"
    user = ("%APPDATA%\\Onslaught CE by default" if windows else
            "$XDG_DATA_HOME/onslaught-ce, or ~/.local/share/onslaught-ce when unset or empty")
    where = (" (from a Command Prompt in this folder, or a shortcut with --report added)" if windows else "")
    return ["Report a problem",
            *textwrap.wrap(f"Choose Onslaught CE > Report a Problem in the main menu, or run {launcher} --report{where}.",
                           README_WIDTH),
            "It opens a new issue on the project's GitHub page in your browser, filled in with your version and",
            "system, and shows the folder that holds your log. Nothing is posted until you submit the issue. Say what",
            "happened and how to make it happen again. Attach the affected run's log from your user folder",
            *textwrap.wrap(f"({user}). The usual file is onslaught-ce.log; onslaught-ce.old.log, when present, "
                           "holds the previous primary log. If multiple copies ran at once, the affected run may "
                           "be in onslaught-ce.concurrent-1.log or onslaught-ce.concurrent-2.log. Check its start "
                           "time to identify the run and save it promptly; later launches can replace log files. "
                           "Each stops at 4 MB. BEA_USER_DIR can select another folder. The log holds",
                           README_WIDTH, break_on_hyphens=False),
            "the version, your OS and GPU and the game's messages. Home paths and account-name path components are",
            "shortened; look the file over before sharing it. You can share it directly with someone helping you.",
            "If you recorded the session (below), attach its tape too.",
            "",
            "Help make Onslaught CE more faithful",
            "We compare recorded input with the original; recordings of real play can reveal differences our tests miss.",
            f"Record one session with {launcher} --record, or every session",
            f"with Onslaught CE > CE Settings > Record Play for Bug Reports. That starts the next time you launch Onslaught CE",
            "(quit the game, then start it again); it puts record_sessions=1 in ce.ini in your user folder.",
            f"While it is on, each start asks whether to record: Stop recording there, or starting with",
            f"{launcher} --no-record, turns it off again.",
            "By default, recording targets 30 frames a second for play and uses the replay clock. Its menus take",
            "the keyboard and mouse, and you can type a career's name. Direct gamepad and flight-stick input is",
            "disabled during recording and replay because the tape does not record it. An older tape made with live",
            "controller input cannot recover it and may replay differently. The screen is normally the original 4:3.",
            "The menus are the original's: the version text, no Onslaught CE menu and no Cheats page, and only cheats",
            "typed into a career's name work. The larger HUD, console, CE tutorial dodge and zoom cues, and the saved display",
            "mode are off; see Walker dodging above. By default the game tries to play sound through the audio monitor",
            "when a device is available.",
            "Each completed session is saved in the tapes folder beside ce.ini as a .tape and a .tape.json. The tape",
            "records key presses and releases, typed text, mouse input, their frame numbers and the game's rendering",
            "size for menu mouse input. The JSON includes version/build, platform/OS, date/time, clock, frame rate,",
            "audio-monitor setting, game arguments and encoded copies of some CE user-folder files at the start,",
            "including game options, careers and possibly other files. Session recording does not capture video, game",
            "audio or microphone audio. Both files can contain personal information; review them before sharing.",
            "To share a completed session, zip both files and say what differed from the original and roughly when.",
            "An interrupted or failed capture may leave an .incomplete file; it is not a completed recording. Keep it",
            "for diagnosis rather than replaying it.",
            ""]


def limitations(windows: bool) -> list:
    """README.txt's "Known limitations", this platform's lines only (public/README.md and the release notes list
    both platforms')."""
    platform = (["- Windows builds have been checked under Wine on Linux and on one Windows 11 virtual machine with",
                 "  Microsoft's software renderer. Physical Windows graphics cards and drivers remain unvalidated for",
                 "  this version; see its release notes for the routes tested.",
                 "- The executables are unsigned, so SmartScreen may warn you (choose More info, then Run anyway)."] if windows else
                [*textwrap.wrap("- You need " + GLIBC_TEXT + ".", README_WIDTH, subsequent_indent="  ", break_on_hyphens=False),
                 *textwrap.wrap("- You need " + VULKAN_TEXT + ".", README_WIDTH, subsequent_indent="  ", break_on_hyphens=False),
                 *textwrap.wrap(LINUX_OPTIONAL, README_WIDTH, subsequent_indent="  ", break_on_hyphens=False)])
    return ["Known limitations", *platform,
            "- Complete tutorial parity with the original has not yet been established.",
            "- Gamepad and flight-stick defaults cover menus, solo play and bonus co-op with uncustomized joystick",
            "  controls. Only virtual devices have been tested; physical controllers and Windows drivers remain open.",
            "- Physical audio devices and listening quality have not been validated by the recorded and virtual controls.",
            "- Widescreen play is not compared with the original (the original has no widescreen); ordinary replays and",
            "  recordings use 4:3.",
            ("- Planned, not in this version yet: matchmaking, versus play, general mod support and additional HUD options." if NETWORK else
             "- Planned, not in this version yet: Internet multiplayer, general mod support and additional HUD options."),
            ""]


def quick_start(windows: bool) -> list:
    """The quick start for this platform's package (the repository README, public/README.md, gives both)."""
    exe, user = ("onslaught-ce.exe", "%APPDATA%\\Onslaught CE") if windows else ("./onslaught-ce", "$XDG_DATA_HOME/onslaught-ce (or ~/.local/share/onslaught-ce when unset or empty)")
    return ["Quick start",
            "1. Extract the download into a new empty folder outside your original game folder. Preserve old installations",
            "   and user folders; do not overwrite them or start a 0.1.0-0.1.6 launcher. You can move the new folder later.",
            f"2. Run {exe}.",
            "3. It finds your Steam copy of Battle Engine Aquila by itself, or asks once where it is.",
            "",
            *textwrap.wrap("Keep the program and its user folder separate from the original game. By default, careers and "
                           f"settings go to {user}. Keep recovery copies before changing save locations; do not point "
                           "BEA_USER_DIR into the game folder.", README_WIDTH, break_on_hyphens=False),
            "Preserve old program and staging folders during the launcher safety hold; keep verified user-data backups."] + (
            [] if windows else [
                "Linux careers: a filename whose in-game name exceeds 31 16-bit units is omitted from the list. The game",
                "emits one name-free diagnostic, normally captured in a launcher log, but shows no on-screen warning.",
                "If a career is missing, keep its file and ask for help. If the same career has older one-byte and UTF-8",
                "spellings across folders, the older spelling can be chosen from the original folder; keep both and ask",
                "for help before overwriting either one.",
            ])


NOTICE_LINES = [
    "Onslaught CE is an unofficial fan project. It is not affiliated with, endorsed by or sponsored by the owners of",
    "Battle Engine Aquila or its publishers and developers. Battle Engine Aquila and related names and marks are",
    "trademarks of their respective owners. No game files are included: Onslaught CE reads the player's own copy.",
]


def notice(version: str, windows: bool) -> bytes:
    target = "windows" if windows else "linux"
    return "\n".join([f"Onslaught CE {version}", "", *NOTICE_LINES, "",
                      "Onslaught CE is free software under the GNU General Public License version 3 only (COPYING).",
                      f"Corresponding source: {source_url(version)};",
                      f"build with `python3 port/build.py --public --target {target} --ce-version {version}`; libvorbis_vc6",
                      "from release vorbis-vc6-1, built by port/vorbis/build.py.",
                      "", *D3DAPP, "",
                      "Third-party components and their licences: THIRD-PARTY-NOTICES.txt and licenses/.", ""]).encode()


LAUNCHER = r'''#!/usr/bin/env bash
# Onslaught CE: checks the 32-bit system libraries, then hands over to onslaught-ce-launch, which finds your Battle
# Engine Aquila install (BEA_GAME_DIR, ce.ini, Steam's libraries, or asks once), starts
# the game from this folder (or the updated version in versions/); saves and settings use a separate user folder.
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
# Mirror the launcher's option prefix: -- or the first unrecognized argument begins game arguments.
# Only recognized commands that cannot start a game may skip the game-library preflight.
preflight=1
for arg in "$@"; do
	case "$arg" in
		--version|--apply) preflight=0; break ;;
		--update-only|--rollback|--report) preflight=0 ;;
		--no-update-check|--record|--no-record) ;;
		*) break ;;
	esac
done
if [ "$preflight" = 1 ] && [ "${BEA_SKIP_PREFLIGHT:-}" != 1 ]; then "$HERE/preflight.sh" || exit 1; fi
export DXVK_WSI_DRIVER=SDL3 DXVK_LOG_LEVEL=${DXVK_LOG_LEVEL:-none}
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/onslaught-ce-launch" "$@"
'''

PREFLIGHT = r'''#!/usr/bin/env bash
# Onslaught CE preflight: bea is a 32-bit (i686) program. It needs the 32-bit C/C++ runtime, zlib and the Vulkan loader,
# plus a 32-bit Vulkan driver for your GPU and the X11 or Wayland client libraries SDL opens at run time.
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
SKIP="(set BEA_SKIP_PREFLIGHT=1 to start anyway)"
# is32 FILE: FILE (after symlinks) is a 32-bit ELF. `file` decides when it is installed; otherwise the ELF header's
# magic and its EI_CLASS byte (offset 4: 01 is 32-bit) do.
if command -v file >/dev/null 2>&1; then
	is32() { file -L "$1" 2>/dev/null | grep -q 'ELF 32-bit'; }
elif command -v od >/dev/null 2>&1; then
	is32() { [ "$(od -An -tx1 -N5 "$1" 2>/dev/null | tr -d ' \n')" = 7f454c4601 ]; }
else
	echo "onslaught-ce: cannot check the 32-bit libraries: install \`file\` or set BEA_SKIP_PREFLIGHT=1" >&2
	exit 1
fi
missing=()
# Inside the Steam Linux Runtime's container (pressure-vessel: Steam's "Steam Linux Runtime 3.0 (sniper)" compatibility
# tool) the host's newer glibc and GPU driver libraries are imported under its overrides folder.
LIBDIRS="/usr/lib32 /usr/lib/i386-linux-gnu /lib/i386-linux-gnu /usr/lib/i686-linux-gnu /usr/lib"
[ -d /run/pressure-vessel ] && LIBDIRS="/usr/lib/pressure-vessel/overrides/lib/i386-linux-gnu $LIBDIRS"
where32() { # $1 = soname: the 32-bit copy's path
	for d in $LIBDIRS; do
		[ -e "$d/$1" ] && is32 "$d/$1" && { echo "$d/$1"; return 0; }
	done
	return 1
}
have() { where32 "$1" >/dev/null; }
check() { # soname, Arch package, Debian/Ubuntu package
	have "$1" || missing+=("$1|$2|$3")
}
check libc.so.6        lib32-glibc        libc6:i386
check libstdc++.so.6   lib32-gcc-libs     libstdc++6:i386
check libgcc_s.so.1    lib32-gcc-libs     libgcc-s1:i386
check libz.so.1        lib32-zlib         zlib1g:i386
check libvulkan.so.1   lib32-vulkan-icd-loader libvulkan1:i386
if [ -n "${WAYLAND_DISPLAY:-}" ]; then
	check libwayland-client.so.0 lib32-wayland libwayland-client0:i386
	check libwayland-cursor.so.0 lib32-wayland libwayland-cursor0:i386
	have libdecor-0.so.0 || echo "onslaught-ce: note: no 32-bit libdecor (lib32-libdecor / libdecor-0-0:i386); windows have no title bar on some desktops" >&2
else
	check libX11.so.6      lib32-libx11       libx11-6:i386
	check libXext.so.6     lib32-libxext      libxext6:i386
	for o in libXrandr.so.2 libXcursor.so.1 libXi.so.6 libXfixes.so.3; do
		have "$o" || echo "onslaught-ce: note: no 32-bit $o (SDL works without it, with fewer display features)" >&2
	done
fi
# A 32-bit Vulkan driver: an ICD manifest whose library resolves to a 32-bit ELF (Arch names the library without a
# path, shared by both architectures; Debian installs per-architecture manifests with absolute paths).
# The Vulkan loader's own override (VK_DRIVER_FILES, or the older VK_ICD_FILENAMES: what the Steam Linux Runtime's
# container sets for the drivers it imports) is used instead of the system folders when set.
icd=0 good=0
files=()
if [ -n "${VK_DRIVER_FILES:-${VK_ICD_FILENAMES:-}}" ]; then
	IFS=: read -r -a files <<<"${VK_DRIVER_FILES:-$VK_ICD_FILENAMES}"
else
	files=(/usr/share/vulkan/icd.d/*.json /etc/vulkan/icd.d/*.json)
fi
for f in "${files[@]}"; do
	[ -e "$f" ] || continue
	lib=$(sed -n 's/.*"library_path"[^"]*"\([^"]*\)".*/\1/p' "$f")
	ok=0
	case "$lib" in
		/*) [ -e "$lib" ] && is32 "$lib" && ok=1 ;;
		?*) have "$lib" && ok=1 ;;
	esac
	[ $ok = 1 ] || continue
	icd=1
	# dxvk 3.1 (Direct3D 9 over Vulkan; dxvk_device_info.cpp) takes only a device with Vulkan 1.3, the extensions it
	# requires (among them VK_KHR_load_store_op_none, VK_KHR_maintenance5 and VK_KHR_maintenance6) and 256 bytes of push
	# constants. The manifest states the version (as 1.N); a Mesa driver (libvulkan_*.so) whose file lacks one of those
	# extension names cannot offer it (Ubuntu 22.04's Mesa 23.2, Debian 12's 22.3). The push-constant size cannot be seen
	# here (Fedora 40's Intel driver, Mesa 24.1, offers 128 bytes and is refused).
	usable=1
	v=$(sed -n 's/.*"api_version"[^"]*"1\.\([0-9]*\)\..*/\1/p' "$f")
	[ -n "$v" ] && [ "$v" -lt 3 ] && usable=0
	case "$lib" in /*) path=$lib ;; *) path=$(where32 "$lib") ;; esac
	case "${path##*/}" in
		libvulkan_*)
			for e in VK_KHR_load_store_op_none VK_KHR_maintenance5 VK_KHR_maintenance6; do
				grep -aqF "$e" "$path" || usable=0
			done ;;
	esac
	[ $usable = 1 ] && good=1
done
if [ ${#missing[@]} -gt 0 ]; then
	echo "onslaught-ce: missing 32-bit system libraries:" >&2
	arch=(); deb=()
	for m in "${missing[@]}"; do
		IFS='|' read -r so a d <<<"$m"
		echo "  $so" >&2
		case " ${arch[*]} " in *" $a "*) ;; *) arch+=("$a") ;; esac
		case " ${deb[*]} " in *" $d "*) ;; *) deb+=("$d") ;; esac
	done
	echo "Arch (multilib enabled):  sudo pacman -S --needed ${arch[*]}" >&2
	echo "Debian/Ubuntu:            sudo dpkg --add-architecture i386 && sudo apt update && sudo apt install ${deb[*]}" >&2
	echo "$SKIP" >&2
	exit 1
fi
# glibc: the bundled programs and libraries need GLIBC_@GLIBC@ symbols (package.py checks the number when packaging);
# the newest GLIBC_2.N version the 32-bit libc.so.6 defines is its release.
need=@GLIBC@
libc=$(where32 libc.so.6) || libc=
if [ -n "$libc" ]; then
	got=$(grep -ao 'GLIBC_2\.[0-9]*' "$libc" | sed 's/^GLIBC_2\.//' | sort -n | tail -1)
	if [ -n "$got" ] && [ "$got" -lt "${need#2.}" ]; then
		echo "onslaught-ce: this build needs glibc $need or newer, and your 32-bit glibc is 2.$got." \
		     "Update your distribution (ldd --version shows its glibc)." >&2
		exit 1
	fi
fi
if [ $icd = 0 ]; then
	echo "onslaught-ce: no 32-bit Vulkan driver found (icd.d). Install your GPU's 32-bit driver, for example:" >&2
	echo "  Arch: lib32-vulkan-radeon, lib32-vulkan-intel or lib32-nvidia-utils;" \
	     "Debian/Ubuntu: mesa-vulkan-drivers:i386 or the NVIDIA i386 libraries" >&2
	echo "  $SKIP" >&2
	exit 1
fi
if [ $good = 0 ]; then
	echo "onslaught-ce: note: your 32-bit Vulkan driver seems to lack what the game's graphics layer (dxvk 3.1) requires:" \
	     "Vulkan 1.3 with VK_KHR_load_store_op_none, VK_KHR_maintenance5 and VK_KHR_maintenance6. The game may stop at" \
	     "its start; a newer 32-bit driver can help (Mesa 24.1 has these extensions; Debian 12 has a newer Mesa in" \
	     "bookworm-backports)." >&2
fi
exit 0
'''


def strip(src: Path, dst: Path, windows: bool):
    tool = deps.DATA / ("llvm-mingw/bin/llvm-strip" if windows else "llvm/bin/llvm-strip")
    subprocess.run([str(tool), "--strip-all", "-o", str(dst), str(src)], check=True)


def machine_paths(path: Path) -> list:
    home = str(Path.home())
    private = [p for p in os.environ.get("BEA_PRIVATE_PATHS", "").split(":") if p]
    pat = re.compile("|".join(re.escape(p) for p in [home, home.replace("/", "\\"), str(ROOT), Path.home().name] + private))
    out = subprocess.run(["strings", "-a", str(path)], capture_output=True, text=True, errors="replace").stdout
    out += subprocess.run(["strings", "-a", "-el", str(path)], capture_output=True, text=True, errors="replace").stdout
    return sorted({l for l in out.splitlines() if pat.search(l)})


def links_enet(symbols: str) -> bool:
    """Whether an `nm` listing shows ENet linked into the program."""
    return re.search(r"(?<![A-Za-z0-9])_?enet_(?:host_create|host_service|peer_send|packet_create)\b", symbols) is not None


def links_ice(symbols: str) -> bool:
    """ICE/libjuice in an unstripped Linux or Windows symbol listing, including older builds without the tag."""
    return re.search(r"(?<![A-Za-z0-9_])(?:(?:__imp_)?_?(?:juice_[A-Za-z0-9_]+|CE_NetCreateIce|"
                     r"ce_network_ice_feature_marker)\b|CeIce(?:Bridge|Session)::)", symbols) is not None


def check_public(binary: Path, windows: bool, *, with_ice=False, with_native_signaling=False):
    if binary.exists():
        has_native_marker = b"CE-NATIVE-SIGNALING-V1" in binary.read_bytes()
        if has_native_marker != with_native_signaling:
            sys.exit("native-signaling binary requires its explicit package opt-in and matching marker")
    elif with_native_signaling:
        sys.exit("native-signaling package binary is missing")
    target = "windows" if windows else "linux"
    deps.require_ffmpeg_shared(target)
    ice_error = f"{binary}: contains ICE/libjuice; its licence and notice packaging has not been reviewed"
    try:
        has_ice_marker = ICE_FEATURE_MARKER in binary.read_bytes()
    except OSError:
        sys.exit(f"{binary}: package candidate missing or unreadable; build with build.py --public")
    if has_ice_marker and not with_ice:
        sys.exit(ice_error)
    program = binary.read_bytes() if windows and not ALLOW_UNREPLACED else b""
    if MODS_CHECK_COMMAND.encode() in program:		# a CE program: whether its mod loader may be on depends on mods.cpp
        try:
            mods_source = MODS_CPP.read_text(errors="replace")
        except OSError:
            sys.exit(f"{MODS_CPP}: cannot be read, and packaging a Windows program needs it to tell whether mods are off on Windows")
        if problem := mods_windows_switch_problem(program, mods_source):
            sys.exit(f"{binary}: {problem}")
    marker = WORK / "mirror/public-unreplaced.txt"
    if not marker.exists():
        sys.exit(f"{marker}: missing; build with build.py --public")
    if marker.read_text().strip() and not ALLOW_UNREPLACED:
        sys.exit("the public build still compiled src/ files it must replace: " + marker.read_text().split().__str__())
    try:
        syms = subprocess.run([str(deps.DATA / ("llvm-mingw/bin/llvm-nm" if windows else "llvm/bin/llvm-nm")), "-C", str(binary)],
                              capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        sys.exit(f"{binary}: cannot inspect candidate symbols; rebuild an unstripped public binary")
    if not syms.strip():
        sys.exit(f"{binary}: no candidate symbols; packaging requires an unstripped public binary")
    has_ice_symbols = links_ice(syms)
    if has_ice_symbols and not with_ice:
        sys.exit(ice_error)
    if with_ice and not (has_ice_marker and has_ice_symbols):
        sys.exit(f"{binary}: explicit ICE package requires its retained feature tag and unstripped ICE symbols")
    if re.search(r"NvStripifier|NvStripInfo|VertexCache", syms):
        sys.exit(f"{binary}: contains NvTriStrip; build with build.py --public")
    has_enet = links_enet(syms)
    if has_enet and not NETWORK:
        sys.exit(f"{binary}: contains ENet, but the build folder is the default public flavour (no ENet notice would ship); rebuild without the network or with --with-network")
    if NETWORK and not has_enet:
        sys.exit(f"{binary}: the build folder says --with-network but the program has no ENet; rebuild with build.py --public --with-network")
    expected = {path.name for path in deps.ffmpeg_shared_files(target, runtime=True)}
    if windows:
        expected = {name.lower() for name in expected}
        tool = deps.DATA / "llvm-mingw/bin/llvm-readobj"
        imports = subprocess.run([str(tool), "--coff-imports", str(binary)], capture_output=True, text=True, check=True).stdout
        if with_native_signaling and not re.search('(?im)^\\s*Name:\\s*WINHTTP\\.dll\\s*$', imports):
            sys.exit('native-signaling Windows binary does not import WinHTTP')
        needed = {name.lower() for name in re.findall(r"^\s+Name: (\S+)\s*$", imports, re.M)}
    else:
        dyn = subprocess.run(["readelf", "-d", str(binary)], capture_output=True, text=True, check=True).stdout
        if with_native_signaling and not re.search('\\(NEEDED\\).*\\[libcurl-gnutls\\.so\\.4\\]', dyn):
            sys.exit('native-signaling Linux binary does not import pinned libcurl-GnuTLS')
        needed = set(re.findall(r"\(NEEDED\).*Shared library: \[([^]]+)\]", dyn))
        if "[$ORIGIN/lib]" not in dyn:
            sys.exit(f"{binary}: RUNPATH is not $ORIGIN/lib; build with build.py --public")
    if missing := expected - needed:
        sys.exit(f"{binary}: missing public FFmpeg shared dependencies: {', '.join(sorted(missing))}; rebuild with build.py --public")


# The system libraries the Linux package links against: every symbol version the bundled programs and libraries need
# from these (GLIBC_, GLIBCXX_, CXXABI_, GCC_) must be one the old-glibc sysroot's own copy defines, so the package loads
# with the Steam Runtime 3 libraries (Debian 11: glibc 2.31, GCC 10's libstdc++ and libgcc_s) and everything newer.
SYSTEM_LIBS = ("libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1", "libstdc++.so.6", "libgcc_s.so.1")


def version_needs(f):
    """{library soname: {version names}} that ELF f needs (readelf's .gnu.version_r)."""
    out = subprocess.run(["readelf", "-V", "-W", str(f)], capture_output=True, text=True, check=True).stdout
    needs, lib = {}, None
    for line in out.split("Version needs section", 1)[-1].splitlines() if "Version needs section" in out else []:
        m = re.search(r"File: (\S+)", line)
        if m:
            lib = m.group(1)
            continue
        m = re.search(r"Name: (\S+)", line)
        if m and lib:
            needs.setdefault(lib, set()).add(m.group(1))
    return needs


def version_defs(f):
    """The version names ELF f defines (readelf's .gnu.version_d)."""
    out = subprocess.run(["readelf", "-V", "-W", str(f)], capture_output=True, text=True, check=True).stdout
    if "Version definition section" not in out:
        return set()
    part = out.split("Version definition section", 1)[1].split("Version needs section", 1)[0]
    return set(re.findall(r"Name: (\S+)", part))


def sysroot_gaps(elfs):
    """Needed system-library versions the sysroot's libraries do not define ([] when every one is there)."""
    defs, gaps = {}, []
    for f in elfs:
        for lib, names in version_needs(f).items():
            if lib not in SYSTEM_LIBS:
                continue
            if lib not in defs:
                where = [d / lib for d in (deps.SYSROOT / "lib/i386-linux-gnu", deps.SYSROOT / "usr/lib/i386-linux-gnu")]
                found = next((w for w in where if w.exists()), None)
                defs[lib] = version_defs(found.resolve()) if found else set()
            gaps += [f"{Path(f).name}: {lib} {n}" for n in sorted(names - defs[lib])]
    return gaps


def glibc_needed(elfs) -> str:
    """The newest GLIBC_2.N symbol version any of elfs requires (readelf's version needs), as "2.N"."""
    newest = 0
    for f in elfs:
        out = subprocess.run(["readelf", "-V", "-W", str(f)], capture_output=True, text=True, check=True).stdout
        for m in re.finditer(r"Name: GLIBC_2\.(\d+)", out):
            newest = max(newest, int(m.group(1)))
    return f"2.{newest}"


def source_epoch() -> int:
    """Every archive member's date: SOURCE_DATE_EPOCH when set, otherwise this checkout's last commit date (an export
    of the public repository carries its source commit's date, tools/export_public.py --init). Fixed per release, so
    the packages stay reproducible, and a real date where 0 showed as 1970."""
    if os.environ.get("SOURCE_DATE_EPOCH"):
        return int(os.environ["SOURCE_DATE_EPOCH"])
    r = subprocess.run(["git", "-C", str(ROOT), "log", "-1", "--format=%ct"], capture_output=True, text=True)
    if r.returncode or not r.stdout.strip():
        sys.exit("no SOURCE_DATE_EPOCH and no git commit to date the packages by")
    return int(r.stdout.strip())


def add_bytes(tar: tarfile.TarFile, name: str, data: bytes, mode=0o644, mtime=0):
    info = tarfile.TarInfo(name)
    info.size, info.mode, info.mtime, info.uname, info.gname = len(data), mode, mtime, "root", "root"
    tar.addfile(info, io.BytesIO(data))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("version")
    ap.add_argument("--out", type=Path)
    ap.add_argument("--test", action="store_true", help="local test packages only: allow a build that still compiled "
                                                        "src/ files the public build must replace, or a Windows build with the mod "
                                                        "loader switched on (build.py --mods-windows-test); never upload these")
    ap.add_argument("--build", type=Path, help="the build folder (default this checkout's local-data/port/build)")
    ap.add_argument("--with-ice", action="store_true", help="explicit experimental ICE package: require exact ICE profile/receipts and include pinned libjuice source and licences")
    ap.add_argument("--with-native-signaling", action="store_true", help="include the pinned native HTTPS dependencies, notices and matching source (requires --with-ice)")
    a = ap.parse_args()
    # Both runtime DLLs and the Linux SDL library stay fixed from validation until the archives are written.
    with deps.sdl_install_lock("linux"), deps.sdl_install_lock("windows"):
        package(a)


def build_flavour(build: Path, *, with_ice=False) -> str:
    """Public flavour; the optional ICE profile is recognised only with an explicit opt-in."""
    marker = build / "flavour.txt"
    word = marker.read_text().strip() if marker.exists() else "public"
    if word not in ("public", "public-network", *(("public-network-ice",) if with_ice else ())):
        sys.exit(f"{marker}: unknown flavour {word!r}")
    return word


def package(a):
    global ALLOW_UNREPLACED, NETWORK
    ALLOW_UNREPLACED = a.test
    build = a.build or WORK / "build"
    with_ice = getattr(a, "with_ice", False)
    with_native_signaling = getattr(a, "with_native_signaling", False)
    if with_native_signaling and not with_ice:
        sys.exit("--with-native-signaling requires --with-ice")
    native_payloads = None
    flavour = build_flavour(build, with_ice=with_ice)
    if with_ice and (flavour != "public-network-ice" or a.test):
        sys.exit("--with-ice requires exactly public-network-ice and does not allow --test")
    NETWORK = flavour in ("public-network", "public-network-ice")
    ice_payloads = None
    if with_ice:
        import package_ice_inputs
        if with_native_signaling:
            import package_signaling_inputs
            ice_payloads = package_ice_inputs.prepare(build, with_native_signaling=True)
            try:
                native_payloads = package_signaling_inputs.prepare()
            except (OSError, ValueError) as error:
                sys.exit(f"native-signaling package inputs: {error}")
        else:
            ice_payloads = package_ice_inputs.prepare(build)
    # Validate both candidates and their selected shared inputs before creating any package or copying symbols.
    for windows in (False, True):
        binary = build / ("windows/bea.exe" if windows else "linux/bea")
        if with_ice:
            if with_native_signaling:
                check_public(binary, windows, with_ice=True, with_native_signaling=True)
            else:
                check_public(binary, windows, with_ice=True)
        else:
            check_public(binary, windows)
    for target in ("linux", "windows"):
        deps.require_sdl_receipt(target)
        deps.require_sdl_link_receipt(build, target)
    out = a.out or WORK / "release" / (a.version + "-ice" if with_ice else a.version)
    (out / "symbols").mkdir(parents=True, exist_ok=True)
    made = []
    epoch = source_epoch()
    for windows in (False, True):
        src = build / ("windows/bea.exe" if windows else "linux/bea")
        exe = "bea.exe" if windows else "bea"
        saved = out / "symbols" / (f"bea-{a.version}.exe" if windows else f"bea-{a.version}")
        shutil.copy2(src, saved)
        ice_payload = ice_payloads["windows" if windows else "linux"] if with_ice else None
        native_payload = native_payloads["windows" if windows else "linux"] if native_payloads else None
        if with_ice:
            package_ice_inputs.check_snapshot(saved, ice_payload["binaries"]["game"])
            src = saved
        stripped = out / ("stage-" + exe)
        strip(src, stripped, windows)
        bad = machine_paths(stripped)
        if bad:
            sys.exit(f"{stripped}: machine paths remain:\n  " + "\n  ".join(bad[:20]))
        top = f"onslaught-ce-{a.version}-{'windows' if windows else 'linux'}-i686"
        files = {f"{top}/{exe}": (stripped.read_bytes(), 0o755)}
        launcher = build / ("windows/onslaught-ce.exe" if windows else "linux/onslaught-ce-launch")
        launcher_name = launcher.name
        if b"CE-UPDATER-TEST-BUILD" in launcher.read_bytes():
            sys.exit(f"{launcher}: an updater test build (test key, test hooks); build the release launcher")
        if with_ice:
            saved_launcher = out / "symbols" / (f"onslaught-ce-{a.version}.exe" if windows else f"onslaught-ce-launch-{a.version}")
            shutil.copy2(launcher, saved_launcher)
            package_ice_inputs.check_snapshot(saved_launcher, ice_payload["binaries"]["launcher"])
            launcher = saved_launcher
        lstripped = out / ("stage-" + launcher_name)
        strip(launcher, lstripped, windows)
        if machine_paths(lstripped):
            sys.exit(f"{lstripped}: machine paths remain")
        files[f"{top}/{launcher_name}"] = (lstripped.read_bytes(), 0o755)
        lstripped.unlink()
        files.update({f"{top}/{k}": (v, 0o644) for k, v in licences(windows).items()})
        files[f"{top}/THIRD-PARTY-NOTICES.txt"] = (notices(a.version, windows), 0o644)
        files[f"{top}/README.txt"] = (readme(a.version, windows), 0o644)
        files[f"{top}/NOTICE.txt"] = (notice(a.version, windows), 0o644)
        if with_ice:
            files.update({f"{top}/{name}": (data, 0o644) for name, data in ice_payload["files"].items()})
            files[f"{top}/THIRD-PARTY-NOTICES.txt"] = (files[f"{top}/THIRD-PARTY-NOTICES.txt"][0] + b"\n" + ice_payload["notice"], 0o644)
            files[f"{top}/NOTICE.txt"] = (files[f"{top}/NOTICE.txt"][0] + b"\nSee ICE-SOURCE-NOTICE.txt for libjuice source and licence options.\n", 0o644)
            files[f"{top}/README.txt"] = (package_ice_inputs.readme(files[f"{top}/README.txt"][0]), 0o644)
        if native_payload:
            for name, data in native_payload["files"].items():
                key = f"{top}/{name}"
                if key in files:
                    sys.exit("native-signaling input collides with an existing package file")
                files[key] = (data, 0o644)
            for name in ("THIRD-PARTY-NOTICES.txt", "NOTICE.txt"):
                key = f"{top}/{name}"
                files[key] = (files[key][0] + native_payload["notice"], files[key][1])
            key = f"{top}/README.txt"
            files[key] = (files[key][0] + b"\nNative-signaling dependency source and license notices are included; see NATIVE-SIGNALING-SOURCE-NOTICE.txt.\n", files[key][1])
        shipped = list(WINDOWS_DLLS) if windows else [(d / so).resolve() for d, so in LINUX_LIBS]
        bad = [f"{f.name}: {l}" for f in shipped for l in machine_paths(f)]
        if bad:
            sys.exit("bundled libraries name machine paths:\n  " + "\n  ".join(bad[:20]))
        missing = [f for row in third_party(windows) for f in row[4] if f"{top}/{f}" not in files]
        missing += [f for f in ("COPYING", "NOTICE.txt", "THIRD-PARTY-NOTICES.txt", "README.txt") if f"{top}/{f}" not in files]
        if missing:
            sys.exit(f"{top}: missing {missing}")
        if windows:
            files.update({f"{top}/{d.name}": (d.read_bytes(), 0o644) for d in WINDOWS_DLLS})
            pkg = out / f"{top}.zip"
            with zipfile.ZipFile(pkg, "w", zipfile.ZIP_DEFLATED) as z:
                for name, (data, mode) in sorted(files.items()):
                    info = zipfile.ZipInfo(name, time.gmtime(epoch)[:6])
                    info.external_attr = mode << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    z.writestr(info, data)
        else:
            for d, so in LINUX_LIBS:
                files[f"{top}/lib/{so}"] = ((d / so).resolve().read_bytes(), 0o755)
            files[f"{top}/onslaught-ce"] = (LAUNCHER.encode(), 0o755)
            files[f"{top}/preflight.sh"] = (PREFLIGHT.replace("@GLIBC@", GLIBC).encode(), 0o755)
            native_elfs = []
            if native_payload:
                for soname, data in native_payload["libraries"].items():
                    key = f"{top}/lib/{soname}"
                    if key in files:
                        if files[key][0] != data:
                            sys.exit("native-signaling SDK library conflicts with an existing runtime")
                    else:
                        native_elfs.append(native_payload["paths"][soname])
                    files[key] = (data, 0o755)
            needed = glibc_needed(list([src, launcher] + [(d / so).resolve() for d, so in LINUX_LIBS]) + native_elfs)
            if needed != GLIBC:
                sys.exit(f"{top}: the programs and libraries need glibc {needed}, but package.py's GLIBC (README.txt, "
                         f"preflight.sh, public/README.md and the release notes) says {GLIBC}")
            if deps.SYSROOT:
                gaps = sysroot_gaps(list([src, launcher] + [(d / so).resolve() for d, so in LINUX_LIBS]) + native_elfs)
                if gaps:
                    sys.exit(f"{top}: needs system library versions the Steam Runtime 3 sysroot does not have:\n  " + "\n  ".join(gaps))
            pkg = out / f"{top}.tar.gz"
            # gzip's header carries a time too: the same fixed epoch, so the archive's bytes are reproducible
            with open(pkg, "wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch) as gz, \
                    tarfile.open(fileobj=gz, mode="w", format=tarfile.GNU_FORMAT) as t:
                for name, (data, mode) in sorted(files.items()):
                    add_bytes(t, name, data, mode, epoch)
        stripped.unlink()
        made.append(pkg)
    # The release's copyleft source asset: FFmpeg's exact pinned tarball ships beside the packages.
    ff = deps.lock()["FFmpeg"]
    data = ff["file"].read_bytes()
    if sha256(data) != ff["sha"]:
        sys.exit(f"{ff['file']}: SHA-256 differs from deps.lock")
    shutil.copy2(ff["file"], out / ff["file"].name)
    made.append(out / ff["file"].name)
    for pkg in made:
        print(f"{sha256(pkg.read_bytes())}  {pkg.name}  ({pkg.stat().st_size} bytes)")
    print(f"unstripped binaries: {out / 'symbols'}")


if __name__ == "__main__":
    main()
