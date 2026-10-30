// SPDX-License-Identifier: GPL-3.0-only
// The complete production DirectSound mixer with controlled SDL boundaries. No device, game data or real timer.
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
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
extern "C" void *SDLCALL SDL_malloc(size_t bytes)
{
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
	else return 1;
	std::printf("{\"case\":\"%s\",\"fixed\":%s,\"checks\":%u,\"mixed\":%llu,\"bounded_underflow\":%s}\n",
		mode, fixed ? "true" : "false", checks, (unsigned long long)gMixer.mixed, hitLimit ? "true" : "false");
	return 0;
}
