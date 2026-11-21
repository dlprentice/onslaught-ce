#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Museum cinema's film playback (port/game/ce_museum_film.h, which ce_museum_cinema.inc forwards to), with mutants.

A host driver plays a fake film through the production functions with a fake clock: the film's level (the game's own
films' rule), the crossfade with the music duck (ramp times; never both at full), the frame order (decode ahead; copy
only a frame that is due; the last frame ends the film), the hold while the game is paused (forwarded once, nothing
decoded, the fade resumes where it was) and the music's return when the film closes. Each mutant of ce_museum_film.h
below must make the driver fail. Nothing runs the game and no retail content is used.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
HEADER = GAME / "ce_museum_film.h"

DRIVER = r"""
#include "ce_museum_film.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace CeMuseumFilm;
namespace D = CeMuseumMusicDuck;
static void Need(bool ok, const char *why, double got = NAN) {
    if (ok) return;
    if (std::isnan(got)) std::fprintf(stderr, "FAIL: %s\n", why); else std::fprintf(stderr, "FAIL: %s (got %.4f)\n", why, got);
    std::exit(2);
}
static bool Near(double a, double b, double e = 1e-3) { return std::fabs(a - b) < e; }
struct Fake { long volume = -1; std::vector<long> volumes; int doFrames = 0, nexts = 0, waits = 0, pauses = 0, lastHold = -1;
              unsigned frameNum = 1, frames = 5; bool due = true; };
static Calls Of(Fake *f, bool pause = true) {
    Calls c = {};
    c.user = f;
    c.setVolume = [](void *u, long v) { Fake *f = (Fake *)u; f->volume = v; f->volumes.push_back(v); };
    c.doFrame = [](void *u) { ((Fake *)u)->doFrames++; };
    c.nextFrame = [](void *u) { Fake *f = (Fake *)u; f->nexts++; f->frameNum = f->frameNum >= f->frames ? 1 : f->frameNum + 1; };
    c.wait = [](void *u) { Fake *f = (Fake *)u; f->waits++; return f->due ? 0 : 1; };
    if (pause) c.pause = [](void *u, int hold) { Fake *f = (Fake *)u; f->pauses++; f->lastHold = hold; return 1; };
    c.frameNum = [](void *u) { return ((Fake *)u)->frameNum; };
    c.frames = [](void *u) { return ((Fake *)u)->frames; };
    return c;
}
int main() {
    // the level: the game's own films' rule
    Need(Volume(1.0f) == 20000 && Volume(0.8f) == 12800 && Volume(0.3f) == 1800, "level: sound volume squared x 20000");
    Need(Volume(0.1f) == 800 && Volume(0.0f) == 800, "level: floored at 0.2 of the sound volume");
    // start: silent, the first frame decoded ahead, the music ducked
    {
        Fake k; Calls c = Of(&k); Film f; D::State duck = D::Off();
        Start(&f, c, 0.8f, true, 10.0, &duck);
        Need(!k.volumes.empty() && k.volumes.front() == 0, "start: the film starts silent");
        Need(k.doFrames == 1 && k.nexts == 1 && k.frameNum == 2, "start: the first frame is decoded ahead");
        Need(f.open && f.volume == 12800, "start: the film is open at the sound volume's level");
        Need(Near(D::Gain(duck, 10.5), 0.0), "start: a film with a track ducks the music to silence");
        Fake k2; Film f2; D::State duck2 = D::Off();
        Start(&f2, Of(&k2), 0.8f, false, 10.0, &duck2);
        Need(Near(D::Gain(duck2, 11.0), D::kBed), "start: a film without a track ducks the music to the bed");
    }
    // the crossfade
    {
        Fake k; Calls c = Of(&k); Film f; D::State duck = D::Off();
        Start(&f, c, 1.0f, true, 20.0, &duck);
        double worst = 0, filmFullAt = -1, musicGoneAt = -1;
        for (int i = 0; i <= 30; ++i) {
            const double now = 20.0 + i / 30.0;
            Fade(&f, c, now);
            const double film = double(k.volume) / f.volume, music = D::Gain(duck, now);
            worst = std::fmax(worst, std::fmin(film, music));
            if (filmFullAt < 0 && film >= 1.0) filmFullAt = now - 20.0;
            if (musicGoneAt < 0 && music <= 0.0) musicGoneAt = now - 20.0;
            if (i == 0) Need(film < 0.01 && music > 0.99, "crossfade: at the start the film is silent and the music full");
        }
        Need(worst <= 0.5 + 0.02, "crossfade: never both at full (they cross at half level)", worst);
        Need(filmFullAt >= 0.45 && filmFullAt <= 0.55, "crossfade: the film reaches full level at 0.5 s", filmFullAt);
        Need(musicGoneAt >= 0.45 && musicGoneAt <= 0.55, "crossfade: the music reaches the duck at 0.5 s", musicGoneAt);
        Fade(&f, c, 20.25);	// after the fade, nothing moves the film's level
        Need(k.volume == f.volume, "crossfade: the film stays at its level once faded in");
        // closing: the music returns over a second
        Closed(&f, 30.0, &duck);
        Need(!f.open, "close: the film is closed");
        Need(Near(D::Gain(duck, 30.0), 0.0) && Near(D::Gain(duck, 30.5), 0.5) && Near(D::Gain(duck, 31.0), 1.0),
             "close: the music returns from silence to full over 1 s");
        D::State before = duck;
        Closed(&f, 40.0, &duck);
        Need(duck.armed == before.armed && duck.start == before.start, "close: closing no film changes nothing");
    }
    // the frame order
    {
        Fake k; k.frames = 3; Calls c = Of(&k); Film f; D::State duck = D::Off();
        Start(&f, c, 0.8f, true, 0.0, &duck);
        k.due = false;
        Need(Tick(&f, c, false, 0.01) == kWait && k.doFrames == 1, "order: a frame not yet due is not shown or decoded");
        k.due = true;
        for (unsigned frame = 1; frame <= 3; ++frame) {
            Need(Tick(&f, c, false, 0.1 * frame) == kShow, "order: a due frame is shown");
            Shown(&f, c);
            Need(f.shown == frame, "order: the frame shown is the one decoded ahead", f.shown);
            Need(k.doFrames == (frame < 3 ? int(frame) + 1 : 3), "order: the next is decoded after showing, none after the last", k.doFrames);
        }
        Need(f.exhausted && Tick(&f, c, false, 1.0) == kEnded, "order: after the last frame the film ends");
        Need(k.doFrames == 3 && k.nexts == 3, "order: nothing is decoded past the end");
    }
    // the pause
    {
        Fake k; Calls c = Of(&k); Film f; D::State duck = D::Off();
        Start(&f, c, 1.0f, true, 50.0, &duck);
        Fade(&f, c, 50.1);
        const long before = k.volume;
        const int waits = k.waits, frames = k.doFrames;
        Need(Tick(&f, c, true, 50.1) == kHold && k.pauses == 1 && k.lastHold == 1, "pause: the film is held once");
        Need(Tick(&f, c, true, 51.0) == kHold && k.pauses == 1, "pause: and not again");
        Need(k.waits == waits && k.doFrames == frames, "pause: nothing waits or decodes while held");
        Fade(&f, c, 52.0);
        Need(k.volume == before, "pause: the fade does not move while held");
        Need(Tick(&f, c, false, 53.1) == kShow && k.pauses == 2 && k.lastHold == 0, "pause: the film goes on when play resumes");
        Fade(&f, c, 53.2);
        Need(Near(double(k.volume) / f.volume, 0.4, 0.02), "pause: the fade goes on from where it was held", double(k.volume) / f.volume);
        Fake n; Calls cn = Of(&n, false); Film g; D::State d2 = D::Off();
        Start(&g, cn, 1.0f, true, 0.0, &d2);
        Need(Tick(&g, cn, true, 0.1) == kHold && n.doFrames == 1, "pause: without a port pause the picture is still held");
    }
    std::printf("film playback checks passed\n");
    return 0;
}
"""

