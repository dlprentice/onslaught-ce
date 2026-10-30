#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise production CE session logging, callback failure accounting and typed game observers on host boundaries.

No game data, SDL installation, display, audio device or network. The unchanged production logger and extracted
Feed/SessionGameUpdate bodies are compiled; fake SDL/time/mixer/game fields expose observation side effects.
Receipts retain every compiled input and command under ignored local-data. This is not physical-output acceptance.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
# The observation API of patch 0187 (an audio-obs line before each summary); the checks that need it run only where the header has it.
OBS_API = "PortSessionObserve" in (ROOT / "port/platform/common/portlog.h").read_text()
# The summary line of the golden mode, taken from main's logger before patch 0187: the audio-obs line is a separate line, so this one must not change by a byte.
GOLDEN_SUMMARY = "events=1 errors=1 callbacks=1 callback_put_failed=2 callback_alloc_failed=3 music_refills=4 music_short_reads=5 music_read_failed=6 music_wait_failed=7 monitor_queue_drops=8 monitor_put_failed=9 media_final=1 mixed_music_frames=1234567890123 mixed_film_frames=987654321 mixed_both_role_frames=55555 callback_frames=4444444 demand_max_frames=441 queued_input_last_bytes=1764 queued_input_max_bytes=3528 queue_samples=333 queue_failures=22 queue_mix_frame=1111111 queue_current_generation=3 retired_queue_observations=7 input=44100Hz-S16LE-stereo silence_or_mute_possible=1 output_device_unobserved=1"
OBS_ZERO = ("music_risk_refills=0 music_pair_refills=0 music_order_anomalies=0 music_lines_dropped=0 music_voice_starts=0 music_stale_halves=0 "
            "music_overwritten_halves=0 audio_callbacks_late=0 events_dropped=0 errors_dropped=0 music_refill_us_max=0 music_notify_age_us_max=0 "
            "music_voices_max=0 audio_gap_us_max=0 device_rate=0 device_channels=0 device_period_frames=0")


def session_lines(text):
    """(kind, body) of every session line of a driver's stderr, in order"""
    return re.findall(r"^\S+ session pid=\d+ t_ms=\d+ P=\d+ (\S+): (.*)$", text, re.M)

SDL = r'''
#ifndef SESSION_TEST_SDL
#define SESSION_TEST_SDL
#include <cstdint>
#include <atomic>
using Uint64 = uint64_t;
using Sint16 = int16_t;
#define SDLCALL
extern std::atomic<unsigned> clock_reads;
Uint64 SDL_GetTicks();
#endif
'''

DRIVER = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <thread>
#include <vector>
#include <cstdint>
#include <atomic>
std::atomic<unsigned> clock_reads;
#include <SDL3/SDL.h>
Uint64 SDL_GetTicks() { ++clock_reads; errno = EIO; return 123456; }
@ADMISSION@
#include "@LOGGER@"
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line=%d %s\n", __LINE__, #x); return 1; } } while (0)
struct SDL_AudioStream {};
const int kRate = 44100;
Uint64 gTicksNs;
Uint64 SDL_GetTicksNS() { return gTicksNs; }	// the mixer's own clock, not the session log's SDL_GetTicks
int mixer_locks, mixer_unlocks, mixed, submitted, freed, error_reads;
bool allocation_failed, submission_failed;
bool gSessionAudio;
void *SDL_malloc(unsigned bytes) { return allocation_failed ? nullptr : std::malloc(bytes); }
void SDL_free(void *p) { ++freed; std::free(p); }
void SDL_LockMutex(void *) { ++mixer_locks; }
void SDL_UnlockMutex(void *) { ++mixer_unlocks; }
bool SDL_PutAudioStreamData(SDL_AudioStream *, const void *, int) { ++submitted; return !submission_failed; }
const char *SDL_GetError() { ++error_reads; return "submission failure\nline must stay whole"; }
struct Mixer {
    void *lock;
    void Mix(Sint16 *, int frames) { ::mixed += frames; }   // the test's counter, not the member below
    // dsound.cpp's media-mix counters (patch 0087) that Feed reads; media observation stays off here.
    Uint64 mixed = 0, streamGeneration = 0, callbackFrames = 0, queueSamples = 0, queueFailures = 0, queueAtMixed = 0;
    Uint64 queueGeneration = 0, retiredQueueObservations = 0;
    unsigned callbackMaxFrames = 0;
    Uint64 feedLastNs = 0;
    unsigned feedLastFrames = 0;
    int queueLastBytes = -1, queueMaxBytes = -1;
    bool mediaStreamActive = false, generationExhausted = false;
    SDL_AudioStream *stream = nullptr;
} gMixer;
int SDL_GetAudioStreamQueued(SDL_AudioStream *) { return 0; }
@FEED@
const int MAX_PLAYERS = 4, GAME_STATE_PRE_RUNNING = 1, GAME_STATE_LEVEL_WON = 5;
struct FVector { float X, Y, Z; };
struct CBattleEngine { int form = 2; FVector pos{}; int GetState() { return form; } FVector &GetPos() { return pos; } } engine;
struct CPlayer { CBattleEngine *engine; int view; CBattleEngine *GetBattleEngine() { return engine; }
    int GetIsInFPV() { return view; } } player;
