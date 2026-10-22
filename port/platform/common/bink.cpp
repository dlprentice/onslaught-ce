// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/bink.cpp: RAD's Bink video entry points the front end calls (src/binkapi.h), for now as a
// player that cannot open a file: BinkOpen returns NULL, so the intro and cut-scene videos are skipped the way the
// game skips a missing video. Decoding the .bik files through FFmpeg's Bink decoder is a later step (port/PLAN.md).
// Same names and calling convention as src/binkapi.h (stdcall on Windows; dxvk-native defines __stdcall away).
#include <windows.h>

typedef unsigned long U32;
typedef long S32;
struct BINK;
typedef BINK *HBINK;
struct BINKSUMMARY;
typedef void *(__stdcall *RADMEMALLOC)(U32 bytes);
typedef void (__stdcall *RADMEMFREE)(void *ptr);
typedef void *(__stdcall *BINKSNDSYSOPEN)(U32 param);

extern "C"
{
void __stdcall RADSetMemory(RADMEMALLOC, RADMEMFREE) {}
void *__stdcall BinkOpenDirectSound(U32) { return NULL; }
S32 __stdcall BinkSetSoundSystem(BINKSNDSYSOPEN, U32) { return 1; }
void __stdcall BinkSetSoundTrack(U32, U32 *) {}
HBINK __stdcall BinkOpen(const char *, U32) { return NULL; }
void __stdcall BinkSetVolume(HBINK, U32, S32) {}
S32 __stdcall BinkDoFrame(HBINK) { return 0; }
void __stdcall BinkNextFrame(HBINK) {}
S32 __stdcall BinkWait(HBINK) { return 0; }
S32 __stdcall BinkCopyToBuffer(HBINK, void *, S32, U32, U32, U32, U32) { return 0; }
void __stdcall BinkGetSummary(HBINK, BINKSUMMARY *) {}
void __stdcall BinkClose(HBINK) {}
}
