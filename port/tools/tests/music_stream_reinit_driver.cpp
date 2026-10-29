// SPDX-License-Identifier: GPL-3.0-only
// The startup's second music initialisation on the production music stream (src/PCMusicStream.cpp with every port
// patch that touches it, in number order). Each stream gets its own buffer, position events, worker and reader, so a
// buffer still playing after its stream was replaced, and a worker writing into another stream's buffer or reading its
// reader, are both visible. The buffers notify the way a looping DirectSound buffer does: only while playing. No
// audio device, game, Windows runtime or files.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>
#include <strings.h>

using DWORD = uint32_t;
using UINT = unsigned;
using BYTE = unsigned char;
using WORD = uint16_t;
using BOOL = int;
using HRESULT = int;
using LPVOID = void *;
using HANDLE = void *;
using LPTHREAD_START_ROUTINE = DWORD (*)(LPVOID);
constexpr BOOL FALSE = 0, TRUE = 1;
constexpr DWORD INFINITE = 0xffffffffu, WAIT_FAILED = 0xffffffffu, WAIT_TIMEOUT = 258, STILL_ACTIVE = 259;
constexpr DWORD DSBSTATUS_PLAYING = 1, DSBSTATUS_LOOPING = 4, DSBPLAY_LOOPING = 1, DSBLOCK_ENTIREBUFFER = 2;
constexpr DWORD DSBCAPS_LOCSOFTWARE = 8, DSBCAPS_CTRLPOSITIONNOTIFY = 256, DSBCAPS_CTRLVOLUME = 128;
constexpr WORD WAVE_FORMAT_PCM = 1;
constexpr int IID_IDirectSoundNotify = 0;
#define SUCCEEDED(x) ((x) >= 0)
#define FAILED(x) ((x) < 0)
#define ZeroMemory(p, n) memset((p), 0, (n))
#define _tcsicmp strcasecmp
#define _tcsncpy strncpy

static std::mutex lock_;						// a leaf lock: every fake below takes it alone
static std::condition_variable changed;
static unsigned checks;
static void Check(bool value, const char *what)
{
	++checks;
	if (!value) { fprintf(stderr, "FAIL %s\n", what); exit(2); }
}

// Events, in creation order: InitMusicStream makes a stream's first-half, second-half, quit and play events.
struct Event { bool signalled = false, closed = false; };
static std::vector<Event *> events;
static unsigned unknown_handles, closed_signals, closed_event_uses, buffer_released_live_worker, reader_deleted_live_worker;
static bool Known(HANDLE handle)
{
	for (Event *e : events) if (e == handle) return true;
	return false;
}
static HANDLE CreateEvent(void *, BOOL, BOOL initial, void *)
{
	std::lock_guard<std::mutex> hold(lock_);
	Event *e = new Event;
	e->signalled = initial;
	events.push_back(e);
	return e;
}
static BOOL SetEvent(HANDLE handle)
{
	std::lock_guard<std::mutex> hold(lock_);
	if (!Known(handle)) { ++unknown_handles; return FALSE; }
	if (static_cast<Event *>(handle)->closed) { ++closed_event_uses; return FALSE; }	// Win32: fails on a closed handle
	static_cast<Event *>(handle)->signalled = true;
	changed.notify_all();
	return TRUE;
}
static BOOL ResetEvent(HANDLE handle)
{
	std::lock_guard<std::mutex> hold(lock_);
	if (!Known(handle)) { ++unknown_handles; return FALSE; }	// retail's ResetEvent((HANDLE)PlayMusicStream): refused, never read
	if (static_cast<Event *>(handle)->closed) { ++closed_event_uses; return FALSE; }
	static_cast<Event *>(handle)->signalled = false;
	return TRUE;
}
static BOOL CloseHandle(HANDLE handle)
{
	std::lock_guard<std::mutex> hold(lock_);
	if (Known(handle)) static_cast<Event *>(handle)->closed = true;
	return TRUE;
}

