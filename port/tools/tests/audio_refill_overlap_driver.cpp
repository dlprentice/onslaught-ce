// SPDX-License-Identifier: GPL-3.0-only
// Actual mixer and patched worker, with authored PCM and a controlled reader; no device or game data.
#define AUDIO_REFILL_OVERLAP 1
#define main original_refill_fixture_main
#include "audio_refill_driver.cpp"
#undef main
#include <algorithm>

static std::mutex refillMutex;
static std::condition_variable refillCv;
static bool pauseRefill, refillPaused, resumeRefill;
static std::atomic<bool> refillTimedOut{false};
static bool raceRefill;
static std::atomic<bool> raceReached{false}, raceRead{false};
static std::mutex publishMutex;
static std::condition_variable publishCv;
static bool publishPaused, resumePublish;

// Only mixer copies instrumented by the runner call this, halfway through their otherwise identical publication.
static void FixturePublishBoundary()
{
    std::unique_lock<std::mutex> lock(publishMutex);
    publishPaused = true;
    publishCv.notify_all();
    if (!publishCv.wait_for(lock, std::chrono::seconds(5), [] { return resumePublish; })) refillTimedOut = true;
}

// The reader pauses after writing only the left channel of frame 64. The relaxed-atomic variant intentionally
// establishes no happens-before edge between this write and the mixer read: it is for ThreadSanitizer only.
static void RefillWriteBoundary(unsigned index)
{
    if (index != 64) return;
    if (raceRefill)
    {
        raceReached.store(true, std::memory_order_relaxed);
        while (!raceRead.load(std::memory_order_relaxed)) std::this_thread::yield();
        return;
    }
    if (!pauseRefill) return;
    std::unique_lock<std::mutex> lock(refillMutex);
    refillPaused = true;
    refillCv.notify_all();
    if (!refillCv.wait_for(lock, std::chrono::seconds(5), [] { return resumeRefill; })) refillTimedOut = true;
}

static void AwaitRefill()
{
    std::unique_lock<std::mutex> lock(refillMutex);
    Check(refillCv.wait_for(lock, std::chrono::seconds(5), [] { return refillPaused; }), "refill reached boundary");
}

static void ReleaseRefill()
{
    std::lock_guard<std::mutex> lock(refillMutex);
    resumeRefill = true;
    refillCv.notify_all();
}

static std::vector<Sint16> Submit(unsigned frames)
{
    submitted.clear();
    Feed(nullptr, gMixer.stream, int(frames * 4), 0);
    Check(submitted.size() == frames * 2, "callback submitted every requested frame");
    return submitted;
}

static Sint16 Output(short value)
{
    return (Sint16)lrintf((float)value * (32767.f / 32768.f));
}

static void Start()
{
    diagnosticsEnabled = PortSessionEnabled();
    device = Make<Sound>();
    InitMusicStream();
#ifdef CE_VERSION
    fixtureExpectStreamGuard = sCeStreamLockReady;
#endif
    char name[] = "authored sequence";
    PlayMusicStream(name);
    Check(Dispatch() == 1 && gMixer.stream && gMixer.numVoices == 1, "actual worker started one music voice");
}

static void Finish()
{
    ShutdownMusicStream();
    Check(!sBuffer && gMixer.numVoices == 0, "shutdown releases the stream and voice");
    device->Release();
}

static int Normal()
{
    Start();
    unsigned total = 0, mismatched = 0, dispatches = 0;
    uint64_t hash = 14695981039346656037ull;
    while (total < 262144)
    {
        unsigned n = std::min(441u, 262144u-total);
        auto pcm = MixFrames(n);
        for (unsigned i = 0; i < n; ++i)
        {
            Sint16 want = Output(1000 + 1000*((total+i)/65536));
            for (int channel = 0; channel < 2; ++channel)
            {
                Uint16 sample = (Uint16)pcm[2*i+channel];
                if (pcm[2*i+channel] != want) ++mismatched;
                hash = (hash ^ (sample & 255)) * 1099511628211ull;
                hash = (hash ^ (sample >> 8)) * 1099511628211ull;
            }
        }
        total += n;
        dispatches += Dispatch();
    }
    Check(mismatched == 0 && gMixer.numVoices == 1, "normal stereo PCM follows the authored sequence");
    Finish();
    std::printf("{\"case\":\"normal\",\"frames\":%u,\"mismatched_samples\":%u,\"refill_dispatches\":%u,\"pcm_fnv64\":\"%016llx\"}\n",
                total, mismatched, dispatches, (unsigned long long)hash);
    return 0;
}

static int LockError()
{
    Start();
    unsigned closed = fixtureReaderCloses, read = ((COggFileRead *)sReader)->frame;
    MusicStreamHandle(1);
    Check(GetMusicStreamFinished() && gMixer.numVoices == 0 && fixtureReaderCloses == closed + 1 &&
          ((COggFileRead *)sReader)->frame == read && sSilentHalves == 0 && sBytesStreamed == 0,
          "invalid refill lock stops cleanly without decoding");
    Finish();
    std::printf("{\"case\":\"lock-error\",\"checks\":%u}\n", checks);
    return 0;
}

