#include	"Common.h"	// FUNCTION: 0x005167b0 init:ZERO_FVECTOR

#if TARGET == PC

#include	"Game.h"	// FUNCTION: 0x005167d0 init:ID_FMATRIX
#include	<dsound.h>
#include	"SoundManager.h"
#include	"Vfw.h"
#include	"DX.h"
#include	"Profile.h"
#include	"text.h"
#include	"CLIParams.h"
#include	"console.h"
#include	"music.h"
#include	"DebugLog.h"
#include	"ltshell.h"
#include	"MessageBox.h"
#include	"DXMemBuffer.h"
#include	"FrontEndText.h"

// pcsoundmanager.cpp: Stuart's DirectSound device, as the retail PC build grew it (005167b0..005180a8). His release
// has an older version of this file; the functions keep his names and order where they survive, and the rest is
// rebuilt from the retail object. The allocations' line numbers are retail's (#line), which is all it proves.
static	bool			sPitchVarianceEnabled;				// 00896c58; defined before SOUND, sDevices before sContinuousRate, sNumDevices = 0: retail's .bss order (placement evidence)
// FUNCTION: 0x005168a0 init:SOUND
CPCSoundManager	SOUND;

// The sound devices DirectSoundEnumerate found (0x78 bytes each).
struct SSoundDevice
{
	GUID	*mGuidPtr;					// +0x00: &mGuid, or NULL for the primary device
	GUID	mGuid;						// +0x04
	WCHAR	mName[50];					// +0x14 (GetDeviceName 00516990)
};

#define	MAX_SOUND_DEVICES	10

static	SSoundDevice	sDevices[MAX_SOUND_DEVICES];		// 008964d8
static	bool			sContinuousRate;					// 008964d0: DSCAPS_CONTINUOUSRATE

static	int				sNumDevices = 0;						// 00896ca0

// The language's compressed sample bank (CSoundManager::LanguageChanged builds its name).
char	*gSampleBankFormat = "%s\\data\\sounds\\sounds_%s_pc.xap";	// 0063e2a8 (name open)
float	gRolloffFactor = 0.7f;										// 0063e2ac (name open)

//---------------------------------------------------------------------------

#define	SM_IN_STEREO			TRUE

//---------------------------------------------------------------------------

#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=NULL; } }

// The output rate the options chose.
static inline DWORD GetSampleRate()
{
	DWORD	rate;
	switch (CLIPARAMS.mSoundRate)
	{
	case 0:
		rate = 44100;
		break;
	case 1:
		rate = 22050;
		break;
	default:
		rate = 11025;
		break;
	}
	return rate;
}

// 16 bits at 44100 and 22050, 8 at 11025.
static inline int GetSampleBits()
{
	if ((CLIPARAMS.mSoundRate < 0) || (CLIPARAMS.mSoundRate > 1))
		return 8;
	return 16;
}

//---------------------------------------------------------------------------
// FUNCTION: 0x00516960 ??_GCPCSample@@UAEPAXI@Z
CPCSample::~CPCSample()	// FUNCTION: 0x005168d0
{
	SOUND.KillAllInstancesOfSample(this);
	SAFE_DELETE(mWavData);
	SAFE_RELEASE(mDSBuffer);
}

//---------------------------------------------------------------------------

int		CPCSoundManager::GetNumDevices()	// FUNCTION: 0x00516980
{
	return sNumDevices;
}

WCHAR	*CPCSoundManager::GetDeviceName(int n)	// FUNCTION: 0x00516990
{
	return sDevices[n].mName;
}

static BOOL CALLBACK EnumSoundDevices(LPGUID guid, LPCSTR desc, LPCSTR module, LPVOID context);

//---------------------------------------------------------------------------
// Initialisation code
//---------------------------------------------------------------------------

