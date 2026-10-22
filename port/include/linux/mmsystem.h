// SPDX-License-Identifier: GPL-3.0-only
// port/include/linux/mmsystem.h: the WinMM subset the game calls (multimedia file I/O for WAV parsing in wavread.cpp,
// timeGetTime), with the SDK's names, values and layouts; implemented in port/platform. Linux only.
#ifndef PORT_MMSYSTEM_H
#define PORT_MMSYSTEM_H
#include <windows.h>
#include <mmreg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef UINT MMRESULT;
typedef DWORD FOURCC;
typedef char *HPSTR;
DECLARE_HANDLE(HMMIO);

#define MMSYSERR_NOERROR 0
#define MMSYSERR_ERROR 1
#define MMIOERR_BASE 256
#define MMIOERR_CANNOTOPEN (MMIOERR_BASE + 3)
#define MMIOERR_CANNOTREAD (MMIOERR_BASE + 5)
#define MMIOERR_CHUNKNOTFOUND (MMIOERR_BASE + 9)
#define MMIOERR_CANNOTEXPAND (MMIOERR_BASE + 8)

#define mmioFOURCC(a, b, c, d) \
	((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))
#define FOURCC_RIFF mmioFOURCC('R', 'I', 'F', 'F')
#define FOURCC_LIST mmioFOURCC('L', 'I', 'S', 'T')

#define MMIO_READ 0x00000000
#define MMIO_WRITE 0x00000001
#define MMIO_READWRITE 0x00000002
#define MMIO_ALLOCBUF 0x00010000
#define MMIO_FINDCHUNK 0x0010
#define MMIO_FINDRIFF 0x0020
#define MMIO_FINDLIST 0x0040
#define MMIO_DIRTY 0x10000000
#define MMIO_EMPTYBUF 0x0010


typedef LRESULT (CALLBACK *LPMMIOPROC)(LPSTR, UINT, LPARAM, LPARAM);

typedef struct _MMIOINFO
{
	DWORD dwFlags;
	FOURCC fccIOProc;
	LPMMIOPROC pIOProc;
	UINT wErrorRet;
	HTASK htask;
	LONG cchBuffer;
	HPSTR pchBuffer;
	HPSTR pchNext;
	HPSTR pchEndRead;
	HPSTR pchEndWrite;
	LONG lBufOffset;
	LONG lDiskOffset;
	DWORD adwInfo[3];
	DWORD dwReserved1;
	DWORD dwReserved2;
	HMMIO hmmio;
} MMIOINFO, *PMMIOINFO, *LPMMIOINFO;
typedef const MMIOINFO *LPCMMIOINFO;

typedef struct _MMCKINFO
{
	FOURCC ckid;
	DWORD cksize;
	FOURCC fccType;
	DWORD dwDataOffset;
	DWORD dwFlags;
} MMCKINFO, *PMMCKINFO, *LPMMCKINFO;
typedef const MMCKINFO *LPCMMCKINFO;

HMMIO WINAPI mmioOpenA(LPSTR pszFileName, LPMMIOINFO pmmioinfo, DWORD fdwOpen);
MMRESULT WINAPI mmioClose(HMMIO hmmio, UINT fuClose);
LONG WINAPI mmioRead(HMMIO hmmio, HPSTR pch, LONG cch);
LONG WINAPI mmioSeek(HMMIO hmmio, LONG lOffset, int iOrigin);
MMRESULT WINAPI mmioGetInfo(HMMIO hmmio, LPMMIOINFO pmmioinfo, UINT fuInfo);
MMRESULT WINAPI mmioSetInfo(HMMIO hmmio, LPCMMIOINFO pmmioinfo, UINT fuInfo);
MMRESULT WINAPI mmioAdvance(HMMIO hmmio, LPMMIOINFO pmmioinfo, UINT fuAdvance);
MMRESULT WINAPI mmioDescend(HMMIO hmmio, LPMMCKINFO pmmcki, const MMCKINFO *pmmckiParent, UINT fuDescend);
MMRESULT WINAPI mmioAscend(HMMIO hmmio, LPMMCKINFO pmmcki, UINT fuAscend);
#define mmioOpen mmioOpenA

DWORD WINAPI timeGetTime(void);
MMRESULT WINAPI timeBeginPeriod(UINT uPeriod);
MMRESULT WINAPI timeEndPeriod(UINT uPeriod);

#ifdef __cplusplus
}
#endif
#endif