// Workers: each remembers the stream whose InitMusicStream created it.
struct Worker
{
	std::thread thread;
	std::atomic<DWORD> exit{STILL_ACTIVE};
	int stream = 0;
	bool waiting = false;
	HANDLE *list = nullptr;
	DWORD count = 0;
	unsigned handled = 0;
};
static std::vector<Worker *> workers;
static thread_local Worker *tWorker;			// null on the main thread
struct IDirectSoundBuffer;
static std::vector<IDirectSoundBuffer *> buffers;
static DWORD WaitForMultipleObjects(DWORD count, HANDLE *list, BOOL, DWORD timeout)
{
	std::unique_lock<std::mutex> hold(lock_);
	for (;;)
	{
		for (DWORD i = 0; i < count; ++i)
		{
			Event *e = static_cast<Event *>(list[i]);
			if (e->signalled)
			{
				e->signalled = false;
				if (tWorker) { tWorker->waiting = false; ++tWorker->handled; }
				changed.notify_all();
				if (tWorker && i == 3)
				{
					hold.unlock();			// descheduled right after it took the quit wake: only a join makes the main thread wait
					std::this_thread::sleep_for(std::chrono::milliseconds(40));
				}
				return i;
			}
		}
		if (!timeout) return WAIT_TIMEOUT;
		if (tWorker) { tWorker->waiting = true; tWorker->list = list; tWorker->count = count; }
		changed.notify_all();
		changed.wait(hold);
	}
}
static DWORD GetLastError() { return 0; }
static void Sleep(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static HANDLE CreateThread(void *, unsigned, LPTHREAD_START_ROUTINE entry, void *arg, unsigned, DWORD *)
{
	Worker *w = new Worker;
	{
		std::lock_guard<std::mutex> hold(lock_);
		w->stream = (int)buffers.size();
		workers.push_back(w);
	}
	w->thread = std::thread([=] {
		tWorker = w;
		DWORD code = entry(arg);
		std::lock_guard<std::mutex> hold(lock_);
		w->exit = code;
		changed.notify_all();
	});
	return w;
}
static BOOL GetExitCodeThread(HANDLE handle, DWORD *code)
{
	*code = static_cast<Worker *>(handle)->exit;
	return TRUE;
}

struct CRITICAL_SECTION { std::recursive_mutex mutex; bool ready = false; };
static unsigned cs_inits, cs_deletes, cs_reinits;
static void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
	if (cs->ready) ++cs_reinits;				// undefined on Windows; counted here, the lock left as it is
	cs->ready = true;
	++cs_inits;
}
static void DeleteCriticalSection(CRITICAL_SECTION *cs) { Check(cs->ready, "destroy an initialised lock"); cs->ready = false; ++cs_deletes; }
static void EnterCriticalSection(CRITICAL_SECTION *cs) { if (!cs->ready) abort(); cs->mutex.lock(); }
static void LeaveCriticalSection(CRITICAL_SECTION *cs) { if (!cs->ready) abort(); cs->mutex.unlock(); }
#include CE_ADMISSION_SOURCE

static bool virtual_clock;
bool PortClockVirtual() { return virtual_clock; }

struct WAVEFORMATEX { WORD wFormatTag, nChannels; DWORD nSamplesPerSec, nAvgBytesPerSec; WORD nBlockAlign, wBitsPerSample, cbSize; };
struct DSBUFFERDESC { DWORD dwSize, dwFlags, dwBufferBytes; WAVEFORMATEX *lpwfxFormat; };
struct DSBPOSITIONNOTIFY { DWORD dwOffset; HANDLE hEventNotify; };
static unsigned max_playing, foreign_locks, foreign_reads;
static unsigned PlayingLocked();
struct IDirectSoundNotify
{
	IDirectSoundBuffer *owner;
	void SetNotificationPositions(unsigned n, DSBPOSITIONNOTIFY *positions);
	void Release() {}
};
struct IDirectSoundBuffer
{
	int stream = 0;								// 1-based creation order
	BYTE data[0x80000]{};
	bool playing = false, released = false;
	unsigned plays = 0, stops = 0, releases = 0, next_half = 0;
	HANDLE half[2]{};							// the events notified at the middle and at the end
	IDirectSoundNotify notify{this};
	HRESULT GetStatus(DWORD *status)
	{
		std::lock_guard<std::mutex> hold(lock_);
		*status = playing ? DSBSTATUS_PLAYING | DSBSTATUS_LOOPING : 0;
		return 0;
	}
	HRESULT Lock(DWORD offset, DWORD size, void **p1, DWORD *n1, void **p2, DWORD *n2, DWORD flags)
	{
		{
			std::lock_guard<std::mutex> hold(lock_);
			if (tWorker && tWorker->stream != stream) ++foreign_locks;
		}
		*p1 = data + offset;
		*n1 = flags & DSBLOCK_ENTIREBUFFER ? sizeof data : size;
		*p2 = nullptr;
		*n2 = 0;
		return 0;
	}
	HRESULT Unlock(void *, DWORD, void *, DWORD) { return 0; }
	HRESULT SetCurrentPosition(DWORD) { return 0; }
	HRESULT Play(DWORD, DWORD, DWORD)
	{
		std::lock_guard<std::mutex> hold(lock_);
		++plays;
		playing = true;
		next_half = 0;
		unsigned now = PlayingLocked();
		if (now > max_playing) max_playing = now;
		changed.notify_all();
		return 0;
	}
	HRESULT Stop()
	{
		std::lock_guard<std::mutex> hold(lock_);
		++stops;
		playing = false;
		changed.notify_all();
		return 0;
	}
	HRESULT SetVolume(int) { return 0; }
	HRESULT QueryInterface(int, void **out) { *out = &notify; return 0; }
	void Release()
	{
		std::lock_guard<std::mutex> hold(lock_);
		if (stream - 1 < (int)workers.size() && workers[stream - 1]->exit == STILL_ACTIVE) ++buffer_released_live_worker;
		++releases;
		released = true;						// the last reference: the buffer and its sound are gone
		playing = false;
		changed.notify_all();
	}
};
void IDirectSoundNotify::SetNotificationPositions(unsigned n, DSBPOSITIONNOTIFY *positions)
{
	for (unsigned i = 0; i < n && i < 2; ++i) owner->half[i] = positions[i].hEventNotify;
}
static unsigned PlayingLocked()
{
	unsigned n = 0;
	for (IDirectSoundBuffer *b : buffers) if (b->playing && !b->released) ++n;
	return n;
}

