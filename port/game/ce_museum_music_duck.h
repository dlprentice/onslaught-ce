// SPDX-License-Identifier: GPL-3.0-only
// The museum cinema's music duck: plain arithmetic, no game object. While a film plays the game's music stream is
// lowered (to silence under a film with its own sound, to a low bed under a silent one) and raised again when the
// film stops, ends or is left. The ramp is linear in amplitude and follows a clock the caller passes in seconds (the
// port's QueryPerformanceCounter, which the virtual clock drives), so the music volume steps with time, not with
// drawing. Patch 0258 adds the result, in DirectSound hundredths of a decibel, to the music stream's own volume
// (SetMusicStreamVolume); the stream's volume is set every music update (CMusic::UpdateStatus), so a change reaches
// the mix within one game update. Unarmed, the extra gain is exactly 0 dB.
#ifndef CE_MUSEUM_MUSIC_DUCK_H
#define CE_MUSEUM_MUSIC_DUCK_H

#include <math.h>

namespace CeMuseumMusicDuck {

const float kFadeOut = 0.5f;		// seconds from full music to the duck when a film starts
const float kFadeIn = 1.0f;			// seconds from the duck back to full music when it stops, ends or is left
const float kSilent = 0.0f;			// under a film with its own sound track
const float kBed = 0.125f;			// under a film without one: -18 dB, a low bed rather than silence
const long kMin = -10000;			// DirectSound's silence (DSBVOLUME_MIN)

struct State {
	bool armed;
	float from, to;					// amplitude gains, 1 = the music as the player set it
	double start;					// when the ramp began (seconds on the caller's clock)
	float length;					// the ramp's length in seconds (0 = at once)
};

inline State Off() { State s = {false, 1.0f, 1.0f, 0.0, 0.0f}; return s; }

inline float Gain(const State &s, double now) {
	if (!s.armed) return 1.0f;
	if (s.length <= 0.0f || now >= s.start + s.length) return s.to;
	if (now <= s.start) return s.from;
	const float t = static_cast<float>((now - s.start) / s.length);
	return s.from + (s.to - s.from) * t;
}

// Ramps from wherever the music is now to 'gain' over 'seconds'. A ramp back to full music disarms itself once done
// (Settle), so the duck holds no state after the film.
inline void To(State *s, float gain, float seconds, double now) {
	const float here = Gain(*s, now);
	s->armed = true;
	s->from = here;
	s->to = gain < 0.0f ? 0.0f : gain > 1.0f ? 1.0f : gain;
	s->start = now;
	s->length = seconds > 0.0f ? seconds : 0.0f;
}

inline void Settle(State *s, double now) {
	if (s->armed && s->to >= 1.0f && Gain(*s, now) >= 1.0f) *s = Off();
}

// The extra gain in hundredths of a decibel (0 = unchanged, kMin = silence).
inline long Hundredths(float gain) {
	if (gain >= 1.0f) return 0;
	if (gain <= 0.00001f) return kMin;
	const long h = static_cast<long>(lround(2000.0 * log10(static_cast<double>(gain))));
	return h < kMin ? kMin : h;
}

}  // namespace CeMuseumMusicDuck

#endif
