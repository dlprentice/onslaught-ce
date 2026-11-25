#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Museum cinema's music duck (port/game/ce_museum_music_duck.h, applied by patch 0258): plain arithmetic, with mutants.

Checks: unarmed the extra gain is exactly 0 dB (the never-armed case: no visit, no film); a film with a track fades the
music to silence over kFadeOut and one without to the -18 dB bed; stopping fades back over kFadeIn and disarms; a new
ramp starts from where the music is, so Stop then Play never jumps. It also reads the port function and the patch to
check that no visit means 0 dB, that every exit path (Close, Reset) restores, and that the patch adds the duck to the
music stream's own volume and nothing else. Nothing runs the game and no retail content is used.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
HEADER = GAME / "ce_museum_music_duck.h"
CINEMA = GAME / "ce_museum_cinema.inc"
PATCH = ROOT / "port/patches/0258-ce-museum-music-duck.patch"

DRIVER = r"""
#include "ce_museum_music_duck.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace CeMuseumMusicDuck;
static void Need(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(2); } }
static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
int main() {
    State s = Off();
    for (double t : {0.0, 1.0, 1e6}) Need(Hundredths(Gain(s, t)) == 0, "unarmed: exactly 0 dB at any time");
    Settle(&s, 5.0); Need(!s.armed && Hundredths(Gain(s, 5.0)) == 0, "settling an unarmed duck keeps it 0 dB");
    Need(Hundredths(1.0f) == 0 && Hundredths(2.0f) == 0, "full or more is 0 dB");
    Need(Hundredths(0.0f) == kMin && Hundredths(-1.0f) == kMin, "silence is DirectSound's minimum");
    Need(Hundredths(0.5f) == -602, "half amplitude is -6.02 dB");
    Need(Hundredths(kBed) == -1806, "the bed is -18.06 dB");
    // a film with a track: 0.5 s to silence
    To(&s, kSilent, kFadeOut, 10.0);
    Need(Near(Gain(s, 10.0), 1.0f), "the fade starts at full music");
    Need(Near(Gain(s, 10.25), 0.5f), "half way down after a quarter second");
    Need(Hundredths(Gain(s, 10.5)) == kMin && Hundredths(Gain(s, 30.0)) == kMin, "silent from 0.5 s and while the film plays");
    Need(Gain(s, 10.1) > Gain(s, 10.2) && Gain(s, 10.2) > Gain(s, 10.3), "the fade falls steadily");
    Settle(&s, 30.0); Need(s.armed, "a duck held under a film is not settled away");
    // stop: 1 s back to full, then disarmed
    To(&s, 1.0f, kFadeIn, 40.0);
    Need(Hundredths(Gain(s, 40.0)) == kMin, "the return starts from silence: no jump");
    Need(Near(Gain(s, 40.5), 0.5f), "half way back after half a second");
    Need(Near(Gain(s, 41.0), 1.0f) && Hundredths(Gain(s, 41.0)) == 0, "full music after a second");
    Settle(&s, 40.9); Need(s.armed, "not settled before the return is done");
    Settle(&s, 41.0); Need(!s.armed && Hundredths(Gain(s, 99.0)) == 0, "settled: unarmed and 0 dB");
    // a film without a track: the bed
    To(&s, kBed, kFadeOut, 50.0);
    Need(Near(Gain(s, 50.5), kBed) && Hundredths(Gain(s, 60.0)) == -1806, "a silent film keeps a -18 dB bed");
    // a new ramp starts where the music is (Stop, then Next and Play mid-return)
    To(&s, 1.0f, kFadeIn, 70.0);
    const float mid = Gain(s, 70.4f);
    To(&s, kSilent, kFadeOut, 70.4);
    Need(Near(Gain(s, 70.4), mid), "a new film's fade starts from the music's current level");
    Need(Hundredths(Gain(s, 70.9)) == kMin, "and reaches silence 0.5 s later");
    // Off at once (leaving the Museum)
    s = Off(); Need(Hundredths(Gain(s, 70.5)) == 0, "Off restores 0 dB at once");
    // a zero-length ramp is immediate, a negative gain is clamped
    To(&s, -3.0f, 0.0f, 80.0); Need(Hundredths(Gain(s, 80.0)) == kMin, "an immediate ramp lands at once, clamped at silence");
    std::printf("music duck arithmetic holds\n");
    return 0;
}
"""

