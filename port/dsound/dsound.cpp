// SPDX-License-Identifier: GPL-3.0-only
// port/dsound/dsound.cpp: DirectSound 8 for the port, on both targets, mixed through SDL3 audio. It replaces
// port/platform/common/dsound.cpp (the platform's silent form, whose COM shape and QueryInterface rules this keeps).
//
// What the game reads back sets the exactness bar (pcsoundmanager.cpp, PCMusicStream.cpp): GetStatus tells it when a
// one-shot sample has finished, and the music thread refills its 512 KB looping buffer when position notifications
// fire. Both follow the mixing clock here: a buffer's play cursor moves only as the mixer consumes its data, so a
// sample stops, and a notification fires, when that part of the buffer has actually been mixed. The mix itself aims to
// be audibly right, not sample-identical to any Windows driver: windowed-sinc resampling for frequency, DirectSound's volume
// and pan in hundredths of a decibel, and DirectSound 3D's "no virtualization" rendering (the game's method 2):
// distance rolloff between the minimum and maximum distance, muting beyond it for DSBCAPS_MUTE3DATMAXDISTANCE, stereo
// pan from the listener's frame and Doppler from the velocities, with DS3D_DEFERRED settings held until
// CommitDeferredSettings.
//
// Environment: BEA_AUDIO_RECORD=file.wav writes the final mix (44.1 kHz 16-bit stereo); BEA_AUDIO_TRACE=file.txt logs
// every Play (mix time, buffer, format, rate, volume, 3D state, the data's peak and RMS); BEA_AUDIO_CLOCK=timer mixes on
// a 10 ms timer without opening a device (cursors and notifications still advance in real time), which is also the
// fallback when no audio device opens.
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include <SDL3/SDL.h>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

unsigned long long PortClockFrames();		// port/platform/common/clock.cpp
bool PortClockVirtual();
unsigned long long PortClockPeekNs();
void PortAudioGuardCheck();			// port/platform/common/audioguard.cpp

