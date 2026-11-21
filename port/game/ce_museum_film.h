// SPDX-License-Identifier: GPL-3.0-only
// The Museum cinema's film playback: its sound level, the crossfade with the music, the frame order and the hold while
// the game is paused. This is the cinema's own behaviour (ce_museum_cinema.inc forwards to it), written against a
// small table of the Bink calls it makes, so a host test drives it with a fake film and a fake clock
// (port/tools/ce_museum_film_tests.py) and its mutants are killed there.
//
// - Level: the game's own films' rule (CBinkOpenThread::Run in src/DXFrontEndVideo.cpp): the career's sound effects
//   volume, floored at 0.2, squared, times 20000 on RAD's scale (32768 = the recording's level).
// - Crossfade: the film's sound rises from silence to that level over kFadeOut while the music falls to the duck over
//   the same time (ce_museum_music_duck.h), both linear in amplitude, so they cross at half level and are never both
//   at full. Closing the film raises the music again over kFadeIn. The fade is held with the film while paused.
// - Frame order: decode one frame ahead, as the front end does (BinkDoFrame, BinkNextFrame, then BinkWait for that
//   frame, then copy it): BinkWait times the frame decoded last, so the picture keeps to the sound.
// - Pause: while the game is paused the film is held, sound and clock (port/bink's PortBinkPause), and nothing is
//   decoded; it goes on together when play resumes.
#ifndef CE_MUSEUM_FILM_H
#define CE_MUSEUM_FILM_H

#include "ce_museum_music_duck.h"

namespace CeMuseumFilm {

// The Bink calls the film makes, on one opened film ('user').
struct Calls {
	void *user;
	void (*setVolume)(void *user, long radVolume);	// BinkSetVolume(film, 0, v)
	void (*doFrame)(void *user);					// BinkDoFrame
	void (*nextFrame)(void *user);					// BinkNextFrame
	int (*wait)(void *user);						// BinkWait: nonzero while the frame decoded last is not yet due
	int (*pause)(void *user, int hold);				// PortBinkPause; null when the port has none
	unsigned (*frameNum)(void *user);				// BINK::FrameNum
	unsigned (*frames)(void *user);					// BINK::Frames
};

struct Film {
	bool open, fading, held, exhausted;
	long volume;					// the film's level on RAD's scale
	double start;					// when its sound started (seconds on the cinema's clock), moved on by any hold
	double heldAt;
	unsigned shown;					// the frame on the screen, 0 before the first
};

inline Film None() { Film f = {false, false, false, false, 0, 0.0, 0.0, 0}; return f; }

inline long Volume(float soundVolume) {
	float volume = soundVolume;
	if (volume < 0.2f) volume = 0.2f;
	return static_cast<long>(volume * volume * 20000.f);
}

// The film's level 'now', on RAD's scale.
inline long Level(const Film &f, double now) {
	if (!f.fading) return f.volume;
	const double at = f.held ? f.heldAt : now;
	const double t = (at - f.start) / CeMuseumMusicDuck::kFadeOut;
	if (t >= 1.0) return f.volume;
	return t > 0.0 ? static_cast<long>(f.volume * t) : 0;
}

// A film was opened: start it silent, decode its first frame (which starts its sound) and duck the music, to silence
// under a film with a sound track and to the bed under one without.
inline void Start(Film *f, const Calls &c, float soundVolume, bool track, double now, CeMuseumMusicDuck::State *duck) {
	*f = None();
	f->open = true;
	f->fading = true;
	f->volume = Volume(soundVolume);
	c.setVolume(c.user, 0);
	c.doFrame(c.user);
	c.nextFrame(c.user);
	f->start = now;
	CeMuseumMusicDuck::To(duck, track ? CeMuseumMusicDuck::kSilent : CeMuseumMusicDuck::kBed, CeMuseumMusicDuck::kFadeOut, now);
}

// One step of the fade (from the visit's frame).
inline void Fade(Film *f, const Calls &c, double now) {
	if (!f->open || !f->fading || f->held) return;
	const long level = Level(*f, now);
	f->fading = now - f->start < CeMuseumMusicDuck::kFadeOut;
	c.setVolume(c.user, f->fading ? level : f->volume);
}

// What the screen does this frame.
enum Step { kHold, kWait, kShow, kEnded };

// Holds or lets go of the film with the game's pause, then says whether the frame decoded ahead is due (kShow: copy
// it, then call Shown), not yet due (kWait), or whether the film has shown its last frame and is due to close.
inline Step Tick(Film *f, const Calls &c, bool paused, double now) {
	if (!f->open) return kHold;
	if (paused != f->held && c.pause) {
		c.pause(c.user, paused ? 1 : 0);
		if (paused) f->heldAt = now;
		else f->start += now - f->heldAt;		// the fade goes on from where it was held
		f->held = paused;
	}
	if (paused) return kHold;
	if (c.wait(c.user)) return kWait;
	return f->exhausted ? kEnded : kShow;
}

// The frame decoded ahead was copied to the screen: note it and decode the next, unless it was the last.
inline void Shown(Film *f, const Calls &c) {
	const unsigned num = c.frameNum(c.user), frames = c.frames(c.user);
	f->shown = num > 1 ? num - 1 : frames;
	f->exhausted = f->shown >= frames;
	if (!f->exhausted) {
		c.doFrame(c.user);
		c.nextFrame(c.user);
	}
}

// The film was closed (Stop, its end, Next, leaving the room): the music comes back over kFadeIn.
inline void Closed(Film *f, double now, CeMuseumMusicDuck::State *duck) {
	if (f->open) CeMuseumMusicDuck::To(duck, 1.0f, CeMuseumMusicDuck::kFadeIn, now);
	*f = None();
}

}  // namespace CeMuseumFilm

#endif