bool CPCSoundManager::DeviceInit()	// FUNCTION: 0x005169b0
{
	mNumChannels = 12;

	if (mWaveSoundRead)
		return TRUE;

#line 229
	mWaveSoundRead=new (MEMTYPE_SOUND) CWaveSoundRead();

	HWND handleToWindow=LT.GetHWnd();

	mDS = NULL;

	for (int i = 0;i < MAX_SOUND_BUFFERS; i++)
	{
		mDSBuffer[i] = NULL;
	}

	sNumDevices = 0;
	SETUP_HISTORY.AddMessage("Enumerating sound devices:");
	DirectSoundEnumerate(EnumSoundDevices, NULL);
	SETUP_HISTORY.AddMessage("");

	if (!sNumDevices)
	{
		SAFE_DELETE(mWaveSoundRead);
		return FALSE;
	}

	if (CLIPARAMS.mSoundDevice >= sNumDevices)
		CLIPARAMS.mSoundDevice = 0;

	SETUP_HISTORY.AddMessage("Using sound device %d", CLIPARAMS.mSoundDevice);

    // Create IDirectSound using the chosen device
    if( FAILED(DirectSoundCreate8(sDevices[CLIPARAMS.mSoundDevice].mGuidPtr, &mDS, NULL ) ) )
	{
		SETUP_HISTORY.AddMessage("Couldn't create sound device");
		SAFE_DELETE(mWaveSoundRead);
		return FALSE;
	}

    // Set co-operative level
    if( FAILED(mDS->SetCooperativeLevel( handleToWindow, DSSCL_PRIORITY ) ) )
	{
		SETUP_HISTORY.AddMessage("Couldn't set sound device coop level");
		SAFE_RELEASE(mDS);
		SAFE_DELETE(mWaveSoundRead);
		return FALSE;
	}

	// The device's capabilities choose the 3D method and the voices
	DSCAPS	caps;
	ZeroMemory(&caps, sizeof(caps));
	caps.dwSize = sizeof(caps);
	sPitchVarianceEnabled = false;

	if (SUCCEEDED(mDS->GetCaps(&caps)))
	{
		SETUP_HISTORY.AddMessage("Sound caps:");

		char	text[220];				// retail frame (0x17c): 217 to 220 bytes, enough for the caps' hex
		char	*p = text;
		for (DWORD n = 0; n < sizeof(caps); n++)
		{
			sprintf(p, "%02x", ((BYTE *)&caps)[n]);
			p += 2;
			if ((n & 3) == 3)
				*p++ = ' ';
		}
		*p = 0;
		SETUP_HISTORY.AddMessage("%s", text);

		if (caps.dwFlags & DSCAPS_CONTINUOUSRATE)
		{
			sContinuousRate = true;
			SETUP_HISTORY.AddMessage("Chosen device has DSCAPS_CONTINUOUSRATE");
		}
		else
		{
			sContinuousRate = false;
			SETUP_HISTORY.AddMessage("Chosen device doesn't have DSCAPS_CONTINUOUSRATE");
		}

		if (CLIPARAMS.m3DSoundMethod == -1)
		{
			if (caps.dwFreeHw3DStaticBuffers > 30)	// STAND-IN (layout; review F 2026-10-15): identical arms; one store loses 005169b0
				m3DSoundMethod = 2;
			else
				m3DSoundMethod = 2;
		}
		else
			m3DSoundMethod = CLIPARAMS.m3DSoundMethod;

		SETUP_HISTORY.AddMessage("Using 3D sound method %d", m3DSoundMethod);

		if (CLIPARAMS.mSoundBuffers == -1)
		{
			if (caps.dwFreeHw3DStaticBuffers > (DWORD)mNumChannels)
				mNumChannels = caps.dwFreeHw3DStaticBuffers;
			if ((m3DSoundMethod == 2) && (caps.dwFreeHwMixingStaticBuffers > (DWORD)mNumChannels))
				mNumChannels = caps.dwFreeHwMixingStaticBuffers;
			if (mNumChannels > MAX_SOUND_BUFFERS)
				mNumChannels = MAX_SOUND_BUFFERS;
		}
		else
			mNumChannels = CLIPARAMS.mSoundBuffers;

		SETUP_HISTORY.AddMessage("Sound using %d voices", mNumChannels);

		sPitchVarianceEnabled = (CLIPARAMS.mHWSound != 0);
	}

    // Create the primary buffer

    DSBUFFERDESC        dsbd;
    ZeroMemory( &dsbd, sizeof(DSBUFFERDESC) );
    dsbd.dwSize        = sizeof(DSBUFFERDESC);
    dsbd.dwFlags       = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
    dsbd.dwBufferBytes = 0;
    dsbd.lpwfxFormat   = NULL;
	LPDIRECTSOUNDBUFFER	pbuffer = NULL;

    if (FAILED(mDS->CreateSoundBuffer(&dsbd, &pbuffer, NULL)))
	{
		SETUP_HISTORY.AddMessage("Couldn't create primary sound buffer");
		SAFE_RELEASE(mDS);
		SAFE_DELETE(mWaveSoundRead);
		return FALSE;
	}

    // Set the primary buffer's format from the options

    WAVEFORMATEX wfx;
    ZeroMemory(&wfx,sizeof(WAVEFORMATEX));
    wfx.wFormatTag=WAVE_FORMAT_PCM;
    wfx.nChannels       = 2;
    wfx.nSamplesPerSec  = GetSampleRate();
    wfx.wBitsPerSample  = GetSampleBits();
    wfx.nBlockAlign     = wfx.wBitsPerSample / 8 * wfx.nChannels;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    if (FAILED(pbuffer->SetFormat(&wfx)))
	{
		SETUP_HISTORY.AddMessage("Couldn't set primary buffer format");
		SAFE_RELEASE(pbuffer);
		SAFE_RELEASE(mDS);
		SAFE_DELETE(mWaveSoundRead);
		return FALSE;
	}

	// Get our listener pointer

	if (FAILED(pbuffer->QueryInterface(IID_IDirectSound3DListener,(void **) &mListener)))
	{
		SETUP_HISTORY.AddMessage("Couldn't get listener");
		SAFE_RELEASE(pbuffer);
		SAFE_RELEASE(mDS);
		SAFE_DELETE(mWaveSoundRead);
		return FALSE;
	}

	if (pbuffer)
		pbuffer->Release();

	return TRUE;
}

