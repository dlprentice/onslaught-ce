# port/vorbis: retail's Ogg Vorbis decoder, rebuilt

Some files and commands named on this page (`tests/`, `probe/`, `run.py`, `check_tape.sh`, `replay_session.py`, `PLAN.md`, `PARITY.md`, `tools/decomp.py`, `docs/release.md`, `tools/release_rc.py`) live only in the private working repository and are not in this public tree; they are named so that the description is complete. The decompilation's public export is [onslaught-decomp](https://github.com/dlprentice/onslaught-decomp).

The game decodes its music and voice lines with Xiph.Org's libvorbis and libogg (BSD), from the `vorbis.dll` and
`ogg.dll` it ships. `build.py` rebuilds that exact decoder from Xiph's source, so the port hears what retail hears,
sample for sample.

## Which decoder retail has

The install's `vorbis.dll` (SHA-256 `b4fa55cf…`) writes the vendor string "Xiph.Org libVorbis I 20020717". That is
libvorbis 1.0, and its `ogg.dll` (`308540db…`) is libogg 1.0. Both were linked on 2002-07-19 by Visual C++ 6 (linker
6.0). Their imports match the Release settings of Xiph's own `win32/vorbis_dynamic.dsp` (`/MD`: MSVCRT and ogg.dll) and
`win32/ogg_dynamic.dsp` (`/MT`: KERNEL32 only).

`build.py` compiles the 1.0 releases (pinned by SHA-256 in the script) with the decomp's pinned VC6 and those
settings. Linked as DLLs, the result is the same size as retail's (974,848 and 49,152 bytes). After masking addresses,
25,724 of retail's 25,902 instructions line up one for one; the rest are three functions placed in a different order,
the import table's order, and jump-table data.

## Why it matters

The retail replay shim logged a content hash for every buffer retail plays (level 100, 3,600 Presents). In that
log, a decoder is right only when it reproduces those hashes.

- **Retail's own DLLs.** Run by `tests/ogg_hash.cpp` under Wine, they reproduce them, but only at x87 precision 0x27F
  or 0x37F, not at 0x07F. The game decodes on worker threads (the music stream, the voice loader), and a new Windows
  thread starts at 0x27F.
- **Clang builds.** Any clang x87 build of libvorbis 1.0 or 1.3.7 differs from retail by ±1 in 0.06–0.10% of samples
  at any precision. The ±1 comes from code generation, not the library version.
