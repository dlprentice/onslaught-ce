// SPDX-License-Identifier: GPL-3.0-only
// Actual mixer and patched worker with authored PCM and controlled SDL/events. No audio device or game data.
#define AUDIO_REFILL_OVERLAP 1
#define AUDIO_REFILL_ORDER_TEST 1
#define main original_refill_fixture_main
#include "audio_refill_driver.cpp"
#undef main

static std::mutex readerMutex;
static std::condition_variable readerCv;
static bool pauseReader, readerPaused, resumeReader;
static std::atomic<bool> readerTimedOut{false};
static bool waitFailureObserved, orderKnownAfterWaitFailure, orderPendingAfterWaitFailure;
static void FixtureWaitFailedBoundary()
{
    waitFailureObserved = true;
#if defined(CE_VERSION) && defined(FIXTURE_HAS_ORDER)
    orderKnownAfterWaitFailure = sPortRefillOrderKnown;
    orderPendingAfterWaitFailure = sPortRefillOrderPending;
#endif
}
static void FixturePublishBoundary() {} // used only by the separate publication-mutation fixture
static void RefillWriteBoundary(unsigned frame)
{
    if (!pauseReader || frame != 64) return;
    std::unique_lock<std::mutex> lock(readerMutex);
    readerPaused = true;
    readerCv.notify_all();
    if (!readerCv.wait_for(lock, std::chrono::seconds(5), [] { return resumeReader; })) readerTimedOut = true;
}

static Sint16 Output(short value)
{
    return (Sint16)lrintf((float)value * (32767.f / 32768.f));
}

static uint64_t pcmHash = 14695981039346656037ull;
static void HashPcm(const std::vector<Sint16> &pcm)
{
    for (Sint16 sample : pcm)
    {
        Uint16 bits = (Uint16)sample;
        pcmHash = (pcmHash ^ (bits & 255)) * 1099511628211ull;
        pcmHash = (pcmHash ^ (bits >> 8)) * 1099511628211ull;
    }
}

static DWORD TakeOne()
{
    HANDLE events[4] = {sPlayEvent, sFirstHalfEvent, sSecondHalfEvent, sQuitEvent};
    DWORD result = WaitForMultipleObjects(4, events, FALSE, 0);
    Check(result == 1 || result == 2, "one music half notification is pending");
    Check(MusicStreamHandle(result) == 0, "actual worker handles the half");
    return result;
}

// Diagnostic control only: this known scenario's older second-half wake is placed first in the fake wait array.
// A fixed priority is not a general repair for arbitrary backlog order or coalesced notifications.
static DWORD TakeKnownOlderSecond()
{
    HANDLE events[4] = {sPlayEvent, sSecondHalfEvent, sFirstHalfEvent, sQuitEvent};
    Check(WaitForMultipleObjects(4, events, FALSE, 0) == 1, "known older second-half event is selected");
    Check(MusicStreamHandle(2) == 0, "actual worker handles the older second half");
    return 2;
}

static short HalfSample(DWORD offset)
{
    void *a = nullptr, *b = nullptr;
    DWORD n = 0, m = 0;
    Check(SUCCEEDED(sBuffer->Lock(offset, 4, &a, &n, &b, &m, 0)) && n == 4 && !m, "lock authored sample");
    short value;
    memcpy(&value, a, sizeof(value));
    Check(SUCCEEDED(sBuffer->Unlock(a, n, b, m)), "unlock authored sample");
    return value;
}