static unsigned readers_created, reader_deletes;
struct CBaseWaveFileRead
{
	int stream = 0;
	char filename[256]{};
	bool open = false;
	CBaseWaveFileRead() { std::lock_guard<std::mutex> hold(lock_); stream = (int)buffers.size(); ++readers_created; }
	~CBaseWaveFileRead()
	{
		std::lock_guard<std::mutex> hold(lock_);
		if (stream - 1 < (int)workers.size() && workers[stream - 1]->exit == STILL_ACTIVE) ++reader_deleted_live_worker;
		++reader_deletes;
	}
	void Close() { open = false; }
	void Open(char *name) { strncpy(filename, name, sizeof filename - 1); open = true; }
	bool HasFormat() { return open; }
	unsigned GetChannels() { return 2; }
	unsigned GetSampleRate() { return 44100; }
	HRESULT Read(UINT size, BYTE *data, UINT *got)
	{
		{
			std::lock_guard<std::mutex> hold(lock_);
			if (tWorker && tWorker->stream != stream) ++foreign_reads;
		}
		*got = open ? size : 0;					// a long track: it never ends inside this scenario
		memset(data, 0x20 + stream, size);
		return 0;
	}
};
using COggFileRead = CBaseWaveFileRead;
struct DirectSound
{
	HRESULT CreateSoundBuffer(DSBUFFERDESC *, IDirectSoundBuffer **out, void *)
	{
		IDirectSoundBuffer *b = new IDirectSoundBuffer;
		std::lock_guard<std::mutex> hold(lock_);
		buffers.push_back(b);
		b->stream = (int)buffers.size();
		*out = b;
		return 0;
	}
};
struct Sound { DirectSound *GetDirectSound() { static DirectSound ds; return &ds; } } SOUND;

// The mixer's side (port/dsound/dsound.cpp): identity tags and a snapshot, no mixing.
bool PortAudioMediaRole(const void *, PortSessionCategory) { return true; }
static unsigned long long fake_ns;
bool PortAudioStreamSnapshot(IDirectSoundBuffer *b, unsigned long long *ns, unsigned long long *mixed, unsigned *voices,
							 unsigned *play)
{
	std::lock_guard<std::mutex> hold(lock_);
	*ns = fake_ns += 1000000;
	*mixed = 0;
	*voices = PlayingLocked();
	*play = 0;
	return b && b->playing && !b->released;
}
void PortAudioPublishMusicHalf(void *destination, const void *decoded, DWORD bytes) { memcpy(destination, decoded, bytes); }

#include MUSIC_SOURCE

// Every live worker waits with nothing signalled for it: the wakes so far are all handled.
static void Settle()
{
	std::unique_lock<std::mutex> hold(lock_);
	bool settled = changed.wait_for(hold, std::chrono::seconds(5), [] {
		for (Worker *w : workers)
		{
			if (w->exit != STILL_ACTIVE) continue;
			if (!w->waiting) return false;
			for (DWORD i = 0; i < w->count; ++i)
				if (static_cast<Event *>(w->list[i])->signalled) return false;
		}
		return true;
	});
	if (!settled) { fprintf(stderr, "timed out waiting for the workers\n"); exit(3); }
}
// Half a buffer plays: every buffer still playing reaches its next notification position.
static void Tick()
{
	{
		std::lock_guard<std::mutex> hold(lock_);
		for (IDirectSoundBuffer *b : buffers)
		{
			if (!b->playing || b->released || !b->half[b->next_half]) continue;
			Event *e = static_cast<Event *>(b->half[b->next_half]);
			if (e->closed) ++closed_signals;
			else e->signalled = true;
			b->next_half ^= 1;
		}
		changed.notify_all();
	}
	if (virtual_clock) PumpMusicStream();		// the game polls the stream every frame (CPCMusic::DeviceGetTrackFinished)
	Settle();
}
static unsigned Playing() { std::lock_guard<std::mutex> hold(lock_); return PlayingLocked(); }