- **The port as it was** (clang-built 1.3.7, worker threads inheriting the main thread's 0x07F) matched none of the
  nine.
- **These objects** match all nine, on Linux and on Windows, through the game's own `src/oggvorbis.h`:

| Buffer | File | Retail hash |
| --- | --- | --- |
| the first music fill (512 KB) | `Music/BEA_04(Master).ogg` | `a7800cee` |
| voice line 1 | `hud_01` | `fc09f99f` |
| voice line 2 | `hud_02` | `836c6e64` |
| voice line 3 | `hud_06` | `6784d28b` |
| voice line 4 | `tutorial_message_log` | `13eda74b` |
| voice line 5 | `tutorial_technician_01` | `d60cd6cc` |
| voice line 6 | `tutorial_13_mod` | `0f461391` |
| voice line 7 | `tutorial_01` | `18f95a93` |
| voice line 8 | `tutorial_scanner` | `05d1099d` |

Beyond the log, every one of the install's 3,057 Ogg files (music, and voice in every language) decodes to the same
whole-file PCM as retail's DLLs do under Wine, at 0x27F (2026-10-15; Linux build of these objects against the Steam
install's `vorbis.dll` and `ogg.dll`).

The voice files are 44.1 kHz. The game's loader reads at most 1 MB of PCM, and `ConvertAudioFormat` averages sample
pairs into a 22.05 kHz buffer (`OGG_HASH_HALF=1` in the harness).

## How the objects reach the port

- **Windows.** `lib/windows/libvorbis_vc6.a` holds the VC6 COFF objects as compiled.
- **Linux.** `lib/linux/libvorbis_vc6.a` holds the same objects converted to ELF by objcopy, with two repairs:
  - every R_386_PC32 addend gets −4, because COFF's REL32 implies it and ELF does not;
  - VC6's COMDAT floating constants (`_real@…`) become weak symbols.

  The code is not position-independent, so link it `-no-pie` (otherwise ld warns of DT_TEXTREL).
- **Runtime.** Each object's references to the C runtime are renamed to `vc6_*`, and `vc6crt.c` serves them on both
  targets: the allocator, memmove, memchr, qsort, floor, ceil, ldexp, frexp, a little stdio, and in assembly `_ftol`,
  `_alloca_probe`/`_chkstk`, `_allshl`/`_allshr`, `_CIpow` and `_CIacos`. VC6 code keeps the stack only 4-byte aligned,
  so each wrapper realigns.
- **Not on the decoding path.** Sine, cosine, square root, logarithm and exponential are inline x87 instructions in
  these objects. `pow` and `acos` (floor 0 and the encoder) are not on the decoding path, and use the host's libm.
- **Layout.** The game compiles `src/oggvorbis.h` with `-malign-double`, which gives MSVC's layout, the one these
  objects use. The clang-built 1.3.7 aligns 64-bit fields to 4 bytes instead, so its structures do not match that
  header.
- **Precision.** The decoder must run at 0x27F to match retail. The port's worker threads currently inherit the main
  thread's 0x07F (Direct3D's single precision). Starting each thread at 0x27F, as Windows does, belongs in the
  platform layer's CreateThread.

## Status and what the build needs

- The exact build needs Visual C++ 6. It is proprietary, so it is not in this repository: it comes from the
  decomp's pinned toolchain in the Onslaught lab (`BEA_VC6`), run under Wine. Without it, a clang build of the same
  source plays correctly but differs from retail by ±1 in about 0.1% of samples.
- The game build (port/build.py) links these VC6-built libvorbis/libogg 1.0 objects (libvorbis_vc6.a; a build without
  VC6 fetches the same library, pinned by SHA-256 in deps.lock, from the release assets), and the game's worker threads
  start at 0x27F (port/platform/linux/kernel.cpp), as retail's do. Release binaries carry libvorbis 1.0's vendor string
  "Xiph.Org libVorbis I 20020717". The clang-built 1.3.x that tools/deps.py installs into the dependency prefix is not
  linked into the game.
- A binary release that links these libraries must reproduce Xiph's BSD notice and disclaimer (port/PLAN.md, release
  obligations).

## In the port

`port/build.py` links `lib/<target>/libvorbis_vc6.a` and compiles `vc6crt.c` with the port (Linux `-no-pie`); it stops
with a message when the library has not been built. Level 100 on the Linux port (`--shim-clock`, 1,500 Presents,
`BEA_AUDIO_CALLLOG`; lead, 2026-10-15): the music fill and all eight voice lines carry retail's PCM, at retail's
Presents. Four hash equal in the log; the other five are the odd-length lines, which the port's DirectSound rounds up
to a whole frame and hashes with the pad byte (port/dsound/README.md): their decoded PCM plus one zero byte gives the
port's hash exactly, and without it retail's. The Windows build links; it has not been run.

## Commands

```
BEA_VC6=<the pinned VC6> [BEA_WINEPREFIX=<prefix>] python3 port/vorbis/build.py
port/vorbis/tests/run_tests.sh      # the nine retail hashes, Linux and Windows (Wine: PEERH_D3DX_WINEPREFIX)
```

`build.py` checks VC6 against `config/toolchain.sha256` and `config/vc6-include.sha256`, and the source tarballs
against their SHA-256. It writes to `local-data/port/vorbis/`. The tests read the game's data from `BEA_GAME_DIR`
(default: the Steam install).
