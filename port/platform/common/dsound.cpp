// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/dsound.cpp: the DirectSound 8 objects the game uses (pcsoundmanager.cpp, PCMusicStream.cpp), on
// both targets. This first form is silent: buffers hold their data, play cursors advance in real time at the buffer's
// byte rate and position notifications fire from a timer, so the game's sound and streaming code runs as it would
// on Windows. Mixing through SDL3 audio, with DirectSound 3D's attenuation and panning, replaces the silence (P3).
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include <SDL3/SDL.h>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>

namespace
{
template <class T, class... A> T *Make(A... a) { return new (malloc(sizeof(T))) T(a...); }
template <class T> void Destroy(T *p)
{
	p->~T();
	free(p);
}

bool SameGuid(REFIID a, const GUID &b) { return memcmp(&a, &b, sizeof(GUID)) == 0; }

struct Buffer;
SDL_Mutex *gLock;
Buffer *gPlaying[256];
int gNumPlaying;

struct Listener;
struct Buffer3D;
struct Notify;

// MinGW declares IDirectSoundBuffer8 apart from IDirectSoundBuffer (same vtable prefix), so conversions between them
// are reinterpret_casts.
struct Buffer : public IDirectSoundBuffer8
{
	LONG refs = 1;
	DWORD flags;
	DWORD bytes;
	unsigned char *data;
	bool sharedData;
	WAVEFORMATEX format;
	LONG volume = 0;
	LONG pan = 0;
	DWORD frequency;
	bool playing = false, looping = false;
	double cursor = 0;			// play position in bytes
	Uint64 lastTick = 0;
	DSBPOSITIONNOTIFY notes[32];
	DWORD numNotes = 0;
	Buffer3D *b3d = NULL;
	Listener *listener = NULL;

	Buffer(LPCDSBUFFERDESC d)
	{
		flags = d->dwFlags;
		bytes = (flags & DSBCAPS_PRIMARYBUFFER) ? 0 : d->dwBufferBytes;
		data = bytes ? (unsigned char *)calloc(1, bytes) : NULL;
		sharedData = false;
		memset(&format, 0, sizeof(format));
		if (d->lpwfxFormat)
			memcpy(&format, d->lpwfxFormat, sizeof(PCMWAVEFORMAT));
		else
		{
			format.wFormatTag = WAVE_FORMAT_PCM;
			format.nChannels = 2;
			format.nSamplesPerSec = 22050;
			format.wBitsPerSample = 16;
			format.nBlockAlign = 4;
			format.nAvgBytesPerSec = 88200;
		}
		frequency = format.nSamplesPerSec;
	}
	~Buffer();

	double BytesPerSecond() const
	{
		double rate = frequency ? frequency : format.nSamplesPerSec;
		return rate * (format.nBlockAlign ? format.nBlockAlign : 1);
	}

	// Moves the cursor to now; fires the notifications it passes. Called with gLock held.
	void Advance(Uint64 now)
	{
		if (!playing || !bytes)
		{
			lastTick = now;
			return;
		}
		double from = cursor;
		cursor += BytesPerSecond() * (double)(now - lastTick) / 1e9;
		lastTick = now;
		for (DWORD i = 0; i < numNotes; i++)
		{
			DWORD off = notes[i].dwOffset;
			if (off == DSBPN_OFFSETSTOP)
				continue;
			double o = off;
			bool passed = cursor >= bytes ? (o >= from || o < cursor - bytes) : (o >= from && o < cursor);
			if (passed && notes[i].hEventNotify)
				SetEvent(notes[i].hEventNotify);
		}
		if (cursor >= bytes)
		{
			if (looping)
				cursor = fmod(cursor, (double)bytes);
			else
			{
				cursor = 0;
				Halt();
			}
		}
	}