//---------------------------------------------------------------------------
// DirectSoundEnumerate's callback: logs each device and keeps its GUID and name
//---------------------------------------------------------------------------

static BOOL CALLBACK EnumSoundDevices(LPGUID guid, LPCSTR desc, LPCSTR module, LPVOID context)	// FUNCTION: 0x00516ed0
{
	if (guid)
		SETUP_HISTORY.AddMessage("Sound device has GUID");
	else
		SETUP_HISTORY.AddMessage("(No guid)");

	if (!desc)
		desc = "";
	if (!module)
		module = "";

	SETUP_HISTORY.AddMessage("Desc: %s, Module %s", desc, module);

	LPDIRECTSOUND8	ds;
	if (FAILED(DirectSoundCreate8(guid, &ds, NULL)))
	{
		SETUP_HISTORY.AddMessage("Couldn't create a sound device");
		return TRUE;
	}

	if (FAILED(ds->SetCooperativeLevel(LT.GetHWnd(), DSSCL_PRIORITY)))
	{
		SETUP_HISTORY.AddMessage("Couldn't set coop level");
		ds->Release();
		return TRUE;
	}

	DSCAPS	caps;
	ZeroMemory(&caps, sizeof(caps));
	caps.dwSize = sizeof(caps);

	if (SUCCEEDED(ds->GetCaps(&caps)))
	{
		if (caps.dwFlags & DSCAPS_CERTIFIED)
			SETUP_HISTORY.AddMessage("Sound device caps flag: DSCAPS_CERTIFIED");
		if (caps.dwFlags & DSCAPS_CONTINUOUSRATE)
			SETUP_HISTORY.AddMessage("Sound device caps flag: DSCAPS_CONTINUOUSRATE");
		if (caps.dwFlags & DSCAPS_EMULDRIVER)
			SETUP_HISTORY.AddMessage("Sound device caps flag: DSCAPS_EMULDRIVER");
		if (caps.dwFlags & DSCAPS_PRIMARY16BIT)
			SETUP_HISTORY.AddMessage("Sound device caps flag: DSCAPS_PRIMARY16BIT");
		if (caps.dwFlags & DSCAPS_PRIMARY8BIT)
			SETUP_HISTORY.AddMessage("Sound device caps flag: DSCAPS_PRIMARY8BIT");
		if (caps.dwFlags & DSCAPS_PRIMARYMONO)
			SETUP_HISTORY.AddMessage("Sound device caps flag: DSCAPS_PRIMARYMONO");
		if (caps.dwFlags & DSCAPS_PRIMARYSTEREO)
			SETUP_HISTORY.AddMessage("Sound device caps flag: DSCAPS_PRIMARYSTEREO");
		if (caps.dwFlags & DSCAPS_SECONDARY16BIT)
			SETUP_HISTORY.AddMessage("Sound device caps flag: DSCAPS_SECONDARY16BIT");
		if (caps.dwFlags & DSCAPS_SECONDARY8BIT)
			SETUP_HISTORY.AddMessage("Sound device caps flag: DSCAPS_SECONDARY8BIT");
		if (caps.dwFlags & DSCAPS_SECONDARYMONO)
			SETUP_HISTORY.AddMessage("Sound device caps flag: DSCAPS_SECONDARYMONO");
		if (caps.dwFlags & DSCAPS_SECONDARYSTEREO)
			SETUP_HISTORY.AddMessage("Sound device caps flag: DSCAPS_SECONDARYSTEREO");

		SETUP_HISTORY.AddMessage("Free 3D Buffers: %d, free static buffers: %d", caps.dwFreeHw3DStaticBuffers, caps.dwFreeHwMixingStaticBuffers);
	}

	ds->Release();

	if (sNumDevices >= sizeof(sDevices) / sizeof(sDevices[0]))
	{
		SETUP_HISTORY.AddMessage("Too many sound devices.");
		return FALSE;
	}

	SSoundDevice	*d = &sDevices[sNumDevices];
	if (guid)
	{
		d->mGuid = *guid;
		d->mGuidPtr = &d->mGuid;
	}
	else
		d->mGuidPtr = NULL;

	if ((desc) && (desc[0]))
		wcsncpy(d->mName, ToWCHAR((char *)desc), 49);
	else
	{
		wcsncpy(d->mName, Localization_GetStringById(8), 45);
		wcscat(d->mName, L":  ");
		d->mName[wcslen(d->mName) - 1] = L'1' + sNumDevices;
	}

	sNumDevices++;

	return TRUE;
}

