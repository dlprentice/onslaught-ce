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
int mixer_locks, mixer_unlocks, mixed, submitted, freed, error_reads;
bool allocation_failed, submission_failed;
bool gSessionAudio;
void *SDL_malloc(unsigned bytes) { return allocation_failed ? nullptr : std::malloc(bytes); }
void SDL_free(void *p) { ++freed; std::free(p); }
void SDL_LockMutex(void *) { ++mixer_locks; }
void SDL_UnlockMutex(void *) { ++mixer_unlocks; }
bool SDL_PutAudioStreamData(SDL_AudioStream *, const void *, int) { ++submitted; return !submission_failed; }
const char *SDL_GetError() { ++error_reads; return "submission failure\nline must stay whole"; }
struct Mixer { void *lock; void Mix(Sint16 *, int frames) { mixed += frames; } } gMixer;
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
        PortSessionPulse(300);
        PortSessionSummary();
        SessionGameUpdate();
        CHECK(gCounts[PORT_COUNT_AUDIO_CALLBACKS] == 0 && gAudioErrorState == 0 && gFrame == 0);
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
        text = (DRIVER.replace("@ADMISSION@", extract(ce.read_text(), r"bool PortCeLayer\(\)"))
                .replace("@LOGGER@", str(logger))
                .replace("@FEED@", extract(mixer.read_text(), r"void SDLCALL Feed\("))
                .replace("@GAME@", extract(game.read_text(), r"static void SessionGameUpdate\(\)")))
        source = cls.out / "driver.cpp"
        source.write_text(text)
        cls.receipt = {"inputs": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in (logger, logger.with_suffix('.h'), ce, mixer, game)}, "commands": [], "cases": []}
        cls.binaries = {}
        for public in (False, True):
            name = "public" if public else "private"
            binary = cls.out / name
            command = [*shlex.split(os.environ.get("CXX", "c++")), "-std=c++17", "-pthread", "-O2", "-Wall", "-Wextra",
                       "-I" + str(cls.out), *(['-DCE_VERSION="test"'] if public else []), str(source), "-o", str(binary)]
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
        self.assertEqual(len(text.splitlines()), 3)

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


if __name__ == "__main__":
    unittest.main()
