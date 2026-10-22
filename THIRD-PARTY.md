# Third-party components

Onslaught CE's own code is GPL-3.0-only (`COPYING`). The build fetches the components below at the versions
`port/deps.lock` pins, each checked by SHA-256; none of their source is in this repository except where a file says
so. Release packages carry each shipped component's licence text in `licenses/`.

## In the program

| Component | Version | Licence | How it is used |
| --- | --- | --- | --- |
| SDL3 | 3.4.16, with `port/deps-patches/SDL3-offscreen-fullscreen-modes.patch` | zlib | window, input and sound; a shared library beside the program (patched, as the patch file records) |
| DXVK (dxvk-native) | 3.1.1 | zlib | Direct3D 9 over Vulkan on Linux; a shared library beside the program |
| libxkbcommon | 1.13.2 | MIT | keyboard layouts for SDL3 on Linux; a shared library beside the program |
| FFmpeg | 7.1.1 (decoders and demuxer only; no GPL or non-free parts) | LGPL-2.1-or-later | the game's films; shared libraries beside the program, which you may replace with your own build. Each release also offers the exact FFmpeg source it was built from. |
| Monocypher | 4.0.2 | BSD-2-Clause OR CC0-1.0 | the launcher's Ed25519 check of the signed release feed; compiled into the launcher |
| zlib | 1.3.2 | Zlib | compressed game files on Windows (a DLL beside the program); Linux uses the system's |
| libogg, libvorbis | 1.0 | BSD-3-Clause | the game's music and voices, built with the game's original compiler into `libvorbis_vc6.a`, which each release attaches with its source and build script (pinned by SHA-256 in `port/deps.lock`) |
| MinGW-w64 runtime | from llvm-mingw 20260922 | ZPL-2.1 and public domain | the Windows C runtime start-up, linked into `bea.exe` |
| libc++, libc++abi, libunwind | from llvm-mingw 20260922 | Apache-2.0 WITH LLVM-exception | the Windows C++ runtime, linked into `bea.exe` |

## In this repository's source

| File | From | Licence |
| --- | --- | --- |
| `port/d3dx/src/bc.cpp` | adapted from Microsoft's DirectXTex (block compression decoders) | MIT, with the original notice in the file; the file is GPL-3.0-only AND MIT |
| `src/oggvorbis.h` | declarations from the Xiph.Org libogg and libvorbis headers | BSD-3-Clause (Xiph.Org Foundation) |
| `src/d3dapp.cpp`, `src/d3dapp.h` | Microsoft's DirectX sample application framework, as Stuart Gillam's GPL release of the game's source includes it | as released by Stuart Gillam under GPL-3.0 |
| `src/adpcm.c` | the IMA ADPCM step tables (Jack Jansen's public reference implementation) | public reference tables |

## Tools only (not shipped)

LLVM/Clang 22.1.8 and llvm-mingw (Apache-2.0 WITH LLVM-exception), Meson (Apache-2.0) and CMake (BSD-3-Clause) build
the program; they are not part of it, apart from the runtime libraries listed above.

## Not part of the public build

NVIDIA's NvTriStrip has no stated licence, so the public build leaves it out (`build.py --public`). The game only needs
it to build meshes from raw model files, which the retail data never does.
