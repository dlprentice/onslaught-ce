// PCMusicStream.cpp: the PC's streamed music (not in Stuart's release; rebuilt from the retail object
// 00527e60..00528680; the file name is not known). CPCMusic (PCMusic.cpp) plays each .ogg track through a
// looping 512 KB DirectSound buffer that a worker thread refills half by half from a COggFileRead: the buffer's
// two position notifications ask for the half just played, a fourth event asks for a new track and a fifth
// stops the thread. Tracks must be 44.1 kHz stereo.

#define PCMUSICSTREAM_CPP
#define _MBCS
#include "common.h"
#include <tchar.h>
#include <dsound.h>
#include "SoundManager.h"
#include "OggFileRead.h"
#include "mathsconst.h"
#include "debuglog.h"

// FUNCTION: 0x00527e60 init:ZERO_FVECTOR
// FUNCTION: 0x00527e80 init:ID_FMATRIX

#define STREAM_BUFFER_SIZE	0x80000
#define STREAM_HALF			(STREAM_BUFFER_SIZE / 2)

static DWORD				sWaitResult;			// 0089bea8: the event the thread last woke for
bool						MUSIC_STREAM_FINISHED;	// 0089beac: the track ran out (CPCMusic::DeviceGetTrackFinished)
static BOOL					sThreadAwake;			// 0089beb0
static HANDLE				sThread;				// 0089beb4
static HANDLE				sPlayEvent;				// 0089beb8: a new track
static HANDLE				sQuitEvent;				// 0089bebc
static HANDLE				sSecondHalfEvent;		// 0089bec0: the play position reached the end (refill the second half)
static HANDLE				sFirstHalfEvent;		// 0089bec4: the play position reached the middle (refill the first half)
static IDirectSoundBuffer	*sBuffer;				// 0089bec8
static DWORD				sBytesStreamed;			// 0089becc
static int					sSilentHalves;			// 0089bed0: halves padded with silence since the track ended
static char					sTrackName[256];		// 0089bed4
static CBaseWaveFileRead	*sReader;				// 0089bfd4

void	PlayMusicStream(char *filename);
void	StopMusicStream();

// Tests HasFormat's result as a bool (test al,al, 00528007) though both readers return BOOL: this file saw a bool
// declaration (the PCMUSICSTREAM_CPP views in WavRead.h and OggFileRead.h).
DWORD	MusicStreamThread(LPVOID)	// FUNCTION: 0x00527f50
{
	HANDLE	events[4] = { sPlayEvent, sFirstHalfEvent, sSecondHalfEvent, sQuitEvent };
	DWORD	offset;
	void	*data1, *data2;
	DWORD	size1, size2;
	UINT	got;

	while (1)
	{
		sThreadAwake = FALSE;
		DWORD result = WaitForMultipleObjects(4, events, FALSE, INFINITE);
		if (result == WAIT_FAILED)
		{
			GetLastError();
			Sleep(100);
			continue;
		}

		sWaitResult = result;
		sThreadAwake = TRUE;
		switch (result)
		{
		case 0:		// a new track
			sReader->Close();
			sSilentHalves = 0;
			sBytesStreamed = 0;
			sReader->Open(sTrackName);
			MUSIC_STREAM_FINISHED = false;
			if (sReader->HasFormat() && (sReader->GetChannels() == 2) && (sReader->GetSampleRate() == 44100))
			{
				if (sBuffer)
				{
					DWORD	status;
					sBuffer->GetStatus(&status);
					if (status & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING))
						break;
				}
				sBuffer->Lock(0, 0, &data1, &size1, &data2, &size2, DSBLOCK_ENTIREBUFFER);
				sReader->Read(size1, (BYTE *)data1, &got);
				if (got == 0)
				{
					MUSIC_STREAM_FINISHED = true;
					StopMusicStream();
					sBuffer->Unlock(data1, size1, data2, size2);
					break;
				}
				if (got < size1)
				{
					memset((BYTE *)data1 + got, 0, size1 - got);
					sSilentHalves++;
				}
				sBuffer->Unlock(data1, size1, data2, size2);
				sBuffer->SetCurrentPosition(0);
				sBuffer->Play(0, 0, DSBPLAY_LOOPING);
				sBytesStreamed += size1;
			}
			else
				StopMusicStream();
			break;

		case 1:		// a half has played: refill it
		case 2:
			if (sSilentHalves > 2)
			{
				StopMusicStream();
				sReader->Close();
				sSilentHalves = 0;
				sBytesStreamed = 0;
				MUSIC_STREAM_FINISHED = true;
				break;
			}
			switch (result)
			{
			case 1:	offset = 0;				break;
			case 2:	offset = STREAM_HALF;	break;
			}
			sBuffer->Lock(offset, STREAM_HALF, &data1, &size1, &data2, &size2, 0);
			sReader->Read(size1, (BYTE *)data1, &got);
			if (got < size1)
			{
				memset((BYTE *)data1 + got, 0, size1 - got);
				sSilentHalves++;
			}
			sBuffer->Unlock(data1, size1, data2, size2);
			sBytesStreamed += size1;
			break;

		case 3:		// shut down
			sThreadAwake = FALSE;
			return 0;
		}
	}
}

