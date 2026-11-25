// SPDX-License-Identifier: GPL-3.0-only
// The complete production DirectSound mixer with controlled SDL boundaries. No device, game data or real timer.
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#ifdef DSOUND_HARDENING_TESTS
static bool failObjectMalloc, failBackingCalloc, failScratchMalloc;
static void *TestObjectMalloc(size_t n) { return failObjectMalloc ? nullptr : std::malloc(n); }
static void *TestBackingCalloc(size_t c, size_t s) { return failBackingCalloc ? nullptr : std::calloc(c, s); }
static void *TestScratchMalloc(size_t n) { return failScratchMalloc ? nullptr : std::malloc(n); }
#define DSOUND_OBJECT_MALLOC TestObjectMalloc
#define DSOUND_BACKING_CALLOC TestBackingCalloc
#define DSOUND_SCRATCH_MALLOC TestScratchMalloc
#endif
#include MIXER_SOURCE

struct SDL_Mutex { std::recursive_mutex value; };
struct SDL_AudioStream { bool resumed = false; };
struct Timer { SDL_TimerID id; SDL_TimerCallback callback; void *data; bool canceled; };
static std::vector<Timer> timers;
static SDL_TimerID nextTimer = 1;
static std::atomic<Uint64> realNow{1000000000ull}, virtualNow{1000000000ull};
static bool virtualMode, resumeFails, initFails, openFails;
static unsigned opens, destroys, audioInits, audioQuits, checks, notifications, submissions;
static std::vector<Sint16> submitted;
static int putLimit;
static bool hitLimit;
struct MixLimit {};
static thread_local int role, lockDepth;
static std::mutex gateMutex;
static std::condition_variable gateCv;
static bool holdBeforeLock, atLock, releaseLock;
static bool dispatchDuringAdd;
static bool holdAllocate, atAllocate, releaseAllocate, stopAtLock;
#ifdef AUDIO_MEDIA_CONCURRENCY
// Relaxed stages schedule an overlap without creating a happens-before edge for the accesses under test.
static std::atomic<int> mediaRaceStage{0};
#endif
static std::thread earlyDispatch;
static Uint32 earlyResult;
static bool earlyDone;

