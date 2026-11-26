// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/mmio.cpp: WinMM's multimedia file I/O for the game's WAV reader (src/wavread.cpp): open, read,
// seek, RIFF chunk descend/ascend, and the buffered direct access through MMIOINFO (mmioGetInfo/Advance/SetInfo).
// Linux only; Windows uses WinMM.
#include "handles.h"
#ifndef CE_NETWORK_OFF
#include "../../network/compatibility.h"
#endif

#ifndef MMSYSERR_INVALHANDLE
#define MMSYSERR_INVALHANDLE 5
#endif
#ifndef MMIOERR_OUTOFMEMORY
#define MMIOERR_OUTOFMEMORY (MMIOERR_BASE + 2)
#endif
#ifndef MMIOERR_CANNOTSEEK
#define MMIOERR_CANNOTSEEK (MMIOERR_BASE + 7)
#endif
#ifndef MMIOERR_INVALIDFILE
#define MMIOERR_INVALIDFILE (MMIOERR_BASE + 16)
#endif

#include <stdint.h>
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
	if (!m || !m->file || !dst || pos < 0 || n < 0)
		return -1;
	if (fseek(m->file, pos, SEEK_SET) != 0)
		return -1;
	return (LONG)fread(dst, 1, (size_t)n, m->file);
}

bool ReadDword(Mmio *m, DWORD *v)
{
	unsigned char b[4];
	if (m->pos < 0 || m->pos > 0x7fffffff - 4 || ReadAt(m, m->pos, b, 4) != 4)
		return false;
	*v = b[0] | (b[1] << 8) | (b[2] << 16) | ((DWORD)b[3] << 24);
	m->pos += 4;
	return true;
}
}

extern "C" HMMIO WINAPI mmioOpenA(LPSTR name, LPMMIOINFO info, DWORD)
{
#ifndef CE_NETWORK_OFF
	if (!CE_CompatibilityUnsupportedReader(NULL, 0))
	{
		if (info) info->wErrorRet = MMIOERR_CANNOTOPEN;
		PortSetLastError(ERROR_ACCESS_DENIED);
		return NULL;
	}
#endif
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
	if (!m)
	{
		fclose(f);
		if (info)
			info->wErrorRet = MMIOERR_OUTOFMEMORY;
		return NULL;
	}
	m->buffer = (char *)malloc(kBuffer);
	if (!m->buffer)
	{
		free(m);
		fclose(f);
		if (info)
			info->wErrorRet = MMIOERR_OUTOFMEMORY;
		return NULL;
	}
	m->file = f;
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
	if (!m || !dst || n < 0)
		return -1;
	LONG got = ReadAt(m, m->pos, dst, n);
	if (got > 0)
		m->pos += got;
	return got;
}

extern "C" LONG WINAPI mmioSeek(HMMIO h, LONG offset, int origin)
{
	Mmio *m = Get(h);
	if (!m)
		return -1;
	int64_t base = 0;
	if (origin == SEEK_CUR)
		base = m->pos;
	else if (origin == SEEK_END)
	{
		if (fseek(m->file, 0, SEEK_END) != 0)
			return -1;
		long end = ftell(m->file);
		if (end < 0)
			return -1;
		base = end;
	}
	else if (origin != SEEK_SET)
		return -1;
	int64_t next = base + offset;
	if (next < 0 || next > 0x7fffffffll)
		return -1;
	m->pos = (LONG)next;
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
	if (!m || !info)
		return MMSYSERR_INVALHANDLE;
	memset(info, 0, sizeof(*info));
	Fill(m, info, m->pos);
	return MMSYSERR_NOERROR;
}

extern "C" MMRESULT WINAPI mmioAdvance(HMMIO h, LPMMIOINFO info, UINT)
{
	Mmio *m = Get(h);
	if (!m || !info || !info->pchBuffer || !info->pchNext)
		return MMSYSERR_INVALHANDLE;
	int64_t next = (int64_t)info->lBufOffset + (int64_t)(info->pchNext - info->pchBuffer);
	if (next < 0 || next > 0x7fffffffll)
		return MMIOERR_CANNOTREAD;
	Fill(m, info, (LONG)next);
	return MMSYSERR_NOERROR;
}

extern "C" MMRESULT WINAPI mmioSetInfo(HMMIO h, LPCMMIOINFO info, UINT)
{
	Mmio *m = Get(h);
	if (!m || !info || !info->pchBuffer || !info->pchNext)
		return MMSYSERR_INVALHANDLE;
	int64_t next = (int64_t)info->lBufOffset + (int64_t)(info->pchNext - info->pchBuffer);
	if (next < 0 || next > 0x7fffffffll)
		return MMIOERR_INVALIDFILE;
	m->pos = (LONG)next;
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
	if (!m || !ck)
		return MMSYSERR_INVALHANDLE;
	uint64_t end = parent ? (uint64_t)parent->dwDataOffset + (uint64_t)parent->cksize : 0x7fffffffull;
	if (end > 0x7fffffffull)
		end = 0x7fffffffull;
	FOURCC want = ck->ckid, wantType = ck->fccType;
	for (;;)
	{
		if (m->pos < 0 || (uint64_t)m->pos + 8ull > end)
			return MMIOERR_CHUNKNOTFOUND;
		uint64_t start = (uint64_t)m->pos;
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
		uint64_t padded = ((uint64_t)found.cksize + 1ull) & ~1ull;
		uint64_t next = start + 8ull + padded;
		if (next <= start || next > end)
			return MMIOERR_CHUNKNOTFOUND;
		m->pos = (LONG)next;
	}
}

extern "C" MMRESULT WINAPI mmioAscend(HMMIO h, LPMMCKINFO ck, UINT)
{
	Mmio *m = Get(h);
	if (!m || !ck)
		return MMSYSERR_INVALHANDLE;
	uint64_t padded = ((uint64_t)ck->cksize + 1ull) & ~1ull;
	uint64_t next = (uint64_t)ck->dwDataOffset + padded;
	if (next > 0x7fffffffull)
		return MMIOERR_CANNOTSEEK;
	m->pos = (LONG)next;
	return MMSYSERR_NOERROR;
}