int main(int argc, char **argv)
{
	if (argc != 2) return 1;
	const char *mode = argv[1];					// ce | faithful | virtual | private
	virtual_clock = !strcmp(mode, "virtual");
	if (!strcmp(mode, "faithful")) setenv("BEA_PORT_TAPE", "synthetic-admission-control", 1);
	if (virtual_clock) setenv("BEA_PORT_CLOCK", "present", 1);
	char track[] = "data\\music\\synthetic-frontend.ogg";

	// WinMain: the options load re-initialises the sound (ReinitialiseSound(true): SOUND.Reinitialise, then
	// MUSIC.PlaySelection(MUS_FRONTEND)) before the window exists.
	InitMusicStream();
	Settle();
	PlayMusicStream(track);
	Settle();
	Check(buffers.size() == 1 && buffers[0]->playing, "the startup re-initialise starts the front end's track");
	Tick();
	Tick();
	// CSystem::Init: SOUND.Init finds the device up, then MUSIC.Initialise.
	InitMusicStream();
	Settle();
	Check(buffers.size() == 2, "the second initialisation makes a second stream");
	bool first_playing_after_second_init = Playing() && buffers[0]->playing && !buffers[0]->released;
	unsigned first_released_at_second_init = buffers[0]->releases;
	bool first_worker_alive_at_second_init = !workers.empty() && workers[0]->exit == STILL_ACTIVE;
	Tick();										// the opening films: a first buffer still looping goes on notifying
	Tick();
	// The front end: MUSIC.PlaySelection(MUS_FRONTEND).
	PlayMusicStream(track);
	Settle();
	Check(buffers[1]->playing, "the front end starts its track on the second stream");
	for (int i = 0; i < 4; ++i)
		Tick();
	unsigned playing_at_front_end = Playing();
	bool second_playing_at_front_end = buffers[1]->playing && !buffers[1]->released;

	// The game's exit: MUSIC.Shutdown, then any worker left behind is told to quit so the process can end.
	ShutdownMusicStream();
	for (size_t i = 0; i < events.size(); i += 4)
	{
		if (i / 4 < workers.size() && workers[i / 4]->exit == STILL_ACTIVE)
		{
			std::lock_guard<std::mutex> hold(lock_);
			events[i + 2]->signalled = true;	// that stream's own quit event
			changed.notify_all();
		}
	}
	for (Worker *w : workers)
		if (w->thread.joinable()) w->thread.join();
	Check(!sBuffer && !sReader, "shutdown clears the stream owners");

	unsigned events_open = 0;
	for (Event *e : events) if (!e->closed) ++events_open;
	printf("{\"buffer_released_live_worker\":%u,\"reader_deleted_live_worker\":%u,\"closed_event_uses\":%u,\"events_open\":%u,"
		   "\"mode\":\"%s\",\"checks\":%u,\"buffers\":%zu,\"workers\":%zu,\"max_playing\":%u,"
		   "\"first_playing_after_second_init\":%s,\"first_released_at_second_init\":%u,"
		   "\"first_worker_alive_at_second_init\":%s,\"playing_at_front_end\":%u,\"second_playing_at_front_end\":%s,"
		   "\"first_plays\":%u,\"second_plays\":%u,\"first_releases\":%u,\"second_releases\":%u,"
		   "\"foreign_locks\":%u,\"foreign_reads\":%u,\"readers_created\":%u,\"reader_deletes\":%u,"
		   "\"locks_initialized\":%u,\"locks_destroyed\":%u,\"locks_reinitialized\":%u,\"unknown_handles\":%u,"
		   "\"closed_signals\":%u}\n",
		   buffer_released_live_worker, reader_deleted_live_worker, closed_event_uses, events_open,
		   mode, checks, buffers.size(), workers.size(), max_playing,
		   first_playing_after_second_init ? "true" : "false", first_released_at_second_init,
		   first_worker_alive_at_second_init ? "true" : "false", playing_at_front_end,
		   second_playing_at_front_end ? "true" : "false", buffers[0]->plays, buffers[1]->plays, buffers[0]->releases,
		   buffers[1]->releases, foreign_locks, foreign_reads, readers_created, reader_deletes, cs_inits, cs_deletes,
		   cs_reinits, unknown_handles, closed_signals);
	return 0;
}
