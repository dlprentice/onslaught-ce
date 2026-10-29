# Third-party components

Status: active
Last updated: 2026-10-28 (SDL3 also carries the X11 optional-extensions patch)
Summary: the licences and purposes of components built into or shipped with Onslaught CE.

Onslaught CE's own code is GPL-3.0-only (`COPYING`). The build fetches the components below at the versions
`port/deps.lock` pins, each checked by SHA-256; none of their source is in this repository except where a file says
so. Release packages carry each shipped component's licence text in `licenses/`.

## In the program

| Component | Version | Licence | How it is used |
| --- | --- | --- | --- |
| SDL3 | 3.4.16, with `port/deps-patches/SDL3-offscreen-fullscreen-modes.patch` and `SDL3-x11-optional-extensions.patch` | zlib | window, input and sound; a shared library beside the program (patched, as the patch file records) |
| hidapi | SDL3 3.4.16's copy (`src/hidapi`) | BSD-3-Clause (the BSD option of its three licences) | game controllers; compiled into the SDL3 library on both platforms |
| yuv2rgb | SDL3 3.4.16's copy (`src/video/yuv2rgb`) | BSD-3-Clause, (c) 2016 Adrien Descamps | YUV to RGB conversion; compiled into the SDL3 library on both platforms |
| fdlibm | SDL3 3.4.16's copy (`src/libm`) | Sun Microsystems' fdlibm notice, (c) 1993 ("provided that this notice is preserved") | maths functions; compiled into the SDL3 library on both platforms |
| stb_image | SDL3 3.4.16's copy (`src/video/stb_image.h`) | MIT OR Unlicense (public domain), (c) 2017 Sean Barrett: either, as the file offers | image loading; compiled into the SDL3 library on both platforms |
| edid-parse | SDL3 3.4.16's copy (`src/video/x11/edid-parse.c`) | MIT, (c) 2007 Red Hat | X11 monitor EDID parsing; compiled into the Linux SDL3 library |
| xsettings-client | SDL3 3.4.16's copy (`src/video/x11/xsettings-client.c`) | HPND-style, (c) 2001, 2007 Red Hat and 2024 Igalia | X11 XSETTINGS; compiled into the Linux SDL3 library |
| imKStoUCS | SDL3 3.4.16's copy (`src/events/imKStoUCS.c`) | MIT, (c) Jamey Sharp, Josh Triplett, Red Hat, Oracle | X11 and Wayland keysym to Unicode; compiled into the Linux SDL3 library |
| Wayland protocols | SDL3 3.4.16's `wayland-protocols/*.xml` (wayland, wayland-protocols and others) | MIT, many holders (each file's notice) | code generated from them by wayland-scanner is compiled into the Linux SDL3 library |
| xdg-user-dir-lookup | SDL3 3.4.16's copy (`src/filesystem/unix/SDL_sysfilesystem.c`, from xdg-user-dirs' `xdg-user-dir-lookup.c`) | MIT, (c) 2007 Red Hat | XDG user folders; compiled into the Linux SDL3 library |
| DXVK (dxvk-native) | 3.1.1 | zlib | Direct3D 9 over Vulkan on Linux; a shared library beside the program |
| dxbc-spirv | `bf14419e` (dxvk 3.1.1's submodule) | MIT, (c) 2025 Philip Rebohle | compiled into dxvk-native's Direct3D 9 library |
| libdisplay-info | `275e6459` (dxvk 3.1.1's submodule) | MIT, (c) 2022 The libdisplay-info Contributors | compiled into dxvk-native's Direct3D 9 library |
| hwdata `pnp.ids` | libdisplay-info `275e6459`'s copy | GPL-2.0-or-later OR XFree86 1.0 (the XFree86 1.0 option; `licenses/hwdata-pnp.ids-LICENSE` carries hwdata's statement and the licence text, both pinned in `port/deps.lock`) | monitor maker names (PNP IDs); its table is compiled into dxvk-native's Direct3D 9 library |
| libxkbcommon | 1.13.2 | MIT | keyboard layouts for SDL3 on Linux; a shared library beside the program |
| FFmpeg | 7.1.1 (decoders and demuxer only; no GPL or non-free parts) | LGPL-2.1-or-later | the game's films; shared libraries beside the program, which you may replace with your own build. Each release also offers the exact FFmpeg source it was built from. |
| Monocypher | 4.0.2 | BSD-2-Clause OR CC0-1.0 | compiled into the launcher: the update feed's signature check |
| zlib | 1.3.2 | Zlib | compressed game files and the launcher's update unpacking on Windows (a DLL beside the program); Linux uses the system's |
| libogg, libvorbis | 1.0 | BSD-3-Clause | the game's music and voices, built with the game's original compiler into `libvorbis_vc6.a` (attached to the `vorbis-vc6-1` release and pinned by SHA-256 in `port/deps.lock`) by `port/vorbis/build.py`, from Xiph.Org's 1.0 source fetched from downloads.xiph.org by the SHA-256 pins in that script |
| MinGW-w64 runtime | from llvm-mingw 20260922 | ZPL-2.1 and others (as the package notice says) | the Windows C runtime start-up, linked into `bea.exe` |
| libc++, libc++abi, libunwind | from llvm-mingw 20260922 | Apache-2.0 WITH LLVM-exception | the Windows C++ runtime, linked into `bea.exe` |

## In this repository's source

| File | From | Licence |
| --- | --- | --- |
| `port/d3dx/src/bc.cpp` | adapted from Microsoft's DirectXTex (its DXT block encoder) | MIT, with the original notice in the file; the file is GPL-3.0-only AND MIT |
| `src/oggvorbis.h` | declarations from the Xiph.Org libogg and libvorbis headers | BSD-3-Clause (Xiph.Org Foundation; `LICENSES/BSD-3-Clause.txt`), with this project's GPL-3.0-only |
| `src/OggFileRead.cpp` | its decode loop follows Xiph.Org's `decoder_example.c` (libvorbis 1.0), turned into a class | BSD-3-Clause (Xiph.Org Foundation), with this project's GPL-3.0-only |
| `src/adpcm.c` | the IMA ADPCM step tables (Jack Jansen's public reference implementation) | public reference tables |

Header-only inputs are not listed: dxvk-native compiles against Khronos's SPIRV-Headers and Vulkan-Headers (its
submodules), which supply declarations and constants only, so no copy or substantial portion of those headers is in
the shipped library, and their notices (which cover copies of the headers) are not carried.

## Tools only (not shipped)

LLVM/Clang 22.1.8 and llvm-mingw (Apache-2.0 WITH LLVM-exception), Meson (Apache-2.0) and CMake (BSD-3-Clause) build
the program; they are not part of it, apart from the runtime libraries listed above.

## Not part of the public build

NVIDIA's NvTriStrip has no stated licence, so the public build leaves it out (`build.py --public`). The game only needs
it to build meshes from raw model files, which the retail data never does.
