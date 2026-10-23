<!-- SPDX-License-Identifier: GPL-3.0-only -->

# port/bink: the game's Bink videos through FFmpeg

Status: active (decoding, sound and timing in place; not yet in the game's build or compared with RAD's binkw32.dll)
Last updated: 2026-10-15
Summary: the Bink entry points the front end calls, decoded with FFmpeg's LGPL Bink decoders, and how it is checked.

Replaces port/platform/common/bink.cpp (the platform's stand-in, which could not open a file; `build.py`
drops it when this directory has sources), with the same 13 names and calling convention as src/binkapi.h.

## What it does

The front end (DXFrontEndVideo.cpp) opens a video on a worker thread, then per frame: `BinkDoFrame` decodes the current
frame, `BinkCopyToBuffer` converts it into a texture (32-bit BGRA, or R5G6B5 when 32-bit textures are off),
`BinkNextFrame` advances the 1-based `FrameNum`, and `BinkWait` says whether the next frame is due yet; a video ends when
`FrameNum` reaches `Frames`. Here `BinkOpen` reads the whole file and demuxes it with FFmpeg's Bink demuxer; video
packets are decoded one per `BinkDoFrame` by FFmpeg's `bink` decoder. The sound track the game chose is decoded
entirely (`binkaudio_rdft`/`binkaudio_dct`) into one DirectSound buffer on the game's own IDirectSound
(`BinkSetSoundSystem(BinkOpenDirectSound, ds)`), so it mixes and records with the rest of the game's sound and takes
`BinkSetVolume` (RAD's 32768 = unity, as hundredths of a decibel); `BinkWait` follows that buffer's play position, or the
wall clock when a video has no sound. Colour conversion is retail binkw32's own, measured against it (below):
nearest-neighbour chroma, and one table per term, each a 16.16 product truncated toward zero, summed and clamped:
luma `((Y-16)*76309)>>16` (0 below 16), red `104597*(V-128)`, green `-25675*(U-128)` and `-53279*(V-128)`, blue
`132219*(U-128)`. The sampled data pins the blue constant only to 132202..132221. Where a video has no alpha plane (all
66 retail videos), the alpha byte is 0, as retail writes it.

All 66 retail videos are Bink 1 revision i (`BIKi`), from 128x128 to 640x480.

## FFmpeg

FFmpeg 7.1.1, `https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz`, SHA-256
`733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1`, licence LGPL 2.1 or later (no GPL or non-free
parts enabled). Public builds link it as shared libraries beside the program, which the player may replace; private
builds link it statically. `port/tools/deps.py`'s `build_ffmpeg_shared` (public) and `build_ffmpeg` (private) are the
recipe: the Bink demuxer, the three Bink decoders and the file protocol only, no assembly, no autodetected libraries;
Windows with its native threads. bink.cpp is compiled with `-mno-ms-bitfields` after the port's layout flags: with `-mms-bitfields` FFmpeg's
structures lay out differently on Linux (AVPacket 80 bytes instead of 76) and decoding fails. `BINK`, the only structure
shared with the game, holds 32-bit fields and lays out the same either way. Link: `-lavformat -lavcodec -lavutil`, plus
`-lm -lpthread` on Linux and `-lbcrypt` on Windows.

## Checks

`tests/run_tests.sh` decodes every frame of all 66 retail videos as the front end does, on Linux and on the Windows
build under Wine: every frame count and size matches the file's Bink header (66 of 66 on both targets), decoding runs at
300 to 2,900 frames a second, and the first, middle and last frames of each video (198 images) are byte-identical
between the targets. Frames looked at: the opening film, the Lost Toys logo, a briefing and the front-end background
show natural colour and no blocking.

Against retail's `binkw32.dll` (the install's, under Wine; `tests/retail_compare.sh`, 2026-10-15):

- **Whole frames.** Every frame of all 66 videos was decoded both ways through bink_test with the game's 32-bit copy
  flags. 48,294 of 48,627 frames are byte-identical, alpha included.
- **How the conversion was found.** FFmpeg's YUV for 114 sampled frames maps to retail's RGB as an exact function,
  separable per term. The tables above reproduce it on every pixel of the 198 sampled frames.
- **The 333 frames that differ** are in five videos: gill_m_on_a_fork (160), PC_231_exact (154), PC_611_exact (11),
  PC_100_exact (7) and PC_311_exact (1).
  - In gill_m_on_a_fork and PC_231 they start as clusters FFmpeg's decoder reconstructs differently from RAD's. That
    motion compensation carries them to the next key frame is inferred from their persistence.
  - The three briefings' single pixels are on the bottom row of 201x149 frames, where retail's colour is luma 235 and
    FFmpeg's is 236 to 238. That cause is unexplained.
  - Details are in port/PARITY.md.
- **What is left.** Exactness would need FFmpeg's decoder patched for those cases. FFmpeg is LGPL, so a modified copy's
  source would ship with the port.

The game plays its videos (level 100 without -skipfmv, the virtual clock): cutscenes/01.vid runs its full 123.8 s and
the level starts after it. Frame 120 was looked at. The sound is the game's chosen track 0, at the game's volume
(−816), and agrees with FFmpeg's own decode within ±2. Nobody has listened to it. Not yet checked: the sound track's
timing against the picture, and the 565 copy path (USE_32_BIT_FMV off).
