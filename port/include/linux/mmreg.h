// SPDX-License-Identifier: GPL-3.0-only
// port/include/linux/mmreg.h: the wave-format declarations the game uses, with the SDK's names, values and packed
// layouts (WAVEFORMATEX is 18 bytes). Linux only; Windows builds use the MinGW header.
#ifndef PORT_MMREG_H
#define PORT_MMREG_H
#include <windows.h>

#define WAVE_FORMAT_PCM 1

#pragma pack(push, 1)
typedef struct tWAVEFORMATEX
{
	WORD wFormatTag;
	WORD nChannels;
	DWORD nSamplesPerSec;
	DWORD nAvgBytesPerSec;
	WORD nBlockAlign;
	WORD wBitsPerSample;
	WORD cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;
typedef const WAVEFORMATEX *LPCWAVEFORMATEX;

typedef struct waveformat_tag
{
	WORD wFormatTag;
	WORD nChannels;
	DWORD nSamplesPerSec;
	DWORD nAvgBytesPerSec;
	WORD nBlockAlign;
} WAVEFORMAT, *LPWAVEFORMAT;

typedef struct pcmwaveformat_tag
{
	WAVEFORMAT wf;
	WORD wBitsPerSample;
} PCMWAVEFORMAT, *LPPCMWAVEFORMAT;
#pragma pack(pop)

#endif