static int Publication(bool shouldBlock)
{
    Start();
    MixFrames(131072);
    std::thread worker([] { role = 5; MusicStreamHandle(1); });
    {
        std::unique_lock<std::mutex> lock(publishMutex);
        Check(publishCv.wait_for(lock, std::chrono::seconds(5), [] { return publishPaused; }), "publisher reached mid-copy");
    }
    std::vector<Sint16> pcm;
    std::atomic<bool> done{false};
    std::thread callback([&] { role = 4; pcm = Submit(128); done = true; });
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!fixtureCallbackLockAttempted && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    bool attempted = fixtureCallbackLockAttempted;
    deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (!done && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    bool completedDuringCopy = done;
    {
        std::lock_guard<std::mutex> lock(publishMutex);
        resumePublish = true;
        publishCv.notify_all();
    }
    worker.join();
    callback.join();
    Check(attempted && !refillTimedOut, "callback and publisher reached the intended boundaries");
    Check(shouldBlock ? !completedDuringCopy : completedDuringCopy, "callback is blocked during publication");
    for (unsigned i = 0; i < 128; ++i)
    {
        Sint16 expected = Output((shouldBlock || i < 64) ? 3000 : 1000);
        Check(pcm[2*i] == expected && pcm[2*i+1] == expected, "publication result uses expected complete frames");
    }
    Check(fixtureOrderViolations == 0, "stream guard precedes mixer lock without a reverse acquisition");
    Finish();
    std::printf("{\"case\":\"%s\",\"new_frames\":%u,\"old_frames\":%u,\"callback_blocked\":%s,\"checks\":%u}\n",
                shouldBlock ? "publication-fixed" : "publication-unlocked", shouldBlock ? 128u : 64u,
                shouldBlock ? 0u : 64u, completedDuringCopy ? "false" : "true", checks);
    return 0;
}

static int Eof(bool emptyInitial)
{
    if (emptyInitial)
    {
        fixtureFramesRemaining = 0;
        diagnosticsEnabled = PortSessionEnabled();
        device = Make<Sound>();
        InitMusicStream();
        char name[] = "empty authored sequence";
        PlayMusicStream(name);
        Check(Dispatch() == 1 && GetMusicStreamFinished() && gMixer.numVoices == 0, "empty initial read finishes without playback");
    }
    else
    {
        Start();
        fixtureFramesRemaining = 8;
        MusicStreamHandle(1);
        sBuffer->SetCurrentPosition(0);
        auto pcm = Submit(16);
        for (unsigned i = 0; i < 16; ++i)
            Check(pcm[2*i] == Output(i < 8 ? 3000 : 0) && pcm[2*i+1] == pcm[2*i], "short refill is zero-padded");
        Check(sSilentHalves == 1 && !GetMusicStreamFinished(), "short refill begins EOF drain");
        MusicStreamHandle(2);
        MusicStreamHandle(1);
        Check(sSilentHalves == 3 && !GetMusicStreamFinished(), "three short halves await drain");
        unsigned closed = fixtureReaderCloses;
        MusicStreamHandle(2);
        Check(GetMusicStreamFinished() && gMixer.numVoices == 0 && sSilentHalves == 0 && sBytesStreamed == 0 &&
              fixtureReaderCloses == closed + 1, "drained EOF stops and closes the reader");
    }
    Finish();
    std::printf("{\"case\":\"%s\",\"checks\":%u}\n", emptyInitial ? "empty-initial" : "short-eof", checks);
    return 0;
}

#ifdef FIXTURE_PRODUCTION
static int Latency()
{
    Start();
    std::vector<unsigned char> source(STREAM_HALF, 0x55), dest(STREAM_HALF);
    std::vector<long long> ns;
    unsigned checksum = 0;
    for (unsigned i = 0; i < 500; ++i)
    {
        source[0] = (unsigned char)i;
        auto begin = std::chrono::steady_clock::now();
        PortAudioPublishMusicHalf(dest.data(), source.data(), STREAM_HALF);
        auto end = std::chrono::steady_clock::now();
        checksum += dest[0];
        ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count());
    }
    std::sort(ns.begin(), ns.end());
    Check(checksum != 0 && dest.back() == 0x55, "measured publication actually copied the full buffer");
    Finish();
    std::printf("{\"case\":\"publication-latency\",\"samples\":%zu,\"bytes\":%u,\"p50_ns\":%lld,\"p99_ns\":%lld,\"max_ns\":%lld}\n",
                ns.size(), STREAM_HALF, ns[ns.size()/2], ns[ns.size()*99/100], ns.back());
    return 0;
}
#endif

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    const std::string mode = argv[1];
    if (mode == "normal") return Normal();
    if (mode == "lock-error") return LockError();
    if (mode == "publication-fixed") return Publication(true);
    if (mode == "publication-unlocked") return Publication(false);
    if (mode == "short-eof") return Eof(false);
    if (mode == "empty-initial") return Eof(true);