MUTANTS = [
    ("unarmed-not-zero", "if (!s.armed) return 1.0f;", "if (!s.armed) return 0.5f;"),
    ("no-ramp", "return s.from + (s.to - s.from) * t;", "return s.to;"),
    ("ramp-backwards", "return s.from + (s.to - s.from) * t;", "return s.to + (s.from - s.to) * t;"),
    ("jump-on-retarget", "s->from = here;", "s->from = 1.0f;"),
    ("never-settles", "if (s->armed && s->to >= 1.0f && Gain(*s, now) >= 1.0f) *s = Off();", "(void)s; (void)now;"),
    ("settles-under-film", "if (s->armed && s->to >= 1.0f && Gain(*s, now) >= 1.0f) *s = Off();", "*s = Off(); (void)now;"),
    ("bed-is-silence", "const float kBed = 0.125f;", "const float kBed = 0.0f;"),
    ("slow-fade-out", "const float kFadeOut = 0.5f;", "const float kFadeOut = 2.0f;"),
    ("power-not-amplitude", "2000.0 * log10", "1000.0 * log10"),
    ("silence-not-minimum", "if (gain <= 0.00001f) return kMin;", "if (gain <= 0.00001f) return -2000;"),
]


def run(command, expect=0):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    return result.returncode == expect, result.stdout + result.stderr


