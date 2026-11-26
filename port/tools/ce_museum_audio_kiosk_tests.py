#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host tests and causal mutants for Grand Museum Audio Kiosks (ce_museum_audio_kiosk.h/.cpp)."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
HEADER = GAME / "ce_museum_audio_kiosk.h"
CPP = GAME / "ce_museum_audio_kiosk.cpp"
CINEMA = GAME / "ce_museum_cinema.inc"

DRIVER = r"""
#include "ce_museum_audio_kiosk.h"
#include "ce_museum_exhibition.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace CeMuseumAudioKiosk;
namespace Grand = CeMuseumExhibition::Grand;

static void Need(bool ok, const char *why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(2);
    }
}

static bool AsciiOnly(const char *s) {
    if (!s || !*s) return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p)
        if (*p < 0x20u || *p > 0x7eu) return false;
    return true;
}

struct FakeBackend {
    bool allowStart = true;
    bool playing = false;
    int starts = 0;
    int stops = 0;
    char lastStem[64] = {};
};

static bool FakeStart(void *u, const char *stem) {
    FakeBackend *f = static_cast<FakeBackend *>(u);
    ++f->starts;
    if (!f->allowStart) return false;
    std::strncpy(f->lastStem, stem, sizeof(f->lastStem) - 1);
    f->lastStem[sizeof(f->lastStem) - 1] = 0;
    f->playing = true;
    return true;
}

static void FakeStop(void *u) {
    FakeBackend *f = static_cast<FakeBackend *>(u);
    ++f->stops;
    f->playing = false;
}

static bool FakeIsPlaying(void *u) {
    return static_cast<FakeBackend *>(u)->playing;
}

int main() {
    const Grand::RoomGraph graph = Grand::MakeRoomGraph();
    Need(kKioskCount == 5, "five wing audio kiosks");

    // 1. Validate kiosk placement, ASCII titles, and safe stems.
    for (unsigned i = 0; i < kKioskCount; ++i) {
        const KioskSpec &k = kKiosks[i];
        Need(AsciiOnly(k.wingLabel), "ASCII wing label");
        Need(Grand::RoomAt(graph, CeMuseumExhibition::Vec2{k.localX, k.localY}) == static_cast<Grand::RoomId>(k.wing),
             "kiosk stands in its declared wing");
        Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{k.localX, k.localY}) == CeMuseumExhibition::kWalkBlocked,
             "kiosk pedestal blocks visitor");
        Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{k.localX + k.facingX * 1.4f, k.localY + k.facingY * 1.4f})
                 == CeMuseumExhibition::kWalkOpen,
             "visitor can stand in front of every kiosk");
        for (unsigned t = 0; t < kTracksPerKiosk; ++t) {
            Need(SafeStem(k.tracks[t].stem), "valid safe stem");
            Need(AsciiOnly(k.tracks[t].title), "ASCII track title");
        }
    }

    // 2. SafeStem rejects path traversal, extensions, spaces, empty, oversize.
    Need(!SafeStem(nullptr) && !SafeStem(""), "empty stem refused");
    Need(!SafeStem("../secret") && !SafeStem("a/b") && !SafeStem("a\\b"), "path traversal refused");
    Need(!SafeStem("110_clip.ogg") && !SafeStem("clip name"), "extension and space refused");
    Need(!SafeStem("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), "64-char stem refused");
    Need(SafeStem("110_enemy-engaged"), "valid stem accepted");

    // 3. PickKiosk ray-picking.
    const KioskSpec &k0 = kKiosks[0];
    Need(PickKiosk(k0.localX, k0.localY + 2.0f, 1.7f, 0.0f, -2.0f, -0.45f, k0.wing) == 0, "direct aim hits kiosk");
    Need(PickKiosk(k0.localX, k0.localY + 2.0f, 1.7f, 0.0f, -2.0f, -0.45f, CeMuseumTour::kWingMuspell) == -1,
         "pick respects active wing");
    Need(PickKiosk(k0.localX, k0.localY + 2.0f, 1.7f, 0.0f, 1.0f, 0.0f, k0.wing) == -1, "looking away misses kiosk");

    // 4. Operate start, music ducking, pause safety, stop & track advance, cinema exclusion, wing exit.
    State s = {};
    Reset(&s);
    FakeBackend fb;
    const Backend be = {&fb, FakeStart, FakeStop, FakeIsPlaying};

    Need(DuckHundredths(s, 0.0) == 0, "idle kiosk leaves music at 0 dB");
    // Refuses to start while cinema plays.
    Need(!Operate(&s, 0, be, 1.0, true), "mutual exclusion: refused while cinema plays");
    Need(s.phase == kPhaseIdle && fb.starts == 0, "no backend call during cinema");

    // Starts clip 0 and ducks music cleanly to bed.
    Need(Operate(&s, 0, be, 2.0, false), "starts track 0");
    Need(s.phase == kPhasePlaying && s.activeKiosk == 0 && std::strcmp(fb.lastStem, kKiosks[0].tracks[0].stem) == 0,
         "playing track 0");
    Need(DuckHundredths(s, 2.5) == -1806, "ducks music to -18 dB bed after fade-out");

    // Game pause holds playback even if backend reports not playing.
    fb.playing = false;
    Step(&s, k0.wing, true, false, be, 3.0);
    Need(s.phase == kPhasePlaying && s.trackIndex[0] == 0, "pause holds kiosk state");

    // Unpausing notices finished clip, stops, ramps music back, and advances to track 1.
    Step(&s, k0.wing, false, false, be, 4.0);
    Need(s.phase == kPhaseIdle && s.trackIndex[0] == 1, "finished clip advances track index");
    Need(DuckHundredths(s, 5.0) == 0, "music returns to 0 dB after stop");

    // Start track 1, then manual repeat press stops and advances to track 2.
    Need(Operate(&s, 0, be, 6.0, false), "starts track 1");
    Need(std::strcmp(fb.lastStem, kKiosks[0].tracks[1].stem) == 0, "track 1 selected");
    Need(Operate(&s, 0, be, 6.5, false), "repeat press stops track 1");
    Need(s.phase == kPhaseIdle && s.trackIndex[0] == 2, "repeat press advances to track 2");

    // Start track 2, then leaving the wing stops playback and restores music.
    Need(Operate(&s, 0, be, 7.0, false), "starts track 2");
    Step(&s, CeMuseumTour::kWingAtrium, false, false, be, 7.5);
    Need(s.phase == kPhaseIdle && !fb.playing, "wing exit stops active kiosk");

    // Starting when cinema flips on stops kiosk immediately.
    Need(Operate(&s, 0, be, 8.0, false), "starts again");
    Step(&s, k0.wing, false, true, be, 8.2);
    Need(s.phase == kPhaseIdle && !fb.playing, "cinema start stops kiosk");

    // Missing / dummy-audio refusal degrades cleanly without ducking music,
    // and re-pressing the same kiosk advances past the missing clip to the next dispatch.
    fb.allowStart = false;
    Reset(&s);
    Need(!Operate(&s, 1, be, 10.0, false), "failed start returns false");
    Need(s.phase == kPhaseUnavailable && s.activeKiosk == 1 && s.trackIndex[1] == 0
         && DuckHundredths(s, 11.0) == 0,
         "unavailable clip leaves music unducked");
    fb.allowStart = true;
    Need(Operate(&s, 1, be, 11.5, false), "repeat press after unavailable clip tries next dispatch");
    Need(s.phase == kPhasePlaying && s.trackIndex[1] == 1
         && std::strcmp(fb.lastStem, kKiosks[1].tracks[1].stem) == 0,
         "unavailable repeat press advances trackIndex to next dispatch");

    std::printf("ce_museum_audio_kiosk_tests: all checks passed\n");
    return 0;
}
"""