//******************************************************************************************
// Near miss: statement and local-layout order of the buffer description not yet reproduced.
void	InitMusicStream()	// FUNCTION: 0x005282b0
{
	WAVEFORMATEX		wfx;
	DSBUFFERDESC		desc;
	IDirectSoundNotify	*notify;

	ZeroMemory(&wfx, sizeof(wfx));
	ZeroMemory(&desc, sizeof(desc));
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = 44100;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = 4;
	wfx.nAvgBytesPerSec = 44100 * 4;
	wfx.cbSize = 0;
	desc.dwSize = sizeof(desc);
	desc.dwBufferBytes = STREAM_BUFFER_SIZE;
	desc.dwFlags = DSBCAPS_LOCSOFTWARE | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_CTRLVOLUME;	// 0x188 (00528336)
	desc.lpwfxFormat = &wfx;

	if (SOUND.GetDirectSound())
	{
		DWORD	id;

		SOUND.GetDirectSound()->CreateSoundBuffer(&desc, &sBuffer, NULL);
		sFirstHalfEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		sSecondHalfEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		sQuitEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		sPlayEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		sThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MusicStreamThread, NULL, 0, &id);

		notify = NULL;
		HRESULT	hr = sBuffer->QueryInterface(IID_IDirectSoundNotify, (void **)&notify);
		if (SUCCEEDED(hr))
		{
			DSBPOSITIONNOTIFY	pos[2];
			pos[0].dwOffset = STREAM_HALF;
			pos[0].hEventNotify = sFirstHalfEvent;
			pos[1].dwOffset = STREAM_BUFFER_SIZE - 2;
			pos[1].hEventNotify = sSecondHalfEvent;
			notify->SetNotificationPositions(2, pos);
		}
		notify->Release();

		sReader = new COggFileRead;
		memset(sTrackName, 0, sizeof(sTrackName));
		sSilentHalves = 0;
	}
}

//******************************************************************************************
void	ShutdownMusicStream()	// FUNCTION: 0x00528460
{
	if (sBuffer)
	{
		DWORD	code;

		SetEvent(sQuitEvent);
		do
		{
			Sleep(10);
			GetExitCodeThread(sThread, &code);
		}
		while (code == STILL_ACTIVE);
		sThread = NULL;

		sBuffer->Stop();
		sBuffer->Release();
		sBuffer = NULL;
		CloseHandle(sFirstHalfEvent);
		CloseHandle(sSecondHalfEvent);
		CloseHandle(sQuitEvent);
		CloseHandle(sPlayEvent);
		sFirstHalfEvent = NULL;
		sSecondHalfEvent = NULL;
		sQuitEvent = NULL;
		sPlayEvent = NULL;
		delete sReader;
		sReader = NULL;
		memset(sTrackName, 0, sizeof(sTrackName));
		sSilentHalves = 0;
	}
}

//******************************************************************************************
// TCHAR code built with _MBCS: the name is compared and copied by the multibyte CRT (_mbsicmp 0055f8a1,
// _mbsnbcpy 0055f807).
void	PlayMusicStream(char *filename)	// FUNCTION: 0x00528540
{
	if (sBuffer)
	{
		DWORD	status;
		sBuffer->GetStatus(&status);
		if ((status & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) || (sThreadAwake && (sWaitResult == 0)))
		{
			if (!_tcsicmp(filename, sTrackName))
				return;
		}
		_tcsncpy(sTrackName, filename, 255);
		MUSIC_STREAM_FINISHED = false;
		SetEvent(sPlayEvent);
	}
}

//******************************************************************************************
void	StopMusicStream()	// FUNCTION: 0x005285b0
{
	if (sBuffer)
	{
		sBuffer->Stop();
		ResetEvent(sFirstHalfEvent);
		ResetEvent(sSecondHalfEvent);
		ResetEvent((HANDLE)PlayMusicStream);		// sic: retail resets the function's address (005285d7)
	}
}

//******************************************************************************************
void	SetMusicStreamVolume(float volume)	// FUNCTION: 0x005285e0
{
	if (sBuffer)
	{
		if (volume < 0.f)
			volume = 0.f;
		else if (volume > 1.f)
			volume = 1.f;

		float v = (float)pow(volume, 0.3f);
		if (v < 0.f)
			v = 0.f;
		else if (v > 1.f)
			v = 1.f;
		sBuffer->SetVolume(int(v * 10000.f - 10000.f));
	}
}