MUTANTS = [
    # the crossfade
    ("film-fades-over-the-music-return-time", "const double t = (at - f.start) / CeMuseumMusicDuck::kFadeOut;",
     "const double t = (at - f.start) / CeMuseumMusicDuck::kFadeIn;"),
    ("film-starts-at-full", "\tc.setVolume(c.user, 0);", "\tc.setVolume(c.user, f->volume);"),
    ("film-never-fades", "\tf->fading = true;\n\tf->volume", "\tf->fading = false;\n\tf->volume"),
    ("music-ducks-slowly", "CeMuseumMusicDuck::kBed, CeMuseumMusicDuck::kFadeOut, now);",
     "CeMuseumMusicDuck::kBed, CeMuseumMusicDuck::kFadeIn, now);"),
    ("music-returns-fast", "CeMuseumMusicDuck::To(duck, 1.0f, CeMuseumMusicDuck::kFadeIn, now);",
     "CeMuseumMusicDuck::To(duck, 1.0f, CeMuseumMusicDuck::kFadeOut, now);"),
    ("music-not-restored-on-close", "if (f->open) CeMuseumMusicDuck::To(duck, 1.0f", "if (false) CeMuseumMusicDuck::To(duck, 1.0f"),
    ("silence-and-bed-swapped", "track ? CeMuseumMusicDuck::kSilent : CeMuseumMusicDuck::kBed",
     "track ? CeMuseumMusicDuck::kBed : CeMuseumMusicDuck::kSilent"),
    ("fade-stops-early", "f->fading = now - f->start < CeMuseumMusicDuck::kFadeOut;", "f->fading = now - f->start < 0.25;"),
    # the unmute (the level)
    ("level-not-floored", "\tif (volume < 0.2f) volume = 0.2f;\n", ""),
    ("level-not-squared", "volume * volume * 20000.f", "volume * 20000.f"),
    ("level-on-the-recording-scale", "volume * volume * 20000.f", "volume * volume * 32768.f"),
    ("level-ignores-the-setting", "\tf->volume = Volume(soundVolume);", "\tf->volume = Volume(1.0f);"),
    # the decode order
    ("no-decode-ahead", "\tc.doFrame(c.user);\n\tc.nextFrame(c.user);\n\tf->start = now;", "\tf->start = now;"),
    ("show-without-waiting", "\tif (c.wait(c.user)) return kWait;\n", ""),
    ("decode-before-noting-the-frame", "\tconst unsigned num = c.frameNum(c.user), frames = c.frames(c.user);",
     "\tc.doFrame(c.user);\n\tc.nextFrame(c.user);\n\tconst unsigned num = c.frameNum(c.user), frames = c.frames(c.user);"),
    ("decodes-past-the-end", "\tif (!f->exhausted) {", "\tif (true) {"),
    ("never-ends", "return f->exhausted ? kEnded : kShow;", "return kShow;"),
    # the pause
    ("pause-not-held", "\tif (paused) return kHold;\n", ""),
    ("pause-not-forwarded", "\t\tc.pause(c.user, paused ? 1 : 0);\n", ""),
    ("pause-forwarded-every-frame", "if (paused != f->held && c.pause) {", "if (c.pause) {"),
    ("fade-jumps-after-a-pause", "\t\telse f->start += now - f->heldAt;", "\t\telse {}"),
    ("fade-runs-while-held", "if (!f->open || !f->fading || f->held) return;", "if (!f->open || !f->fading) return;"),
]


