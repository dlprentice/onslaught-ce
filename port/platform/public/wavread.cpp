// SPDX-License-Identifier: GPL-3.0-only
// port/platform/public/wavread.cpp: the RIFF/WAVE reader (WavRead.h), written for the public build in place of
// src/wavread.cpp. It reads RIFF chunks through multimedia I/O, allocates the format from the game's sound memory
// (a PCM format as a bare WAVEFORMATEX, any other with its extra bytes), and copies samples from the "data" chunk
// through the file's I/O buffer.
// The public reader owns only a successfully opened handle: unopened destruction, repeated Close and failed/repeated
// Open are safe. This corrects the inherited lifetime bug without changing the byte-matching reader in src/.
#include "common.h"
#include "Platform.h"
#include "WavRead.h"

namespace
{
// Descends into the file's RIFF chunk, checks it is a WAVE file and reads its "fmt " chunk into a new format.
HRESULT LoadFormat(HMMIO file, MMCKINFO *riff, WAVEFORMATEX **format)
{
	*format = NULL;
	if (mmioDescend(file, riff, NULL, 0) != 0)
		return E_FAIL;
	if (riff->ckid != FOURCC_RIFF || riff->fccType != mmioFOURCC('W', 'A', 'V', 'E'))
		return E_FAIL;

	MMCKINFO	chunk;
	chunk.ckid = mmioFOURCC('f', 'm', 't', ' ');
	if (mmioDescend(file, &chunk, riff, MMIO_FINDCHUNK) != 0)
		return E_FAIL;
	if (chunk.cksize < (LONG)sizeof(PCMWAVEFORMAT))
		return E_FAIL;

	PCMWAVEFORMAT	pcm;
	if (mmioRead(file, (HPSTR)&pcm, sizeof(pcm)) != sizeof(pcm))
		return E_FAIL;

	if (pcm.wf.wFormatTag == WAVE_FORMAT_PCM)
	{
		WAVEFORMATEX	*f = new(MEMTYPE_SOUND) WAVEFORMATEX;
		if (!f)
			return E_FAIL;
		memcpy(f, &pcm, sizeof(pcm));
		f->cbSize = 0;
		*format = f;
	}
	else
	{
		// Another format: the word after the PCM part gives the count of extra bytes that follow it.
		WORD	extra = 0;
		if (mmioRead(file, (CHAR *)&extra, sizeof(extra)) != sizeof(extra))
			return E_FAIL;
		WAVEFORMATEX	*f = (WAVEFORMATEX *)new(MEMTYPE_SOUND) CHAR[sizeof(WAVEFORMATEX) + extra];
		if (!f)
			return E_FAIL;
		memcpy(f, &pcm, sizeof(pcm));
		f->cbSize = extra;
		*format = f;
		// The extra bytes follow the cbSize field.
		if (mmioRead(file, (CHAR *)((BYTE *)&f->cbSize + sizeof(WORD)), extra) != extra)
		{
			delete f;
			*format = NULL;
			return E_FAIL;
		}
	}

	if (mmioAscend(file, &chunk, 0) != 0)
	{
		delete *format;
		*format = NULL;
		return E_FAIL;
	}
	return S_OK;
}
}

CWaveSoundRead::CWaveSoundRead()
{
	m_pwfx = NULL;
	m_hmmioIn = NULL;
}

CWaveSoundRead::~CWaveSoundRead()
{
	Close();
	SAFE_DELETE(m_pwfx);
}

HRESULT CWaveSoundRead::Open(CHAR *name)
{
	Close();
	SAFE_DELETE(m_pwfx);

	HMMIO	file = mmioOpen(ToTCHAR(name), NULL, MMIO_ALLOCBUF | MMIO_READ);
	if (!file)
		return E_FAIL;
	HRESULT	hr = LoadFormat(file, &m_ckInRiff, &m_pwfx);
	if (FAILED(hr))
	{
		mmioClose(file, 0);
		return hr;
	}
	m_hmmioIn = file;
	hr = Reset();
	if (FAILED(hr))
	{
		Close();
		SAFE_DELETE(m_pwfx);
	}
	return hr;
}

// Back to the first sample: seek past the RIFF chunk's form type and find the "data" chunk.
HRESULT CWaveSoundRead::Reset()
{
	if (!m_hmmioIn)
		return E_FAIL;
	if (mmioSeek(m_hmmioIn, m_ckInRiff.dwDataOffset + sizeof(FOURCC), SEEK_SET) == -1)
		return E_FAIL;
	m_ckIn.ckid = mmioFOURCC('d', 'a', 't', 'a');
	if (mmioDescend(m_hmmioIn, &m_ckIn, &m_ckInRiff, MMIO_FINDCHUNK) != 0)
		return E_FAIL;
	return S_OK;
}

// Up to `bytes` bytes of samples, fewer at the chunk's end; the file's I/O buffer is refilled as it empties.
HRESULT CWaveSoundRead::Read(UINT bytes, BYTE *out, UINT *got)
{
	*got = 0;
	if (!m_hmmioIn)
		return E_FAIL;
	MMIOINFO	io;
	if (mmioGetInfo(m_hmmioIn, &io, 0) != 0)
		return E_FAIL;

	UINT	n = bytes < m_ckIn.cksize ? bytes : m_ckIn.cksize;
	m_ckIn.cksize -= n;
	for (DWORD k = 0; k < n; k++)
	{
		if (io.pchNext == io.pchEndRead)
		{
			if (mmioAdvance(m_hmmioIn, &io, MMIO_READ) != 0 || io.pchNext == io.pchEndRead)
				return E_FAIL;
		}
		out[k] = (BYTE)*io.pchNext++;
	}

	if (mmioSetInfo(m_hmmioIn, &io, 0) != 0)
		return E_FAIL;
	*got = n;
	return S_OK;
}

HRESULT CWaveSoundRead::Close()
{
	if (m_hmmioIn)
	{
		HMMIO file = m_hmmioIn;
		m_hmmioIn = NULL;
		mmioClose(file, 0);
	}
	return S_OK;
}
