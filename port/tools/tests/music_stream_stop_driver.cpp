// SPDX-License-Identifier: GPL-3.0-only
// Production worker translation unit, with deterministic event/reader/buffer boundaries. No audio device or files.
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
#define ZeroMemory(p, n) memset((p), 0, (n))
#define _tcsicmp strcasecmp
#define _tcsncpy strncpy

struct Event { bool signalled = false; };
static std::mutex event_mutex;
static std::condition_variable event_cv;
static std::vector<Event *> events;
static bool hold_dispatch, waiting, hold_read, reading, empty_read, hold_wake, wake_consumed;
static int wake_index;
static unsigned handled, invalid_resets, checks, wait_entries, reads, releases, reader_deletes, joins_polled;
static std::atomic<unsigned> plays{0}, stops{0};
static std::atomic<bool> buffer_playing{false};
static std::atomic<unsigned> last_play_marker{0};
static std::atomic<int> last_volume{1};
static bool virtual_clock, ce_layer = true, no_sound, fail_buffer;
static unsigned cs_inits, cs_deletes;
static std::atomic<unsigned> cs_waiters{0};
static void Check(bool value, const char *what);
struct CRITICAL_SECTION { std::recursive_mutex mutex; bool ready = false; };
static void InitializeCriticalSection(CRITICAL_SECTION *cs) { Check(!cs->ready, "initialize once"); cs->ready = true; ++cs_inits; }
static void DeleteCriticalSection(CRITICAL_SECTION *cs) { Check(cs->ready, "destroy initialized lock"); cs->ready = false; ++cs_deletes; }
static void EnterCriticalSection(CRITICAL_SECTION *cs)
{
    if (!cs->ready) abort();
    ++cs_waiters; event_cv.notify_all(); cs->mutex.lock(); --cs_waiters;
}
static void LeaveCriticalSection(CRITICAL_SECTION *cs) { if (!cs->ready) abort(); cs->mutex.unlock(); }
#include CE_ADMISSION_SOURCE

static void Check(bool value, const char *what)
{
    ++checks;
    if (!value) { fprintf(stderr, "FAIL %s\n", what); exit(2); }
}
template<class Predicate> static void Await(Predicate predicate)
{
    std::unique_lock<std::mutex> lock(event_mutex);
    if (!event_cv.wait_for(lock, std::chrono::seconds(3), predicate)) { fprintf(stderr, "timed out\n"); exit(3); }
}
static HANDLE CreateEvent(void *, BOOL, BOOL initial, void *)
{
    Event *e = new Event;
    e->signalled = initial;
    events.push_back(e);
    return e;
}
static BOOL SetEvent(HANDLE event)
{
    std::lock_guard<std::mutex> lock(event_mutex);
    static_cast<Event *>(event)->signalled = true;
    event_cv.notify_all();
    return TRUE;
}
static BOOL ResetEvent(HANDLE event)
{
    std::lock_guard<std::mutex> lock(event_mutex);
    for (Event *e : events) if (e == event) { e->signalled = false; return TRUE; }
    ++invalid_resets; // A function address is not a valid event; refuse without dereferencing it.
    return FALSE;
}
static DWORD WaitForMultipleObjects(DWORD count, HANDLE *list, BOOL, DWORD timeout)
{
    std::unique_lock<std::mutex> lock(event_mutex);
    waiting = true;
    ++wait_entries;
    event_cv.notify_all();
    for (;;)
    {
        if (!hold_dispatch)
            for (DWORD i = 0; i < count; ++i)
                if (static_cast<Event *>(list[i])->signalled)
                {
                    static_cast<Event *>(list[i])->signalled = false;
                    ++handled;
                    if ((int)i == wake_index && hold_wake)
                    {
                        wake_consumed = true;
                        event_cv.notify_all();
                        event_cv.wait(lock, [] { return !hold_wake; });
                    }
                    event_cv.notify_all();
                    return i;
                }
        if (!timeout) return WAIT_TIMEOUT;
        event_cv.wait(lock);
    }
}
static DWORD GetLastError() { return 0; }
static void Sleep(unsigned n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }
struct Thread { std::thread worker; std::atomic<DWORD> exit{STILL_ACTIVE}; };
static Thread *thread_owner;
static HANDLE CreateThread(void *, unsigned, LPTHREAD_START_ROUTINE entry, void *arg, unsigned, DWORD *)
{
    thread_owner = new Thread;
    Thread *t = thread_owner;
    t->worker = std::thread([=] { t->exit = entry(arg); event_cv.notify_all(); });
    return t;
}
static BOOL GetExitCodeThread(HANDLE handle, DWORD *code)
{
    { std::lock_guard<std::mutex> lock(event_mutex); ++joins_polled; event_cv.notify_all(); }
    *code = static_cast<Thread *>(handle)->exit;
    return TRUE;
}
static BOOL CloseHandle(HANDLE) { return TRUE; }
bool PortClockVirtual() { return virtual_clock; }