def run(command, expect=0):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
    return result.returncode == expect, result.stdout + result.stderr


def main():
    failures = []

    def check(name, passed, detail=""):
        print(("ok   " if passed else "FAIL ") + name + ("" if passed else ("\n" + detail if detail else "")))
        if not passed:
            failures.append(name)

    out = ROOT / "local-data" / "museum-film-tests"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as folder:
        folder = Path(folder)
        (folder / "driver.cpp").write_text(DRIVER)
        compile_ = ["c++", "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror"]
        good, detail = run(compile_ + ["-I" + str(GAME), str(folder / "driver.cpp"), "-o", str(folder / "driver")])
        check("the film playback compiles", good, detail)
        if good:
            good, detail = run([str(folder / "driver")])
            check("the film playback holds", good, detail)
        text = HEADER.read_text()
        for label, old, new in MUTANTS:
            if text.count(old) != 1:
                check("mutant " + label + " applies", False, "source text not found once: " + old)
                continue
            mutant = folder / label
            mutant.mkdir()
            (mutant / HEADER.name).write_text(text.replace(old, new))
            (mutant / "ce_museum_music_duck.h").write_text((GAME / "ce_museum_music_duck.h").read_text())
            built, detail = run(compile_[:3] + ["-I" + str(mutant), str(folder / "driver.cpp"), "-o", str(mutant / "driver")])
            if not built:
                check("mutant " + label + " is caught", False, detail)
                continue
            caught, detail = run([str(mutant / "driver")], 2)
            check("mutant " + label + " is caught" + (": " + detail.strip().splitlines()[-1] if caught and detail.strip() else ""), caught)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