static void Check(bool value, const char *why)
{
	++checks;
	if (!value) { std::fprintf(stderr, "FAIL %s\n", why); std::exit(2); }
}
static void AwaitLock()
{
	std::unique_lock<std::mutex> lock(gateMutex);
	Check(gateCv.wait_for(lock, std::chrono::seconds(3), [] { return atLock; }), "callback reached lock boundary");
}
static void ReleaseLockBoundary()
{
	std::lock_guard<std::mutex> lock(gateMutex);
	releaseLock = true;
	gateCv.notify_all();
}
extern "C" SDL_Mutex *SDLCALL SDL_CreateMutex() { return new SDL_Mutex; }
extern "C" void SDLCALL SDL_LockMutex(SDL_Mutex *mutex)
{
	if (role == 3)
	{
		std::lock_guard<std::mutex> lock(gateMutex);
		stopAtLock = true; gateCv.notify_all();
	}
	if (role == 2)
	{
		std::lock_guard<std::mutex> lock(gateMutex);
		atLock = true; gateCv.notify_all();
	}
	if (role == 1 && holdBeforeLock)
	{
		std::unique_lock<std::mutex> lock(gateMutex);
		atLock = true; gateCv.notify_all();
		gateCv.wait(lock, [] { return releaseLock; });
	}
#ifdef AUDIO_MEDIA_CONCURRENCY
	if (role == 43 && mediaRaceStage.load(std::memory_order_relaxed) == 2)
		mediaRaceStage.store(3, std::memory_order_relaxed);
#endif
	mutex->value.lock(); ++lockDepth;
#ifdef AUDIO_MEDIA_CONCURRENCY
	if (role == 42)
	{
		mediaRaceStage.store(2, std::memory_order_relaxed);
		while (mediaRaceStage.load(std::memory_order_relaxed) < 3) std::this_thread::yield();
	}
#endif
}
extern "C" void SDLCALL SDL_UnlockMutex(SDL_Mutex *mutex) { --lockDepth; mutex->value.unlock(); }
extern "C" Uint64 SDLCALL SDL_GetTicksNS() { return realNow.load(); }
static size_t maxSdlMalloc;
extern "C" void *SDLCALL SDL_malloc(size_t bytes)
{
	if (bytes > maxSdlMalloc) maxSdlMalloc = bytes;
	if (role == 1 && holdAllocate)
	{
		std::unique_lock<std::mutex> lock(gateMutex);
		atAllocate = true; gateCv.notify_all();
		gateCv.wait(lock, [] { return releaseAllocate; });
	}
	return std::malloc(bytes);
}
extern "C" void SDLCALL SDL_free(void *p) { std::free(p); }
extern "C" const char *SDLCALL SDL_getenv(const char *name) { return std::getenv(name); }
extern "C" int SDLCALL SDL_strcmp(const char *a, const char *b) { return std::strcmp(a, b); }
extern "C" const char *SDLCALL SDL_GetError() { return "controlled failure"; }
extern "C" bool SDLCALL SDL_InitSubSystem(SDL_InitFlags) { ++audioInits; return !initFails; }
extern "C" void SDLCALL SDL_QuitSubSystem(SDL_InitFlags) { ++audioQuits; }
extern "C" const char *SDLCALL SDL_GetCurrentAudioDriver() { return "controlled"; }
extern "C" SDL_AudioStream *SDLCALL SDL_OpenAudioDeviceStream(SDL_AudioDeviceID, const SDL_AudioSpec *, SDL_AudioStreamCallback, void *)
{
	++opens; return openFails ? nullptr : new SDL_AudioStream;
}
extern "C" bool SDLCALL SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream)
{
	if (resumeFails) return false;
	stream->resumed = true; return true;
}
extern "C" void SDLCALL SDL_DestroyAudioStream(SDL_AudioStream *stream)
{
#ifdef AUDIO_MEDIA_CONCURRENCY
	if (role == 43)
	{
		mediaRaceStage.store(1, std::memory_order_relaxed);
		while (mediaRaceStage.load(std::memory_order_relaxed) < 2) std::this_thread::yield();
	}
#endif
	++destroys; delete stream;
}
extern "C" SDL_AudioDeviceID SDLCALL SDL_GetAudioStreamDevice(SDL_AudioStream *) { return 7; }
extern "C" bool SDLCALL SDL_GetAudioDeviceFormat(SDL_AudioDeviceID, SDL_AudioSpec *spec, int *frames)
{
	*spec = {SDL_AUDIO_S16LE, 2, 44100}; *frames = 441; return true;
}
extern "C" const char *SDLCALL SDL_GetAudioDeviceName(SDL_AudioDeviceID) { return "controlled device"; }
extern "C" int SDLCALL SDL_GetAudioStreamQueued(SDL_AudioStream *) { return 12000; }
extern "C" bool SDLCALL SDL_ClearAudioStream(SDL_AudioStream *) { return true; }
extern "C" bool SDLCALL SDL_PutAudioStreamData(SDL_AudioStream *, const void *pcm, int bytes)
{
	++submissions;
	if (putLimit && submissions >= (unsigned)putLimit) throw MixLimit{};
	submitted.assign((const Sint16 *)pcm, (const Sint16 *)pcm + bytes / 2);
	return true;
}
extern "C" SDL_TimerID SDLCALL SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *data)
{
	SDL_TimerID id = nextTimer++;
	timers.push_back({id, callback, data, false});
	if (dispatchDuringAdd)
	{
		dispatchDuringAdd = false;
		realNow += 10000000;
		earlyDispatch = std::thread([=] {
			role = 2; earlyResult = callback(data, id, interval);
			std::lock_guard<std::mutex> lock(gateMutex); earlyDone = true; gateCv.notify_all();
		});
		AwaitLock();
		if (!lockDepth)
		{
			std::unique_lock<std::mutex> lock(gateMutex);
			Check(gateCv.wait_for(lock, std::chrono::seconds(3), [] { return earlyDone; }), "unlocked publication dispatch returned");
		}
	}
	return id;
}
extern "C" bool SDLCALL SDL_RemoveTimer(SDL_TimerID id)
{
	// Match SDL 3.4's cancellation: an already-dispatched callback is allowed to finish after this returns.
	for (auto &timer : timers) if (timer.id == id) { timer.canceled = true; return true; }
	return false;
}
extern "C" BOOL WINAPI SetEvent(HANDLE) { ++notifications; return TRUE; }
unsigned long long PortClockFrames() { return 0; }
bool PortClockVirtual() { return virtualMode; }
unsigned long long PortClockPeekNs() { return virtualNow.load(); }
void PortAudioGuardCheck() {}
bool PortSessionEnabled() { return true; }
bool PortDevLog() { return false; }
#ifdef AUDIO_MEDIA_CONCURRENCY
// Consume the actual arguments; an empty stub lets the optimizer remove the snapshot reads being checked.
__attribute__((noinline)) void PortSessionLog(PortSessionCategory, bool, const char *format, ...)
{
	char text[1024]; va_list args; va_start(args, format); vsnprintf(text, sizeof text, format, args); va_end(args);
	__asm__ __volatile__("" : : "r"(text[0]) : "memory");
}
#else
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}
#endif
void PortSessionAsset(PortSessionCategory, const char *, const char *, long) {}
void PortSessionMediaActive(PortSessionCategory, const void *, bool) {}
void PortSessionSetFinalMedia(const PortSessionFinalMedia *) {}
void PortSessionCount(PortSessionCounter) {}
void PortSessionObserve(PortSessionObservation, unsigned) {}
void PortSessionObserveMax(PortSessionObservation, unsigned) {}
void PortSessionAudioError(const char *) {}
void PortSessionSummary() {}
void PortSessionPulse(unsigned) {}