struct WAVEFORMATEX { WORD wFormatTag, nChannels; DWORD nSamplesPerSec, nAvgBytesPerSec; WORD nBlockAlign, wBitsPerSample, cbSize; };
struct DSBUFFERDESC { DWORD dwSize, dwFlags, dwBufferBytes; WAVEFORMATEX *lpwfxFormat; };
struct DSBPOSITIONNOTIFY { DWORD dwOffset; HANDLE hEventNotify; };
struct IDirectSoundNotify
{
    void SetNotificationPositions(unsigned, DSBPOSITIONNOTIFY *) {}
    void Release() {}
};
struct IDirectSoundBuffer
{
    BYTE data[0x80000]{};
    HRESULT GetStatus(DWORD *status) { *status = buffer_playing ? DSBSTATUS_PLAYING | DSBSTATUS_LOOPING : 0; return 0; }
    HRESULT Lock(DWORD offset, DWORD size, void **p1, DWORD *n1, void **p2, DWORD *n2, DWORD flags)
    {
        *p1 = data + offset; *n1 = flags & DSBLOCK_ENTIREBUFFER ? sizeof data : size; *p2 = nullptr; *n2 = 0; return 0;
    }
    HRESULT Unlock(void *, DWORD, void *, DWORD) { return 0; }
    HRESULT SetCurrentPosition(DWORD) { return 0; }
    HRESULT Play(DWORD, DWORD, DWORD)
    {
        std::lock_guard<std::mutex> lock(event_mutex);
        last_play_marker = data[0]; ++plays; buffer_playing = true; event_cv.notify_all(); return 0;
    }
    HRESULT Stop()
    {
        std::lock_guard<std::mutex> lock(event_mutex);
        ++stops; buffer_playing = false; event_cv.notify_all(); return 0;
    }
    HRESULT SetVolume(int value) { last_volume = value; return 0; }
    HRESULT QueryInterface(int, void **out) { static IDirectSoundNotify notify; *out = &notify; return 0; }
    void Release() { ++releases; }
};
struct CBaseWaveFileRead
{
    char filename[256]{};
    ~CBaseWaveFileRead() { ++reader_deletes; }
    void Close() {}
    void Open(char *name) { strncpy(filename, name, sizeof filename - 1); }
    bool HasFormat() { return true; }
    unsigned GetChannels() { return 2; }
    unsigned GetSampleRate() { return 44100; }
    void Read(UINT size, BYTE *data, UINT *got)
    {
        std::unique_lock<std::mutex> lock(event_mutex);
        ++reads;
        reading = true;
        event_cv.notify_all();
        event_cv.wait(lock, [] { return !hold_read; });
        *got = empty_read ? 0 : size;
        memset(data, strstr(filename, "-b.") ? 0x46 : 0x25, *got);
    }
};
using COggFileRead = CBaseWaveFileRead;
struct DirectSound
{
    void CreateSoundBuffer(DSBUFFERDESC *, IDirectSoundBuffer **buffer, void *) { static IDirectSoundBuffer b; *buffer = fail_buffer ? nullptr : &b; }
};
struct Sound { DirectSound *GetDirectSound() { static DirectSound ds; return no_sound ? nullptr : &ds; } } SOUND;

#include MUSIC_SOURCE

