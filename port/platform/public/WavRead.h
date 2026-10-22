// SPDX-License-Identifier: GPL-3.0-only
// port/platform/public/WavRead.h: the game's wave-file readers, written for the public build in place of src/WavRead.h.
// The interface is the one the game's code uses: an abstract reader (opened by name, read in pieces, asked for its
// format) that the Ogg reader also implements, and the RIFF/WAVE reader the sound manager owns.
#ifndef WAVE_READ_H
#define WAVE_READ_H

#include <mmreg.h>
#include <mmsystem.h>

// A sound file reader: Open, then Read until it returns fewer bytes than asked; HasFormat says whether Open found a
// format, which the sample rate and channel count then describe.
class CBaseWaveFileRead
{
public:
	virtual ~CBaseWaveFileRead() {}

	virtual HRESULT Open(CHAR *name) = 0;
	virtual HRESULT Read(UINT bytes, BYTE *out, UINT *got) = 0;
	virtual HRESULT Close() = 0;
#ifdef PCMUSICSTREAM_CPP	// the music stream tests the answer as a bool
	virtual bool	HasFormat() = 0;
#else
	virtual BOOL	HasFormat() = 0;
#endif
	virtual DWORD	GetSampleRate() = 0;
	virtual DWORD	GetChannels() = 0;
};

// A RIFF/WAVE file read through the multimedia I/O calls: the format from its "fmt " chunk, the samples from its
// "data" chunk.
class CWaveSoundRead : public CBaseWaveFileRead
{
public:
	WAVEFORMATEX	*m_pwfx;		// the format, or NULL before a successful Open
	HMMIO			m_hmmioIn;		// the open file
	MMCKINFO		m_ckIn;			// the "data" chunk; its size counts down as it is read
	MMCKINFO		m_ckInRiff;		// the RIFF chunk

	CWaveSoundRead();
	~CWaveSoundRead();

	HRESULT Open(CHAR *name);
	HRESULT Reset();
	HRESULT Read(UINT bytes, BYTE *out, UINT *got);
	HRESULT Close();
#ifdef PCMUSICSTREAM_CPP
	bool	HasFormat()		{ return m_pwfx != NULL; }
#else
	BOOL	HasFormat()		{ return m_pwfx != NULL; }
#endif
	DWORD	GetSampleRate()	{ return m_pwfx->nSamplesPerSec; }
	DWORD	GetChannels()	{ return m_pwfx->nChannels; }
};

#endif