//---------------------------------------------------------------------------
// This is called when the system shuts down
//---------------------------------------------------------------------------

void CPCSoundManager::DeviceShutdown()	// FUNCTION: 0x005171e0
{
	TRACE("Shutting down sound device\n");

	for(int i = 0; i < MAX_SOUND_BUFFERS; i++)
	{
		SAFE_RELEASE(mDS3DBuffer[i]);
		SAFE_RELEASE(mDSBuffer[i]);
	}

	SAFE_RELEASE(mDS);

	SAFE_DELETE(mWaveSoundRead);
}

//---------------------------------------------------------------------------
// This function should shut down any playing sounds
//---------------------------------------------------------------------------

void CPCSoundManager::DeviceReset()	// FUNCTION: 0x00517260
{
	for(int i=0;i<MAX_SOUND_BUFFERS;i++)
	{
		if (mDSBuffer[i])
		{
			mDSBuffer[i]->Stop();
		}
	}
}

//---------------------------------------------------------------------------
// Loads a new sample: the PC's samples come only from the language's bank
//---------------------------------------------------------------------------

CSample	*CPCSoundManager::LoadNewSample(char *fname,BOOL music)	// FUNCTION: 0x00517290
{
	return NULL;
}

//---------------------------------------------------------------------------
// The IMA ADPCM decoder's state and tables (the reference decoder's)
//---------------------------------------------------------------------------

struct SADPCMState
{
	SWORD	valprev;
	char	index;
};


//---------------------------------------------------------------------------
// A sample from the bank: its size, then its 4-bit ADPCM data
//---------------------------------------------------------------------------

CSample *CPCSoundManager::LoadSampleFromBuffer(CMEMBUFFER *mb,BOOL music,CSample *replace)	// FUNCTION: 0x005172a0
{
	DWORD	size;
	mb->Read(&size, 4);

	CPCSample	*s = (CPCSample *)replace;
	if (s)
	{
		delete s->mWavData;
		s->mWavData = NULL;
		SAFE_RELEASE(s->mDSBuffer);
	}
	else
	{
#line 679
		s = new( MEMTYPE_SOUND_SAMPLE ) CPCSample;
	}

	DWORD	adpcmsize = (size + 3) / 4;		// 4-bit samples
#line 684
	BYTE	*adpcm = new( MEMTYPE_SOUND_SAMPLE ) BYTE [adpcmsize];
	if (!mb->Read(adpcm, adpcmsize))
	{
		delete adpcm;
		delete s;
		return NULL;
	}

	s->mWavSize = size;

	void	*data = CreateSoundBuffer(&s->mDSBuffer, size);
	if (data)
	{
		SADPCMState	state;	// STAND-IN (order; review F 2026-10-15): zeroed through a DWORD store (both arms); the two field stores lose 005172a0
		if (CLIPARAMS.mSoundRate == 0)
		{
			*(DWORD *)&state = 0;
			DecodeADPCM(adpcm, data, (DWORD)s->mWavSize / 2, &state);
		}
		else
		{
#line 714
			BYTE	*pcm = new( MEMTYPE_SCRATCHPAD ) BYTE [s->mWavSize];
			*(DWORD *)&state = 0;
			DecodeADPCM(adpcm, pcm, (DWORD)s->mWavSize / 2, &state);
			ConvertAudioFormat(data, pcm, s->mWavSize);
			delete pcm;
		}

		s->mDSBuffer->Unlock(data, s->mWavSize, NULL, 0);
	}

	delete adpcm;
	return s;
}