#ifdef FIXTURE_PRODUCTION
    if (mode == "publication-latency") return Latency();
#endif
    const bool oldExpected = mode == "overlap-old" || mode == "race-old";
    const bool fixedExpected = mode == "overlap-fixed" || mode == "race-fixed" || mode == "shutdown";
    const bool safeHalf = mode == "safe-half" || mode == "race-safe";
    const bool race = mode == "race-fixed" || mode == "race-old" || mode == "race-safe";
    const bool late = mode == "late-dispatch";
    if (!oldExpected && !fixedExpected && !safeHalf && !late) return 1;
    Start();
    MixFrames(safeHalf ? 65537 : 131072);
    std::vector<Sint16> first;
    long long callbackNs = 0;
    if (late)
    {
        first = Submit(128);
        MusicStreamHandle(1);
    }
    else if (race)
    {
        raceRefill = true;
        std::thread worker([] { role = 5; MusicStreamHandle(1); });
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!raceReached.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < end) std::this_thread::yield();
        Check(raceReached.load(std::memory_order_relaxed), "race control reached refill");
        first = Submit(128);
        raceRead.store(true, std::memory_order_relaxed);
        worker.join();
    }
    else
    {
        pauseRefill = true;
        fixtureManualWorkerDone = false;
        std::thread worker([] { role = 5; MusicStreamHandle(1); fixtureManualWorkerDone = true; });
        AwaitRefill();
        std::atomic<bool> shutdownDone{false};
        std::thread shutdown;
        if (mode == "shutdown")
        {
            shutdown = std::thread([&] { role = 6; ShutdownMusicStream(); shutdownDone = true; });
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!fixtureShutdownAttempted && std::chrono::steady_clock::now() < until) std::this_thread::yield();
            Check(fixtureShutdownAttempted && !shutdownDone, "shutdown waits for the guarded in-flight reader");
        }
        std::atomic<bool> callbackDone{false};
        auto begin = std::chrono::steady_clock::now();
        std::thread callback([&] { role = 4; first = Submit(128); callbackDone = true; });
        auto until = begin + std::chrono::seconds(2);
        while (!callbackDone && std::chrono::steady_clock::now() < until) std::this_thread::yield();
        bool completedDuringRead = callbackDone;
        if (!completedDuringRead) ReleaseRefill();
        callback.join();
        callbackNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
        if (completedDuringRead) ReleaseRefill();
        worker.join();
        if (shutdown.joinable()) shutdown.join();
        Check(completedDuringRead, "callback completes while decoder is held");
        Check(!refillTimedOut, "reader did not time out");
        if (mode == "shutdown")
        {
            Check(shutdownDone && fixtureManualWorkerDone && !sBuffer && gMixer.numVoices == 0, "shutdown joins worker and releases voice");
            Check(fixtureOrderViolations == 0, "stream guard precedes mixer lock without a reverse acquisition");
            device->Release();
            std::printf("{\"case\":\"shutdown\",\"checks\":%u,\"callback_ns\":%lld}\n", checks, callbackNs);
            return 0;
        }
    }
    auto next = Submit(128);
    unsigned oldFrames = 0, newFrames = 0, splitFrames = 0, otherFrames = 0;
    const Sint16 old = Output(safeHalf ? 2000 : 1000), fresh = Output(3000);
    for (unsigned i = 0; i < 128; ++i)
    {
        const Sint16 l = first[2*i], r = first[2*i+1];
        if (l == old && r == old) ++oldFrames;
        else if (l == fresh && r == fresh) ++newFrames;
        else if (l == fresh && r == old) ++splitFrames;
        else ++otherFrames;
    }
    if (oldExpected)
        Check(oldFrames == 63 && newFrames == 64 && splitFrames == 1 && otherFrames == 0,
              "predecessor exposes partial and split samples");
    else
        Check(oldFrames == 128 && newFrames == 0 && splitFrames == 0 && otherFrames == 0,
              "callback sees whole old samples until publication");
    const Sint16 expectedNext = safeHalf ? old : fresh;
    for (Sint16 sample : next) Check(sample == expectedNext, "post-refill samples are consistent");
    Check(gMixer.numVoices == 1, "the effect is within a single music voice");
    Check(fixtureOrderViolations == 0, "stream guard precedes mixer lock without a reverse acquisition");
    std::printf("{\"case\":\"%s\",\"frames\":128,\"old_frames\":%u,\"new_frames\":%u,"
                "\"split_frames\":%u,\"other_frames\":%u,\"voices\":%d,\"checks\":%u,\"callback_ns\":%lld}\n",
                argv[1], oldFrames, newFrames, splitFrames, otherFrames, gMixer.numVoices, checks, callbackNs);
    Finish();
    return 0;
}