static Buffer *Voice()
{
	WAVEFORMATEX format{};
	format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2; format.nSamplesPerSec = 44100;
	format.wBitsPerSample = 16; format.nBlockAlign = 4; format.nAvgBytesPerSec = 176400;
	DSBUFFERDESC desc{}; desc.dwSize = sizeof desc; desc.dwFlags = DSBCAPS_CTRLVOLUME;
	desc.dwBufferBytes = 44100 * 4; desc.lpwfxFormat = &format;
	Buffer *b = Make<Buffer>(&desc, (Data *)nullptr);
	for (DWORD i = 0; i < b->Bytes() / 2; ++i) ((Sint16 *)b->data->bytesPtr)[i] = 10000;
	b->Play(0, 0, DSBPLAY_LOOPING);
	return b;
}
static Timer LatestTimer() { Check(!timers.empty(), "a timer was created"); return timers.back(); }
static Uint32 Tick(Timer timer) { return timer.callback(timer.data, timer.id, 10); }
static void Cleanup(Buffer *voice)
{
	voice->Stop(); voice->Release(); gMixer.Stop();
}
static void BoundCallback(Timer timer, bool direct)
{
	role = 1;
	try { if (direct) gMixer.CatchUp(); else Tick(timer); }
	catch (MixLimit &) {
		hitLimit = true;
		// The deliberately broken control never reaches the production unlock. Release only locks this test owns.
		while (lockDepth) SDL_UnlockMutex(gMixer.lock);
	}
}