//---------------------------------------------------------------------------
// A static buffer in the options' format, locked for filling
//---------------------------------------------------------------------------

void *CPCSoundManager::CreateSoundBuffer(IDirectSoundBuffer **buffer, DWORD size)	// FUNCTION: 0x00517440
{
	PCMWAVEFORMAT	pcmwf;				// retail: 16 bytes zeroed (00517443)
	memset(&pcmwf, 0, sizeof(PCMWAVEFORMAT));
	pcmwf.wf.wFormatTag = WAVE_FORMAT_PCM;
	pcmwf.wf.nChannels = 1;
	pcmwf.wf.nSamplesPerSec = GetSampleRate();
	pcmwf.wBitsPerSample = GetSampleBits();
	pcmwf.wf.nBlockAlign = pcmwf.wBitsPerSample / 8;
	pcmwf.wf.nAvgBytesPerSec = pcmwf.wf.nSamplesPerSec * pcmwf.wf.nBlockAlign;

	DSBUFFERDESC	dsbdesc;
	ZeroMemory(&dsbdesc, sizeof(dsbdesc));
	dsbdesc.dwSize = sizeof(DSBUFFERDESC);
	dsbdesc.dwFlags = CLIPARAMS.mHWSound ? (DSBCAPS_LOCDEFER | DSBCAPS_MUTE3DATMAXDISTANCE | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRL3D)
									 : (DSBCAPS_MUTE3DATMAXDISTANCE | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRL3D | DSBCAPS_LOCSOFTWARE);

	GUID	alg;
	switch (SOUND.m3DSoundMethod)
	{
	case 0:
		alg = DS3DALG_HRTF_FULL;
		break;
	case 1:
		alg = DS3DALG_HRTF_LIGHT;
		break;
	case 2:
		alg = DS3DALG_NO_VIRTUALIZATION;
		break;
	default:
		alg = DS3DALG_HRTF_FULL;
		break;
	}
	dsbdesc.guid3DAlgorithm = alg;

	switch (CLIPARAMS.mSoundRate)
	{
	case 1:
		dsbdesc.dwBufferBytes = (size + 1) / 2;
		break;
	case 2:
		dsbdesc.dwBufferBytes = (size + 7) / 8;
		break;
	default:
		dsbdesc.dwBufferBytes = size;
		break;
	}
	dsbdesc.lpwfxFormat = (LPWAVEFORMATEX)&pcmwf;

	if (FAILED(SOUND.mDS->CreateSoundBuffer(&dsbdesc, buffer, NULL)))
		return NULL;

	void	*data = NULL;
	if (FAILED((*buffer)->Lock(0, 0, &data, &size, NULL, NULL, DSBLOCK_ENTIREBUFFER)))
	{
		SAFE_RELEASE(*buffer);
		return NULL;
	}

	return data;
}

//---------------------------------------------------------------------------
// 16-bit mono at 44100 into the options' rate and depth
//---------------------------------------------------------------------------

void CPCSoundManager::ConvertAudioFormat(void *dest, void *source, int size)	// FUNCTION: 0x00517600
{
	int		i;

	switch (CLIPARAMS.mSoundRate)
	{
	case 0:
		memcpy(dest, source, size);
		break;

	case 1:
		for (i = 0; i < size / 4; i++)
			((SWORD *)dest)[i] = (((SWORD *)source)[i * 2] + ((SWORD *)source)[i * 2 + 1]) >> 1;
		if (size & 3)
			((SWORD *)dest)[i] = 0;
		break;

	default:
	{
		SWORD	*s = (SWORD *)source;
		for (i = 0; i < size / 8; i++, s += 4)
			((BYTE *)dest)[i] = ((s[0] + s[1] + s[2] + s[3]) >> 10) + 0x80;
		break;
	}
	}
}

//---------------------------------------------------------------------------
// A sample from 16-bit data in memory
//---------------------------------------------------------------------------