namespace
{
template <class T, class... A> T *Make(A... a) { return new (malloc(sizeof(T))) T(a...); }
template <class T> void Destroy(T *p)
{
	p->~T();
	free(p);
}

bool SameGuid(REFIID a, const GUID &b) { return memcmp(&a, &b, sizeof(GUID)) == 0; }

const int kRate = 44100;			// the mix's rate; voices are resampled to it
const int kMaxVoices = 256;

struct Buffer;
struct Buffer3D;
struct Listener;

// ---- the mixer: one per process, shared by every IDirectSound8

struct Vec
{
	float x, y, z;
};
Vec Sub(const Vec &a, const Vec &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Dot(const Vec &a, const Vec &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec Cross(const Vec &a, const Vec &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec ToVec(const D3DVECTOR &v) { return {v.x, v.y, v.z}; }

struct Mixer
{
	SDL_Mutex *lock = NULL;
	SDL_AudioStream *stream = NULL;
	SDL_TimerID timer = 0;
	Uint64 timerLast = 0;
	double timerFrac = 0;
	int users = 0;
	Buffer *voices[kMaxVoices];
	int numVoices = 0;
	Buffer3D *all3D[1024];			// every live 3D buffer, for CommitDeferredSettings
	int num3D = 0;
	Listener *listener = NULL;
	FILE *wav = NULL;
	Uint32 wavBytes = 0;
	Uint32 wavHeaderAt = 0;
	float *scratch = NULL;
	int scratchFrames = 0;
	Uint64 mixed = 0;				// frames mixed since the first device opened
	bool virt = false;				// BEA_PORT_CLOCK: the mix follows the port's virtual clock (CatchUp)
	Uint64 virtBase = 0;			// the virtual time the mix started at
	FILE *trace = NULL;				// BEA_AUDIO_TRACE: one line per Play, for diagnosing a mix

	void Start();
	void Stop();
	void Mix(Sint16 *out, int frames);		// called with lock held
	void CatchUp();
	void Record(const Sint16 *pcm, int frames);
	void FinishWav();
};
Mixer gMixer;

// ---- BEA_AUDIO_CALLLOG: every call in the retail replay shim's DirectSound log format (the shim's log:
// "D <frame> <seconds> <Call> ..." with 8-hex pointers and 3D values in thousandths), so the port's log and retail's
// diff after both pass through C's dsnorm.py. Frames are the port's Presents (PortClockFrames, 1/30 s each under the
// virtual clock).
FILE *gCallLog;
bool gCallLogChecked;
SDL_Mutex *gCallLock;

void CallLog(const char *fmt, ...)
{
	if (!gCallLogChecked)
	{
		gCallLogChecked = true;
		const char *path = SDL_getenv("BEA_AUDIO_CALLLOG");
		if (path && path[0])
		{
			gCallLog = fopen(path, "w");
			gCallLock = SDL_CreateMutex();
		}
	}
	if (!gCallLog)
		return;
	SDL_LockMutex(gCallLock);
	unsigned long long frames = PortClockFrames();
	double t = frames / 30.0;
	// once per Present: the virtual time the mix follows (on the shim clock, peek / dt - Presents = spin escapes)
	static unsigned long long lastClockFrame = ~0ull;
	if (frames != lastClockFrame)
	{
		lastClockFrame = frames;
		unsigned long long ns = PortClockPeekNs();
		fprintf(gCallLog, "D %llu %d.%03d port.Clock peek_us=%llu\n", frames, (int)t, (int)((t - (int)t) * 1000.0), ns / 1000);
	}
	fprintf(gCallLog, "D %llu %d.%03d ", frames, (int)t, (int)((t - (int)t) * 1000.0));
	va_list a;
	va_start(a, fmt);
	vfprintf(gCallLog, fmt, a);
	va_end(a);
	fputc('\n', gCallLog);
	fflush(gCallLog);
	SDL_UnlockMutex(gCallLock);
}
inline unsigned long P(const void *p) { return (unsigned long)(uintptr_t)p; }
inline int F(float f) { return (int)(f * 1000.0f + (f < 0 ? -0.5f : 0.5f)); }

// ---- buffers

// A buffer's sample memory, shared by DuplicateSoundBuffer and freed with its last user.
struct Data
{
	LONG refs;
	DWORD bytes;
	unsigned char *bytesPtr;
};

struct Params3D
{
	DS3DBUFFER now, pending;
	bool dirty = false;
};

struct Buffer final : public IDirectSoundBuffer8
{
	LONG refs = 1;
	DWORD flags;
	Data *data;
	WAVEFORMATEX format;
	LONG volume = 0;
	LONG pan = 0;
	DWORD frequency;
	bool playing = false, looping = false;
	double cursor = 0;			// play position in frames of the buffer's format
	DSBPOSITIONNOTIFY notes[32];
	DWORD numNotes = 0;
	Buffer3D *b3d = NULL;
	Listener *listener = NULL;
	float lastGain[2] = {0, 0};	// the previous mix's gains, ramped to avoid clicks

	Buffer(LPCDSBUFFERDESC d, Data *shared)
	{
		flags = d->dwFlags;
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
		if (!format.nBlockAlign)
			format.nBlockAlign = (WORD)(format.nChannels * format.wBitsPerSample / 8);
		frequency = format.nSamplesPerSec;
		if (shared)
		{
			data = shared;
			data->refs++;
		}
		else
		{
			data = (Data *)malloc(sizeof(Data));
			data->refs = 1;
			// rounded up to whole frames, as Wine's DirectSound does (retail's log: a 242,509-byte voice locks as 242,510)
			data->bytes = (flags & DSBCAPS_PRIMARYBUFFER) ? 0
						  : (d->dwBufferBytes + Align() - 1) / Align() * Align();
			data->bytesPtr = data->bytes ? (unsigned char *)calloc(1, data->bytes) : NULL;
		}
	}
	~Buffer();

	DWORD Bytes() const { return data->bytes; }
	DWORD Align() const { return format.nBlockAlign ? format.nBlockAlign : 1; }
	DWORD Frames() const { return format.nBlockAlign ? Bytes() / format.nBlockAlign : 0; }

	// Sample c of frame f, as a float in [-1, 1).
	float Sample(DWORD f, int c) const
	{
		const unsigned char *p = data->bytesPtr + f * format.nBlockAlign;
		if (format.wBitsPerSample == 8)
			return ((int)p[c] - 128) * (1.0f / 128.0f);
		return (Sint16)(p[2 * c] | p[2 * c + 1] << 8) * (1.0f / 32768.0f);
	}

	// Fires the notifications whose offsets the cursor passed from one frame position to another. Lock held.
	void Notify(double fromFrame, double toFrame, bool wrapped)
	{
		double from = fromFrame * format.nBlockAlign, to = toFrame * format.nBlockAlign;
		for (DWORD i = 0; i < numNotes; i++)
		{
			DWORD off = notes[i].dwOffset;
			if (off == DSBPN_OFFSETSTOP || !notes[i].hEventNotify)
				continue;
			double o = off;
			bool passed = wrapped ? (o >= from || o < to) : (o >= from && o < to);
			if (passed)
				SetEvent(notes[i].hEventNotify);
		}
	}

	void Halt()		// lock held
	{
		playing = false;
		for (int i = 0; i < gMixer.numVoices; i++)
			if (gMixer.voices[i] == this)
			{
				gMixer.voices[i] = gMixer.voices[--gMixer.numVoices];
				break;
			}
		for (DWORD i = 0; i < numNotes; i++)
			if (notes[i].dwOffset == DSBPN_OFFSETSTOP && notes[i].hEventNotify)
				SetEvent(notes[i].hEventNotify);
	}

	void Gains(float out[2], double *step);		// lock held
	void TracePlay();							// lock held

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID *out) override;
	ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)__atomic_add_fetch(&refs, 1, __ATOMIC_ACQ_REL); }
	ULONG ReleaseQuiet()			// a 3D, listener or notify interface's release (retail's log does not see those)
	{
		gMixer.CatchUp();
		LONG r = __atomic_sub_fetch(&refs, 1, __ATOMIC_ACQ_REL);
		if (!r)
			Destroy(this);
		return (ULONG)r;
	}
	ULONG STDMETHODCALLTYPE Release() override
	{
		CallLog("Release obj=%08lX refs=%u", P(this), (unsigned)(refs - 1));
		return ReleaseQuiet();
	}
	HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS c) override
	{
		c->dwFlags = flags;
		c->dwBufferBytes = Bytes();
		c->dwUnlockTransferRate = 0;
		c->dwPlayCpuOverhead = 0;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD play, LPDWORD write) override
	{
		gMixer.CatchUp();
		SDL_LockMutex(gMixer.lock);
		DWORD p = (DWORD)cursor * format.nBlockAlign;
		SDL_UnlockMutex(gMixer.lock);
		if (play)
			*play = p;
		if (write)	// DirectSound keeps the write cursor a little ahead of the play cursor: here one 10 ms mix
			*write = Bytes() ? (p + format.nAvgBytesPerSec / 100 / format.nBlockAlign * format.nBlockAlign) % Bytes() : 0;
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
		gMixer.CatchUp();
		SDL_LockMutex(gMixer.lock);
		*s = (playing ? DSBSTATUS_PLAYING : 0) | (playing && looping ? DSBSTATUS_LOOPING : 0);
		SDL_UnlockMutex(gMixer.lock);
		CallLog("GetStatus obj=%08lX status=%lx hr=0", P(this), (unsigned long)*s);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUND, LPCDSBUFFERDESC) override { return DSERR_ALREADYINITIALIZED; }
	HRESULT STDMETHODCALLTYPE Lock(DWORD off, DWORD n, LPVOID *p1, LPDWORD n1, LPVOID *p2, LPDWORD n2, DWORD lockFlags) override
	{
		gMixer.CatchUp();
		DWORD bytes = Bytes();
		if (!bytes)
		{
			CallLog("Lock obj=%08lX offset=%lu bytes=%lu flags=%lx got=0/0 hr=%lx", P(this), (unsigned long)off, (unsigned long)n,
					(unsigned long)lockFlags, (unsigned long)DSERR_INVALIDCALL);
			return DSERR_INVALIDCALL;
		}
		DWORD reqOff = off, reqN = n;
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
		*p1 = data->bytesPtr + off;
		*n1 = first;
		if (p2)
			*p2 = first < n ? data->bytesPtr : NULL;
		if (n2)
			*n2 = n - first;
		CallLog("Lock obj=%08lX offset=%lu bytes=%lu flags=%lx got=%lu/%lu hr=0", P(this), (unsigned long)reqOff, (unsigned long)reqN,
				(unsigned long)lockFlags, (unsigned long)first, (unsigned long)(n - first));
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD priority, DWORD playFlags) override
	{
		gMixer.CatchUp();
		if (gCallLog || !gCallLogChecked)
		{
			// as retail's log: FNV-1a over the whole buffer, the peak sample, and the cursors before playing
			unsigned h = 2166136261u;
			int peak = 0;
			DWORD bytes = Bytes();
			for (DWORD i = 0; i < bytes; i++)
				h = (h ^ data->bytesPtr[i]) * 16777619u;
			if (format.wBitsPerSample == 16)
				for (DWORD i = 0; i + 1 < bytes; i += 2)
				{
					int v = (short)(data->bytesPtr[i] | data->bytesPtr[i + 1] << 8);
					peak = v < 0 && -v > peak ? -v : v > peak ? v : peak;
				}
			DWORD cp = 0, cw = 0;
			GetCurrentPosition(&cp, &cw);
			CallLog("Play obj=%08lX priority=%lu flags=%lx fnv32=%08x peak=%d hashed=%d bytes=%lu cursor=%lu/%lu hr=0", P(this),
					(unsigned long)priority, (unsigned long)playFlags, h, peak, bytes ? 1 : 0, (unsigned long)bytes,
					(unsigned long)cp, (unsigned long)cw);
		}
		SDL_LockMutex(gMixer.lock);
		looping = (playFlags & DSBPLAY_LOOPING) != 0;
		if (!playing && Bytes() && !(flags & DSBCAPS_PRIMARYBUFFER) && gMixer.numVoices < kMaxVoices)
		{
			if (gMixer.trace)
				TracePlay();
			playing = true;
			lastGain[0] = lastGain[1] = -1;		// start at the target gain, without a ramp
			gMixer.voices[gMixer.numVoices++] = this;
		}
		SDL_UnlockMutex(gMixer.lock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD p) override
	{
		gMixer.CatchUp();
		CallLog("SetCurrentPosition obj=%08lX position=%lu hr=0", P(this), (unsigned long)p);
		SDL_LockMutex(gMixer.lock);
		cursor = Bytes() ? (double)((p % Bytes()) / format.nBlockAlign) : 0;
		SDL_UnlockMutex(gMixer.lock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX f) override
	{
		// The primary buffer's format: the mix stays at 44.1 kHz stereo and SDL converts for the device.
		memcpy(&format, f, sizeof(PCMWAVEFORMAT));
		frequency = format.nSamplesPerSec;
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetVolume(LONG v) override
	{
		gMixer.CatchUp();
		bool bad = v < DSBVOLUME_MIN || v > DSBVOLUME_MAX;
		CallLog("SetVolume obj=%08lX volume=%ld hr=%lx", P(this), (long)v, bad ? (unsigned long)DSERR_INVALIDPARAM : 0UL);
		if (bad)
			return DSERR_INVALIDPARAM;
		SDL_LockMutex(gMixer.lock);
		volume = v;
		SDL_UnlockMutex(gMixer.lock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetPan(LONG p) override
	{
		gMixer.CatchUp();
		bool bad = p < DSBPAN_LEFT || p > DSBPAN_RIGHT;
		CallLog("SetPan obj=%08lX pan=%ld hr=%lx", P(this), (long)p, bad ? (unsigned long)DSERR_INVALIDPARAM : 0UL);
		if (bad)
			return DSERR_INVALIDPARAM;
		SDL_LockMutex(gMixer.lock);
		pan = p;
		SDL_UnlockMutex(gMixer.lock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetFrequency(DWORD f) override
	{
		gMixer.CatchUp();
		bool bad = f != DSBFREQUENCY_ORIGINAL && (f < DSBFREQUENCY_MIN || f > DSBFREQUENCY_MAX);
		CallLog("SetFrequency obj=%08lX frequency=%lu hr=%lx", P(this), (unsigned long)f, bad ? (unsigned long)DSERR_INVALIDPARAM : 0UL);
		if (bad)
			return DSERR_INVALIDPARAM;
		SDL_LockMutex(gMixer.lock);
		frequency = f ? f : format.nSamplesPerSec;
		SDL_UnlockMutex(gMixer.lock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE Stop() override
	{
		gMixer.CatchUp();
		CallLog("Stop obj=%08lX hr=0", P(this));
		SDL_LockMutex(gMixer.lock);
		if (playing)
			Halt();
		SDL_UnlockMutex(gMixer.lock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD n1, LPVOID, DWORD n2) override
	{
		gMixer.CatchUp();
		// DirectSound rejects an unlock larger than the buffer (the game unlocks its samples with twice their size and
		// gets this error in retail too; the data is written either way)
		HRESULT hr = (n1 > Bytes() || n2 > Bytes()) ? DSERR_INVALIDPARAM : DS_OK;
		CallLog("Unlock obj=%08lX n1=%lu n2=%lu hr=%lx", P(this), (unsigned long)n1, (unsigned long)n2, (unsigned long)hr);
		return hr;
	}
	HRESULT STDMETHODCALLTYPE Restore() override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetFX(DWORD, LPDSEFFECTDESC, LPDWORD) override { return DSERR_CONTROLUNAVAIL; }
	HRESULT STDMETHODCALLTYPE AcquireResources(DWORD, DWORD, LPDWORD) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetObjectInPath(REFGUID, DWORD, REFGUID, LPVOID *) override { return DSERR_CONTROLUNAVAIL; }
};

// A 3D buffer, listener or notify interface lives as long as its buffer and forwards its references to it.
struct Buffer3D final : public IDirectSound3DBuffer
{
	Buffer *owner;
	Params3D p;
	explicit Buffer3D(Buffer *b) : owner(b)
	{
		DS3DBUFFER &d = p.now;
		memset(&d, 0, sizeof(d));
		d.dwSize = sizeof(d);
		d.dwInsideConeAngle = d.dwOutsideConeAngle = DS3D_DEFAULTCONEANGLE;
		d.flMinDistance = DS3D_DEFAULTMINDISTANCE;
		d.flMaxDistance = DS3D_DEFAULTMAXDISTANCE;
		d.dwMode = DS3DMODE_NORMAL;
		p.pending = d;
		SDL_LockMutex(gMixer.lock);
		if (gMixer.num3D < 1024)
			gMixer.all3D[gMixer.num3D++] = this;
		SDL_UnlockMutex(gMixer.lock);
	}
	~Buffer3D()
	{
		SDL_LockMutex(gMixer.lock);
		for (int i = 0; i < gMixer.num3D; i++)
			if (gMixer.all3D[i] == this)
			{
				gMixer.all3D[i] = gMixer.all3D[--gMixer.num3D];
				break;
			}
		SDL_UnlockMutex(gMixer.lock);
	}
	// Applies a change now or keeps it for CommitDeferredSettings.
	template <class F> HRESULT Change(DWORD apply, F f)
	{
		gMixer.CatchUp();
		SDL_LockMutex(gMixer.lock);
		f(p.pending);
		if (apply == DS3D_DEFERRED)
			p.dirty = true;
		else
		{
			f(p.now);
		}
		SDL_UnlockMutex(gMixer.lock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, LPVOID *o) override { return owner->QueryInterface(r, o); }
	ULONG STDMETHODCALLTYPE AddRef() override { return owner->AddRef(); }
	ULONG STDMETHODCALLTYPE Release() override { return owner->ReleaseQuiet(); }
	HRESULT STDMETHODCALLTYPE GetAllParameters(LPDS3DBUFFER o) override { *o = p.pending; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetConeAngles(LPDWORD a, LPDWORD b) override { *a = p.pending.dwInsideConeAngle; *b = p.pending.dwOutsideConeAngle; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetConeOrientation(D3DVECTOR *v) override { *v = p.pending.vConeOrientation; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetConeOutsideVolume(LPLONG v) override { *v = p.pending.lConeOutsideVolume; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetMaxDistance(D3DVALUE *v) override { *v = p.pending.flMaxDistance; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetMinDistance(D3DVALUE *v) override { *v = p.pending.flMinDistance; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetMode(LPDWORD m) override { *m = p.pending.dwMode; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetPosition(D3DVECTOR *v) override { *v = p.pending.vPosition; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetVelocity(D3DVECTOR *v) override { *v = p.pending.vVelocity; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetAllParameters(LPCDS3DBUFFER n, DWORD a) override
	{
		gMixer.CatchUp();
		CallLog("port.3D.SetAllParameters obj=%08lX pos=%d,%d,%d vel=%d,%d,%d cone=%lu,%lu orient=%d,%d,%d outvol=%ld min=%d max=%d mode=%lu "
				"apply=%lu hr=0", P(this), F(n->vPosition.x), F(n->vPosition.y), F(n->vPosition.z), F(n->vVelocity.x),
				F(n->vVelocity.y), F(n->vVelocity.z), (unsigned long)n->dwInsideConeAngle, (unsigned long)n->dwOutsideConeAngle,
				F(n->vConeOrientation.x), F(n->vConeOrientation.y), F(n->vConeOrientation.z), (long)n->lConeOutsideVolume,
				F(n->flMinDistance), F(n->flMaxDistance), (unsigned long)n->dwMode, (unsigned long)a);
		DS3DBUFFER v = *n;
		v.dwSize = sizeof(v);
		return Change(a, [&](DS3DBUFFER &d) { d = v; });
	}
	HRESULT STDMETHODCALLTYPE SetConeAngles(DWORD i, DWORD o, DWORD a) override
	{
		return Change(a, [&](DS3DBUFFER &d) { d.dwInsideConeAngle = i; d.dwOutsideConeAngle = o; });
	}
	HRESULT STDMETHODCALLTYPE SetConeOrientation(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD a) override
	{
		return Change(a, [&](DS3DBUFFER &d) { d.vConeOrientation.x = x; d.vConeOrientation.y = y; d.vConeOrientation.z = z; });
	}
	HRESULT STDMETHODCALLTYPE SetConeOutsideVolume(LONG v, DWORD a) override
	{
		return Change(a, [&](DS3DBUFFER &d) { d.lConeOutsideVolume = v; });
	}
	HRESULT STDMETHODCALLTYPE SetMaxDistance(D3DVALUE v, DWORD a) override
	{
		CallLog("3D.SetMaxDistance obj=%08lX d=%d apply=%lu hr=0", P(this), F(v), (unsigned long)a);
		return Change(a, [&](DS3DBUFFER &d) { d.flMaxDistance = v; });
	}
	HRESULT STDMETHODCALLTYPE SetMinDistance(D3DVALUE v, DWORD a) override
	{
		CallLog("3D.SetMinDistance obj=%08lX d=%d apply=%lu hr=0", P(this), F(v), (unsigned long)a);
		return Change(a, [&](DS3DBUFFER &d) { d.flMinDistance = v; });
	}
	HRESULT STDMETHODCALLTYPE SetMode(DWORD m, DWORD a) override
	{
		CallLog("port.3D.SetMode obj=%08lX mode=%lu apply=%lu hr=0", P(this), (unsigned long)m, (unsigned long)a);
		return Change(a, [&](DS3DBUFFER &d) { d.dwMode = m; });
	}
	HRESULT STDMETHODCALLTYPE SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD a) override
	{
		CallLog("3D.SetPosition obj=%08lX x=%d y=%d z=%d apply=%lu hr=0", P(this), F(x), F(y), F(z), (unsigned long)a);
		return Change(a, [&](DS3DBUFFER &d) { d.vPosition.x = x; d.vPosition.y = y; d.vPosition.z = z; });
	}
	HRESULT STDMETHODCALLTYPE SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD a) override
	{
		CallLog("3D.SetVelocity obj=%08lX x=%d y=%d z=%d apply=%lu hr=0", P(this), F(x), F(y), F(z), (unsigned long)a);
		return Change(a, [&](DS3DBUFFER &d) { d.vVelocity.x = x; d.vVelocity.y = y; d.vVelocity.z = z; });
	}
};

struct Listener final : public IDirectSound3DListener
{
	Buffer *owner;
	DS3DLISTENER now, pending;
	bool dirty = false;
	explicit Listener(Buffer *b) : owner(b)
	{
		memset(&now, 0, sizeof(now));
		now.dwSize = sizeof(now);
		now.vOrientFront.z = 1;
		now.vOrientTop.y = 1;
		now.flDistanceFactor = DS3D_DEFAULTDISTANCEFACTOR;
		now.flRolloffFactor = DS3D_DEFAULTROLLOFFFACTOR;
		now.flDopplerFactor = DS3D_DEFAULTDOPPLERFACTOR;
		pending = now;
		SDL_LockMutex(gMixer.lock);
		gMixer.listener = this;
		SDL_UnlockMutex(gMixer.lock);
	}
	~Listener()
	{
		SDL_LockMutex(gMixer.lock);
		if (gMixer.listener == this)
			gMixer.listener = NULL;
		SDL_UnlockMutex(gMixer.lock);
	}
	template <class F> HRESULT Change(DWORD apply, F f)
	{
		gMixer.CatchUp();
		SDL_LockMutex(gMixer.lock);
		f(pending);
		if (apply == DS3D_DEFERRED)
			dirty = true;
		else
			f(now);
		SDL_UnlockMutex(gMixer.lock);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, LPVOID *o) override { return owner->QueryInterface(r, o); }
	ULONG STDMETHODCALLTYPE AddRef() override { return owner->AddRef(); }
	ULONG STDMETHODCALLTYPE Release() override { return owner->ReleaseQuiet(); }
	HRESULT STDMETHODCALLTYPE GetAllParameters(LPDS3DLISTENER o) override { *o = pending; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetDistanceFactor(D3DVALUE *v) override { *v = pending.flDistanceFactor; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetDopplerFactor(D3DVALUE *v) override { *v = pending.flDopplerFactor; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetOrientation(D3DVECTOR *f, D3DVECTOR *t) override { *f = pending.vOrientFront; *t = pending.vOrientTop; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetPosition(D3DVECTOR *v) override { *v = pending.vPosition; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetRolloffFactor(D3DVALUE *v) override { *v = pending.flRolloffFactor; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetVelocity(D3DVECTOR *v) override { *v = pending.vVelocity; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetAllParameters(LPCDS3DLISTENER n, DWORD a) override
	{
		gMixer.CatchUp();
		CallLog("port.Listener.SetAllParameters pos=%d,%d,%d vel=%d,%d,%d front=%d,%d,%d top=%d,%d,%d distance=%d doppler=%d rolloff=%d "
				"apply=%lu hr=0", F(n->vPosition.x), F(n->vPosition.y), F(n->vPosition.z), F(n->vVelocity.x), F(n->vVelocity.y),
				F(n->vVelocity.z), F(n->vOrientFront.x), F(n->vOrientFront.y), F(n->vOrientFront.z), F(n->vOrientTop.x),
				F(n->vOrientTop.y), F(n->vOrientTop.z), F(n->flDistanceFactor), F(n->flDopplerFactor), F(n->flRolloffFactor),
				(unsigned long)a);
		DS3DLISTENER v = *n;
		v.dwSize = sizeof(v);
		return Change(a, [&](DS3DLISTENER &d) { d = v; });
	}
	HRESULT STDMETHODCALLTYPE SetDistanceFactor(D3DVALUE v, DWORD a) override
	{
		return Change(a, [&](DS3DLISTENER &d) { d.flDistanceFactor = v; });
	}
	HRESULT STDMETHODCALLTYPE SetDopplerFactor(D3DVALUE v, DWORD a) override
	{
		return Change(a, [&](DS3DLISTENER &d) { d.flDopplerFactor = v; });
	}
	HRESULT STDMETHODCALLTYPE SetOrientation(D3DVALUE fx, D3DVALUE fy, D3DVALUE fz, D3DVALUE tx, D3DVALUE ty, D3DVALUE tz, DWORD a) override
	{
		return Change(a, [&](DS3DLISTENER &d) {
			d.vOrientFront.x = fx; d.vOrientFront.y = fy; d.vOrientFront.z = fz;
			d.vOrientTop.x = tx; d.vOrientTop.y = ty; d.vOrientTop.z = tz;
		});
	}
	HRESULT STDMETHODCALLTYPE SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD a) override
	{
		return Change(a, [&](DS3DLISTENER &d) { d.vPosition.x = x; d.vPosition.y = y; d.vPosition.z = z; });
	}
	HRESULT STDMETHODCALLTYPE SetRolloffFactor(D3DVALUE v, DWORD a) override
	{
		return Change(a, [&](DS3DLISTENER &d) { d.flRolloffFactor = v; });
	}
	HRESULT STDMETHODCALLTYPE SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD a) override
	{
		return Change(a, [&](DS3DLISTENER &d) { d.vVelocity.x = x; d.vVelocity.y = y; d.vVelocity.z = z; });
	}
	HRESULT STDMETHODCALLTYPE CommitDeferredSettings() override
	{
		gMixer.CatchUp();
		CallLog("port.Listener.CommitDeferredSettings hr=0");
		SDL_LockMutex(gMixer.lock);
		if (dirty)
		{
			now = pending;
			dirty = false;
		}
		for (int i = 0; i < gMixer.num3D; i++)
			if (gMixer.all3D[i]->p.dirty)
			{
				gMixer.all3D[i]->p.now = gMixer.all3D[i]->p.pending;
				gMixer.all3D[i]->p.dirty = false;
			}
		SDL_UnlockMutex(gMixer.lock);
		return DS_OK;
	}
};

struct NotifyIface final : public IDirectSoundNotify
{
	Buffer *owner;
	explicit NotifyIface(Buffer *b) : owner(b) {}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, LPVOID *o) override { return owner->QueryInterface(r, o); }
	ULONG STDMETHODCALLTYPE AddRef() override { return owner->AddRef(); }
	ULONG STDMETHODCALLTYPE Release() override { return owner->ReleaseQuiet(); }
	HRESULT STDMETHODCALLTYPE SetNotificationPositions(DWORD n, LPCDSBPOSITIONNOTIFY p) override
	{
		gMixer.CatchUp();
		CallLog("Notify.SetNotificationPositions obj=%08lX count=%lu hr=0", P(this), (unsigned long)n);
		for (DWORD i = 0; i < n; i++)
			CallLog("  notify[%lu] offset=%lu event=%08lX", (unsigned long)i, (unsigned long)p[i].dwOffset, P(p[i].hEventNotify));
		SDL_LockMutex(gMixer.lock);
		owner->numNotes = n < 32 ? n : 32;
		memcpy(owner->notes, p, owner->numNotes * sizeof(DSBPOSITIONNOTIFY));
		SDL_UnlockMutex(gMixer.lock);
		return DS_OK;
	}
};

void Buffer::TracePlay()
{
	double peak = 0, sum = 0;
	DWORD n = Frames() * format.nChannels;
	for (DWORD i = 0; i < Frames(); i++)
		for (int c = 0; c < format.nChannels; c++)
		{
			double v = fabs(Sample(i, c));
			peak = v > peak ? v : peak;
			sum += v * v;
		}
	fprintf(gMixer.trace, "%.3f play buf=%p data=%p bytes=%lu rate=%lu ch=%d bits=%d freq=%lu vol=%ld loop=%d cursor=%.0f "
			"peak=%.3f rms=%.4f", gMixer.mixed / (double)kRate, (void *)this, (void *)data, (unsigned long)Bytes(),
			(unsigned long)format.nSamplesPerSec, format.nChannels, format.wBitsPerSample, (unsigned long)frequency,
			(long)volume, looping ? 1 : 0, cursor, peak, n ? sqrt(sum / n) : 0.0);
	if (b3d)
		fprintf(gMixer.trace, " pos=%.1f,%.1f,%.1f min=%.1f max=%.1f", b3d->p.now.vPosition.x, b3d->p.now.vPosition.y,
				b3d->p.now.vPosition.z, b3d->p.now.flMinDistance, b3d->p.now.flMaxDistance);
	fprintf(gMixer.trace, "\n");
	fflush(gMixer.trace);
	// BEA_AUDIO_DUMP=dir: each played buffer's data once, as a WAV named by its data block
	const char *dir = SDL_getenv("BEA_AUDIO_DUMP");
	if (dir && dir[0])
	{
		char name[512];
		snprintf(name, sizeof(name), "%s/%p.wav", dir, (void *)data);
		FILE *f = fopen(name, "rb");
		if (f)
			fclose(f);
		else if ((f = fopen(name, "wb")) != NULL)
		{
			Uint32 bytes = Bytes();
			Uint32 hdr[11] = {0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16,
							  (Uint32)WAVE_FORMAT_PCM | (Uint32)format.nChannels << 16, format.nSamplesPerSec,
							  format.nSamplesPerSec * format.nBlockAlign,
							  (Uint32)format.nBlockAlign | (Uint32)format.wBitsPerSample << 16, 0x61746164, bytes};
			fwrite(hdr, 4, 11, f);
			fwrite(data->bytesPtr, 1, bytes, f);
			fclose(f);
		}
	}
			}

Buffer::~Buffer()
{
	SDL_LockMutex(gMixer.lock);
	if (playing)
		Halt();
	SDL_UnlockMutex(gMixer.lock);
	if (b3d)
		Destroy(b3d);
	if (listener)
		Destroy(listener);
	if (--data->refs == 0)
	{
		free(data->bytesPtr);
		free(data);
	}
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
		*out = static_cast<IDirectSoundNotify *>(Make<NotifyIface>(this));		// small; freed with the process
	else if (SameGuid(riid, IID_IDirectSoundBuffer) || SameGuid(riid, IID_IDirectSoundBuffer8) || SameGuid(riid, IID_IUnknown))
		*out = static_cast<IDirectSoundBuffer8 *>(this);
	else
	{
		CallLog("QueryInterface obj=%08lX iid=other out=00000000 hr=%lx", P(this), (unsigned long)E_NOINTERFACE);
		return E_NOINTERFACE;
	}
	AddRef();
	const char *nm = SameGuid(riid, IID_IDirectSound3DBuffer) ? "3DBuffer" : SameGuid(riid, IID_IDirectSound3DListener) ? "3DListener"
					 : SameGuid(riid, IID_IDirectSoundNotify) ? "Notify" : "Buffer";
	CallLog("QueryInterface obj=%08lX iid=%s out=%08lX hr=0", P(this), nm, P(*out));
	return S_OK;
}

// ---- mixing

// The resampler: a 32-tap windowed sinc (Blackman) in 256 phases, cut off at 0.90 of the source's Nyquist frequency, so
// upsampling the game's 22.05 kHz samples leaves little above it (linear interpolation mirrored the speech band up to
// 22 kHz). Measured response for a 22.05 kHz source: flat to 8 kHz, -0.65 dB at 9 kHz, -6.9 dB at 10 kHz; images
// -28 dB at 11.0-12 kHz, -75 dB from 12 kHz up. A voice whose step is exactly 1 (its frequency equals the mix rate) is
// copied without filtering. Steps above 1 (pitched up past the mix rate) get no extra low-pass, so they can alias; the
// game's pitch variance is small.
const int kTaps = 32, kPhases = 256;
float gKernel[kPhases + 1][kTaps];

void BuildKernel()
{
	const double cutoff = 0.90;		// of the source's Nyquist frequency
	for (int ph = 0; ph <= kPhases; ph++)
	{
		double frac = (double)ph / kPhases, sum = 0;
		for (int t = 0; t < kTaps; t++)
		{
			double x = (t - (kTaps / 2 - 1)) - frac;	// taps at -15..+16 around the position
			double s = x == 0 ? cutoff : sin(M_PI * cutoff * x) / (M_PI * x);
			double w = (x + kTaps / 2.0) / kTaps;		// 0..1 across the window
			double win = w <= 0 || w >= 1 ? 0 : 0.42 - 0.5 * cos(2 * M_PI * w) + 0.08 * cos(4 * M_PI * w);
			gKernel[ph][t] = (float)(s * win);
			sum += s * win;
		}
		for (int t = 0; t < kTaps; t++)	// unity gain at DC in every phase
			gKernel[ph][t] = (float)(gKernel[ph][t] / sum);
	}
}

float DbToGain(LONG hundredths) { return hundredths <= DSBVOLUME_MIN ? 0.0f : powf(10.0f, hundredths / 2000.0f); }

// The voice's left and right gains and its source step per output frame, from volume, pan, 3D and frequency.
void Buffer::Gains(float out[2], double *step)
{
	float g = DbToGain(volume);
	float left = g, right = g;
	double freq = frequency ? frequency : format.nSamplesPerSec;
	if (b3d && b3d->p.now.dwMode != DS3DMODE_DISABLE && gMixer.listener)
	{
		const DS3DBUFFER &s = b3d->p.now;
		const DS3DLISTENER &l = gMixer.listener->now;
		Vec rel = s.dwMode == DS3DMODE_HEADRELATIVE ? ToVec(s.vPosition) : Sub(ToVec(s.vPosition), ToVec(l.vPosition));
		float dist = sqrtf(Dot(rel, rel));
		float minD = s.flMinDistance > 0 ? s.flMinDistance : 1.0f, maxD = s.flMaxDistance;
		float att = 1.0f;
		if (dist > minD)
		{
			if (dist > maxD)
			{
				if (flags & DSBCAPS_MUTE3DATMAXDISTANCE)
					att = 0.0f;
				dist = maxD;
			}
			if (att > 0.0f)	// DirectSound's rolloff: 6 dB per doubling of distance at a rolloff factor of 1
				att = minD / (minD + l.flRolloffFactor * (dist - minD));
		}
		float pan3d = 0.0f;
		Vec front = ToVec(l.vOrientFront), top = ToVec(l.vOrientTop);
		Vec rightAxis = Cross(top, front);
		float rlen = sqrtf(Dot(rightAxis, rightAxis));
		float rd = sqrtf(Dot(rel, rel));
		if (rlen > 0 && rd > 1e-6f)
			pan3d = Dot(rel, rightAxis) / (rlen * rd);
		// equal power across the stereo field, centre at unity
		float a = (pan3d + 1.0f) * 0.78539816f;
		float pl = cosf(a) * 1.41421356f, pr = sinf(a) * 1.41421356f;
		left *= att * (pl > 1.0f ? 1.0f : pl);
		right *= att * (pr > 1.0f ? 1.0f : pr);
		// Doppler: the velocity components along the line from the listener to the source
		if (l.flDopplerFactor > 0 && rd > 1e-6f)
		{
			float c = 343.0f / (l.flDistanceFactor > 0 ? l.flDistanceFactor : 1.0f);
			float vs = Dot(ToVec(s.vVelocity), rel) / rd, vl = Dot(ToVec(l.vVelocity), rel) / rd;
			vs *= l.flDopplerFactor;
			vl *= l.flDopplerFactor;
			if (vs > 0.9f * c) vs = 0.9f * c;
			if (vs < -0.9f * c) vs = -0.9f * c;
			freq *= (c + vl) / (c + vs);
		}
	}
	else if (pan)
	{
		if (pan > 0)
			left *= DbToGain(-pan);
		else
			right *= DbToGain(pan);
	}
	out[0] = left;
	out[1] = right;
	*step = freq / kRate;
}

void Mixer::Mix(Sint16 *out, int frames)
{
	if (scratchFrames < frames)
	{
		free(scratch);
		scratch = (float *)malloc(sizeof(float) * 2 * frames);
		scratchFrames = frames;
	}
	memset(scratch, 0, sizeof(float) * 2 * frames);
	for (int v = numVoices - 1; v >= 0; v--)
	{
		Buffer *b = voices[v];
		DWORD total = b->Frames();
		if (!total)
		{
			b->Halt();
			continue;
		}
		float gain[2];
		double step;
		b->Gains(gain, &step);
		float g0[2] = {b->lastGain[0] < 0 ? gain[0] : b->lastGain[0], b->lastGain[1] < 0 ? gain[1] : b->lastGain[1]};
		b->lastGain[0] = gain[0];
		b->lastGain[1] = gain[1];
		int ch = b->format.nChannels;
		double pos = b->cursor, start = pos;
		bool wrapped = false, ended = false;
		int i;
		for (i = 0; i < frames; i++)
		{
			DWORD f0 = (DWORD)pos;
			if (f0 >= total)
			{
				if (b->looping)
				{
					pos -= total;
					wrapped = true;
					f0 = (DWORD)pos;
					if (f0 >= total)
						f0 = 0, pos = 0;
				}
				else
				{
					ended = true;
					break;
				}
			}
			float ramp = (float)i / frames;
			float gl = g0[0] + (gain[0] - g0[0]) * ramp, gr = g0[1] + (gain[1] - g0[1]) * ramp;
			float sl, sr;
			double frac = pos - f0;
			if (step == 1.0 && frac == 0.0)
			{
				sl = b->Sample(f0, 0);
				sr = ch > 1 ? b->Sample(f0, 1) : sl;
			}
			else
			{
				const float *k = gKernel[(int)(frac * kPhases + 0.5)];
				sl = sr = 0;
				for (int t = 0; t < kTaps; t++)
				{
					long f = (long)f0 + t - (kTaps / 2 - 1);
					if (f < 0 || f >= (long)total)
					{
						if (!b->looping)
							continue;
						f = ((f % (long)total) + (long)total) % (long)total;
					}
					float l = b->Sample((DWORD)f, 0);
					sl += k[t] * l;
					sr += k[t] * (ch > 1 ? b->Sample((DWORD)f, 1) : l);
				}
			}
			scratch[2 * i] += sl * gl;
			scratch[2 * i + 1] += sr * gr;
			pos += step;
		}
		if (pos >= total && b->looping)
		{
			pos = fmod(pos, (double)total);
			wrapped = true;
		}
		b->Notify(start, ended ? (double)total : pos, wrapped);
		if (ended)
		{
			b->cursor = 0;
			b->Halt();
		}
		else
			b->cursor = pos;
	}
	for (int i = 0; i < 2 * frames; i++)
	{
		float s = scratch[i];
		s = s > 1.0f ? 1.0f : s < -1.0f ? -1.0f : s;
		out[i] = (Sint16)lrintf(s * 32767.0f);
	}
	Record(out, frames);
	mixed += frames;
}

void Mixer::Record(const Sint16 *pcm, int frames)
{
	if (!wav)
		return;
	fwrite(pcm, 4, frames, wav);
	wavBytes += 4 * frames;
	// keep the header valid as the file grows (about once a second), so an interrupted run leaves a playable file
	if (wavBytes - wavHeaderAt >= kRate * 4)
	{
		wavHeaderAt = wavBytes;
		long here = ftell(wav);
		Uint32 riff = 36 + wavBytes;
		fseek(wav, 4, SEEK_SET);
		fwrite(&riff, 4, 1, wav);
		fseek(wav, 40, SEEK_SET);
		fwrite(&wavBytes, 4, 1, wav);
		fseek(wav, here, SEEK_SET);
		fflush(wav);
	}
}

void Mixer::FinishWav()
{
	if (!wav)
		return;
	Uint32 riff = 36 + wavBytes;
	fseek(wav, 4, SEEK_SET);
	fwrite(&riff, 4, 1, wav);
	fseek(wav, 40, SEEK_SET);
	fwrite(&wavBytes, 4, 1, wav);
	fclose(wav);
	wav = NULL;
}

void SDLCALL Feed(void *, SDL_AudioStream *stream, int additional, int)
{
	int frames = additional / 4;
	if (frames <= 0)
		return;
	Sint16 *buf = (Sint16 *)SDL_malloc(frames * 4);
	SDL_LockMutex(gMixer.lock);
	gMixer.Mix(buf, frames);
	SDL_UnlockMutex(gMixer.lock);
	SDL_PutAudioStreamData(stream, buf, frames * 4);
	SDL_free(buf);
}

// Under the port's virtual clock (BEA_PORT_CLOCK) no device plays and the mix runs on demand: every call that reads or
// changes playback first mixes whole 10 ms blocks up to the virtual time now, so cursors, GetStatus and the state the
// game changes all fall on the same block in every run. The timer only keeps notifications coming while nothing calls.
const int kVirtualBlock = kRate / 100;

void Mixer::CatchUp()
{
	if (!virt)
		return;
	Uint64 now = PortClockPeekNs();
	if (!now)
		return;
	SDL_LockMutex(lock);
	if (!virtBase)
		virtBase = now;
	Uint64 target = (now - virtBase) * kRate / 1000000000ull;
	Sint16 block[kVirtualBlock * 2];
	while (mixed + kVirtualBlock <= target)
		Mix(block, kVirtualBlock);
	SDL_UnlockMutex(lock);
}

Uint32 SDLCALL TimerTick(void *, SDL_TimerID, Uint32 interval)
{
	if (gMixer.virt)
	{
		gMixer.CatchUp();
		return interval;
	}
	Uint64 now = SDL_GetTicksNS();
	double frames = gMixer.timerFrac + (double)(now - gMixer.timerLast) * kRate / 1e9;
	gMixer.timerLast = now;
	int n = (int)frames;
	gMixer.timerFrac = frames - n;
	if (n > 0)
	{
		Sint16 *buf = (Sint16 *)SDL_malloc(n * 4);
		SDL_LockMutex(gMixer.lock);
		gMixer.Mix(buf, n);
		SDL_UnlockMutex(gMixer.lock);
		SDL_free(buf);
	}
	return interval;
}

void Mixer::Start()
{
	if (users++)
		return;
	PortAudioGuardCheck();		// a run that requires SDL's dummy driver gets it before any device can open
	if (!lock)
	{
		lock = SDL_CreateMutex();
		BuildKernel();
	}
	const char *rec = SDL_getenv("BEA_AUDIO_RECORD");
	if (rec && rec[0] && (wav = fopen(rec, "wb")) != NULL)
	{
		Uint32 hdr[11] = {0x46464952, 36, 0x45564157, 0x20746d66, 16, 0x00020001, kRate, kRate * 4, 0x00100004,
						  0x61746164, 0};
		fwrite(hdr, 4, 11, wav);
		wavBytes = wavHeaderAt = 0;
	}
	const char *tr = SDL_getenv("BEA_AUDIO_TRACE");
	if (tr && tr[0])
		trace = fopen(tr, "w");
	const char *clock = SDL_getenv("BEA_AUDIO_CLOCK");
	virt = PortClockVirtual();
	if (virt)
		virtBase = PortClockPeekNs();
	bool timerOnly = virt || (clock && SDL_strcmp(clock, "timer") == 0);
	if (!timerOnly && SDL_InitSubSystem(SDL_INIT_AUDIO))
	{
		SDL_AudioSpec spec = {SDL_AUDIO_S16LE, 2, kRate};
		stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, NULL);
		if (stream)
			SDL_ResumeAudioStreamDevice(stream);
		else
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
	if (!stream)
	{
		timerLast = SDL_GetTicksNS();
		timerFrac = 0;
		timer = SDL_AddTimer(10, TimerTick, NULL);
	}
	// so a run's log shows where the sound went (SDL's "dummy" driver and the timer clock are silent)
	fprintf(stderr, "bea-port: audio %s\n", stream ? SDL_GetCurrentAudioDriver() : virt ? "virtual clock, no device" : "timer clock, no device");
}

void Mixer::Stop()
{
	if (--users)
		return;
	if (stream)
	{
		SDL_DestroyAudioStream(stream);
		stream = NULL;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
	if (timer)
	{
		SDL_RemoveTimer(timer);
		timer = 0;
	}
	SDL_LockMutex(lock);
	FinishWav();
	if (trace)
	{
		fclose(trace);
		trace = NULL;
	}
	SDL_UnlockMutex(lock);
}

// ---- the device

struct Sound final : public IDirectSound8
{
	LONG refs = 1;
	Sound() { gMixer.Start(); }
	~Sound() { gMixer.Stop(); }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID *out) override
	{
		*out = this;
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)__atomic_add_fetch(&refs, 1, __ATOMIC_ACQ_REL); }
	ULONG STDMETHODCALLTYPE Release() override
	{
		LONG r = __atomic_sub_fetch(&refs, 1, __ATOMIC_ACQ_REL);
		if (!r)
			Destroy(this);
		return (ULONG)r;
	}
	HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPCDSBUFFERDESC d, LPDIRECTSOUNDBUFFER *out, IUnknown *) override
	{
		const WAVEFORMATEX *w = d->lpwfxFormat;
		if (!(d->dwFlags & DSBCAPS_PRIMARYBUFFER) && (d->dwBufferBytes < 4 || d->dwBufferBytes > 0x0FFFFFFF))
		{
			CallLog("CreateSoundBuffer ds=%08lX flags=%lx bytes=%lu tag=%u ch=%u rate=%lu bits=%u alg3d=%08lx out=00000000 hr=%lx",
					P(this), (unsigned long)d->dwFlags, (unsigned long)d->dwBufferBytes, w ? w->wFormatTag : 0, w ? w->nChannels : 0,
					w ? (unsigned long)w->nSamplesPerSec : 0UL, w ? w->wBitsPerSample : 0,
					d->dwSize >= sizeof(DSBUFFERDESC) ? (unsigned long)d->guid3DAlgorithm.Data1 : 0UL, (unsigned long)DSERR_INVALIDPARAM);
			return DSERR_INVALIDPARAM;
		}
		*out = reinterpret_cast<IDirectSoundBuffer *>(static_cast<IDirectSoundBuffer8 *>(Make<Buffer>(d, (Data *)NULL)));
		CallLog("CreateSoundBuffer ds=%08lX flags=%lx bytes=%lu tag=%u ch=%u rate=%lu bits=%u alg3d=%08lx out=%08lX hr=0", P(this),
				(unsigned long)d->dwFlags, (unsigned long)d->dwBufferBytes, w ? w->wFormatTag : 0, w ? w->nChannels : 0,
				w ? (unsigned long)w->nSamplesPerSec : 0UL, w ? w->wBitsPerSample : 0,
				d->dwSize >= sizeof(DSBUFFERDESC) ? (unsigned long)d->guid3DAlgorithm.Data1 : 0UL, P(*out));
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
		d.dwBufferBytes = s->Bytes();
		d.lpwfxFormat = &s->format;
		// a duplicate shares its source's memory (DirectSound's rule) and starts stopped at position 0
		*out = reinterpret_cast<IDirectSoundBuffer *>(static_cast<IDirectSoundBuffer8 *>(Make<Buffer>(&d, s->data)));
		CallLog("DuplicateSoundBuffer ds=%08lX src=%08lX new=%08lX hr=0", P(this), P(src), P(*out));
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD level) override
	{
		CallLog("SetCooperativeLevel ds=%08lX level=%lu hr=0", P(this), (unsigned long)level);
		return DS_OK;
	}
	HRESULT STDMETHODCALLTYPE Compact() override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD c) override { *c = 4; return DS_OK; }	// DSSPEAKER_STEREO
	HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE Initialize(const GUID *) override { return DSERR_ALREADYINITIALIZED; }
	HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD c) override { *c = 0; return DS_OK; }
};
} // namespace

extern "C" HRESULT WINAPI DirectSoundEnumerateA(LPDSENUMCALLBACKA cb, LPVOID ctx)
{
	cb(NULL, "Primary Sound Driver", "", ctx);
	return DS_OK;
}

extern "C" HRESULT WINAPI DirectSoundCreate8(const GUID *g, LPDIRECTSOUND8 *out, IUnknown *)
{
	*out = Make<Sound>();
	CallLog("DirectSoundCreate8 guid=%08lx out=%08lX hr=0", g ? (unsigned long)g->Data1 : 0UL, P(*out));
	return DS_OK;
}

extern "C" HRESULT WINAPI DirectSoundCreate(const GUID *g, LPDIRECTSOUND *out, IUnknown *u)
{
	return DirectSoundCreate8(g, (LPDIRECTSOUND8 *)out, u);
}