def main():
    failures = []

    def check(name, passed, detail=""):
        print(("ok   " if passed else "FAIL ") + name + ("" if passed else ("\n" + detail if detail else "")))
        if not passed:
            failures.append(name)

    out = ROOT / "local-data" / "museum-music-duck-tests"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as folder:
        folder = Path(folder)
        (folder / "driver.cpp").write_text(DRIVER)
        compile_ = ["c++", "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror"]
        good, detail = run(compile_ + ["-I" + str(GAME), str(folder / "driver.cpp"), "-o", str(folder / "driver")])
        check("the duck compiles", good, detail)
        if good:
            good, detail = run([str(folder / "driver")])
            check("the duck's arithmetic holds", good, detail)
        text = HEADER.read_text()
        for label, old, new in MUTANTS:
            if text.count(old) != 1:
                check("mutant " + label + " applies", False, "source text not found once: " + old)
                continue
            mutant = folder / label
            mutant.mkdir()
            (mutant / HEADER.name).write_text(text.replace(old, new))
            built, detail = run(compile_[:3] + ["-I" + str(mutant), str(folder / "driver.cpp"), "-o", str(mutant / "driver")])
            if not built:
                check("mutant " + label + " is caught", False, detail)
                continue
            caught, _ = run([str(mutant / "driver")], 2)
            check("mutant " + label + " is caught", caught)

    # The port function and the patch, read as source: the never-armed and every-exit conditions.
    cinema = CINEMA.read_text()
    fn = re.search(r'extern "C" long PortCeMuseumMusicDuck\(void\) \{(.*?)\n\}', cinema, re.S)
    check("the port function is found", bool(fn))
    if fn:
        body = fn.group(1)
        first = body.strip().splitlines()[0]
        check("no visit active: the duck is dropped and 0 dB returned before anything else",
              "!PortCeMuseumVisitActive()" in first and "Off()" in first and "return 0;" in first, first)
    close = re.search(r"void CinemaClose\(Cinema::Phase next\) \{(.*?)\n\}", cinema, re.S)
    check("closing a film (Stop, end, leaving the room, Next) ramps the music back to full",
          bool(close) and "CeMuseumFilm::Closed(&film, CinemaClock(), &duck);" in close.group(1))
    reset = re.search(r"void CinemaReset\(\) \{(.*?)\n\}", cinema, re.S)
    check("leaving the Museum drops the duck at once", bool(reset) and "Cinema::duck = CeMuseumMusicDuck::Off();" in reset.group(1))
    check("every CinemaClose caller is a film exit (no other path holds the duck)",
          set(re.findall(r"CinemaClose\((\w+(?:::\w+)?)\)", cinema)) <= {"Idle", "Finished", "Missing", "Cinema::Idle"})
    patch = PATCH.read_text()
    added = [line[1:] for line in patch.splitlines() if line.startswith("+") and not line.startswith("+++")]
    check("the patch only touches the music stream's volume", "--- a/PCMusicStream.cpp" in patch and patch.count("--- a/") == 1)
    check("the patch adds the duck to the stream's own volume, clamped at silence",
          any("portVolume += PortCeMuseumMusicDuck();" in line for line in added)
          and any("portVolume < -10000 ? -10000 : portVolume" in line for line in added))
    check("the patch's call is weak and CE-only", any("__attribute__((weak))" in line for line in added)
          and "#ifdef CE_VERSION" in patch and "if (PortCeMuseumMusicDuck)" in patch)
    # The film's behaviour is ce_museum_film.h, tested with its mutants by ce_museum_film_tests.py; here, its wiring.
    check("a film starts through CeMuseumFilm::Start with the career's sound effects volume and its track",
          "CeMuseumFilm::Start(&film, CinemaFilmCalls(), CAREER.GetSoundVolume(), track, CinemaClock(), &duck);" in cinema)
    fade = re.search(r"void CinemaFilmFade\(\) \{(.*?)\n\}", cinema, re.S)
    visitor = re.search(r"void CinemaVisitor\(float x,float y\) \{(.*?)\n\}", cinema, re.S)
    check("the crossfade steps from the visit's frame", bool(fade and visitor) and "CeMuseumFilm::Fade(" in fade.group(1)
          and "CinemaFilmFade();" in visitor.group(1))
    held = re.search(r"bool CinemaHeld\(\) \{(.*?)\n\}", cinema, re.S)
    clock = re.search(r"double CinemaClock\(\) \{(.*?)\n\}", cinema, re.S)
    check("the cinema's hold is the game's pause or Photo Mode, and its one clock stops for it",
          bool(held) and "GAME.IsPaused()" in held.group(1) and "PortCePhotoOn()" in held.group(1)
          and bool(clock) and "CeMuseumCinemaHeldClockRead(&held," in clock.group(1) and "CinemaHeld()" in clock.group(1))
    check("the film's frames, its crossfade, its close and the duck all read that one clock",
          cinema.count("QueryPerformanceCounter") == 1
          and "CeMuseumMusicDuck::Settle(&Cinema::duck, now);" in cinema and "const double now = CinemaClock();" in cinema
          and "CeMuseumFilm::Fade(&Cinema::film, CinemaFilmCalls(), CinemaClock());" in cinema)
    check("the screen asks CeMuseumFilm::Tick with the cinema's hold, and copies only a frame that is due",
          "CeMuseumFilm::Tick(&film,calls,CinemaHeld(),CinemaClock());" in cinema
          and "if(step==CeMuseumFilm::kShow) {" in cinema and "CeMuseumFilm::Shown(&film,calls);" in cinema
          and "if(step==CeMuseumFilm::kEnded) { CinemaClose(Finished);return; }" in cinema)
    check("nothing in the duck writes the career", "CAREER.Set" not in cinema and "MUSIC.SetVolume" not in cinema)

    # The held clock and the open-failure rule, as arithmetic (ce_museum_cinema.h), with mutants.
    held_driver = r"""
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "ce_museum_cinema_console.h"
#include "ce_museum_cinema.h"
static void Need(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(2); } }
static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }
int main() {
    CeMuseumCinemaHeldClock c = CeMuseumCinemaHeldClockNew();
    Need(Near(CeMuseumCinemaHeldClockRead(&c, 1.0, false), 1.0), "running clock follows raw time");
    Need(Near(CeMuseumCinemaHeldClockRead(&c, 2.5, false), 2.5), "still following");
    Need(Near(CeMuseumCinemaHeldClockRead(&c, 3.0, true), 3.0), "the read that first sees the hold stops there");
    Need(Near(CeMuseumCinemaHeldClockRead(&c, 9.0, true), 3.0), "held: six seconds of pause pass and the clock has not moved");
    Need(Near(CeMuseumCinemaHeldClockRead(&c, 60.0, true), 3.0), "held: a minute in Photo Mode and it has not moved");
    Need(Near(CeMuseumCinemaHeldClockRead(&c, 61.0, false), 3.0), "released: it continues from where it stopped, with no jump");
    Need(Near(CeMuseumCinemaHeldClockRead(&c, 62.5, false), 4.5), "then runs at the raw rate again");
    Need(Near(CeMuseumCinemaHeldClockRead(&c, 63.0, true), 5.0), "a second hold stops it again");
    Need(Near(CeMuseumCinemaHeldClockRead(&c, 70.0, false), 5.0), "and releases without a jump again");
    Need(Near(CeMuseumCinemaHeldClockRead(&c, 71.0, false), 6.0), "two holds, 65 s skipped in all");
    double last = -1.0;
    CeMuseumCinemaHeldClock m = CeMuseumCinemaHeldClockNew();
    for (int i = 0; i < 4000; ++i) {
        const bool hold = ((i / 37) % 3) == 1;
        const double v = CeMuseumCinemaHeldClockRead(&m, 0.016 * i, hold);
        Need(v >= last - 1e-12, "the held clock never runs backwards");
        if (hold && i > 0 && (((i - 1) / 37) % 3) == 1) Need(Near(v, last), "it is still for every read of a hold");
        last = v;
    }
    Need(Near(CeMuseumCinemaHeldClockRead(NULL, 7.0, true), 7.0), "a missing clock passes raw time through");
    Need(CeMuseumCinemaAfterOpenFailure(true), "the first scan finding nothing playable means films cannot be shown");
    Need(!CeMuseumCinemaAfterOpenFailure(false), "one chosen film failing leaves the console usable");
    return 0;
}
"""
    cinema_h = (GAME / "ce_museum_cinema.h").read_text()
    held_mutants = [
        ("clock-runs-while-held", "const double at = clock->held ? clock->heldAt : raw;", "const double at = raw;"),
        ("clock-jumps-on-release", "if (raw > clock->heldAt) clock->skipped += raw - clock->heldAt;", "(void)raw;"),
        ("clock-never-holds", "if (hold && !clock->held) { clock->held = true; clock->heldAt = raw; }",
         "if (false && hold && !clock->held) { clock->held = true; clock->heldAt = raw; }"),
        ("one-bad-film-locks-console", "return scanningWholeList;", "return true || scanningWholeList;"),
    ]
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        (folder / "held.cpp").write_text(held_driver)
        built, detail = run(["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror", "-I" + str(GAME),
                             str(folder / "held.cpp"), "-o", str(folder / "held")])
        check("the held clock driver builds", built, detail)
        if built:
            good, detail = run([str(folder / "held")])
            check("a hold stops the cinema clock, a release continues it without a jump", good, detail)
        for label, old_text, new_text in held_mutants:
            if cinema_h.count(old_text) != 1:
                check("held-clock mutant " + label + " applies", False, "not found once: " + old_text)
                continue
            mdir = folder / label
            mdir.mkdir()
            (mdir / "ce_museum_cinema.h").write_text(cinema_h.replace(old_text, new_text))
            built, detail = run(["g++", "-std=c++14", "-O1", "-I" + str(mdir), "-I" + str(GAME),
                                 str(folder / "held.cpp"), "-o", str(mdir / "held")])
            if not built:
                check("held-clock mutant " + label + " is caught", False, detail)
                continue
            caught, _ = run([str(mdir / "held")], 2)
            check("held-clock mutant " + label + " is caught", caught)

    # A film that cannot be shown never locks the console, and the projector closes before the sound device is rebuilt.
    opened = re.search(r"void CinemaOpen\(unsigned selected\) \{(.*?)\n\}", cinema, re.S)
    start = re.search(r"bool CinemaStartFilm\(void \*, const CeMuseumCinemaCatalogue::Entry &entry\) \{(.*?)\n\}", cinema, re.S)
    screen = re.search(r"void CinemaScreen\(IDirect3DDevice9 \*dev\) \{(.*?)\n\}", cinema, re.S)
    check("only the first whole-list scan can leave the console saying films cannot be shown",
          bool(opened) and "const bool scanning = s.firstPlayable;" in opened.group(1)
          and "CinemaClose(CeMuseumCinemaAfterOpenFailure(scanning) ? Missing : Idle);" in opened.group(1)
          and cinema.count("Missing : Idle") == 1
          and len(re.findall(r"CinemaClose\(Missing\)", cinema)) == 0)
    check("a film that will not start, a lost screen target and a frame that cannot be written all return to idle",
          bool(start) and "CinemaClose(Idle); return false;" in start.group(1) and "Missing" not in start.group(1)
          and bool(screen) and "Missing" not in screen.group(1) and screen.group(1).count("CinemaClose(Idle)") == 2)
    hall = (GAME / "ce_museum_hall.cpp").read_text()
    kiosk = (GAME / "ce_museum_audio_kiosk.cpp").read_text()
    reinit = re.search(r'extern "C" void PortCeMuseumCinemaBeforeSoundReinit\(void\) \{(.*?)\n\}', hall, re.S)
    before = re.search(r'extern "C" void PortCeSoundBeforeReinit\(void\) \{(.*?)\n\}', kiosk, re.S)
    check("the projector's film is closed before the sound device is rebuilt",
          bool(reinit) and "CinemaClose(Cinema::Idle);" in reinit.group(1)
          and bool(before) and "PortCeMuseumCinemaBeforeSoundReinit();" in before.group(1)
          and "extern \"C\" void PortCeMuseumCinemaBeforeSoundReinit(void);" in cinema_h)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