	void Halt()
	{
		playing = false;
		for (int i = 0; i < gNumPlaying; i++)
			if (gPlaying[i] == this)
			{
				gPlaying[i] = gPlaying[--gNumPlaying];
				break;
			}
		for (DWORD i = 0; i < numNotes; i++)
			if (notes[i].dwOffset == DSBPN_OFFSETSTOP && notes[i].hEventNotify)
				SetEvent(notes[i].hEventNotify);
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID *out) override;
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG r = --refs;
		if (!r)
			Destroy(this);
		return r;
	}
	HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS c) override
	{
		c->dwFlags = flags;
		c->dwBufferBytes = bytes;
		c->dwUnlockTransferRate = 0;
		c->dwPlayCpuOverhead = 0;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD play, LPDWORD write) override
	{
		SDL_LockMutex(gLock);
		Advance(SDL_GetTicksNS());
		DWORD p = (DWORD)cursor;
		SDL_UnlockMutex(gLock);
		if (play)
			*play = p;
		if (write)
			*write = bytes ? (p + format.nAvgBytesPerSec / 100) % bytes : 0;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX f, DWORD size, LPDWORD written) override
	{
		DWORD n = size < sizeof(format) ? size : sizeof(format);
		if (f)
			memcpy(f, &format, n);
		if (written)
			*written = sizeof(format);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE GetVolume(LPLONG v) override { *v = volume; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetPan(LPLONG p) override { *p = pan; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD f) override { *f = frequency; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD s) override
	{
		SDL_LockMutex(gLock);
		Advance(SDL_GetTicksNS());
		*s = (playing ? DSBSTATUS_PLAYING : 0) | (playing && looping ? DSBSTATUS_LOOPING : 0);
		SDL_UnlockMutex(gLock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUND, LPCDSBUFFERDESC) override { return DSERR_ALREADYINITIALIZED; }
	HRESULT STDMETHODCALLTYPE Lock(DWORD off, DWORD n, LPVOID *p1, LPDWORD n1, LPVOID *p2, LPDWORD n2, DWORD lockFlags) override
	{
		if (!bytes)
			return DSERR_INVALIDCALL;
		if (lockFlags & DSBLOCK_ENTIREBUFFER)
		{
			off = 0;
			n = bytes;
		}
		if (lockFlags & DSBLOCK_FROMWRITECURSOR)
			GetCurrentPosition(NULL, &off);
		off %= bytes;
		if (n > bytes)
			n = bytes;
		DWORD first = bytes - off < n ? bytes - off : n;
		*p1 = data + off;
		*n1 = first;
		if (p2)
			*p2 = first < n ? data : NULL;
		if (n2)
			*n2 = n - first;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD, DWORD playFlags) override
	{
		SDL_LockMutex(gLock);
		looping = (playFlags & DSBPLAY_LOOPING) != 0;
		if (!playing && bytes && gNumPlaying < 256)
		{
			playing = true;
			lastTick = SDL_GetTicksNS();
			gPlaying[gNumPlaying++] = this;
		}
		SDL_UnlockMutex(gLock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD p) override
	{
		SDL_LockMutex(gLock);
		cursor = bytes ? p % bytes : 0;
		lastTick = SDL_GetTicksNS();
		SDL_UnlockMutex(gLock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX f) override
	{
		memcpy(&format, f, sizeof(PCMWAVEFORMAT));
		frequency = format.nSamplesPerSec;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetVolume(LONG v) override { volume = v; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetPan(LONG p) override { pan = p; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetFrequency(DWORD f) override
	{
		SDL_LockMutex(gLock);
		Advance(SDL_GetTicksNS());
		frequency = f ? f : format.nSamplesPerSec;
		SDL_UnlockMutex(gLock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE Stop() override
	{
		SDL_LockMutex(gLock);
		if (playing)
		{
			Advance(SDL_GetTicksNS());
			if (playing)
				Halt();
		}
		SDL_UnlockMutex(gLock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE Restore() override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetFX(DWORD, LPDSEFFECTDESC, LPDWORD) override { return DSERR_CONTROLUNAVAIL; }
	HRESULT STDMETHODCALLTYPE AcquireResources(DWORD, DWORD, LPDWORD) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetObjectInPath(REFGUID, DWORD, REFGUID, LPVOID *) override { return DSERR_CONTROLUNAVAIL; }
};

// A 3D buffer, listener or notify interface lives as long as its buffer and forwards its references to it.
struct Buffer3D : public IDirectSound3DBuffer
{
	Buffer *owner;
	DS3DBUFFER p;
	explicit Buffer3D(Buffer *b) : owner(b)
	{
		memset(&p, 0, sizeof(p));
		p.dwSize = sizeof(p);
		p.dwInsideConeAngle = p.dwOutsideConeAngle = DS3D_DEFAULTCONEANGLE;
		p.flMinDistance = DS3D_DEFAULTMINDISTANCE;
		p.flMaxDistance = DS3D_DEFAULTMAXDISTANCE;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, LPVOID *o) override { return owner->QueryInterface(r, o); }
	ULONG STDMETHODCALLTYPE AddRef() override { return owner->AddRef(); }
	ULONG STDMETHODCALLTYPE Release() override { return owner->Release(); }
	HRESULT STDMETHODCALLTYPE GetAllParameters(LPDS3DBUFFER o) override { *o = p; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetConeAngles(LPDWORD a, LPDWORD b) override { *a = p.dwInsideConeAngle; *b = p.dwOutsideConeAngle; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetConeOrientation(D3DVECTOR *v) override { *v = p.vConeOrientation; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetConeOutsideVolume(LPLONG v) override { *v = p.lConeOutsideVolume; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetMaxDistance(D3DVALUE *v) override { *v = p.flMaxDistance; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetMinDistance(D3DVALUE *v) override { *v = p.flMinDistance; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetMode(LPDWORD m) override { *m = p.dwMode; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetPosition(D3DVECTOR *v) override { *v = p.vPosition; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetVelocity(D3DVECTOR *v) override { *v = p.vVelocity; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetAllParameters(LPCDS3DBUFFER n, DWORD) override { p = *n; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetConeAngles(DWORD a, DWORD b, DWORD) override { p.dwInsideConeAngle = a; p.dwOutsideConeAngle = b; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetConeOrientation(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD) override
	{
		p.vConeOrientation.x = x; p.vConeOrientation.y = y; p.vConeOrientation.z = z;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetConeOutsideVolume(LONG v, DWORD) override { p.lConeOutsideVolume = v; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetMaxDistance(D3DVALUE v, DWORD) override { p.flMaxDistance = v; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetMinDistance(D3DVALUE v, DWORD) override { p.flMinDistance = v; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetMode(DWORD m, DWORD) override { p.dwMode = m; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD) override
	{
		p.vPosition.x = x; p.vPosition.y = y; p.vPosition.z = z;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD) override
	{
		p.vVelocity.x = x; p.vVelocity.y = y; p.vVelocity.z = z;
		return DS_OK;
	}
};

struct Listener : public IDirectSound3DListener
{
	Buffer *owner;
	DS3DLISTENER p;
	explicit Listener(Buffer *b) : owner(b)
	{
		memset(&p, 0, sizeof(p));
		p.dwSize = sizeof(p);
		p.vOrientFront.z = 1;
		p.vOrientTop.y = 1;
		p.flDistanceFactor = DS3D_DEFAULTDISTANCEFACTOR;
		p.flRolloffFactor = DS3D_DEFAULTROLLOFFFACTOR;
		p.flDopplerFactor = DS3D_DEFAULTDOPPLERFACTOR;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, LPVOID *o) override { return owner->QueryInterface(r, o); }
	ULONG STDMETHODCALLTYPE AddRef() override { return owner->AddRef(); }
	ULONG STDMETHODCALLTYPE Release() override { return owner->Release(); }
	HRESULT STDMETHODCALLTYPE GetAllParameters(LPDS3DLISTENER o) override { *o = p; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetDistanceFactor(D3DVALUE *v) override { *v = p.flDistanceFactor; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetDopplerFactor(D3DVALUE *v) override { *v = p.flDopplerFactor; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetOrientation(D3DVECTOR *f, D3DVECTOR *t) override { *f = p.vOrientFront; *t = p.vOrientTop; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetPosition(D3DVECTOR *v) override { *v = p.vPosition; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetRolloffFactor(D3DVALUE *v) override { *v = p.flRolloffFactor; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetVelocity(D3DVECTOR *v) override { *v = p.vVelocity; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetAllParameters(LPCDS3DLISTENER n, DWORD) override { p = *n; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetDistanceFactor(D3DVALUE v, DWORD) override { p.flDistanceFactor = v; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetDopplerFactor(D3DVALUE v, DWORD) override { p.flDopplerFactor = v; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetOrientation(D3DVALUE fx, D3DVALUE fy, D3DVALUE fz, D3DVALUE tx, D3DVALUE ty, D3DVALUE tz, DWORD) override
	{
		p.vOrientFront.x = fx; p.vOrientFront.y = fy; p.vOrientFront.z = fz;
		p.vOrientTop.x = tx; p.vOrientTop.y = ty; p.vOrientTop.z = tz;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD) override
	{
		p.vPosition.x = x; p.vPosition.y = y; p.vPosition.z = z;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetRolloffFactor(D3DVALUE v, DWORD) override { p.flRolloffFactor = v; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD) override
	{
		p.vVelocity.x = x; p.vVelocity.y = y; p.vVelocity.z = z;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE CommitDeferredSettings() override { return DS_OK; }
};

struct Notify : public IDirectSoundNotify
{
	Buffer *owner;
	explicit Notify(Buffer *b) : owner(b) {}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, LPVOID *o) override { return owner->QueryInterface(r, o); }
	ULONG STDMETHODCALLTYPE AddRef() override { return owner->AddRef(); }
	ULONG STDMETHODCALLTYPE Release() override { return owner->Release(); }
	HRESULT STDMETHODCALLTYPE SetNotificationPositions(DWORD n, LPCDSBPOSITIONNOTIFY p) override
	{
		SDL_LockMutex(gLock);
		owner->numNotes = n < 32 ? n : 32;
		memcpy(owner->notes, p, owner->numNotes * sizeof(DSBPOSITIONNOTIFY));
		SDL_UnlockMutex(gLock);
		return DS_OK;
	}
};

Buffer::~Buffer()
{
	SDL_LockMutex(gLock);
	if (playing)
		Halt();
	SDL_UnlockMutex(gLock);
	if (!sharedData)
		free(data);
	if (b3d)
		Destroy(b3d);
	if (listener)
		Destroy(listener);
}

HRESULT STDMETHODCALLTYPE Buffer::QueryInterface(REFIID riid, LPVOID *out)
{
	*out = NULL;
	if (SameGuid(riid, IID_IDirectSound3DBuffer) && (flags & DSBCAPS_CTRL3D) && !(flags & DSBCAPS_PRIMARYBUFFER))
	{
		if (!b3d)
			b3d = Make<Buffer3D>(this);
		*out = static_cast<IDirectSound3DBuffer *>(b3d);
	}
	else if (SameGuid(riid, IID_IDirectSound3DListener) && (flags & DSBCAPS_PRIMARYBUFFER))
	{
		if (!listener)
			listener = Make<Listener>(this);
		*out = static_cast<IDirectSound3DListener *>(listener);
	}
	else if (SameGuid(riid, IID_IDirectSoundNotify) && (flags & DSBCAPS_CTRLPOSITIONNOTIFY))
		*out = static_cast<IDirectSoundNotify *>(Make<Notify>(this));		// small; freed with the process
	else if (SameGuid(riid, IID_IDirectSoundBuffer) || SameGuid(riid, IID_IDirectSoundBuffer8) || SameGuid(riid, IID_IUnknown))
		*out = static_cast<IDirectSoundBuffer8 *>(this);
	else
		return E_NOINTERFACE;
	AddRef();
	return S_OK;
}

// Fires position notifications for playing buffers nobody is polling.
Uint32 SDLCALL Tick(void *, SDL_TimerID, Uint32 interval)
{
	SDL_LockMutex(gLock);
	Uint64 now = SDL_GetTicksNS();
	for (int i = gNumPlaying - 1; i >= 0; i--)
		gPlaying[i]->Advance(now);
	SDL_UnlockMutex(gLock);
	return interval;
}

struct Sound : public IDirectSound8
{
	LONG refs = 1;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID *out) override
	{
		*out = this;
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG r = --refs;
		if (!r)
			Destroy(this);
		return r;
	}
	HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPCDSBUFFERDESC d, LPDIRECTSOUNDBUFFER *out, IUnknown *) override
	{
		if (!(d->dwFlags & DSBCAPS_PRIMARYBUFFER) && (d->dwBufferBytes < 4 || d->dwBufferBytes > 0x0FFFFFFF))
			return DSERR_INVALIDPARAM;
		*out = reinterpret_cast<IDirectSoundBuffer *>(static_cast<IDirectSoundBuffer8 *>(Make<Buffer>(d)));
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS c) override
	{
		DWORD size = c->dwSize;
		memset(c, 0, size);
		c->dwSize = size;
		c->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARY16BIT | DSCAPS_CONTINUOUSRATE |
					 DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO | DSCAPS_SECONDARY8BIT | DSCAPS_SECONDARY16BIT;
		c->dwMinSecondarySampleRate = DSBFREQUENCY_MIN;
		c->dwMaxSecondarySampleRate = DSBFREQUENCY_MAX;
		c->dwPrimaryBuffers = 1;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER src, LPDIRECTSOUNDBUFFER *out) override
	{
		Buffer *s = static_cast<Buffer *>(reinterpret_cast<IDirectSoundBuffer8 *>(src));
		DSBUFFERDESC d;
		memset(&d, 0, sizeof(d));
		d.dwSize = sizeof(d);
		d.dwFlags = s->flags;
		d.dwBufferBytes = 4;
		d.lpwfxFormat = &s->format;
		Buffer *b = Make<Buffer>(&d);
		free(b->data);
		b->data = s->data;			// a duplicate shares its source's memory (DirectSound's rule)
		b->bytes = s->bytes;
		b->sharedData = true;
		s->AddRef();				// keeps the shared memory alive (released with the process)
		*out = reinterpret_cast<IDirectSoundBuffer *>(static_cast<IDirectSoundBuffer8 *>(b));
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE Compact() override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD c) override { *c = 4; return DS_OK; }	// DSSPEAKER_STEREO
	HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE Initialize(const GUID *) override { return DSERR_ALREADYINITIALIZED; }
	HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD c) override { *c = 0; return DS_OK; }
};
}

extern "C" HRESULT WINAPI DirectSoundEnumerateA(LPDSENUMCALLBACKA cb, LPVOID ctx)
{
	cb(NULL, "Primary Sound Driver", "", ctx);
	return DS_OK;
}

extern "C" HRESULT WINAPI DirectSoundCreate8(const GUID *, LPDIRECTSOUND8 *out, IUnknown *)
{
	if (!gLock)
	{
		gLock = SDL_CreateMutex();
		SDL_AddTimer(10, Tick, NULL);
	}
	*out = Make<Sound>();
	return DS_OK;
}

extern "C" HRESULT WINAPI DirectSoundCreate(const GUID *g, LPDIRECTSOUND *out, IUnknown *u)
{
	return DirectSoundCreate8(g, (LPDIRECTSOUND8 *)out, u);
}
