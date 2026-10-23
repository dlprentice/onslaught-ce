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
FFMPEG_LINUX = deps.FFMPEG_SHARED / "linux/usr/lib"
FFMPEG_WINDOWS = deps.FFMPEG_SHARED / "windows/usr/bin"
LINUX_LIBS = [(deps.DATA / "dxvk-native/usr/lib32", "libdxvk_d3d9.so.0"), (deps.PREFIX / "lib", "libSDL3.so.0"),
              (deps.PREFIX / "lib", "libxkbcommon.so.0"), (FFMPEG_LINUX, "libavformat.so.61"),
              (FFMPEG_LINUX, "libavcodec.so.61"), (FFMPEG_LINUX, "libavutil.so.59")]
WINDOWS_DLLS = [deps.PREFIX_WIN / "bin/SDL3.dll", deps.PREFIX_WIN / "bin/libz.dll", FFMPEG_WINDOWS / "avformat-61.dll",
                FFMPEG_WINDOWS / "avcodec-61.dll", FFMPEG_WINDOWS / "avutil-59.dll"]
# The newest glibc symbol version the Linux package's programs and libraries need (their .gnu.version_r, checked by
# glibc_needed when packaging): README.txt, preflight.sh, public/README.md and the release notes state it.
GLIBC = "2.44"
GLIBC_TEXT = (f"glibc {GLIBC} or newer, which for now means Arch and other rolling-release distributions (Debian 13 "
              "and Ubuntu 26.04 and older have an older glibc; ldd --version shows yours)")
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
    rows = [("DirectXTex", "1acf4ebe", "MIT", "the DXT block encoder (port/d3dx/src/bc.cpp) is adapted from it", ["licenses/DirectXTex-LICENSE"]),
            ("Monocypher", lock["monocypher"]["version"], "BSD-2-Clause OR CC0-1.0", "the launcher's update-signature check",
             ["licenses/Monocypher-LICENCE.md"]),
            ("FFmpeg", lock["FFmpeg"]["version"], "LGPL-2.1-or-later", f"{ffl}: shared libraries, decoders and demuxer only (Bink video and audio)",
             ["licenses/FFmpeg-COPYING.LGPLv2.1", "licenses/FFmpeg-LICENSE.md"]),
            ("libvorbis, libogg", "1.0", "BSD-3-Clause", "statically linked (libvorbis_vc6.a: Xiph's 1.0 source built with Visual C++ 6, as the "
             "original game's DLLs; vendor string \"Xiph.Org libVorbis I 20020717\")", ["licenses/libvorbis-1.0-COPYING", "licenses/libogg-1.0-COPYING"]),
            ("SDL3", lock["SDL3"]["version"], "Zlib", ("SDL3.dll" if windows else "lib/libSDL3.so.0") +
             ", patched: port/deps-patches/SDL3-offscreen-fullscreen-modes.patch in the source", ["licenses/SDL3-LICENSE.txt"]),
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


# The Microsoft sample framework the game's source release carries (compiled into both programs).
D3DAPP = ["src/d3dapp.cpp and src/d3dapp.h are Microsoft's DirectX SDK sample application framework, (c) Microsoft Corporation,",
          "carried because Stuart Gillam's GPL release of the game's source includes them; Microsoft has not licensed them",
          "under the GPL."]


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
        text += ["The source paths under llvm-mingw/llvm-project in bea.exe come from llvm-mingw's own build machine,",
                 "inside its statically linked libc++abi and libunwind.", ""]
    return "\n".join(text).encode()


# Report a problem's one network action, in the same words as public/README.md and SECURITY.md.
REPORT_PRIVACY = ["Report a problem opens a GitHub page in your browser with your version and OS in its address;",
                  "nothing is posted until you submit it."]


def readme(version: str, windows: bool) -> bytes:
    launcher = "onslaught-ce.exe" if windows else "onslaught-ce"
    common = ["It finds your Battle Engine Aquila install in Steam's libraries, or asks you once for its folder and",
              "remembers it (BEA_GAME_DIR overrides). Your game folder is only read: careers and settings go to " +
              ("%APPDATA%\\Onslaught CE." if windows else "~/.local/share/onslaught-ce."),
              "On start it checks this project's GitHub releases for a newer signed version and asks before updating",
              "(turn it off with update_check=0 in ce.ini or BEA_NO_UPDATE_CHECK=1). There is no telemetry: the update",
              "check and a download you agree to are the only network requests it makes by itself.", *REPORT_PRIVACY]
    run = (["Start onslaught-ce.exe. (bea.exe also runs directly, without the install search and update check.)"] + common +
           ["This Windows build was tested under Wine on Linux only (see Known limitations)."] if windows else
           ["Run ./onslaught-ce [game arguments]. It first checks for the 32-bit system libraries it needs and names the",
            "packages to install when one is missing. It needs " + GLIBC_TEXT + "."] + common)
    exe = "bea.exe" if windows else "bea"
    return "\n".join([f"Onslaught CE {version} (preview)", ""] + quick_start(windows) + ["",
                      "A native source port of Battle Engine Aquila (2003). It contains no game files: you need your own",
                      "copy of the game (for example from Steam).", ""] + run +
                     ["", f"{exe} is Onslaught CE's game program; {launcher} is the program to start.", ""] +
                     IN_THE_GAME + help_lines(windows) + limitations(windows) + NOTICE_LINES +
                     ["", "Licence: GPL-3.0-only (COPYING). Third-party components: THIRD-PARTY-NOTICES.txt and licenses/.", ""]).encode()


# README.txt's "In the game" and "Screen modes and widescreen" (public/README.md says the same).
IN_THE_GAME = [
    "In the game",
    "- Main menu > Onslaught CE: About (version, licence and links), Check for updates (explains the check the",
    "  launcher makes at every start, how to be asked again about a skipped version, and how to turn checks back",
    "  on), Record sessions and Report a problem (below).",
    "- Options > Cheats: the game's own cheats as On/Off switches, kept in ce.ini so they apply to every career;",
    "  the line under the list says what the highlighted one does:",
    "  - Unlock all levels (original code TURKEY): every level opens in the level select",
    "  - Unlock all goodies (MALLOY): every goodie opens on the Goodies page",
    "  - Goodie hints (lat\u00eate): the Goodies page shows how to unlock each goodie; while it is on, goodies",
    "    cannot be opened",
    "  - God mode option (Maladim): adds a God switch to Controller Options, in the pause menu (applies at once)",
    "    and in Options (applies from the next level), for no damage and endless energy; turning the cheat off",
    "    ends god mode. God mode set this way is kept in ce.ini, not in your careers, so a recorded session never",
    "    carries it.",
    "  - Free camera (F key, Aurore): in a level, F lets you fly the camera; F again returns it",
    "  They are off while a session is being recorded. Typing a code into a career's name still works as in the",
    "  original.",
    "- Keyboard: no key release is lost, typed characters come from the key presses themselves (no desktop input",
    "  method in the way), and the letter and digit keys work by their position on any keyboard layout.",
    "- A quiet console: the developers' messages are hidden unless BEA_PORT_VERBOSE=1 is set.",
    "- A log file for reports, onslaught-ce.log in your user folder, and session recording (both below).",
    "",
    "Screen modes and widescreen",
    "Onslaught CE starts at your desktop's resolution, and Video Options lists your monitor's sizes, widescreen ones",
    "included. On a widescreen monitor the 3D view shows more to the sides (the original's vertical view is kept); the",
    "menus, loading pictures and films keep their original 4:3 shape, centred with black bars, and the in-game HUD sits",
    "where it would on a 4:3 screen. To get the original's 4:3-only list and 640 x 480 start back, set",
    "BEA_PORT_WIDESCREEN=0. Replays and recordings always use the original 4:3 behaviour. The original game shares one",
    "random number source between visual effects and play, so a wider view can change the details of a fight.",
    "",
]


def help_lines(windows: bool) -> list:
    """README.txt's "Report a problem" and "Help make Onslaught CE more faithful" (public/README.md says the same)."""
    launcher = "onslaught-ce.exe" if windows else "./onslaught-ce"
    user = "%APPDATA%\\Onslaught CE" if windows else "~/.local/share/onslaught-ce"
    ini = user + ("\\" if windows else "/") + "ce.ini"
    where = (" (from a Command Prompt in this folder, or a shortcut with --report added)" if windows else "")
    return ["Report a problem",
            f"Choose Onslaught CE > Report a problem in the main menu, or run {launcher} --report{where}.",
            "It opens a new issue on the project's GitHub page in your browser, filled in with your version and",
            "system, and shows the folder that holds your log. Nothing is posted until you submit the issue. Say what",
            "happened and how to make it happen again, and attach onslaught-ce.log from that folder",
            f"({user}; onslaught-ce.old.log is the run before, and each stops at 4 MB). The log holds",
            "the version, your OS and GPU and the game's messages. It writes your " +
            ("user folders as %APPDATA%" if windows else "home folder as ~") + " and never your account name,",
            "but look it over before you attach it. If you recorded the session (below), attach its tape too.",
            "",
            "Help make Onslaught CE more faithful",
            "The goal is a game that behaves exactly like the original, frame by frame; recordings of real play find",
            f"differences our own tests miss. Record one session with {launcher} --record, or every session",
            f"with Onslaught CE > Record sessions in the main menu. That starts the next time you launch Onslaught CE",
            f"(quit the game, then start it again); it puts record_sessions=1 in {ini}.",
            f"While it is on, each start asks whether to record: Stop recording there, or starting with",
            f"{launcher} --no-record, turns it off again.",
            "While recording the game runs at 30 frames a second (the timing replays use), its menus take the keyboard",
            "only (the mouse still works in battle), the screen is the original 4:3 (no widescreen), and the menus are",
            "the original's: the version text, no Onslaught CE menu and no Cheats page, and only cheats typed into a",
            "career's name work.",
            "Each session is saved in the tapes folder beside ce.ini as a .tape (your key presses and mouse movement",
            "only) and a .tape.json (version, OS, date, game arguments, and a copy of your options and saved careers as",
            "the session started). To share one, zip the two files and attach them to an issue, saying what differed",
            "from the original and roughly when.",
            ""]


def limitations(windows: bool) -> list:
    """README.txt's "Known limitations", this platform's lines only (public/README.md and the release notes list
    both platforms')."""
    platform = (["- This Windows build has been tested only under Wine on Linux, not on real Windows. Its self-update path",
                 "  in particular is untested on real Windows. Please report any problems.",
                 "- The executables are unsigned, so SmartScreen may warn you (choose More info, then Run anyway)."] if windows else
                ["- You need " + GLIBC_TEXT + ".",
                 "- You need 32-bit graphics drivers (Vulkan)."])
    return ["Known limitations", *platform,
            "- The tutorial is playable through its third zone; its final drone fight is still being completed.",
            "- Joystick and controller support is limited to what the original game supported, and controllers have not",
            "  been tested yet.",
            "- Widescreen play is not compared with the original (the original has no widescreen); replays and recordings",
            "  use 4:3.",
            "- Planned, not in this version yet: multiplayer, mods and HUD options.",
            ""]


def quick_start(windows: bool) -> list:
    """The quick start for this platform's package (the repository README, public/README.md, gives both)."""
    exe, user = ("onslaught-ce.exe", "%APPDATA%\\Onslaught CE") if windows else ("./onslaught-ce", "~/.local/share/onslaught-ce")
    return ["Quick start",
            "1. Extract the download into any folder, for example your Downloads folder, not your game folder. You can move",
            "   the folder anywhere later.",
            f"2. Run {exe}.",
            "3. It finds your Steam copy of Battle Engine Aquila by itself, or asks once where it is.",
            "",
            "Don't copy or replace anything in your game folder: Onslaught CE only reads it, and the original game keeps",
            f"working as before. Your careers and settings are kept in {user}; delete the program folder to",
            "uninstall."]


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
# Engine Aquila install (BEA_GAME_DIR, ce.ini, Steam's libraries, or asks once), checks for a signed update and starts
# the game from this folder. Your game folder is only read.
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
case "${1:-}" in
	--version|--report|--update-only) ;;	# these start no game: no library check
	*) if [ "${BEA_SKIP_PREFLIGHT:-}" != 1 ]; then "$HERE/preflight.sh" || exit 1; fi ;;