CSample *CPCSoundManager::CreateSampleFromData(void *data,int size,BOOL music,CSample *replace)	// FUNCTION: 0x005176d0
{
	CPCSample	*s = (CPCSample *)replace;
	if (s)
	{
		delete s->mWavData;
		s->mWavData = NULL;
		SAFE_RELEASE(s->mDSBuffer);
	}
	else
	{
#line 753
		s = new( MEMTYPE_SOUND_SAMPLE ) CPCSample;
	}

	s->mWavSize = size;

	void	*p = CreateSoundBuffer(&s->mDSBuffer, size);
	if (p)
	{
		ConvertAudioFormat(p, data, s->mWavSize);
		s->mDSBuffer->Unlock(p, s->mWavSize, NULL, 0);
	}

	return s;
}

//---------------------------------------------------------------------------
// Play a sound
//---------------------------------------------------------------------------

// FUNCTION: 0x00517790 ?PlaySoundA@CPCSoundManager@@QAEXPAVCSoundEvent@@@Z
// (windows.h's mmsystem renames PlaySound to PlaySoundA here and in every caller, as in retail)
void CPCSoundManager::PlaySound(CSoundEvent *event)
{
	PROFILE_FN(PlaySound);

	SINT 	vol = DSBVOLUME_MAX;//Fade(GetVolumeForThing(event->mOwner.ToRead()), event);

	CPCSample *s	= (CPCSample *) event->mSample;
	int		channel = event->mChannel;

	int		fromsample = ((int) ((event->mStartPoint+event->mTime) * GetSampleRate())) * 2;
	int		tosample;
	if (event->mEndPoint!=-1.0f)
		tosample = ((int) (GetSampleRate() * event->mEndPoint)) * 2;
	else
		tosample = s->mWavSize;

	if (fromsample>=tosample) // Nothing to do!
		return;

	if ((s->mDSBuffer != NULL) && (s->mWavSize))
	{
		if (mDSBuffer[channel])
		{
			SAFE_RELEASE(mDSBuffer[channel]);
			SAFE_RELEASE(mDS3DBuffer[channel]);
		}

		// Duplicate the buffer into this sound channel

		if (SUCCEEDED(mDS->DuplicateSoundBuffer(s->mDSBuffer,&mDSBuffer[channel])))
		{
			mDSBuffer[channel]->SetVolume(vol);

			// Get the DS3D interface for this channel

			mDSBuffer[channel]->QueryInterface(IID_IDirectSound3DBuffer,(void**) &mDS3DBuffer[channel]);

			if (mDS3DBuffer[channel])
				mDS3DBuffer[channel]->SetMaxDistance(50.0f, DS3D_IMMEDIATE);
			if (mDS3DBuffer[channel])
				mDS3DBuffer[channel]->SetMinDistance(3.0f, DS3D_IMMEDIATE);

			//Now just play the buffer, it gets passed into the the primary buffer and mixed atuomatically.

			DWORD flags=0;

			if (event->mLooping)
				flags |= DSBPLAY_LOOPING;

			UpdateSoundPosition(event, FALSE);

			UpdateSound(event, TRUE);

			mDSBuffer[channel]->SetCurrentPosition(fromsample);
			mDSBuffer[channel]->Play(0,0,flags);
			event->mPlaying=true;
		}
		else
			SAFE_RELEASE(mDSBuffer[channel]);
	}
}


//---------------------------------------------------------------------------
// UnPause
//---------------------------------------------------------------------------
void CPCSoundManager::UnPauseSound(CSoundEvent *event)	// FUNCTION: 0x00517960
{
	int		channel = event->mChannel;
	if	(mDSBuffer[channel])
	{
		DWORD flags=0;

		if (event->mLooping)
			flags |= DSBPLAY_LOOPING;
		mDSBuffer[channel]->Play(0,0,flags);
	}
}


//---------------------------------------------------------------------------
// Pause
//---------------------------------------------------------------------------
void CPCSoundManager::PauseSound(CSoundEvent *event)	// FUNCTION: 0x00517990
{
	int		channel = event->mChannel;
	if	(mDSBuffer[channel])
	{
		mDSBuffer[event->mChannel]->Stop();
	}
}


//---------------------------------------------------------------------------
// Stop a sound
//---------------------------------------------------------------------------

void CPCSoundManager::StopSound(CSoundEvent *event,BOOL blockuntilstopped)	// FUNCTION: 0x005179b0
{
	if (mDSBuffer[event->mChannel])
	{
		mDSBuffer[event->mChannel]->Stop();
		SAFE_RELEASE(mDSBuffer[event->mChannel]);
		SAFE_RELEASE(mDS3DBuffer[event->mChannel]);
	}
}

//---------------------------------------------------------------------------
// Update global data (called at start of update process)
//---------------------------------------------------------------------------