int main(int argc, char **argv)
{
	if (argc != 3) return 1;
	const char *mode = argv[1]; bool fixed = !std::strcmp(argv[2], "fixed");
	virtualMode = !std::strcmp(mode, "virtual-reopen") || !std::strcmp(mode, "stale-catchup") || !std::strcmp(mode, "stale-timer");
	bool timerMode = !std::strcmp(mode, "normal-timer") || !std::strcmp(mode, "timer-reopen") || !std::strcmp(mode, "publish") || !std::strcmp(mode, "stop-inflight");
	if (timerMode) setenv("BEA_AUDIO_CLOCK", "timer", 1);
	if (!std::strcmp(mode, "stale-catchup") || !std::strcmp(mode, "stale-timer")) setenv("BEA_AUDIO_MONITOR", "1", 1);
	resumeFails = !std::strcmp(mode, "resume-failed");
	initFails = !std::strcmp(mode, "init-failed"); openFails = !std::strcmp(mode, "open-failed");
	if (!std::strcmp(mode, "publish")) dispatchDuringAdd = true;
	gMixer.Start();
#ifdef AUDIO_MEDIA_CONCURRENCY
	if (!std::strcmp(mode, "media-stop-snapshot"))
	{
		Buffer *b = Voice();
		Check(PortAudioMediaRole(static_cast<IDirectSoundBuffer8 *>(b), PORT_SESSION_MUSIC), "music buffer registered");
		std::thread stopper([&] { role = 43; gMixer.Stop(); });
		while (mediaRaceStage.load(std::memory_order_relaxed) < 1) std::this_thread::yield();
		std::thread snapshot([&] { role = 42; MediaSnapshot(b, "test"); });
		snapshot.join(); stopper.join();
		b->Stop(); b->Release();
	}
	else
#endif
	if (!std::strcmp(mode, "publish"))
	{
		earlyDispatch.join(); Check(earlyResult == 10, "new timer survives dispatch during publication");
		gMixer.Stop();
	}
	else if (!std::strcmp(mode, "stop-inflight"))
	{
		Buffer *b = Voice(); Timer old = LatestTimer(); holdAllocate = true; realNow += 10000000;
		std::thread worker([=] { role = 1; Tick(old); });
		{
			std::unique_lock<std::mutex> lock(gateMutex);
			Check(gateCv.wait_for(lock, std::chrono::seconds(3), [] { return atAllocate; }), "callback reached production allocation");
		}
		std::atomic<bool> stopped{false};
		std::thread stopper([&] { role = 3; gMixer.Stop(); std::lock_guard<std::mutex> lock(gateMutex); stopped = true; gateCv.notify_all(); });
		{
			std::unique_lock<std::mutex> lock(gateMutex);
			Check(gateCv.wait_for(lock, std::chrono::seconds(3), [] { return stopAtLock; }), "Stop reached production lock");
			if (fixed) Check(!stopped, "Stop cannot retire a callback holding the mixer lock");
			else Check(gateCv.wait_for(lock, std::chrono::seconds(3), [&] { return stopped.load(); }), "negative control returns before callback owns lock");
			releaseAllocate = true; gateCv.notify_all();
		}
		worker.join(); stopper.join(); Check(gMixer.mixed == 441, "one in-flight callback completed");
		b->Stop(); b->Release();
	}
	else if (!std::strcmp(mode, "normal-callback"))
	{
		Check(gMixer.stream && !gMixer.timer, "ordinary device callback is selected"); Buffer *b = Voice();
		Feed(nullptr, gMixer.stream, 400, 400);
		Check(gMixer.mixed == 100 && b->cursor == 100 && submitted.size() == 200, "ordinary PCM/cursor/submission unchanged");
		Check(submitted[0] >= 9999 && submitted[0] <= 10000, "ordinary PCM content unchanged"); Cleanup(b);
	}
	else if (timerMode || resumeFails || initFails || openFails)
	{
		bool hasTimer = gMixer.timer != 0;
		if (resumeFails) Check(hasTimer == fixed && (gMixer.stream == nullptr) == fixed, "resume failure uses timer only after releasing stream");
		else Check(hasTimer && !gMixer.stream, "explicit timer/init/open fallback is selected");
		Buffer *b = Voice();
		if (hasTimer)
		{
			Timer old = LatestTimer(); realNow += 10000000; Tick(old); Check(b->cursor == 441, "timer progresses first10ms");
			if (!std::strcmp(mode, "timer-reopen"))
			{
				realNow += 10000000;
				holdBeforeLock = true; std::thread worker([=] { BoundCallback(old, false); }); AwaitLock();
				b->Stop(); b->Release(); gMixer.Stop(); realNow += 100000000; gMixer.Start(); b = Voice();
				Uint64 before = gMixer.mixed; ReleaseLockBoundary(); worker.join();
				Check((gMixer.mixed == before) == fixed && (b->cursor == 0) == fixed, "retired timer cannot mix a reopened voice");
				Check(timers[0].canceled, "actual old timer was canceled");
			}
		}
		Cleanup(b);
		if (resumeFails) Check(destroys == 1 && audioQuits == 1, "failed or stopped stream is destroyed/quit exactly once");
	}
	else if (virtualMode)
	{
		Buffer *b = Voice(); virtualNow += 2000000000; gMixer.CatchUp(); Check(gMixer.mixed == 88200, "virtual initial2s mixed");
		if (!std::strcmp(mode, "virtual-reopen"))
		{
			b->Stop(); b->Release(); gMixer.Stop(); gMixer.Start(); b = Voice(); virtualNow += 10000000; gMixer.CatchUp();
			Check((b->cursor == 441) == fixed && gMixer.mixed == (fixed ? 88641u : 88200u), "reopened virtual clock advances immediately");
		}
		else
		{
			Timer old = LatestTimer(); holdBeforeLock = true;
			std::thread worker([=] { BoundCallback(old, !std::strcmp(mode, "stale-catchup")); }); AwaitLock();
			b->Stop(); b->Release(); gMixer.Stop(); virtualNow += 100000000; gMixer.Start(); b = Voice();
			Uint64 before = gMixer.mixed; submissions = 0; putLimit = 16; ReleaseLockBoundary(); worker.join();
			Check(hitLimit != fixed && (gMixer.mixed == before) == fixed, "old now cannot underflow a reopened virtual base");
		}
		Cleanup(b);
	}
#ifdef DSOUND_HARDENING_TESTS
	else if (!std::strcmp(mode, "buffer-validation"))
	{
		IDirectSound8 *ds = nullptr;
		Check(DirectSoundCreate8(nullptr, &ds, nullptr) == DS_OK && ds, "DirectSoundCreate8 succeeds");
		WAVEFORMATEX wf{};
		wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = 44100;
		wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = 176400;
		DSBUFFERDESC bd{}; bd.dwSize = sizeof bd; bd.dwFlags = DSBCAPS_CTRLVOLUME;
		bd.dwBufferBytes = 4096; bd.lpwfxFormat = &wf;

		int usersBefore = gMixer.users;
		IDirectSoundBuffer *out = (IDirectSoundBuffer *)0x1;
		failBackingCalloc = true;
		Check(ds->CreateSoundBuffer(&bd, &out, nullptr) == DSERR_OUTOFMEMORY && out == nullptr, "backing OOM returns DSERR_OUTOFMEMORY and nulls output");
		Check(gMixer.users == usersBefore, "backing OOM leaves mixer user count untouched");
		failBackingCalloc = false;

		out = (IDirectSoundBuffer *)0x1;
		failObjectMalloc = true;
		Check(ds->CreateSoundBuffer(&bd, &out, nullptr) == DSERR_OUTOFMEMORY && out == nullptr, "object OOM returns DSERR_OUTOFMEMORY and nulls output");
		failObjectMalloc = false;

		// Invalid PCM block alignment, channel count, bit depth, and format tag must be rejected before Sample() can OOB.
		wf.nBlockAlign = 2; // mismatched for 2ch * 16-bit (expects 4)
		Check(FAILED(ds->CreateSoundBuffer(&bd, &out, nullptr)) && out == nullptr, "mismatched nBlockAlign rejected");
		wf.nBlockAlign = 4; wf.nChannels = 3;
		Check(FAILED(ds->CreateSoundBuffer(&bd, &out, nullptr)) && out == nullptr, "3-channel PCM rejected");
		wf.nChannels = 2; wf.wBitsPerSample = 24; wf.nBlockAlign = 6;
		Check(FAILED(ds->CreateSoundBuffer(&bd, &out, nullptr)) && out == nullptr, "24-bit PCM rejected");
		wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.wFormatTag = 3; // IEEE float
		Check(FAILED(ds->CreateSoundBuffer(&bd, &out, nullptr)) && out == nullptr, "non-PCM tag rejected");

		// Primary buffer SetFormat also validates block alignment.
		DSBUFFERDESC pbd{}; pbd.dwSize = sizeof pbd; pbd.dwFlags = DSBCAPS_PRIMARYBUFFER;
		IDirectSoundBuffer *prim = nullptr;
		Check(ds->CreateSoundBuffer(&pbd, &prim, nullptr) == DS_OK && prim, "primary buffer created");
		wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.wBitsPerSample = 16; wf.nBlockAlign = 1;
		Check(FAILED(prim->SetFormat(&wf)), "primary SetFormat rejects mismatched nBlockAlign");
		prim->Release();
		ds->Release();
		gMixer.Stop();
	}
	else if (!std::strcmp(mode, "spatial-and-phase"))
	{
		IDirectSound8 *ds = nullptr;
		Check(DirectSoundCreate8(nullptr, &ds, nullptr) == DS_OK && ds, "DirectSoundCreate8");
		DSBUFFERDESC pbd{}; pbd.dwSize = sizeof pbd; pbd.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
		IDirectSoundBuffer *prim = nullptr;
		Check(ds->CreateSoundBuffer(&pbd, &prim, nullptr) == DS_OK && prim, "primary 3D buffer");
		IDirectSound3DListener8 *lis = nullptr;
		Check(prim->QueryInterface(IID_IDirectSound3DListener, (void **)&lis) == DS_OK && lis, "3D listener");

		WAVEFORMATEX wf{};
		wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1; wf.nSamplesPerSec = 44100;
		wf.wBitsPerSample = 16; wf.nBlockAlign = 2; wf.nAvgBytesPerSec = 88200;
		DSBUFFERDESC bd{}; bd.dwSize = sizeof bd; bd.dwFlags = DSBCAPS_CTRL3D | DSBCAPS_CTRLFREQUENCY;
		bd.dwBufferBytes = 44100 * 2; bd.lpwfxFormat = &wf;
		IDirectSoundBuffer *sb = nullptr;
		Check(ds->CreateSoundBuffer(&bd, &sb, nullptr) == DS_OK && sb, "3D secondary buffer");
		IDirectSound3DBuffer8 *b3d = nullptr;
		Check(sb->QueryInterface(IID_IDirectSound3DBuffer, (void **)&b3d) == DS_OK && b3d, "3D buffer interface");

		// Nonfinite rejections across immediate and deferred setters.
		for (DWORD apply : {(DWORD)DS3D_IMMEDIATE, (DWORD)DS3D_DEFERRED})
		{
			Check(b3d->SetPosition(NAN, 0.f, 0.f, apply) == DSERR_INVALIDPARAM, "SetPosition rejects NaN");
			Check(b3d->SetVelocity(0.f, INFINITY, 0.f, apply) == DSERR_INVALIDPARAM, "SetVelocity rejects Inf");
			Check(b3d->SetMinDistance(NAN, apply) == DSERR_INVALIDPARAM, "SetMinDistance rejects NaN");
			Check(b3d->SetMaxDistance(-1.f, apply) == DSERR_INVALIDPARAM, "SetMaxDistance rejects <= 0");
			Check(lis->SetPosition(NAN, 0.f, 0.f, apply) == DSERR_INVALIDPARAM, "Listener SetPosition rejects NaN");
			Check(lis->SetVelocity(0.f, -INFINITY, 0.f, apply) == DSERR_INVALIDPARAM, "Listener SetVelocity rejects -Inf");
			Check(lis->SetOrientation(NAN, 0.f, 1.f, 0.f, 1.f, 0.f, apply) == DSERR_INVALIDPARAM, "Listener SetOrientation rejects NaN");
			Check(lis->SetDistanceFactor(0.f, apply) == DSERR_INVALIDPARAM, "Listener SetDistanceFactor rejects <= 0");
			Check(lis->SetRolloffFactor(NAN, apply) == DSERR_INVALIDPARAM, "Listener SetRolloffFactor rejects NaN");
			Check(lis->SetDopplerFactor(-1.f, apply) == DSERR_INVALIDPARAM, "Listener SetDopplerFactor rejects < 0");
		}

		// Supersonic listener + source velocities and inverted maxD < minD must keep finite positive step and bounded gains.
		Check(b3d->SetPosition(10.f, 0.f, 0.f, DS3D_IMMEDIATE) == DS_OK, "source at +10x");
		Check(b3d->SetMaxDistance(2.f, DS3D_IMMEDIATE) == DS_OK, "maxD = 2");
		Check(b3d->SetMinDistance(10.f, DS3D_IMMEDIATE) == DS_OK, "minD = 10 > maxD");
		Check(b3d->SetVelocity(-1.0e6f, 0.f, 0.f, DS3D_IMMEDIATE) == DS_OK, "extreme source velocity");
		Check(lis->SetVelocity(1.0e6f, 0.f, 0.f, DS3D_IMMEDIATE) == DS_OK, "extreme listener velocity");

		Buffer *raw = static_cast<Buffer *>(sb);
		for (DWORD i = 0; i < raw->Bytes() / 2; ++i) ((Sint16 *)raw->data->bytesPtr)[i] = 16000;
		float g[2] = {0.f, 0.f}; double step = 0.0;
		raw->Gains(g, &step);
		Check(std::isfinite(g[0]) && std::isfinite(g[1]) && g[0] >= 0.f && g[0] <= 1.001f && g[1] >= 0.f && g[1] <= 1.001f, "inverted maxD<minD keeps gains <= 1");
		Check(std::isfinite(step) && step > 0.0, "supersonic listener/source Doppler stays positive and finite");

		// Negative fractional cursor must clamp gKernel phase cleanly without OOB or (DWORD) underflow.
		Check(sb->Play(0, 0, DSBPLAY_LOOPING) == DS_OK, "play 3D voice");
		raw->cursor = -0.25;
		Feed(nullptr, gMixer.stream, 256, 256);
		Check(std::isfinite(raw->cursor) && raw->cursor >= 0.0, "negative fractional cursor mixes cleanly");

		sb->Stop(); b3d->Release(); sb->Release(); lis->Release(); prim->Release(); ds->Release();
		gMixer.Stop();
	}
	else if (!std::strcmp(mode, "scratch-oom"))
	{
		Buffer *b = Voice();
		Feed(nullptr, gMixer.stream, 400, 400); // 100 frames -> allocates scratchFrames = 100
		Check(gMixer.scratch != nullptr && gMixer.scratchFrames == 100, "initial 100-frame scratch allocated");
		float *savedScratch = gMixer.scratch;

		// Request 512 frames while scratch growth fails: old scratch & scratchFrames stay intact, output is silence, mixed advances!
		failScratchMalloc = true;
		Uint64 mixedBefore = gMixer.mixed;
		Feed(nullptr, gMixer.stream, 512 * 4, 512 * 4);
		failScratchMalloc = false;
		Check(gMixer.scratch == savedScratch && gMixer.scratchFrames == 100, "failed scratch growth preserves old scratch buffer and frame count");
		Check(gMixer.mixed == mixedBefore + 512, "Mix OOM still advances mixed count so CatchUpLocked never spins");
		bool allZero = !submitted.empty();
		for (Sint16 s : submitted) if (s != 0) allZero = false;
		Check(allZero, "Mix OOM outputs clean silence");

		// Subsequent 100-frame mix reuses preserved scratch and produces audio again.
		Feed(nullptr, gMixer.stream, 400, 400);
		Check(submitted[0] >= 9999, "subsequent <=100-frame mix succeeds with preserved scratch");
		Cleanup(b);
	}
	else if (!std::strcmp(mode, "timer-bounds"))
	{
		gMixer.Stop();
		setenv("BEA_AUDIO_CLOCK", "timer", 1);
		gMixer.Start();
		Buffer *b = Voice();
		Timer t = LatestTimer();
		realNow += 10000000ull;
		Tick(t);
		Uint64 afterFirst = gMixer.mixed;
		Check(afterFirst == 441, "initial 10ms timer tick mixed 441 frames");

		// Backward clock jump must not wrap unsigned delta.
		realNow -= 5000000ull;
		Check(Tick(t) == 10 && gMixer.mixed == afterFirst, "backward clock jump mixes 0 frames and returns interval");

		// 4-hour suspend jump must cap backlog and chunk SDL_malloc <= kRate * 4 bytes.
		maxSdlMalloc = 0;
		realNow += 4ull * 3600ull * 1000000000ull;
		Check(Tick(t) == 10, "4-hour jump returns interval without hanging");
		Uint64 deltaMixed = gMixer.mixed - afterFirst;
		Check(deltaMixed > 0 && deltaMixed <= 88200ull, "suspend backlog capped to <= 2s");
		Check(maxSdlMalloc > 0 && maxSdlMalloc <= 44100u * 4u, "TimerTick chunks SDL_malloc to <= kRate frames");
		Cleanup(b);
	}
#endif
	else return 1;
	std::printf("{\"case\":\"%s\",\"fixed\":%s,\"checks\":%u,\"mixed\":%llu,\"bounded_underflow\":%s}\n",
		mode, fixed ? "true" : "false", checks, (unsigned long long)gMixer.mixed, hitLimit ? "true" : "false");
	return 0;
}