static void JoinWorker()
{
    if (thread_owner)
    {
        if (thread_owner->worker.joinable()) thread_owner->worker.join();
    }
}
static void ReleaseDispatch() { std::lock_guard<std::mutex> lock(event_mutex); hold_dispatch = false; event_cv.notify_all(); }
static void ReleaseRead() { std::lock_guard<std::mutex> lock(event_mutex); hold_read = false; event_cv.notify_all(); }
static void ReleaseWake() { std::lock_guard<std::mutex> lock(event_mutex); hold_wake = false; event_cv.notify_all(); }
static void FinishWorker() { SetEvent(sQuitEvent); ReleaseDispatch(); ReleaseWake(); JoinWorker(); }
static void Played(unsigned count)
{
    if (virtual_clock) Check(plays == count, "synchronous virtual pump plays");
    else Await([=] { return plays == count && wait_entries >= count + 1; });
}

int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    const char *scenario = argv[1];
    ce_layer = !strcmp(argv[2], "ce");
    virtual_clock = !strcmp(scenario, "virtual-normal");
    empty_read = !strcmp(scenario, "empty-start");
    no_sound = !strncmp(scenario, "no-sound", 8);
    fail_buffer = !strncmp(scenario, "failed-buffer", 13);
    if (!ce_layer) setenv("BEA_PORT_TAPE", "synthetic-admission-control", 1);
    if (virtual_clock)
    {
        setenv("BEA_PORT_CLOCK", "present", 1);
        if (ce_layer) setenv("BEA_PORT_CE_LAYER", "1", 1); // explicitly forced CE virtual control
    }
    hold_dispatch = !virtual_clock;
    hold_read = !strcmp(scenario, "inflight-stop") || !strcmp(scenario, "shutdown-fill") || !strcmp(scenario, "replace-inflight");
    hold_wake = !strcmp(scenario, "consumed-stop");
    wake_index = 0;
    InitMusicStream();
    if (thread_owner) Await([] { return waiting; });
    char track[] = "synthetic-a.ogg", replacement[] = "synthetic-b.ogg";
    bool after_stop_playing = false;
    if (no_sound || fail_buffer)
    {
        PlayMusicStream(track); StopMusicStream(); SetMusicStreamVolume(0.5f); ShutdownMusicStream();
        Check(!sBuffer && !sReader && !thread_owner, "no-buffer lifecycle owns no worker or reader");
        Check(cs_inits == 0 && cs_deletes == 0, "no-buffer lifecycle never uses a critical section");
    }
    else if (!strcmp(scenario, "queued-stop") || !strcmp(scenario, "consumed-stop"))
    {
        PlayMusicStream(track);
        if (hold_wake) { ReleaseDispatch(); Await([] { return wake_consumed; }); }
        Check(plays == 0, "worker has not started queued or consumed request");
        StopMusicStream();
        Check(!buffer_playing, "Stop returns stopped before handler");
        FinishWorker();
        after_stop_playing = buffer_playing;
    }
    else if (hold_read)
    {
        PlayMusicStream(track);
        ReleaseDispatch();
        Await([] { return reading; });
        Check(plays == 0, "worker is decoding before its final Play");
        std::atomic<bool> operation_done{false};
        bool shutdown = !strcmp(scenario, "shutdown-fill"), replace = !strcmp(scenario, "replace-inflight");
        std::thread operation([&] {
            if (shutdown) ShutdownMusicStream();
            else { StopMusicStream(); if (replace) PlayMusicStream(replacement); }
            operation_done = true; event_cv.notify_all();
        });
#if EXPECT_SERIALIZED
        Await([] { return cs_waiters.load() > 0; });
        Check(!operation_done, "Stop waits for the in-flight operation");
#else
        if (shutdown) Await([] { return joins_polled > 0; });
        else Await([&] { return operation_done.load(); });
#endif
        ReleaseRead();
        operation.join();
        if (shutdown) { JoinWorker(); Check(!sBuffer && !sReader, "shutdown releases stream after worker exits"); }
        else
        {
            if (replace)
            {
#if EXPECT_SERIALIZED
                Played(2);
                Check(last_play_marker == 0x46, "replacement start contains the requested track bytes");
#else
                Await([] { return wait_entries >= 3; });
                Check(plays == 1, "negative control does not restart buffer for replacement while prior fill starts");
#endif
                StopMusicStream();
            }
            FinishWorker();
        }
        after_stop_playing = buffer_playing;
    }
    else
    {
        if (!strcmp(scenario, "stop-then-play")) StopMusicStream();
        PlayMusicStream(track);
        if (!virtual_clock) ReleaseDispatch();
        if (!empty_read) Played(1);
        else Await([] { return stops.load() == 1 && wait_entries >= 2; });
        Check(empty_read ? !buffer_playing : buffer_playing.load(), "normal start or empty result");
        SetMusicStreamVolume(0.f); Check(last_volume == -10000, "zero music volume is silent");
        SetMusicStreamVolume(1.f); Check(last_volume == 0, "full music volume is unity");
        StopMusicStream();
        if (!strcmp(scenario, "restart-same") || !strcmp(scenario, "restart-different"))
        {
            PlayMusicStream(!strcmp(scenario, "restart-same") ? track : replacement);
            Played(2);
            Check(last_play_marker == (!strcmp(scenario, "restart-same") ? 0x25u : 0x46u), "restart uses requested track bytes");
            StopMusicStream();
        }
        else if (!strcmp(scenario, "stopped-refill") || !strcmp(scenario, "refill-new-track"))
        {
            { std::lock_guard<std::mutex> lock(event_mutex); wake_index = 1; hold_wake = true; }
            SetEvent(sFirstHalfEvent);
            Await([] { return wake_consumed; });
            bool replace = !strcmp(scenario, "refill-new-track");
            if (replace) PlayMusicStream(replacement);
            ReleaseWake();
            if (replace) { Played(2); Check(last_play_marker == 0x46, "new track survives stale refill"); StopMusicStream(); }
            FinishWorker();
#if EXPECT_SERIALIZED
            Check(reads == (replace ? 2u : 1u), "stale refill never touches reader after Stop or replacement");
#else
            Check(reads == (replace ? 3u : 2u), "negative control processes consumed stale refill");
#endif
        }
        if (thread_owner) FinishWorker();
        after_stop_playing = buffer_playing;
        Check(!after_stop_playing, "ordinary Stop remains stopped");
    }
    ShutdownMusicStream();
    JoinWorker();
    Check(cs_inits == cs_deletes, "every initialized critical section destroyed after join");
    if (!no_sound && !fail_buffer)
    {
        Check(releases == 1 && reader_deletes == 1, "shutdown releases one buffer and reader");
        Check(!sBuffer && !sReader, "shutdown clears stream owners");
    }
    if (!strcmp(scenario, "reinit") || strstr(scenario, "recover"))
    {
        delete thread_owner; thread_owner = nullptr;
        no_sound = fail_buffer = false;
        unsigned before = plays, owned = releases;
        { std::lock_guard<std::mutex> lock(event_mutex); waiting = false; wait_entries = 0; hold_dispatch = true; }
        InitMusicStream();
        Await([] { return waiting; });
        PlayMusicStream(replacement); ReleaseDispatch();
        Await([=] { return plays == before + 1 && wait_entries >= 2; });
        Check(last_play_marker == 0x46, "reinit starts the fresh requested track");
        StopMusicStream(); FinishWorker(); ShutdownMusicStream(); JoinWorker();
        Check(!buffer_playing && releases == owned + 1 && reader_deletes == owned + 1, "reinit owns and releases a fresh stream");
        Check(cs_inits == cs_deletes, "reinit balances critical section lifetime");
    }
    printf("{\"checks\":%u,\"plays\":%u,\"stops\":%u,\"invalid_resets\":%u,\"handled_events\":%u,\"reads\":%u,"
           "\"locks_initialized\":%u,\"locks_destroyed\":%u,\"after_stop_playing\":%s}\n",
           checks, plays.load(), stops.load(), invalid_resets, handled, reads, cs_inits, cs_deletes, after_stop_playing ? "true" : "false");
    for (Event *e : events) delete e;
    delete thread_owner;
    return 0;
}