static void CheckOrders(unsigned expected)
{
    if (!getenv("FIXTURE_ORDER_VALIDATE")) return;
    Check(fixtureOrderLines == expected, "unexpected handled-wake observation count");
    if (expected) Check(fixtureOrderMessages[0].find("expected_half=1 handled_half=0") != std::string::npos &&
                        fixtureOrderMessages[0].find("audible output unknown") != std::string::npos,
                        "anomaly identifies only handled wake order");
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    bool onTime = !strcmp(argv[1], "on-time"), backlog = !strcmp(argv[1], "backlog"),
         ordered = !strcmp(argv[1], "backlog-ordered"),
         collapsed = !strcmp(argv[1], "collapsed"), boundary = !strcmp(argv[1], "boundary"),
         equality = !strcmp(argv[1], "equality-recover"),
         spanning = !strcmp(argv[1], "spanning"), slowRead = !strcmp(argv[1], "slow-read"),
         readerOpen = !strcmp(argv[1], "reader-open"), restart = !strcmp(argv[1], "restart"),
         eof = !strcmp(argv[1], "short-eof"), rearm = !strcmp(argv[1], "rearm"), budget = !strcmp(argv[1], "budget"),
         noVoice = !strcmp(argv[1], "no-voice"), stopGuard = !strcmp(argv[1], "stop-guard"),
         eofGate = !strcmp(argv[1], "eof-gate"), failLock = !strcmp(argv[1], "failed-lock"),
         failRead = !strcmp(argv[1], "failed-read"), failUnlock = !strcmp(argv[1], "failed-unlock"),
         suppressed = !strcmp(argv[1], "suppressed-wake"), beforeArm = !strcmp(argv[1], "suppressed-before-arm"),
         failWait = !strcmp(argv[1], "failed-wait"), quitGuard = !strcmp(argv[1], "quit-guard"),
         gate = !strcmp(argv[1], "gate");
    if (!onTime && !backlog && !ordered && !collapsed && !boundary && !equality && !spanning && !slowRead &&
        !readerOpen && !restart && !eof && !rearm && !budget && !noVoice && !stopGuard && !eofGate && !failLock &&
        !failRead && !failUnlock && !suppressed && !beforeArm && !failWait && !quitGuard && !gate) return 1;
    const bool detect = getenv("FIXTURE_ORDER_DETECT") != nullptr;
    diagnosticsEnabled = PortSessionEnabled();
    device = Make<Sound>();
    InitMusicStream();
#ifdef CE_VERSION
    fixtureExpectStreamGuard = sCeStreamLockReady;
#endif
    char name[] = "authored sequence";
    PlayMusicStream(name);
    Check(Dispatch() == 1 && gMixer.numVoices == (noVoice ? 0 : 1), "initial authored voice state");
    if (noVoice || stopGuard || eofGate || failLock || failRead || failUnlock || suppressed || beforeArm || failWait || quitGuard || gate || rearm || budget)
    {
        if (noVoice)
        {
            MusicStreamHandle(2); // a mismatching accepted wake must not arm a voice that never started
            CheckOrders(0);
        }
        else if (stopGuard)
        {
            StopMusicStream();
#if defined(CE_VERSION) && defined(FIXTURE_HAS_ORDER)
            if (diagnosticsEnabled) Check(!sPortRefillOrderKnown && !sPortRefillOrderPending, "stop invalidates ordering state");
#endif
            MusicStreamHandle(2);
            CheckOrders(0);
        }
        else if (eofGate)
        {
            MixFrames(65537);
            Check(TakeOne() == 1, "first half arms ordinary ordering");
            sSilentHalves = 3;
            MusicStreamHandle(1); // mismatching wake is consumed by EOF handling, not a refill
            Check(GetMusicStreamFinished() && gMixer.numVoices == 0, "EOF gate finishes normally");
            CheckOrders(0);
        }
        else if (failLock)
        {
            MixFrames(65537);
            Check(TakeOne() == 1, "first lock succeeds");
            MusicStreamHandle(1); // injected second lock fails before this mismatch can be observed
            Check(GetMusicStreamFinished() && gMixer.numVoices == 0, "failed refill lock stops the stream");
            CheckOrders(0);
        }
        else if (failRead || failUnlock)
        {
            MixFrames(65537);
            fixtureFailNextRead = failRead;
            Check(TakeOne() == 1, "first refill consumes its wake with an injected failure");
            MusicStreamHandle(1); // uncertain history must not be compared with the next wake
            CheckOrders(0);
        }
        else if (suppressed || beforeArm)
        {
            MixFrames(65537);
            HANDLE events[4] = {sPlayEvent, sFirstHalfEvent, sSecondHalfEvent, sQuitEvent};
            if (suppressed)
            {
                Check(TakeOne() == 1, "first refill arms the current voice's order");
                MixFrames(65535);
            }
            DWORD consumed = WaitForMultipleObjects(4, events, FALSE, 0);
            Check(consumed == (suppressed ? 2u : 1u), "a wake is consumed before replacement");
            char replacement[] = "different authored sequence";
            PlayMusicStream(replacement);
            Check(MusicStreamHandle(consumed) == 0 && Dispatch() == 1 && gMixer.numVoices == 1,
                  "pending replacement discards consumed wake and opens reader on same voice");
            MixFrames(suppressed ? 65537 : 65535);
            Check(TakeOne() == (suppressed ? 1u : 2u), "the next accepted wake follows the discarded one");
            CheckOrders(0);
        }
        else if (quitGuard)
        {
            MixFrames(65537);
            Check(TakeOne() == 1 && MusicStreamHandle(3) == 1, "worker handles first refill and quit");
#if defined(CE_VERSION) && defined(FIXTURE_HAS_ORDER)
            if (diagnosticsEnabled) Check(!sPortRefillOrderKnown && !sPortRefillOrderPending,
                                          "quit invalidates ordering state");
#endif
            CheckOrders(0);
        }
        else if (failWait)
        {
            MixFrames(65537);
            Check(TakeOne() == 1, "first refill arms ordering before wait failure");
            fixtureWaitFailOnce = true;
            SetEvent(sQuitEvent);
            Check(MusicStreamThread(nullptr) == 0, "worker handles one failed wait and quit");
#if defined(CE_VERSION) && defined(FIXTURE_HAS_ORDER)
            if (diagnosticsEnabled) Check(waitFailureObserved && !orderKnownAfterWaitFailure && !orderPendingAfterWaitFailure,
                                          "failed wait invalidates ordering state");
#endif
            CheckOrders(0);
        }
        else if (gate)
        {
            MixFrames(65537);
            Check(TakeOne() == 1, "gate control handles a normal wake");
            CheckOrders(0);
        }
        else
        {
            unsigned epochs = budget ? 10 : 2;
            for (unsigned i = 0; i < epochs; ++i)
            {
                MixFrames(65537);
                Check(TakeOne() == 1, "epoch begins with first-half wake");
                MusicStreamHandle(1);
                MusicStreamHandle(1); // only the first mismatch in an epoch can log
                StopMusicStream();
                if (i + 1 < epochs)
                {
                    PlayMusicStream(name);
                    Check(Dispatch() == 1 && gMixer.numVoices == 1, "another epoch starts");
                }
            }
            CheckOrders(detect ? std::min(epochs, 8u) : 0);
        }
        std::printf("{\"case\":\"%s\",\"order_lines\":%u,\"order_calls\":%u,\"snapshot_calls\":%u,"
                    "\"clock_reads\":%u,\"checks\":%u}\n", argv[1], fixtureOrderLines, fixtureOrderCalls,
                    fixtureSnapshotCalls, clockReads, checks);
        ShutdownMusicStream();
        device->Release();
        return 0;
    }
    if (readerOpen || restart || eof)
    {
        MixFrames(65537);
        if (eof) fixtureFramesRemaining = 8;
        Check(TakeOne() == 1, "first half is handled");
        if (readerOpen)
        {
            IDirectSoundBuffer *original = sBuffer;
            char replacement[] = "different authored sequence";
            PlayMusicStream(replacement);
            Check(Dispatch() == 1 && sBuffer == original && gMixer.numVoices == 1 &&
                  ((COggFileRead *)sReader)->frame == 0, "new reader opens while original voice keeps playing");
            MixFrames(65535);
            Check(TakeOne() == 2, "same voice's next handled half remains the second half");
        }
        else if (restart)
        {
            StopMusicStream();
            Check(gMixer.numVoices == 0, "stop removes the initial voice");
            PlayMusicStream(name);
            Check(Dispatch() == 1 && gMixer.numVoices == 1, "same track starts a new playback epoch");
            MixFrames(65537);
            Check(TakeOne() == 1, "new epoch's first handled half is the first half");
        }
        else
        {
            Check(sSilentHalves == 1 && !GetMusicStreamFinished(), "short read starts the EOF drain");
            MixFrames(65535);
            Check(TakeOne() == 2, "EOF drain handles the second half");
            MixFrames(65537);
            Check(TakeOne() == 1 && sSilentHalves == 3 && !GetMusicStreamFinished(),
                  "three silent halves do not finish until the following wake");
            MixFrames(65535);
            Check(TakeOne() == 2 && GetMusicStreamFinished() && gMixer.numVoices == 0,
                  "the following wake drains EOF and stops the voice");
        }
        CheckOrders(0);
        std::printf("{\"case\":\"%s\",\"order_lines\":%u,\"order_calls\":%u,\"snapshot_calls\":%u,"
                    "\"clock_reads\":%u,\"checks\":%u}\n", argv[1], fixtureOrderLines, fixtureOrderCalls,
                    fixtureSnapshotCalls, clockReads, checks);
        ShutdownMusicStream();
        device->Release();
        return 0;
    }
    if (equality)
    {
        MixFrames(131072);
        Check(gMixer.mixed == 131072 && ((EventBoundary *)sFirstHalfEvent)->signaled &&
              ((EventBoundary *)sSecondHalfEvent)->signaled, "deadline reached with both notifications pending");
        Check(TakeOne() == 1 && HalfSample(0) == 3000, "first refill can still publish at equality");
        submitted.clear();
        Feed(nullptr, gMixer.stream, 4, 0);
        Check(submitted.size() == 2 && submitted[0] == Output(3000) && submitted[1] == Output(3000),
              "the first frame after equality uses the newly published half");
        CheckOrders(0);
        std::printf("{\"case\":\"equality-recover\",\"played_before_publish\":131072,"
                    "\"next_sample\":%d,\"checks\":%u}\n", submitted[0], checks);
        ShutdownMusicStream();
        device->Release();
        return 0;
    }
    if (boundary || spanning || slowRead)
    {
        // Here gMixer.mixed equals this music voice's source-frame count only because the fixture starts at frame
        // zero, uses exactly one voice and step 1, and never stops or seeks. The production counter is global.
        const unsigned beforeDeadline = slowRead ? 64 : 1;
        MixFrames(131072 - beforeDeadline);
        unsigned long long before = gMixer.mixed;
        Check(before == 131072 - beforeDeadline && ((EventBoundary *)sFirstHalfEvent)->signaled,
              "first refill is pending just before its deadline");
        std::thread worker;
        if (slowRead)
        {
            HANDLE events[4] = {sPlayEvent, sFirstHalfEvent, sSecondHalfEvent, sQuitEvent};
            Check(WaitForMultipleObjects(4, events, FALSE, 0) == 1, "worker consumes the first-half event");
            pauseReader = true;
            worker = std::thread([] { role = 5; MusicStreamHandle(1); });
            std::unique_lock<std::mutex> lock(readerMutex);
            Check(readerCv.wait_for(lock, std::chrono::seconds(5), [] { return readerPaused; }),
                  "in-flight decode reached its boundary");
        }
        unsigned frames = slowRead ? 128 : spanning ? 3 : 1;
        submitted.clear();
        Feed(nullptr, gMixer.stream, frames * 4, 0);
        Check(submitted.size() == frames * 2, "callback submitted the requested authored frames");
        for (unsigned i = 0; i < frames; ++i)
        {
            Sint16 expected = Output(i < beforeDeadline ? 2000 : 1000);
            Check(submitted[2*i] == expected && submitted[2*i+1] == expected,
                  "callback crosses from the valid half into stale samples");
        }
        unsigned long long after = gMixer.mixed;
        if (slowRead)
        {
            {
                std::lock_guard<std::mutex> lock(readerMutex);
                resumeReader = true;
                readerCv.notify_all();
            }
            worker.join();
            Check(!readerTimedOut && fixtureOrderViolations == 0 && HalfSample(0) == 3000,
                  "current worker publishes the now-late completed half after the callback");
        }
        if (boundary)
        {
            Check(before == 131071 && after == 131072 && submitted[0] == Output(2000),
                  "the deadline is reached immediately after the last valid frame");
            submitted.clear();
            Feed(nullptr, gMixer.stream, 4, 0);
            Check(submitted.size() == 2 && submitted[0] == Output(1000), "the next frame is stale");
        }
        else Check(after == 131072 + frames - beforeDeadline, "one callback spans the first stale-frame boundary");
        CheckOrders(0);
        std::printf("{\"case\":\"%s\",\"before_frames\":%llu,\"after_frames\":%llu,"
                    "\"callback_frames\":%u,\"valid_frames\":%u,\"stale_frames\":%u,\"checks\":%u}\n",
                    argv[1], before, after, frames, beforeDeadline, frames-beforeDeadline, checks);
        ShutdownMusicStream();
        device->Release();
        return 0;
    }
    unsigned baseNotifications = notifications;
    HashPcm(MixFrames(65537)); // crosses midpoint and moves one frame into half 1
    Check(((EventBoundary *)sFirstHalfEvent)->signaled && !((EventBoundary *)sSecondHalfEvent)->signaled,
          "first half is pending first");
    DWORD first = 0, second = 0, third = 0;
    if (!collapsed)
    {
        first = TakeOne();
        Check(first == 1 && HalfSample(0) == 3000, "on-time first refill publishes the next decoded half");
    }
    HashPcm(MixFrames(65535)); // end of half 1, now at start of half 0
    Check(((EventBoundary *)sSecondHalfEvent)->signaled, "second half event fires");
    if (onTime)
    {
        second = TakeOne();
        Check(second == 2 && HalfSample(STREAM_HALF) == 4000, "second refill follows playback order");
    }
    auto crossing = MixFrames(65537); // next first-half event, plus one sample of half 1
    HashPcm(crossing);
    Check(((EventBoundary *)sFirstHalfEvent)->signaled, "next first-half event fires");
    if (collapsed)
    {
        MixFrames(65535);
        unsigned signals = notifications - baseNotifications;
        Check(signals == 4, "two cycles generated four notifications");
        Check(((EventBoundary *)sFirstHalfEvent)->signaled && ((EventBoundary *)sSecondHalfEvent)->signaled,
              "the two auto-reset states are both set");
        unsigned handled = Dispatch();
        Check(handled == 2 && ((COggFileRead *)sReader)->frame == 262144,
              "four crossings collapse to two pending worker wakes");
        CheckOrders(0); // the coalesced loss is invisible to an alternating handled-order witness
        std::printf("{\"case\":\"collapsed\",\"signals\":%u,\"worker_wakes\":%u,\"checks\":%u}\n",
                    signals, handled, checks);
    }
    else
    {
        if (backlog || ordered)
        {
            Check(((EventBoundary *)sFirstHalfEvent)->signaled && ((EventBoundary *)sSecondHalfEvent)->signaled,
                  "second-then-first notifications are both pending");
            second = ordered ? TakeKnownOlderSecond() : TakeOne();
            third = TakeOne();
        }
        else third = TakeOne();
        short half0 = HalfSample(0), half1 = HalfSample(STREAM_HALF);
        Sint16 crossingSample = crossing[crossing.size()-2];
        submitted.clear();
        Feed(nullptr, gMixer.stream, 128 * 4, 0);
        Check(submitted.size() == 256, "callback submits authored PCM");
        HashPcm(submitted);
        Sint16 submittedSample = submitted[0];
        if (onTime)
            Check(first == 1 && second == 2 && third == 1 && half0 == 5000 && half1 == 4000 &&
                  crossingSample == Output(4000) && submittedSample == Output(4000), "on-time sequence is chronological");
        else if (backlog)
            Check(first == 1 && second == 1 && third == 2 && half0 == 4000 && half1 == 5000 &&
                  crossingSample == Output(2000) && submittedSample == Output(5000),
                  "backlog replays old PCM and fills in event-index order");
        else
            Check(first == 1 && second == 2 && third == 1 && half0 == 5000 && half1 == 4000 &&
                  crossingSample == Output(2000) && submittedSample == Output(4000),
                  "known chronological choice preserves half order after the earlier stale sample");
        Check(gMixer.numVoices == 1, "backlog remains one music voice");
        CheckOrders(backlog && detect ? 1 : 0);
        std::printf("{\"case\":\"%s\",\"worker_order\":[%lu,%lu,%lu],\"half0\":%d,\"half1\":%d,"
                    "\"crossing_sample\":%d,\"submitted_sample\":%d,\"pcm_fnv64\":\"%016llx\",\"voices\":%d,\"checks\":%u}\n",
                    argv[1], (unsigned long)first, (unsigned long)second, (unsigned long)third,
                    half0, half1, crossingSample, submittedSample, (unsigned long long)pcmHash, gMixer.numVoices, checks);
    }
    ShutdownMusicStream();
    Check(!sBuffer && gMixer.numVoices == 0, "stream shut down");
    device->Release();
    return 0;
}
