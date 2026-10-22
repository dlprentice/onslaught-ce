// PCSoundManager.h: the PC's sound device (reconstructed from the retail object, pcsoundmanager.cpp 005167b0..005180a8;
// Stuart's release has an older DirectSound version of this file, whose names and member order it keeps).
// Don't include this file directly - use "SoundManager.h"

#ifndef PCSOUNDMANAGER_H
#define PCSOUNDMANAGER_H

class CMEMBUFFER;

// DirectSound's interfaces, declared here so the headers that reach this one need not include dsound.h.
struct IDirectSound8;
struct IDirectSoundBuffer;
struct IDirectSound3DBuffer;
struct IDirectSound3DListener;

// The number of sound buffers we want available
#define MAX_SOUND_BUFFERS	64			// retail: DeviceShutdown and DeviceReset walk 64 (005171f9, 00517268)

#include "WavRead.h"

//---------------------------------------------------------------------------

class	CPCSample : public CSample
{
public:
	CPCSample()
	{
		mWavData = NULL;
		mDSBuffer = NULL;				// PC (LoadSampleFromBuffer 00517316)
	}

	virtual ~CPCSample();				// 005168d0

	BYTE	*mWavData;					// +0x78
	SINT	mWavSize;					// +0x7c

	IDirectSoundBuffer		*mDSBuffer;	// +0x80
};

//---------------------------------------------------------------------------

class CPCSoundManager : public CSoundManager
{
public:
					CPCSoundManager()
					{
						mWaveSoundRead=NULL;
						mNumChannels=12;		// retail (SOUND's initializer 005168a0)
						m3DSoundMethod=0;
					}

			bool	DeviceInit();								// 005169b0
			void	DeviceShutdown();							// 005171e0
			void	DeviceReset();								// 00517260

			void	UnloadSample(CSample *s) {};

			CSample *LoadNewSample(char *filename,BOOL music);	// 00517290: the PC loads nothing loose (returns NULL)
			CSample *LoadSampleFromBuffer(CMEMBUFFER *mb,BOOL music,CSample *replace);	// 005172a0
			CSample *CreateSampleFromData(void *data,int size,BOOL music,CSample *replace);	// 005176d0 (name open)

			void	PlaySound(CSoundEvent *event);				// 00517790
			void	StopSound(CSoundEvent *event,BOOL blockuntilstopped=FALSE);	// 005179b0
			void	PauseSound(CSoundEvent *event);				// 00517990
			void	UnPauseSound(CSoundEvent *event);			// 00517960
			void	UpdateSound(CSoundEvent *event, BOOL first_update = FALSE);	// 00517ae0
			void	UpdateGlobals();							// 00517a20
			void	UpdatesDone();								// 00517c40
			void	DevicePauseAll() {};
			void	DeviceUnpauseAll() {};

			float	GetSampleLength(CSample *sample);			// 00517c60

			int		GetAvailableChannels() { return mNumChannels; }	// inline (CSoundManager::SortEventList 004e1090)
			int		FindFreeChannel();							// 00517cb0

			void	LoadSampleBank(bool reload);				// 00517d00 (name open): the language's compressed samples
			void	Reinitialise();								// 00517f10: after the options change the device or format
	static	int		GetNumDevices();							// 00516980
	static	WCHAR	*GetDeviceName(int n);						// 00516990 (cdecl)
	static	bool	GetPitchVarianceEnabled();					// 00517ad0 (name open): gHardwareSound on a device with caps

	// the bank's decoding (cdecl; names open)
	static	void	*CreateSoundBuffer(IDirectSoundBuffer **buffer, DWORD size);	// 00517440: locked for filling
	static	void	ConvertAudioFormat(void *dest, void *source, int size);			// 00517600
	static	void	DecodeADPCM(void *indata, void *outdata, int len, struct SADPCMState *state);	// 00517fa0

	int						m3DSoundMethod;				// +0xbc (00896a44): the 3D sound method in use (C3DSoundMethod 004cf29a)

#if defined(DXFRONTENDVIDEO_CPP) || defined(PCMUSICSTREAM_CPP)	// the video's and the music stream's view only, so the other objects' declaration counts stay put
	IDirectSound8			*GetDirectSound() { return mDS; }	// Bink plays through it (CBinkOpenThread::Run 00541140)
#endif

protected:
	IDirectSound8			*mDS;						// +0xc0
	IDirectSoundBuffer		*mDSBuffer[MAX_SOUND_BUFFERS];		// +0xc4
	IDirectSound3DBuffer	*mDS3DBuffer[MAX_SOUND_BUFFERS];	// +0x1c4
	IDirectSound3DListener	*mListener;					// +0x2c4 (DeviceInit 00516e3a; Stuart's mPBuffer is a local)

	CWaveSoundRead			*mWaveSoundRead;			// +0x2c8

public:
	int						mNumChannels;				// +0x2cc (00896c54): the voices in use, at most MAX_SOUND_BUFFERS
};

#endif
