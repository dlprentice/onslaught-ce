// SPDX-License-Identifier: GPL-3.0-only
// port/include/linux/vfw.h: the AVI recorder's declarations (ltshell.cpp); the port's recorder is a stub that never
// opens a stream. Linux only.
#ifndef PORT_VFW_H
#define PORT_VFW_H
#include <windows.h>
#include <mmsystem.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct IAVIFile *PAVIFILE;
typedef struct IAVIStream *PAVISTREAM;
typedef struct _AVISTREAMINFOA
{
	DWORD fccType, fccHandler, dwFlags, dwCaps;
	WORD wPriority, wLanguage;
	DWORD dwScale, dwRate, dwStart, dwLength, dwInitialFrames, dwSuggestedBufferSize, dwQuality, dwSampleSize;
	RECT rcFrame;
	DWORD dwEditCount, dwFormatChangeCount;
	char szName[64];
} AVISTREAMINFOA, AVISTREAMINFO;
typedef struct
{
	DWORD fccType, fccHandler, dwKeyFrameEvery, dwQuality, dwBytesPerSecond, dwFlags;
	LPVOID lpFormat;
	DWORD cbFormat;
	LPVOID lpParms;
	DWORD cbParms, dwInterleaveEvery;
} AVICOMPRESSOPTIONS;
#define streamtypeVIDEO mmioFOURCC('v', 'i', 'd', 's')
#define AVIIF_KEYFRAME 0x00000010L
#define OF_WRITE 0x00000001
#define OF_CREATE 0x00001000
void WINAPI AVIFileInit(void);
void WINAPI AVIFileExit(void);
HRESULT WINAPI AVIFileOpenA(PAVIFILE *ppfile, LPCSTR szFile, UINT uMode, void *lpHandler);
HRESULT WINAPI AVIFileCreateStreamA(PAVIFILE pfile, PAVISTREAM *ppavi, AVISTREAMINFOA *psi);
HRESULT WINAPI AVIMakeCompressedStream(PAVISTREAM *ppsCompressed, PAVISTREAM ppsSource, AVICOMPRESSOPTIONS *lpOptions, void *pclsidHandler);
HRESULT WINAPI AVIStreamSetFormat(PAVISTREAM pavi, LONG lPos, LPVOID lpFormat, LONG cbFormat);
HRESULT WINAPI AVIStreamWrite(PAVISTREAM pavi, LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, DWORD dwFlags,
							  LONG *plSampWritten, LONG *plBytesWritten);
ULONG WINAPI AVIStreamRelease(PAVISTREAM pavi);
ULONG WINAPI AVIFileRelease(PAVIFILE pfile);
#define AVIFileOpen AVIFileOpenA
#define AVIFileCreateStream AVIFileCreateStreamA
#ifdef __cplusplus
}
#endif
#endif