void CPCSoundManager::UpdateGlobals()	// FUNCTION: 0x00517a20
{
	if (mInitialised== FALSE) return ;
	DS3DLISTENER l;
	l.dwSize=sizeof(l);

	l.vPosition.x=0;
	l.vPosition.y=0;
	l.vPosition.z=0;
	l.vOrientFront.x=0;
	l.vOrientFront.y=1.0f;
	l.vOrientFront.z=0;
	l.vOrientTop.x=0;
	l.vOrientTop.y=0;
	l.vOrientTop.z=-1.0f;
	l.vVelocity.x=0;
	l.vVelocity.y=0;
	l.vVelocity.z=0;
	l.flRolloffFactor=gRolloffFactor;
	l.flDistanceFactor=1.0f;
	l.flDopplerFactor=1.0f;

	if (mListener) mListener->SetAllParameters(&l,DS3D_DEFERRED);
}

//---------------------------------------------------------------------------

bool	CPCSoundManager::GetPitchVarianceEnabled()	// FUNCTION: 0x00517ad0
{
	return sPitchVarianceEnabled;
}

//---------------------------------------------------------------------------
// Update a sound's information
//---------------------------------------------------------------------------

void CPCSoundManager::UpdateSound(CSoundEvent *event, BOOL first_time)	// FUNCTION: 0x00517ae0
{
	if (mDSBuffer[event->mChannel])
	{
		if (mDS3DBuffer[event->mChannel])
		{
			DWORD	apply = first_time ? DS3D_IMMEDIATE : DS3D_DEFERRED;
			mDS3DBuffer[event->mChannel]->SetPosition(event->mPos.X, event->mPos.Y, event->mPos.Z, apply);
			mDS3DBuffer[event->mChannel]->SetVelocity(event->mVelocity.X*20.0f, event->mVelocity.Y*20.0f, event->mVelocity.Z*20.0f, apply);
			mDS3DBuffer[event->mChannel]->SetMaxDistance(50.0f, apply);
		}

		// Ensure we actually fall off to silence

		int vol=event->mCurrentVolume;	// PC: the unattenuated volume (00517b79); DirectSound attenuates in 3D

		if (vol<-4000)
		{
			vol=vol+((vol+4000)*2);
		}

		float	pitch = event->mPitchMultiplier;

		mDSBuffer[event->mChannel]->SetVolume(vol);

		if (sContinuousRate)
			mDSBuffer[event->mChannel]->SetFrequency((DWORD) (GetSampleRate() * pitch));

		// Stop the event if the sample has finished

		DWORD status = 0;

		if ((!first_time) && (!event->mPaused))
		{
			mDSBuffer[event->mChannel]->GetStatus(&status);
			if (!(status & DSBSTATUS_PLAYING))
			{
				StopSoundEvent(event);
			}
		}
	}
}

//---------------------------------------------------------------------------
// Called after all the UpdateSound() calls have been made for a frame
//---------------------------------------------------------------------------

void CPCSoundManager::UpdatesDone()	// FUNCTION: 0x00517c40
{
	if ((mInitialised) && (mListener))
		mListener->CommitDeferredSettings();
}

//---------------------------------------------------------------------------
// Returns the length of the sample in seconds
//---------------------------------------------------------------------------

float CPCSoundManager::GetSampleLength(CSample *sample)	// FUNCTION: 0x00517c60
{
	return(((float) ((CPCSample *) sample)->mWavSize)/(GetSampleRate()*2.0f));
}

//---------------------------------------------------------------------------
// Returns the number of a free channel, or -1 if all are occupied
//---------------------------------------------------------------------------

int CPCSoundManager::FindFreeChannel()	// FUNCTION: 0x00517cb0
{
	for (int i=0;i<mNumChannels;i++)
	{
		CSoundEvent *se=SOUND.GetFirstSoundEvent();

		bool ok=true;

		while (se)
		{
			if (se->mChannel==i)
				ok=false;
			se=se->mNextEvent;
		}

		if (ok & (!mDSBuffer[i]))	// STAND-IN (layout; review F 2026-10-15): bitwise and; `ok && !mDSBuffer[i]` loses 00517cb0
			return(i);
	}

	return(-1);
}

//---------------------------------------------------------------------------
// The language's compressed samples (the XAP file named by mLanguageSampleBank)
//---------------------------------------------------------------------------

