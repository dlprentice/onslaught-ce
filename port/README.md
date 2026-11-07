# Native port (Linux and Windows)

Status: active (both targets build, link and run; measured scope and limits are summarized in the public CE README's Known limitations; native Windows acceptance uses a software-rendered VM, not physical-GPU validation)
Last updated: 2026-11-06 (maintainer release steps marked, environment variables listed, files named but not in this tree; compile contract and migration wording corrected)
Summary: a second build of the decompilation's sources as native i686 Linux and Windows programs; layout, ownership,
commands and rules.

The port compiles the same `src/` the matching build compiles, with clang for i686 Linux and i686 Windows, against
Direct3D 9 (dxvk-native on Linux, the system's on Windows) and one SDL3 platform layer for the window, input and
audio. Retail data is read from the player's install; nothing retail is committed. The VC6 matching build is separate.
The [public decompilation](https://github.com/dlprentice/onslaught-decomp#reproducing-the-match) documents its pinned
inputs, measured matching coverage, compiled-object concession and remaining exclusions. The public CE README owns
the port's [known limitations](https://github.com/dlprentice/onslaught-ce#known-limitations) and
[public setup](https://github.com/dlprentice/onslaught-ce#building-from-source).

    python3 port/tools/deps.py build     # pinned inputs, both targets, into local-data/port
    python3 port/build.py --public [--target linux|windows] [-k]   # the game: local-data/port/build/<target>/bea[.exe]

`-k` keeps the build going after errors (passed to ninja as `-k 0`) and prints a census of the error sites by message and
file; the full log is `local-data/port/build/<target>/last.log`.

Files this tree names but does not hold: comments and guides under `port/` and `src/` sometimes name `PLAN.md`,
`PARITY.md`, `tools/decomp.py`, `docs/release.md`, `tools/release_rc.py` and folders such as `tests/` and `probe/`. They
belong to the maintainers' working source, which this repository is generated from, and are not in this tree; the
[public decompilation](https://github.com/dlprentice/onslaught-decomp) is the public home of the matching tools.

Licence: GPL-3.0 (`SPDX-License-Identifier: GPL-3.0-only`), matching the upstream Onslaught source (GPL version 3,
no "or later" grant). The public NOTICE retains the source attribution; third-party inputs keep their own licences
(`deps.lock`).

## Public release

The port is released as [Onslaught CE](https://github.com/dlprentice/onslaught-ce), from an export of this source. Its
licence text is [COPYING](../COPYING) (GPL version 3).

The two commands below are the maintainers' release steps, not part of contributing. A contributor builds a test
package with the development steps in the README's
[Building from source](https://github.com/dlprentice/onslaught-ce#building-from-source) and does not publish one.

    python3 port/build.py --target all --public --ce-version X.Y   # the shippable build: no NvTriStrip, bundled libraries
    BEA_PRIVATE_PATHS=/first/folder:/second/folder python3 port/tools/package.py X.Y   # Linux tarball and Windows zip

`--ce-version` names the version the game and launcher show ("Onslaught CE VX.Y"); without it the build is
"vdev". `package.py` scans every packaged binary for the builder's home folder, the checkout path and the account name
always, and for the colon-separated folders in `BEA_PRIVATE_PATHS` too. Left unset, folders outside the home and the
checkout (scratch space, external drives) are not scanned, so list them in `BEA_PRIVATE_PATHS` when building a release.

`--public` compiles `platform/public/`'s replacements in place of the files the public build must not ship and reports
"not shippable" for any still missing; `package.py` refuses such a build and any binary that still names a private
path. The launcher and updater, and what the signed feed holds, are in [updater/README.md](updater/README.md). No game
files are packaged: the launcher reads the player's own install. The launcher's updater is v2 ([updater/UPDATER-V2.md](updater/UPDATER-V2.md)): a
signed `ce-update-v2.json` feed, never `latest.json`, which launchers 0.1.0–0.1.6 fetch and which is never published
again (`tools/sign_release.py` is retired: running it only prints a refusal and exits). The v2 feed has been published with every release since 0.1.8. There is no telemetry.

### Released launcher safety hold and manual migration

Do not run released launchers 0.1.0 through 0.1.6 on either platform. Their source contains name-based startup
deletion of neighboring `<program folder>.old-*` paths; the shipped Linux 0.1.6 package has reproduced data deletion
and traversal through a matching directory link. Windows reparse behavior remains unmeasured. Disabling updates
does not disable startup cleanup. Predictable download/unpack names and unverified rollback paths are additional
reasons to avoid invoking an affected updater. The corrected successor is the latest release on the
[GitHub releases page](https://github.com/dlprentice/onslaught-ce/releases); 0.1.7 switched the old updater off, and 0.1.8 brought the v2 updater.

Migration must not start the old launcher. Preserve its entire program folder and uncertain neighboring
`.old-*`, `.download-*` and `.unpack-*` paths. A relative `BEA_USER_DIR` can put launcher and game user data in different
locations; identify and preserve both complete folders and verify independent backups, including careers. Use a
separate working copy of the identified game user-data folder outside the original game and CE program/staging folders
and set `BEA_USER_DIR` to its absolute path for the successor.
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
| `deps.lock`, `tools/` | pinned third-party inputs and their builds into `local-data/port/`; isolated run and check tools |
| `build.py` | the port build (ninja): flags, include overlay, mirror + patch queue, link |
| `include/` | Win32, DirectSound, DirectInput, mmsystem and VC6 CRT shims over dxvk-native's headers |
| `platform/` | `common/` (both targets: the SDL3 window and message pump, DirectInput 8, the Direct3D 9 hooks, clocks, registry, NvTriStrip bridge), `linux/` (Win32 over POSIX and SDL3, paths, entry point), `windows/` (file overlay, clocks) |
| `aliases.tsv` | linklib/aliases.tsv's second names in the Itanium ABI, the C++ name-mangling scheme of the Linux and MinGW toolchains (the original compiler mangled names differently); applied with `--defsym` on Linux and `/alternatename` on Windows |
| `game/` | CE gameplay additions and integration code |
| `platform/public/` | public replacements for excluded SDK/framework components |
| `patches/` | port-only source edits that cannot be byte-neutral in `src/`, applied to a generated mirror |
| `d3dx/` | `include/` (d3dx9*.h, dxerr9.h) and `src/`: maths, zlib-wrapped DDS loader, vs/ps 1.x assembler, DXGetErrorString9 (its tests are maintainer-only) |

## Rules

- `src/` line counts never change (`__LINE__` strings and `.bss` creation order depend on them). Propose source changes
  through [CE contribution guide](https://github.com/dlprentice/onslaught-ce/blob/main/CONTRIBUTING.md); maintainers run the matching check in their working source. That tool
  is not part of the CE export. The separate [public decompilation](https://github.com/dlprentice/onslaught-decomp)
  documents its own function-check setup. CE behavior changes normally belong in `game/`, platform code or `patches/`.
- Pinned dependency builds go to the ignored `local-data/port/` of your clone's main working tree, which every Git
  worktree of that clone shares (or to the folder `BEA_PORT_DATA` names). The first setup
  downloads roughly 3.2 GiB before extraction and builds (an estimate: `deps.lock` records no sizes). What derives
  from a checkout's own `src/` (the mirror, objects, probe output) goes to that checkout's ignored `local-data/port/`.

## Environment variables

The variables a player sets are in the [README](https://github.com/dlprentice/onslaught-ce#readme) (`BEA_GAME_DIR`,
`BEA_USER_DIR` and the settings it lists). Build and development variables:

| Variable | Used by | Meaning |
| --- | --- | --- |
| `BEA_PORT_DATA` | `tools/deps.py`, `build.py` | where the pinned dependencies are built and cached (default `local-data/port` in the clone's main working tree) |
| `BEA_PORT_SYSROOT` | `tools/deps.py`, `build.py` | `sniper` (the default, an old-glibc sysroot for the Linux build) or `host` |
| `BEA_GAME_DIR` | the build and many checks | the game installation whose two build inputs `config/game-data.sha256` pins |
| `BEA_PRIVATE_PATHS` | `tools/package.py` | colon-separated folders to scan packaged binaries for, besides the home folder and the clone; a maintainer's release step |
| `BEA_VC6` | `vorbis/build.py` | the pinned Visual C++ 6 toolchain, only to regenerate the prebuilt libvorbis, which needs Wine and a toolchain this repository does not supply |
| `BEA_PORT_TAPE`, `BEA_PORT_RECORD`, `BEA_PORT_CLOCK`, `BEA_AUDIO_MONITOR` | the launcher and the checks | replay, recording, virtual clock and audio-monitor controls ([updater/README.md](updater/README.md), [dsound/README.md](dsound/README.md)); developer switches, not player settings |

Other `BEA_PORT_*` names appear in individual checks and patches; the file that reads one is the place that describes it.

## Compile contract

`build.py` applies the flags below to both targets: Linux `--target=i686-pc-linux-gnu` and Windows
`--target=i686-w64-mingw32` (llvm-mingw), both clang. Every game translation unit (`src/`, compiled from its generated
mirror, and `port/game/`) gets `-fms-extensions -fno-strict-aliasing -fwrapv -malign-double -mms-bitfields
-fshort-wchar`, and then one of two flag classes:

- **x87 class** (game sources, the generated mirror, and port code whose results must match the VC6 build, such as
  d3dx's `maths_x87.cpp`): `-std=gnu++14 -Wno-c++11-narrowing -Wno-reserved-user-defined-literal` (C: `-std=gnu99`; port
  code uses `-std=gnu++17`, below) `-mno-sse -mfpmath=387`. clang 22's i686 default does float maths in SSE, and
  `-mfpmath=387` is refused while SSE is on (measured). These files never include SDL headers. C++11 or later is forced
  by dxvk-native's headers (`constexpr` GUID helpers in `windows_base.h`); the two warning switches keep VC6-era forms
  compiling (HRESULT `case` labels above 0x7fffffff, `"text"MACRO`), measured on a probe TU that also showed x87-only
  code.
- **platform class** (`platform/`, anything that includes SDL): `-std=gnu++17`, the target's default SSE.

Port code outside `port/game/` compiles as `-std=gnu++17` with the list above minus `-malign-double`. Its per-file flags
come from `port/d3dx/flags.txt` and `port/bink/flags.txt` (`file: -flags`), appended after the class flags:
`maths.cpp` and `texture.cpp` take `-msse -mfpmath=sse`, where retail d3dx9's SSE path is what players got, and
`bink.cpp` takes `-mno-ms-bitfields`. Other d3dx files keep the x87 switches, and the rest of `dsound`, `bink` and
`platform/` take the target's default SSE. The C files under `port/` (`vorbis/vc6crt.c`, `updater/sha256.c`) use
`-std=gnu99 -O2 -w` and none of the switches above.

The include order is `port/d3dx/include`, `port/include`, then dxvk-native's `usr/include/dxvk` (Linux) or llvm-mingw's
MinGW headers (Windows), so the port's d3dx and shim headers win. dxvk-native (Linux only) defines `WINAPI`,
`STDMETHODCALLTYPE` and `__stdcall` empty and `LONG`/`HRESULT` as `int32_t`. D3DX types keep the SDK layout
(`D3DXMATRIX` derives from `D3DMATRIX`; game code reads `.m` and `_11`). `-fshort-wchar` gives a 2-byte `WCHAR` on
Linux, as on Windows; no D3D9 entry point the game calls takes one.
