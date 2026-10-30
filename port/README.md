# Native port (Linux and Windows)

Status: active (both targets build, link and run the game; Linux replays are compared with retail Present by Present, logged in PARITY.md in the private working tree; native Windows acceptance uses a software-rendered VM, not physical-GPU validation)
Last updated: 2026-10-29 (matching concession and upstream credit clarified; earlier: the v2 feed, private-tree references, released-launcher cleanup warning and manual migration)
Summary: a second build of the decompilation's sources as native i686 Linux and Windows programs; layout, ownership,
commands and rules.

The port compiles the same `src/` the matching build compiles, with clang for i686 Linux and i686 Windows, against
Direct3D 9 (dxvk-native on Linux, the system's on Windows) and one SDL3 platform layer for the window, input and
audio. Retail data is read from the player's install; nothing retail is committed. The VC6 matching build is separate:
the private working tree's `tools/decomp.py check` compares its compiled functions with the executable using a recorded
one-byte compiled-object concession. That tree's README records the score and source stand-ins; its `port/PLAN.md` has
dated targets, API counts, parity hazards and phases (neither is in the public export; the public decompilation is
https://github.com/dlprentice/onslaught-decomp).

    python3 port/tools/deps.py build     # pinned inputs, both targets, into local-data/port (about 10 minutes)
    python3 port/probe/run.py            # private tree only: P0, the triangle on Linux (NVIDIA, Intel) and Windows-under-Wine, compared
    python3 port/build.py [--target linux|windows] [-k]   # the game: local-data/port/build/<target>/bea[.exe]

Licence: GPL-3.0 (`SPDX-License-Identifier: GPL-3.0-only`), matching the upstream Onslaught source (GPL version 3,
no "or later" grant). The public NOTICE retains the source attribution; third-party inputs keep their own licences
(`deps.lock`).

## Public release

The port is released as [Onslaught CE](https://github.com/dlprentice/onslaught-ce), from an export of this source. Its
licence text is [COPYING](../COPYING) (GPL version 3).

    python3 port/build.py --target all --public --ce-version X.Y   # the shippable build: no NvTriStrip, bundled libraries
    BEA_PRIVATE_PATHS=<scratch:archive folders> python3 port/tools/package.py X.Y   # Linux tarball and Windows zip

`--ce-version` names the version the game and launcher show ("Onslaught CE VX.Y"); without it the build is
"vdev". `package.py` scans every packaged binary for this machine's paths: the home folder, the repository and the
account name always, and the colon-separated folders in `BEA_PRIVATE_PATHS` too. Left unset, folders outside the home
and the repository (scratch space, archive drives) are not scanned, so set it to them when building a release.

`--public` compiles `platform/public/`'s replacements in place of the files the public build must not ship and reports
"not shippable" for any still missing; `package.py` refuses such a build and any binary that still names a private
path. The launcher and updater, and what the signed feed holds, are in [updater/README.md](updater/README.md). No game
files are packaged: the launcher reads the player's own install. The launcher's updater is v2 ([updater/UPDATER-V2.md](updater/UPDATER-V2.md)): a
signed `ce-update-v2.json` feed, never `latest.json`, which launchers 0.1.0–0.1.6 fetch and which is never published
again (`tools/sign_release.py` is retired). The v2 feed has been published with every release since 0.1.8. There is no telemetry.

### Released launcher safety hold and manual migration

Do not run released launchers 0.1.0 through 0.1.6 on either platform. Their source contains name-based startup
deletion of neighboring `<program folder>.old-*` paths; the shipped Linux 0.1.6 package has reproduced data deletion
and traversal through a matching directory link. Windows reparse behavior remains unmeasured. Disabling updates
does not disable startup cleanup. Predictable download/unpack names and unverified rollback paths are additional
reasons to avoid invoking an affected updater. The latest release is the corrected successor (0.1.7 switched the old updater off, and 0.1.8 brought the v2 updater).

Migration must not start the old launcher. Preserve its entire program folder and uncertain neighboring
`.old-*`, `.download-*` and `.unpack-*` paths. A relative `BEA_USER_DIR` can put launcher and game user data in different
locations; identify and preserve both complete folders and verify independent backups, including careers. Use a
separate working copy of the identified game user-data folder outside the original game and CE program/staging folders
and select its absolute user path.
Keep originals and backups, check careers, and do not merge unclear or conflicting files.
Only then verify the latest release's download and extract it into a new,
empty, ordinary writable folder. Run that successor only; retain backups and uncertain old paths.

The complete steps live in the [player README](https://github.com/dlprentice/onslaught-ce#manual-migration-from-affected-launchers).
Windows 0.1.5's helper additionally hits installer detection in the measured ordinary session. Although 0.1.6 embeds
`asInvoker`, that declaration does not repair the cleanup hazard. Do not use administrator execution or disabled UAC
as migration workarounds, and do not use 0.1.6 as an intermediate release.

## Layout and owners

| Path | What |
|---|---|
| `deps.lock`, `tools/`, `probe/` | pinned third-party inputs and their builds into `local-data/port/`; the private-Xvfb runner; the P0 probe |
| `build.py` | the port build (ninja): flags, include overlay, mirror + patch queue, link |
| `include/` | Win32, DirectSound, DirectInput, mmsystem and VC6 CRT shims over dxvk-native's headers |
| `platform/` | `common/` (both targets: the SDL3 window and message pump, DirectInput 8, the Direct3D 9 hooks, clocks, registry, NvTriStrip bridge), `linux/` (Win32 over POSIX and SDL3, paths, entry point), `windows/` (file overlay, clocks) |
| `aliases.tsv` | linklib/aliases.tsv's second names in the Itanium ABI (`--defsym` on Linux, `/alternatename` on Windows) |
| `patches/` | port-only source edits that cannot be byte-neutral in `src/`, applied to a generated mirror |
| `d3dx/` | `include/` (d3dx9*.h, dxerr9.h) and `src/`: maths, zlib-wrapped DDS loader, vs/ps 1.x assembler, DXGetErrorString9; `tests/` |

## Rules

- `src/` line counts never change (`__LINE__` strings and `.bss` creation order depend on them). Byte-neutral fixes go to
  `src/` as ordinary claimed commits, proven by `tools/decomp.py check`; anything else is a patch in `patches/`.
- Pinned dependency builds go to the canonical checkout's ignored `local-data/port/` (shared by every worktree:
  identical pinned inputs, about 1.6 GB, `BEA_PORT_DATA` overrides). What derives from a checkout's own `src/` (the
  mirror, objects, probe output) goes to that checkout's ignored `local-data/port/`.

## Compile contract (until `build.py` owns it)

Targets: Linux `--target=i686-pc-linux-gnu` and Windows `--target=i686-w64-mingw32` (llvm-mingw), both clang, both
built here. Every translation unit gets `-fms-extensions -fno-strict-aliasing -fwrapv -malign-double -mms-bitfields
-fshort-wchar`, and then one of two flag classes:

- **x87 class** (game sources, the generated mirror, and port code whose results must match the VC6 build, such as the
  d3dx maths): `-std=gnu++14 -Wno-c++11-narrowing -Wno-reserved-user-defined-literal` (C: `-std=gnu99`)
  `-mno-sse -mfpmath=387`. clang 22's i686 default does float maths in SSE, and `-mfpmath=387` is refused while SSE is
  on (measured). These files never include SDL headers. C++11 or later is forced by dxvk-native's headers
  (`constexpr` GUID helpers in `windows_base.h`); the two warning switches keep VC6-era forms compiling (HRESULT `case`
  labels above 0x7fffffff, `"text"MACRO`), measured on a probe TU that also showed x87-only code.
- **platform class** (`platform/`, anything that includes SDL): `-std=gnu++17`, the target's default SSE.

`port/d3dx/flags.txt` (`file: -flags`) appends per-file flags, for example `-msse -mfpmath=sse` where retail d3dx9's
SSE path is what players got. The include order is `port/d3dx/include`, `port/include`, then dxvk-native's
`usr/include/dxvk` (Linux) or llvm-mingw's MinGW headers (Windows), so the port's d3dx and shim headers win. dxvk-native
(Linux only) defines `WINAPI`, `STDMETHODCALLTYPE` and `__stdcall` empty and `LONG`/`HRESULT` as `int32_t`. D3DX types keep the
SDK layout (`D3DXMATRIX` derives from `D3DMATRIX`; game code reads `.m` and `_11`). `-fshort-wchar` gives a 2-byte
`WCHAR` on Linux, as on Windows; no D3D9 entry point the game calls takes one.