MUTANTS = [
    ("allow-path-traversal", "const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||",
     "const bool ok = c == '/' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||"),
    ("ignore-cinema-exclusion", "if (cinemaPlaying) return false;", "if (false) return false;"),
    ("no-wing-exit-stop", "if (s->activeKiosk >= 0 && (kKiosks[s->activeKiosk].wing != currentWing || cinemaPlaying))",
     "if (false && s->activeKiosk >= 0 && (kKiosks[s->activeKiosk].wing != currentWing || cinemaPlaying))"),
    ("no-music-duck", "CeMuseumMusicDuck::To(&s->duck, CeMuseumMusicDuck::kBed, CeMuseumMusicDuck::kFadeOut, now);",
     "(void)now;"),
    ("ignore-pause-hold", "if (s->phase == kPhasePlaying && !paused)", "if (s->phase == kPhasePlaying)"),
    ("no-track-advance", "s->trackIndex[kiosk] = (s->trackIndex[kiosk] + 1) % kTracksPerKiosk;", "(void)kiosk;"),
    ("no-unavailable-advance", "s->trackIndex[kiosk] = nextTrack;", "(void)nextTrack;"),
]


def run(cmd, expect=0):
    res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return res.returncode == expect, res.stdout + res.stderr


def main():
    failures = []

    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + ("" if ok else ("\n" + detail if detail else "")))
        if not ok:
            failures.append(name)

    out = ROOT / "local-data" / "museum-audio-kiosk-tests"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        d = Path(tmp)
        (d / "driver.cpp").write_text(DRIVER)
        flags = ["c++", "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        ok, detail = run(flags + ["-I" + str(GAME), str(d / "driver.cpp"), "-o", str(d / "driver")])
        check("audio kiosk driver builds with ASan/UBSan", ok, detail)
        if ok:
            ok, detail = run([str(d / "driver")])
            check("audio kiosk state, safety, ducking and mutual exclusion hold", ok, detail)

        src = HEADER.read_text()
        for label, old, new in MUTANTS:
            if src.count(old) != 1:
                check("mutant anchor " + label, False, "not found once: " + old)
                continue
            m = d / label
            m.mkdir()
            (m / HEADER.name).write_text(src.replace(old, new, 1))
            built, detail = run(["c++", "-std=c++14", "-O1", "-I" + str(m), "-I" + str(GAME),
                                 str(d / "driver.cpp"), "-o", str(m / "driver")])
            if not built:
                check("mutant " + label + " caught", False, detail)
                continue
            caught, _ = run([str(m / "driver")], 2)
            check("mutant " + label + " caught", caught)

    cpp = CPP.read_text()
    cinema = CINEMA.read_text()
    check("runtime uses COggFileRead + CreateSampleFromData with 1 MiB cap",
          "COggFileRead" in cpp and "kMaxDecodeBytes" in cpp and "CreateSampleFromData" in cpp)
    check("runtime keeps COggFileRead on stack and 1 MiB PCM buffer on CRT heap (off retail heap)",
          "COggFileRead reader;" in cpp and "new COggFileRead" not in cpp
          and "std::malloc(kMaxDecodeBytes)" in cpp and "std::free(pcm)" in cpp)
    check("runtime checks dummy-audio / playback failure and cleans up including gOwner.Shutdown()",
          "SOUND.IsInitialised()" in cpp and "IsSamplePlaying" in cpp and "ReleaseSample();" in cpp
          and "gOwner.Shutdown();" in cpp[cpp.index("void RuntimeShutdown()"):])
    check("PortCeMuseumMusicDuck combines cinema and kiosk ducking",
          "CeMuseumAudioKiosk::RuntimeMusicDuckHundredths()" in cinema)
    check("no forbidden words in kiosk labels",
          not re.search(r'"[^"]*(?:prototype|experimental|carrier|ce\.ini)[^"]*"', HEADER.read_text(), re.I))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
