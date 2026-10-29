// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/mmio.cpp: WinMM's multimedia file I/O for the game's WAV reader (src/wavread.cpp): open, read,
// seek, RIFF chunk descend/ascend, and the buffered direct access through MMIOINFO (mmioGetInfo/Advance/SetInfo).
// Linux only; Windows uses WinMM.
#include "handles.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace
{
const LONG kBuffer = 8192;

struct Mmio
{
	FILE *file;
	LONG pos;		// logical read position
	char *buffer;
};

Mmio *Get(HMMIO h) { return (Mmio *)h; }

LONG ReadAt(Mmio *m, LONG pos, void *dst, LONG n)
{
	if (fseek(m->file, pos, SEEK_SET) != 0)
		return -1;
	return (LONG)fread(dst, 1, n, m->file);
}

bool ReadDword(Mmio *m, DWORD *v)
{
	unsigned char b[4];
	if (ReadAt(m, m->pos, b, 4) != 4)
		return false;
	*v = b[0] | (b[1] << 8) | (b[2] << 16) | ((DWORD)b[3] << 24);
	m->pos += 4;
	return true;
}
}

extern "C" HMMIO WINAPI mmioOpenA(LPSTR name, LPMMIOINFO info, DWORD)
{
	char path[4096];
	if (PortResolvePath(name, PORT_PATH_READ, path, sizeof(path)) != 0)
		return NULL;
	int mod = PortModReadFd(name, path);
	FILE *f = mod >= 0 ? fdopen(mod, "rb") : fopen(path, "rb");
	if (!f && mod >= 0)
		close(mod);
	if (!f)
	{
		if (info)
			info->wErrorRet = MMIOERR_CANNOTOPEN;
		return NULL;
	}
	Mmio *m = (Mmio *)calloc(1, sizeof(Mmio));
	m->file = f;
	m->buffer = (char *)malloc(kBuffer);
	return (HMMIO)m;
}

extern "C" MMRESULT WINAPI mmioClose(HMMIO h, UINT)
{
	Mmio *m = Get(h);
	if (!m)
		return MMSYSERR_ERROR;
	fclose(m->file);
	free(m->buffer);
	free(m);
	return MMSYSERR_NOERROR;
}

extern "C" LONG WINAPI mmioRead(HMMIO h, HPSTR dst, LONG n)
{
	Mmio *m = Get(h);
	LONG got = ReadAt(m, m->pos, dst, n);
	if (got > 0)
		m->pos += got;
	return got;
}

extern "C" LONG WINAPI mmioSeek(HMMIO h, LONG offset, int origin)
{
	Mmio *m = Get(h);
	LONG base = 0;
	if (origin == SEEK_CUR)
		base = m->pos;
	else if (origin == SEEK_END)
	{
		fseek(m->file, 0, SEEK_END);
		base = (LONG)ftell(m->file);
	}
	m->pos = base + offset;
	return m->pos;
}

static void Fill(Mmio *m, LPMMIOINFO info, LONG pos)
{
	LONG got = ReadAt(m, pos, m->buffer, kBuffer);
	if (got < 0)
		got = 0;
	info->pchBuffer = m->buffer;
	info->pchNext = m->buffer;
	info->pchEndRead = m->buffer + got;
	info->pchEndWrite = m->buffer + kBuffer;
	info->cchBuffer = kBuffer;
	info->lBufOffset = pos;
	info->lDiskOffset = pos + got;
	info->hmmio = (HMMIO)m;
}

extern "C" MMRESULT WINAPI mmioGetInfo(HMMIO h, LPMMIOINFO info, UINT)
{
	Mmio *m = Get(h);
	memset(info, 0, sizeof(*info));
	Fill(m, info, m->pos);
	return MMSYSERR_NOERROR;
}

extern "C" MMRESULT WINAPI mmioAdvance(HMMIO h, LPMMIOINFO info, UINT)
{
	Mmio *m = Get(h);
	Fill(m, info, info->lBufOffset + (LONG)(info->pchNext - info->pchBuffer));
	return MMSYSERR_NOERROR;
}

extern "C" MMRESULT WINAPI mmioSetInfo(HMMIO h, LPCMMIOINFO info, UINT)
{
	Mmio *m = Get(h);
	m->pos = info->lBufOffset + (LONG)(info->pchNext - info->pchBuffer);
	return MMSYSERR_NOERROR;
}

// Reads the chunk header at the current position into ck; the position moves to its data (past the form type for
// RIFF and LIST chunks).
static bool ReadHeader(Mmio *m, LPMMCKINFO ck)
{
	if (!ReadDword(m, &ck->ckid) || !ReadDword(m, &ck->cksize))
		return false;
	ck->dwDataOffset = m->pos;
	ck->dwFlags = 0;
	ck->fccType = 0;
	if (ck->ckid == FOURCC_RIFF || ck->ckid == FOURCC_LIST)
		return ReadDword(m, &ck->fccType);
	return true;
}

extern "C" MMRESULT WINAPI mmioDescend(HMMIO h, LPMMCKINFO ck, const MMCKINFO *parent, UINT flags)
{
	Mmio *m = Get(h);
	LONG end = parent ? (LONG)(parent->dwDataOffset + parent->cksize) : 0x7fffffff;
	FOURCC want = ck->ckid, wantType = ck->fccType;
	for (;;)
	{
		if (m->pos + 8 > end)
			return MMIOERR_CHUNKNOTFOUND;
		LONG start = m->pos;
		MMCKINFO found;
		if (!ReadHeader(m, &found))
			return MMIOERR_CHUNKNOTFOUND;
		bool match = true;
		if (flags & MMIO_FINDCHUNK)
			match = found.ckid == want;
		else if (flags & MMIO_FINDRIFF)
			match = found.ckid == FOURCC_RIFF && found.fccType == wantType;
		else if (flags & MMIO_FINDLIST)
			match = found.ckid == FOURCC_LIST && found.fccType == wantType;
		if (match)
		{
			*ck = found;
			return MMSYSERR_NOERROR;
		}
		m->pos = start + 8 + (LONG)((found.cksize + 1) & ~1u);
	}
}

extern "C" MMRESULT WINAPI mmioAscend(HMMIO h, LPMMCKINFO ck, UINT)
{
	Mmio *m = Get(h);
	m->pos = (LONG)(ck->dwDataOffset + ((ck->cksize + 1) & ~1u));
	return MMSYSERR_NOERROR;
}