void CPCSoundManager::LoadSampleBank(bool reload)	// FUNCTION: 0x00517d00
{
	if (CLIPARAMS.mBuildResources)
	{
		TRACE("Not loading XAP during resource build\n");
		return;
	}

	if (!CLIPARAMS.mUseSampleBank)
	{
		TRACE("Not loading compressed sounds.\n");
		return;
	}

	CMEMBUFFER	mb;
	if (!mb.InitFromFile(mLanguageSampleBank, MEMTYPE_MEMBUFFER, TRUE, 0))
	{
		TRACE("XAP file ");
		TRACE(mLanguageSampleBank);
		TRACE(" not found\n");
		return;
	}

	TRACE("Loading PC XAP file\n");

	int		version, count;
	char	text[256];
	char	name[64];

	mb.Read(&version, 4);
	mb.Read(&count, 4);

	sprintf(text, "Caching %d samples from PC XAP\n", count);
	TRACE(text);

	for (int i = 0; i < count; i++)
	{
		mb.Read(&version, 4);
		mb.Read(name, 64);

		sprintf(text, "Caching sample %s\n", name);
		TRACE(text);

		if (!CreateSample(name, FALSE, &mb, reload))
			TRACE("Failed to create sample\n");

		CONSOLE.SetLoadingFraction(float(i) / (DWORD)count);
	}

	mb.Close();
}

//---------------------------------------------------------------------------
// After the options change the device or the format: every sample again
//---------------------------------------------------------------------------

void CPCSoundManager::Reinitialise()	// FUNCTION: 0x00517f10
{
	TRACE("Killing all DSBuffers for the samples\n");

	DeleteAllSamples();
	MUSIC.Shutdown();
	if (GAME.GetMessageBox())
		GAME.GetMessageBox()->StopCurrentPlayingSound();

	DeviceShutdown();

	if (DeviceInit())
	{
		if (CLIPARAMS.mMusic)
			MUSIC.Initialise();

		LoadSampleBank(TRUE);

		if (!CLIPARAMS.mUseSampleBank)
		{
			for (CSample *s = SOUND.GetFirstSample(); s; s = s->mNextSample)
				SOUND.GetSample(s->mName, s->mMusic, TRUE);
		}
	}
}

//---------------------------------------------------------------------------
// The IMA ADPCM reference decoder
//---------------------------------------------------------------------------

// The tables are in adpcm.c, a C object of their own in retail (0063e85c, 0063e89c).
extern "C" int indexTable[16];
extern "C" int stepsizeTable[89];

void CPCSoundManager::DecodeADPCM(void *indata, void *outdata, int len, SADPCMState *state)	// FUNCTION: 0x00517fa0
{
    signed char *inp;		/* Input buffer pointer */
    short *outp;		/* output buffer pointer */
    int sign;			/* Current adpcm sign bit */
    int delta;			/* Current adpcm output value */
    int step;			/* Stepsize */
    int valpred;		/* Predicted value */
    int vpdiff;			/* Current change to valpred */
    int index;			/* Current step change index */
    int inputbuffer;		/* place to keep next 4-bit value */
    int bufferstep;		/* toggle between inputbuffer/input */

    outp = (short *)outdata;
    inp = (signed char *)indata;

    valpred = state->valprev;
    index = state->index;
    step = stepsizeTable[index];

    bufferstep = 0;

    for ( ; len > 0 ; len-- ) {

	/* Step 1 - get the delta value */
	if ( bufferstep ) {
	    delta = inputbuffer & 0xf;
	} else {
	    inputbuffer = *inp++;
	    delta = (inputbuffer >> 4) & 0xf;
	}
	bufferstep = !bufferstep;

	/* Step 2 - Find new index value (for later) */
	index += indexTable[delta];
	if ( index < 0 ) index = 0;
	if ( index > 88 ) index = 88;

	/* Step 3 - Separate sign and magnitude */
	sign = delta & 8;
	delta = delta & 7;

	/* Step 4 - Compute difference and new predicted value */
	vpdiff = step >> 3;
	if ( delta & 4 ) vpdiff += step;
	if ( delta & 2 ) vpdiff += step>>1;
	if ( delta & 1 ) vpdiff += step>>2;

	if ( sign )
	  valpred -= vpdiff;
	else
	  valpred += vpdiff;

	/* Step 5 - clamp output value */
	if ( valpred > 32767 )
	  valpred = 32767;
	else if ( valpred < -32768 )
	  valpred = -32768;

	/* Step 6 - Update step value */
	step = stepsizeTable[index];

	/* Step 7 - Output value */
	*outp++ = valpred;
    }

    state->valprev = valpred;
    state->index = index;
}

#endif