struct Game {
    int level=100, phase=0, pause=0, players=99, player_reads=0;
    int GetCurrentLevel() { return level; } int GetGameState() { return phase; } int IsPaused() { return pause; }
    int GetNPlayers() { return players; }
    CPlayer *GetPlayer(int n) { ++player_reads; if (n != 0) std::abort(); return &player; }
} GAME;
@GAME@
static unsigned short CW() { unsigned short cw; __asm__ volatile("fnstcw %0" : "=m"(cw)); return cw; }
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    bool enabled = PortSessionEnabled();
    const char *mode = argv[1];
    if (!std::strcmp(mode, "disabled")) {
        CHECK(!enabled);
        errno = EDOM;
        PortSessionLog(PORT_SESSION_AUDIO, true, "must not print");
        PortSessionCount(PORT_COUNT_AUDIO_CALLBACKS);
        PortSessionAudioError("must not copy");
        PortSessionAsset(PORT_SESSION_MUSIC, "must not print", "/sensitive/name.ogg");
        PortSessionMediaActive(PORT_SESSION_MUSIC, &player, true);
        PortSessionFinalMedia final{};
        PortSessionSetFinalMedia(&final);
        PortSessionPulse(300);
        PortSessionSummary();
#ifdef OBS_API
        PortSessionObserve(PORT_OBS_MUSIC_RISK_REFILLS, 5);
        PortSessionObserveMax(PORT_OBS_DEVICE_RATE, 48000);
        for (int i = 0; i < PORT_SESSION_OBSERVATIONS; ++i) CHECK(gObs[i] == 0);
#endif
        SessionGameUpdate();
        CHECK(gCounts[PORT_COUNT_AUDIO_CALLBACKS] == 0 && gAudioErrorState == 0 && gFrame == 0 && !gFinalMediaPending);
        CHECK(clock_reads == 0 && GAME.player_reads == 0 && errno == EDOM);
    } else if (!std::strcmp(mode, "basic")) {
        CHECK(enabled);
        unsigned short before = CW();
        errno = EDOM;
        PortSessionPulse(17);
        PortSessionLog(PORT_SESSION_GAME, false, "level=%d message=one\ntwo\rthree", 110);
        PortSessionAsset(PORT_SESSION_MUSIC, "start", "C:\\Users\\secret-account\\music\\BEA_09.ogg");
        CHECK(CW() == before && errno == EDOM);
        PortSessionSummary();
    } else if (!std::strcmp(mode, "limits")) {
        CHECK(enabled);
        for (int i=0; i<9000; ++i) PortSessionLog(PORT_SESSION_WINDOW, false, "ordinary %d", i);
        PortSessionLog(PORT_SESSION_AUDIO, true, "critical after ordinary overflow");
        for (int i=0; i<9000; ++i) PortSessionLog(PORT_SESSION_AUDIO, true, "error %d", i);
        for (int i=0; i<100; ++i) { PortSessionCount(PORT_COUNT_AUDIO_PUT_FAILED); PortSessionPulse(300u*(i+1)); }
        PortSessionSummary();
        CHECK(gNormal == kNormalLimit + 1 && gErrors == kErrorLimit + 1 && gPulseSummaries == 32);
#ifdef OBS_API
        CHECK(gObs[PORT_OBS_EVENTS_DROPPED] == 9000 - kNormalLimit);				// every ordinary event past the limit, the notice's own included
        CHECK(gObs[PORT_OBS_ERRORS_DROPPED] == 9001 - kErrorLimit);					// the critical one and the 9000 more
#endif
    } else if (!std::strcmp(mode, "threads")) {
        CHECK(enabled);
        std::vector<std::thread> workers;
        for (int t=0; t<8; ++t) workers.emplace_back([t] {
            for (int i=0; i<100; ++i) {
                PortSessionLog(PORT_SESSION_MUSIC, false, "thread=%d event=%d END", t, i);
                PortSessionCount(PORT_COUNT_MUSIC_REFILLS);
            }
        });
        for (auto &w : workers) w.join();
        CHECK(gCounts[PORT_COUNT_MUSIC_REFILLS] == 800);
        PortSessionSummary();
    } else if (!std::strcmp(mode, "media-final-threads")) {
        CHECK(enabled);
        std::vector<std::thread> workers;
        for (unsigned t=0; t<4; ++t) workers.emplace_back([t] {
            for (unsigned i=0; i<100; ++i) {
                unsigned long long id = t*100 + i + 1;
                PortSessionFinalMedia s{};
                s.musicFrames=s.filmFrames=s.bothRoleFrames=id;
                PortSessionSetFinalMedia(&s);
                PortSessionSummary();
            }
        });
        for (auto &w : workers) w.join();
        PortSessionSummary();
    } else if (!std::strcmp(mode, "media")) {
        CHECK(enabled);
        PortSessionMediaActive(PORT_SESSION_MUSIC, &player, true);
        PortSessionMediaActive(PORT_SESSION_MUSIC, &player, true);
        PortSessionMediaActive(PORT_SESSION_FILM, &engine, true);
        PortSessionMediaActive(PORT_SESSION_FILM, &engine, false);
        PortSessionMediaActive(PORT_SESSION_FILM, &engine, false);
        PortSessionMediaActive(PORT_SESSION_MUSIC, &player, false);
        int tokens[17];
        for (int &t : tokens) PortSessionMediaActive(PORT_SESSION_FILM, &t, true);
        for (int &t : tokens) PortSessionMediaActive(PORT_SESSION_FILM, &t, false);
        for (const auto &m : gMedia) CHECK(!m.token);
    } else if (!std::strncmp(mode, "feed-", 5)) {
        CHECK(enabled);
        gSessionAudio = true;
        allocation_failed = !std::strcmp(mode, "feed-alloc-fail");
        submission_failed = !std::strcmp(mode, "feed-put-fail");
        unsigned before = clock_reads;
        Feed(nullptr, nullptr, 0, 0);
        Feed(nullptr, nullptr, 400, 0);
        CHECK(clock_reads == before && gNormal == 0 && gErrors == 0);
        CHECK(gCounts[PORT_COUNT_AUDIO_CALLBACKS] == 1);
        CHECK(gCounts[PORT_COUNT_AUDIO_ALLOC_FAILED] == (allocation_failed ? 1u : 0u));
        CHECK(gCounts[PORT_COUNT_AUDIO_PUT_FAILED] == (submission_failed ? 1u : 0u));
        CHECK(mixed == (allocation_failed ? 0 : 100));
        CHECK(mixer_locks == (allocation_failed ? 0 : 1) && mixer_unlocks == mixer_locks);
        CHECK(submitted == (allocation_failed ? 0 : 1) && freed == submitted);
        CHECK(error_reads == (submission_failed ? 1 : 0));
        if (submission_failed) {
            CHECK(gAudioErrorState == 2);
            PortSessionAudioError("must not replace first error");
            CHECK(!std::strcmp(gAudioError, "submission failure\nline must stay whole"));
        }
        PortSessionSummary();
    } else if (!std::strcmp(mode, "game")) {
        CHECK(enabled);
        unsigned short before = CW();
        SessionGameUpdate(); CHECK(GAME.player_reads == 0);
        GAME.phase=3; GAME.players=1;
        SessionGameUpdate(); CHECK(GAME.player_reads == 1);
        player.engine=&engine;
        SessionGameUpdate();
        unsigned calls = clock_reads;
        SessionGameUpdate(); CHECK(clock_reads == calls);
        engine.form=3; player.view=1;
        SessionGameUpdate();
        uint32_t negative_one=0xbf800000u; std::memcpy(&engine.pos.X, &negative_one, 4);
        SessionGameUpdate();
        uint32_t y_over=0x44004000u; std::memcpy(&engine.pos.Y, &y_over, 4);
        SessionGameUpdate();
        uint32_t nan=0x7fc00000u; std::memcpy(&engine.pos.X, &nan, 4);
        SessionGameUpdate();
        CHECK(CW() == before);
        GAME.phase=9; GAME.players=99;
        int reads=GAME.player_reads;
        SessionGameUpdate(); CHECK(GAME.player_reads == reads);
    } else if (!std::strcmp(mode, "golden")) {
        // Fixed counters and a fixed final media snapshot, only the API main has: the summary line this prints must equal main's byte for byte.
        CHECK(enabled);
        PortSessionLog(PORT_SESSION_GAME, false, "one");
        PortSessionLog(PORT_SESSION_AUDIO, true, "two");
        for (int i = 0; i < PORT_SESSION_COUNTERS; ++i)
            for (int n = 0; n <= i; ++n) PortSessionCount(static_cast<PortSessionCounter>(i));
        PortSessionFinalMedia f{};
        f.musicFrames = 1234567890123ull; f.filmFrames = 987654321ull; f.bothRoleFrames = 55555ull; f.callbackFrames = 4444444ull;
        f.queueSamples = 333ull; f.queueFailures = 22ull; f.queueAtMixed = 1111111ull; f.retiredQueueObservations = 7ull;
        f.callbackMaxFrames = 441; f.queueLastBytes = 1764; f.queueMaxBytes = 3528; f.queueCurrentGeneration = 3;
        PortSessionSetFinalMedia(&f);
        PortSessionSummary();
#ifdef OBS_API
    } else if (!std::strcmp(mode, "obs")) {
        // The observation API: adds, saturates, keeps the largest, ignores an index out of range, and prints one line of numbers.
        CHECK(enabled);
        PortSessionObserve(PORT_OBS_MUSIC_RISK_REFILLS);
        PortSessionObserve(PORT_OBS_MUSIC_RISK_REFILLS, 4);
        CHECK(gObs[PORT_OBS_MUSIC_RISK_REFILLS] == 5);
        PortSessionObserve(PORT_OBS_MUSIC_PAIR_REFILLS, 0xfffffffeu);
        PortSessionObserve(PORT_OBS_MUSIC_PAIR_REFILLS, 5);
        CHECK(gObs[PORT_OBS_MUSIC_PAIR_REFILLS] == 0xffffffffu);
        PortSessionObserveMax(PORT_OBS_MUSIC_REFILL_US_MAX, 300);
        PortSessionObserveMax(PORT_OBS_MUSIC_REFILL_US_MAX, 700);
        PortSessionObserveMax(PORT_OBS_MUSIC_REFILL_US_MAX, 100);		// the largest is not the last
        CHECK(gObs[PORT_OBS_MUSIC_REFILL_US_MAX] == 700);
        PortSessionObserveMax(PORT_OBS_DEVICE_RATE, 48000);
        PortSessionObserve(static_cast<PortSessionObservation>(-1));
        PortSessionObserve(PORT_SESSION_OBSERVATIONS);
        PortSessionObserveMax(static_cast<PortSessionObservation>(-1), 9);
        PortSessionObserveMax(PORT_SESSION_OBSERVATIONS, 9);
        unsigned long long sum = 0;
        for (int i = 0; i < PORT_SESSION_OBSERVATIONS; ++i) sum += gObs[i];
        CHECK(sum == 5ull + 0xffffffffull + 700 + 48000);
        PortSessionSummary();
    } else if (!std::strcmp(mode, "obs-threads")) {
        CHECK(enabled);
        std::vector<std::thread> workers;
        for (unsigned t = 0; t < 8; ++t) workers.emplace_back([t] {
            for (unsigned i = 0; i < 1000; ++i) {
                PortSessionObserve(PORT_OBS_MUSIC_STALE_HALVES);
                PortSessionObserveMax(PORT_OBS_MUSIC_VOICES_MAX, t * 1000 + i);
            }
        });
        for (auto &w : workers) w.join();
        CHECK(gObs[PORT_OBS_MUSIC_STALE_HALVES] == 8000 && gObs[PORT_OBS_MUSIC_VOICES_MAX] == 7999);
        PortSessionSummary();
    } else if (!std::strcmp(mode, "obs-feed-gap")) {
        // The real Feed: the time since the previous callback of a live stream, and one count when it is over three chunks (30 ms of 441 frames).
        CHECK(enabled);
        gSessionAudio = true;
        SDL_AudioStream live;
        gMixer.stream = &live; gMixer.mediaStreamActive = true;
        unsigned before = clock_reads;
        gTicksNs = 1000000000ull; Feed(nullptr, &live, 1764, 0);
        CHECK(gObs[PORT_OBS_AUDIO_GAP_US_MAX] == 0 && gObs[PORT_OBS_AUDIO_CALLBACKS_LATE] == 0);		// the first callback of a stream has no gap
        gTicksNs += 10000000ull; Feed(nullptr, &live, 1764, 0);
        CHECK(gObs[PORT_OBS_AUDIO_GAP_US_MAX] == 10000 && gObs[PORT_OBS_AUDIO_CALLBACKS_LATE] == 0);	// one chunk
        gTicksNs += 29000000ull; Feed(nullptr, &live, 1764, 0);
        CHECK(gObs[PORT_OBS_AUDIO_GAP_US_MAX] == 29000 && gObs[PORT_OBS_AUDIO_CALLBACKS_LATE] == 0);	// under three chunks
        gTicksNs += 31000000ull; Feed(nullptr, &live, 1764, 0);
        CHECK(gObs[PORT_OBS_AUDIO_GAP_US_MAX] == 31000 && gObs[PORT_OBS_AUDIO_CALLBACKS_LATE] == 1);	// over three chunks
        gTicksNs += 5000000ull; Feed(nullptr, &live, 1764, 0);
        CHECK(gObs[PORT_OBS_AUDIO_GAP_US_MAX] == 31000 && gObs[PORT_OBS_AUDIO_CALLBACKS_LATE] == 1);	// the largest stays
        gMixer.feedLastNs = 0; gTicksNs += 900000000ull; Feed(nullptr, &live, 1764, 0);
        CHECK(gObs[PORT_OBS_AUDIO_GAP_US_MAX] == 31000 && gObs[PORT_OBS_AUDIO_CALLBACKS_LATE] == 1);	// a new stream lifetime has no previous callback
        CHECK(clock_reads == before && gNormal == 0 && gErrors == 0);										// the session log's clock is never read in the callback
        PortSessionSummary();
    } else if (!std::strcmp(mode, "obs-max")) {
        // Every counter and observation at its largest, and a final media snapshot of the largest values: no line is cut short.
        CHECK(enabled);
        for (int i = 0; i < PORT_SESSION_COUNTERS; ++i) gCounts[i] = 0xffffffffu;
        for (int i = 0; i < PORT_SESSION_OBSERVATIONS; ++i) gObs[i] = 0xffffffffu;
        gNormal = gErrors = 0xfffffff0u;
        PortSessionFinalMedia f{};
        f.musicFrames = f.filmFrames = f.bothRoleFrames = f.callbackFrames = ~0ull;
        f.queueSamples = f.queueFailures = f.queueAtMixed = f.retiredQueueObservations = ~0ull;
        f.callbackMaxFrames = 0xffffffffu; f.queueLastBytes = f.queueMaxBytes = 0x7fffffff; f.queueCurrentGeneration = 0xffffffffu;
        PortSessionSetFinalMedia(&f);
        PortSessionSummary();
#endif
    } else return 2;
    std::puts("PASS");
}
'''


def extract(text, signature):
    matches = re.findall(r"^" + signature + r"[^\n]*\n\{\n.*?^\}", text, re.M | re.S)
    if len(matches) != 1:
        raise RuntimeError("expected one production function: " + signature)
    return matches[0]


class SessionLog(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        parent = ROOT / "local-data/port/session-log-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        (cls.out / "SDL3").mkdir()
        (cls.out / "SDL3/SDL.h").write_text(SDL)
        logger = ROOT / "port/platform/common/portlog.cpp"
        ce = ROOT / "port/platform/common/ce.cpp"
        mixer = ROOT / "port/dsound/dsound.cpp"
        game = ROOT / "port/game/updatedump.cpp"
        cls.template = (DRIVER.replace("@ADMISSION@", extract(ce.read_text(), r"bool PortCeLayer\(\)"))
                        .replace("@FEED@", extract(mixer.read_text(), r"void SDLCALL Feed\("))
                        .replace("@GAME@", extract(game.read_text(), r"static void SessionGameUpdate\(\)")))
        text = cls.template.replace("@LOGGER@", str(logger))
        source = cls.out / "driver.cpp"
        source.write_text(text)
        cls.receipt = {"inputs": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in (logger, logger.with_suffix('.h'), ce, mixer, game)}, "commands": [], "cases": []}
        cls.binaries = {}
        for public in (False, True):
            name = "public" if public else "private"
            binary = cls.out / name
            command = [*shlex.split(os.environ.get("CXX", "c++")), "-std=c++17", "-pthread", "-O2", "-Wall", "-Wextra",
                       "-I" + str(cls.out), *(['-DCE_VERSION="test"'] if public else []), *(["-DOBS_API"] if OBS_API else []), str(source), "-o", str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            (cls.out / f"{name}-build.log").write_text(result.stdout + result.stderr)
            cls.receipt["commands"].append({"command": command, "exit": result.returncode})
            if result.returncode:
                raise RuntimeError(f"host boundary build failed: {cls.out}/{name}-build.log")
            cls.binaries[name] = binary

    @classmethod
    def tearDownClass(cls):
        (cls.out / "receipt.json").write_text(json.dumps(cls.receipt, indent=2) + "\n")
        print(cls.out / "receipt.json")

    def run_case(self, mode, public=True, **overrides):
        env = {k: v for k, v in os.environ.items() if not k.startswith("BEA_")}
        env.update(overrides)
        result = subprocess.run([str(self.binaries["public" if public else "private"]), mode],
                                capture_output=True, text=True, timeout=10, env=env)
        self.receipt["cases"].append({"mode": mode, "public": public, "environment": overrides, "exit": result.returncode,
                                       "stdout": result.stdout, "stderr": result.stderr})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout, "PASS\n")
        return result.stderr

    def test_ordinary_events_preserve_errno_control_word_and_redact_asset_paths(self):
        text = self.run_case("basic")
        self.assertIn("t_ms=123456 P=17 game: level=110 message=one?two?three", text)
        self.assertIn("asset=BEA_09.ogg", text)
        self.assertNotIn("secret-account", text)
        self.assertNotIn("Users", text)
        self.assertEqual([kind for kind, _ in session_lines(text)], ["game", "music", "audio-obs", "summary"] if OBS_API else ["game", "music", "summary"])
        self.assertEqual(len(text.splitlines()), 4 if OBS_API else 3)

    def test_private_has_no_observers_or_clock_reads(self):
        self.assertEqual(self.run_case("disabled", public=False), "")

    def test_faithful_replay_recording_and_ce_drawing_override_stay_disabled(self):
        for name in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"):
            for force in ("", "1"):
                with self.subTest(name=name, force=force):
                    self.assertEqual(self.run_case("disabled", **{name: "selected", "BEA_PORT_CE_LAYER": force}), "")
        self.assertEqual(self.run_case("disabled", BEA_PORT_CE_LAYER="0"), "")

    def test_error_reserve_saturation_and_final_summary_survive_event_flood(self):
        text = self.run_case("limits")
        self.assertIn("critical after ordinary overflow", text)
        self.assertEqual(text.count("ordinary event limit reached"), 1)
        self.assertEqual(text.count("error event limit reached"), 1)
        self.assertEqual(sum(" summary: " in line for line in text.splitlines()), 33)
        self.assertTrue(text.rstrip().endswith("monitor_put_failed=0"))
        self.assertLess(len(text.encode()), 2 << 20)

    def test_concurrent_worker_events_are_whole_and_counts_not_lost(self):
        text = self.run_case("threads")
        events = [line for line in text.splitlines() if "music: thread=" in line]
        self.assertEqual(len(events), 800)
        self.assertEqual(len(set(re.findall(r"thread=\d+ event=\d+ END", text))), 800)
        self.assertTrue(all(line.endswith(" END") for line in events))
        self.assertIn("music_refills=800", text)

    def test_concurrent_final_media_snapshots_are_whole_and_bounded(self):
        text = self.run_case("media-final-threads")
        lines = [line for line in text.splitlines() if "media_final=1" in line]
        self.assertGreater(len(lines), 0)
        self.assertLessEqual(len(lines), 400)
        for line in lines:
            values = re.search(r"mixed_music_frames=(\d+) mixed_film_frames=(\d+) mixed_both_role_frames=(\d+)", line)
            self.assertIsNotNone(values)
            self.assertEqual(values.group(1), values.group(2))
            self.assertEqual(values.group(2), values.group(3))
            self.assertLess(len(line), 1200)

    def test_lifetime_ledger_deduplicates_without_claiming_audible_overlap(self):
        text = self.run_case("media")
        self.assertIn("outstanding_music_starts=1 outstanding_film_starts=1 request_lifetimes_overlap=1", text)
        self.assertIn("tracking_overflow=1", text)
        self.assertIn("not active-voice or audible-content proof", text)
        self.assertNotIn("active_music_buffers", text)

    def test_real_callback_success_formats_nothing_and_preserves_submission(self):
        text = self.run_case("feed-ok")
        self.assertIn("callbacks=1 callback_put_failed=0 callback_alloc_failed=0", text)

    def test_real_callback_failed_submission_is_counted_and_first_error_reported_later(self):
        text = self.run_case("feed-put-fail")
        self.assertIn("callbacks=1 callback_put_failed=1", text)
        self.assertIn("first callback submission error: submission failure?line must stay whole", text)
        self.assertNotIn("must not replace", text)

    def test_real_callback_allocation_failure_returns_without_null_mix_or_submit(self):
        text = self.run_case("feed-alloc-fail")
        self.assertIn("callbacks=1 callback_put_failed=0 callback_alloc_failed=1", text)

    def test_typed_game_observer_guards_lifetimes_and_logs_only_changed_form_view_boundary(self):
        text = self.run_case("game")
        self.assertIn("form=3", text)
        self.assertIn("first_person=1", text)
        self.assertIn("xy_outside_0_512=1", text)
        self.assertIn("xy_outside_0_512=9", text)
        self.assertIn("xy_outside_0_512=24", text)
        self.assertIn("position_ieee754=bf800000", text)
        self.assertEqual(sum("position_ieee754=" in line for line in text.splitlines()), 5)

    def test_the_summary_line_is_byte_identical_to_mains(self):
        # Counters and a final media snapshot with fixed values: main's logger printed exactly this summary text, and the observation line is a separate one.
        text = self.run_case("golden")
        lines = session_lines(text)
        self.assertEqual([body for kind, body in lines if kind == "summary"], [GOLDEN_SUMMARY])
        self.assertEqual(lines[-1][0], "summary")

    def obs_text(self, **values):
        pairs = [part.split("=") for part in OBS_ZERO.split()]
        return " ".join(f"{name}={values.get(name, value)}" for name, value in pairs)

    def check_obs_lines(self, text, **values):
        """one audio-obs line with these values (zero elsewhere), immediately before the summary, which is the last line"""
        lines = session_lines(text)
        self.assertEqual([kind for kind, _ in lines][-2:], ["audio-obs", "summary"])
        self.assertEqual(lines[-2][1], self.obs_text(**values))
        self.assertTrue(text.rstrip().endswith(lines[-1][1]))
        self.assertEqual(len(text.splitlines()), len(lines))

    @unittest.skipUnless(OBS_API, "the observation API is patch 0187's")
    def test_observations_add_saturate_keep_the_largest_ignore_a_bad_index_and_print_one_line(self):
        self.check_obs_lines(self.run_case("obs"), music_risk_refills=5, music_pair_refills=4294967295, music_refill_us_max=700, device_rate=48000)

    @unittest.skipUnless(OBS_API, "the observation API is patch 0187's")
    def test_observation_counts_and_maxima_are_exact_across_threads(self):
        self.check_obs_lines(self.run_case("obs-threads"), music_stale_halves=8000, music_voices_max=7999)

    @unittest.skipUnless(OBS_API, "the observation API is patch 0187's")
    def test_the_real_callback_measures_the_time_between_callbacks_without_the_session_clock(self):
        self.check_obs_lines(self.run_case("obs-feed-gap"), audio_gap_us_max=31000, audio_callbacks_late=1)

    @unittest.skipUnless(OBS_API, "the observation API is patch 0187's")
    def test_events_the_session_limits_drop_are_counted(self):
        text = self.run_case("limits")
        last = [body for kind, body in session_lines(text) if kind == "audio-obs"][-1]
        self.assertIn("events_dropped=6952 errors_dropped=8489", last)

    @unittest.skipUnless(OBS_API, "the observation API is patch 0187's")
    def test_the_largest_values_fit_both_lines(self):
        text = self.run_case("obs-max")
        lines = session_lines(text)
        self.assertEqual([kind for kind, _ in lines], ["audio-obs", "summary"])
        everything = " ".join(f"{name}=4294967295" for name, _ in (part.split("=") for part in OBS_ZERO.split()))
        self.assertEqual(lines[0][1], everything)
        summary = lines[1][1]
        self.assertTrue(summary.endswith("output_device_unobserved=1"), summary[-60:])
        self.assertIn("monitor_put_failed=4294967295 media_final=1 mixed_music_frames=18446744073709551615", summary)
        self.assertIn("retired_queue_observations=18446744073709551615 input=44100Hz-S16LE-stereo", summary)

    @unittest.skipUnless(OBS_API, "the observation API is patch 0187's")
    def test_an_observation_index_out_of_range_touches_nothing_under_the_address_sanitizer(self):
        folder = self.out / "sanitized"
        folder.mkdir()
        binary = folder / "driver"
        command = [*shlex.split(os.environ.get("CXX", "c++")), "-std=c++17", "-pthread", "-O1", "-w", "-fsanitize=address,undefined",
                   "-I" + str(self.out), '-DCE_VERSION="test"', "-DOBS_API", str(self.out / "driver.cpp"), "-o", str(binary)]
        build = subprocess.run(command, capture_output=True, text=True, timeout=120)
        self.assertEqual(build.returncode, 0, build.stderr[-2000:])
        result = subprocess.run([str(binary), "obs"], capture_output=True, text=True, timeout=20,
                                env={k: v for k, v in os.environ.items() if not k.startswith("BEA_")})
        self.assertEqual((result.returncode, result.stdout), (0, "PASS\n"), result.stderr[-1500:])

    @unittest.skipUnless(OBS_API, "the observation API is patch 0187's")
    def test_mutants_of_the_observations_are_caught(self):
        logger = ROOT / "port/platform/common/portlog.cpp"
        source = logger.read_text()
        guard = "if (__atomic_load_n(&gEnabled, __ATOMIC_ACQUIRE) != 1 || which < 0 || which >= PORT_SESSION_OBSERVATIONS) return;"
        mutants = (
            # name, text that must occur once, its replacement, the mode that must fail (or None: the line checks must), the environment
            ("no-saturation", "~0u - n < amount ? ~0u : n + amount", "n + amount", "obs", {}),
            ("max-takes-the-last", "while (value > n && !__atomic_compare_exchange_n(&gObs[which], &n, value, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}",
             "__atomic_store_n(&gObs[which], value, __ATOMIC_RELAXED);", "obs", {}),
            ("add-without-admission", "the audio callback never calls getenv or SDL here.\n\t" + guard,
             "the audio callback never calls getenv or SDL here.\n\tif (which < 0 || which >= PORT_SESSION_OBSERVATIONS) return;",
             "disabled", {"BEA_PORT_TAPE": "selected", "BEA_PORT_CE_LAYER": ""}),
            ("max-without-admission", "unsigned value)\n{\n\t" + guard, "unsigned value)\n{\n\tif (which < 0 || which >= PORT_SESSION_OBSERVATIONS) return;",
             "disabled", {"BEA_PORT_TAPE": "selected", "BEA_PORT_CE_LAYER": ""}),
            ("no-index-check", "unsigned amount)\n{\n\t// The same already-published admission as PortSessionCount: the audio callback never calls getenv or SDL here.\n\t"
             "if (__atomic_load_n(&gEnabled, __ATOMIC_ACQUIRE) != 1 || which < 0 || which >= PORT_SESSION_OBSERVATIONS) return;",
             "unsigned amount)\n{\n\t// The same already-published admission as PortSessionCount: the audio callback never calls getenv or SDL here.\n\t"
             "if (__atomic_load_n(&gEnabled, __ATOMIC_ACQUIRE) != 1) return;", "obs", {"-fsanitize=address": ""}),
            ("dropped-events-not-counted", "\t\tPortSessionObserve(error ? PORT_OBS_ERRORS_DROPPED : PORT_OBS_EVENTS_DROPPED);\n", "", "limits", {}),
            ("dropped-kinds-swapped", "PortSessionObserve(error ? PORT_OBS_ERRORS_DROPPED : PORT_OBS_EVENTS_DROPPED);",
             "PortSessionObserve(error ? PORT_OBS_EVENTS_DROPPED : PORT_OBS_ERRORS_DROPPED);", "limits", {}),
            ("observation-line-after-the-summary", "\tLine(\"audio-obs\", obs);\n\tLine(\"summary\", text);\n", "\tLine(\"summary\", text);\n\tLine(\"audio-obs\", obs);\n", None, {}),
            ("two-names-swapped", "\"music_risk_refills\", \"music_pair_refills\"", "\"music_pair_refills\", \"music_risk_refills\"", None, {}),
            ("observation-buffer-too-small", "char obs[768];", "char obs[96];", None, {}),
            ("summary-buffer-too-small", "char text[1024];\n\tint used = snprintf(text, sizeof text, \"events=%u errors=%u\"", "char text[600];\n\tint used = snprintf(text, sizeof text, \"events=%u errors=%u\"", None, {}),
        )
        for name, old, new, mode, env in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, name)
                folder = self.out / f"mutant-{name}"
                folder.mkdir()
                (folder / "portlog.h").write_text(logger.with_suffix(".h").read_text())
                (folder / "portlog.cpp").write_text(source.replace(old, new))
                driver = folder / "driver.cpp"
                driver.write_text(self.template.replace("@LOGGER@", str(folder / "portlog.cpp")))
                binary = folder / "driver"
                sanitize = [flag for flag in env if flag.startswith("-fsanitize")]
                command = [*shlex.split(os.environ.get("CXX", "c++")), "-std=c++17", "-pthread", "-O1" if sanitize else "-O2", "-w", *sanitize,
                           "-I" + str(self.out), '-DCE_VERSION="test"', "-DOBS_API", str(driver), "-o", str(binary)]
                build = subprocess.run(command, capture_output=True, text=True, timeout=60)
                self.assertEqual(build.returncode, 0, build.stderr[-2000:])
                if mode:
                    environment = {k: v for k, v in os.environ.items() if not k.startswith("BEA_")}
                    environment.update({k: v for k, v in env.items() if not k.startswith("-")})
                    result = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=10, env=environment)
                    self.assertTrue(result.returncode != 0 or result.stdout != "PASS\n", f"mutant {name} survived mode {mode}")
                else:
                    big = "obs-max" if "buffer" in name else "obs"
                    result = subprocess.run([str(binary), big], capture_output=True, text=True, timeout=10,
                                            env={k: v for k, v in os.environ.items() if not k.startswith("BEA_")})
                    stderr = result.stderr
                    lines = session_lines(stderr)
                    if big == "obs":
                        values = dict(music_risk_refills=5, music_pair_refills=4294967295, music_refill_us_max=700, device_rate=48000)
                        detected = (len(lines) != 2 or [kind for kind, _ in lines][-2:] != ["audio-obs", "summary"] or lines[-2][1] != self.obs_text(**values))
                    else:
                        everything = " ".join(f"{n}=4294967295" for n, _ in (part.split("=") for part in OBS_ZERO.split()))
                        detected = (len(lines) != 2 or lines[0][1] != everything or not lines[1][1].endswith("output_device_unobserved=1"))
                    self.assertTrue(detected, f"mutant {name} survived the line checks")


if __name__ == "__main__":
    unittest.main()