esac
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
where32() { # $1 = soname: the 32-bit copy's path
	for d in /usr/lib32 /usr/lib/i386-linux-gnu /lib/i386-linux-gnu /usr/lib/i686-linux-gnu /usr/lib; do
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
command -v curl >/dev/null 2>&1 || echo "onslaught-ce: note: curl is not installed; the update check needs curl" >&2
# A 32-bit Vulkan driver: an ICD manifest whose library resolves to a 32-bit ELF (Arch names the library without a
# path, shared by both architectures; Debian installs per-architecture manifests with absolute paths).
icd=0
for f in /usr/share/vulkan/icd.d/*.json /etc/vulkan/icd.d/*.json; do
	[ -e "$f" ] || continue
	lib=$(sed -n 's/.*"library_path"[^"]*"\([^"]*\)".*/\1/p' "$f")
	case "$lib" in
		/*) [ -e "$lib" ] && is32 "$lib" && icd=1 ;;
		?*) have "$lib" && icd=1 ;;
	esac
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
		echo "onslaught-ce: this build needs glibc $need or newer, and your 32-bit glibc is 2.$got. For now that means" \
		     "Arch or another rolling-release distribution; a build for older distributions is not available yet." >&2
		echo "  $SKIP" >&2
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


def check_public(binary: Path, windows: bool):
    marker = WORK / "mirror/public-unreplaced.txt"
    if not marker.exists():
        sys.exit(f"{marker}: missing; build with build.py --public")
    if marker.read_text().strip() and not ALLOW_UNREPLACED:
        sys.exit("the public build still compiled src/ files it must replace: " + marker.read_text().split().__str__())
    syms = subprocess.run([str(deps.DATA / ("llvm-mingw/bin/llvm-nm" if windows else "llvm/bin/llvm-nm")), "-C", str(binary)],
                          capture_output=True, text=True).stdout
    if re.search(r"NvStripifier|NvStripInfo|VertexCache", syms):
        sys.exit(f"{binary}: contains NvTriStrip; build with build.py --public")
    if not windows:
        dyn = subprocess.run(["readelf", "-d", str(binary)], capture_output=True, text=True).stdout
        if "[$ORIGIN/lib]" not in dyn:
            sys.exit(f"{binary}: RUNPATH is not $ORIGIN/lib; build with build.py --public")


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
                                                        "src/ files the public build must replace (never upload these)")
    ap.add_argument("--build", type=Path, help="the build folder (default this checkout's local-data/port/build)")
    a = ap.parse_args()
    global ALLOW_UNREPLACED
    ALLOW_UNREPLACED = a.test
    out = a.out or WORK / "release" / a.version
    (out / "symbols").mkdir(parents=True, exist_ok=True)
    build = a.build or WORK / "build"
    made = []
    epoch = source_epoch()
    for windows in (False, True):
        src = build / ("windows/bea.exe" if windows else "linux/bea")
        check_public(src, windows)
        exe = "bea.exe" if windows else "bea"
        shutil.copy2(src, out / "symbols" / (f"bea-{a.version}.exe" if windows else f"bea-{a.version}"))
        stripped = out / ("stage-" + exe)
        strip(src, stripped, windows)
        bad = machine_paths(stripped)
        if bad:
            sys.exit(f"{stripped}: machine paths remain:\n  " + "\n  ".join(bad[:20]))
        top = f"onslaught-ce-{a.version}-{'windows' if windows else 'linux'}-i686"
        files = {f"{top}/{exe}": (stripped.read_bytes(), 0o755)}
        launcher = build / ("windows/onslaught-ce.exe" if windows else "linux/onslaught-ce-launch")
        lstripped = out / ("stage-" + launcher.name)
        strip(launcher, lstripped, windows)
        if machine_paths(lstripped):
            sys.exit(f"{lstripped}: machine paths remain")
        files[f"{top}/{launcher.name}"] = (lstripped.read_bytes(), 0o755)
        lstripped.unlink()
        files.update({f"{top}/{k}": (v, 0o644) for k, v in licences(windows).items()})
        files[f"{top}/THIRD-PARTY-NOTICES.txt"] = (notices(a.version, windows), 0o644)
        files[f"{top}/README.txt"] = (readme(a.version, windows), 0o644)
        files[f"{top}/NOTICE.txt"] = (notice(a.version, windows), 0o644)
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
            needed = glibc_needed([src, launcher] + [(d / so).resolve() for d, so in LINUX_LIBS])
            if needed != GLIBC:
                sys.exit(f"{top}: the programs and libraries need glibc {needed}, but package.py's GLIBC (README.txt, "
                         f"preflight.sh, public/README.md and the release notes) says {GLIBC}")
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
