// SPDX-License-Identifier: GPL-3.0-only
// Ordinary CE diagnostics, independent of the game/virtual clocks. Integer-only formatting and read-only observers
// cannot consume game RNG or change simulation clock reads. Never called to format from the audio callback.
#include "portlog.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifdef CE_VERSION
bool PortCeLayer();
#endif

namespace
{
const unsigned kNormalLimit = 2048, kErrorLimit = 512;
unsigned gNormal, gErrors, gCounts[PORT_SESSION_COUNTERS];
unsigned gLast[PORT_SESSION_COUNTERS];
unsigned gFrame;
int gEnabled = -1;
unsigned gPulseSummaries;
unsigned gAudioErrorState;
char gAudioError[192];
unsigned char gMediaLock;
unsigned char gFinalMediaLock;
PortSessionFinalMedia gFinalMedia;
bool gFinalMediaPending;
struct Media { const void *token; PortSessionCategory kind; } gMedia[16];
const char *const kNames[] = {"game", "window", "video", "sound", "music", "film"};
const char *const kCounters[] = {"callbacks", "callback_put_failed", "callback_alloc_failed", "music_refills",
	"music_short_reads", "music_read_failed", "music_wait_failed", "monitor_queue_drops", "monitor_put_failed"};

struct ErrorGuard
{
	int error = errno;
#ifdef _WIN32
	DWORD last = GetLastError();
#endif
	~ErrorGuard()
	{
		errno = error;
#ifdef _WIN32
		SetLastError(last);
#endif
	}
};

void Clean(char *text)
{
	for (unsigned char *p = (unsigned char *)text; *p; ++p)
		if (*p < 32 || *p == 127) *p = '?';
}

void Line(const char *kind, const char *text)
{
	// A single stdio call keeps concurrent music/video worker lines whole. Elapsed time is SDL's host clock,
	// explicitly not the game's clock; reading it cannot advance a deterministic replay clock.
	unsigned long pid;
#ifdef _WIN32
	pid = (unsigned long)GetCurrentProcessId();
#else
	pid = (unsigned long)getpid();
#endif
	fprintf(stderr, PORT_LOG_PREFIX "session pid=%lu t_ms=%llu P=%u %s: %s\n",
		pid, (unsigned long long)SDL_GetTicks(),
		__atomic_load_n(&gFrame, __ATOMIC_RELAXED), kind, text);
}
}

bool PortSessionEnabled()
{
#ifdef CE_VERSION
	int enabled = __atomic_load_n(&gEnabled, __ATOMIC_ACQUIRE);
	if (enabled >= 0) return enabled == 1;
	enabled = PortCeLayer() ? 1 : 0;
	// The CE drawing override is not a diagnostics override. Even screenshot tests which force that layer on
	// keep the game clock, recording and replay routes free of ordinary session observations.
	const char *const names[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"};
	for (const char *name : names)
	{
		const char *value = getenv(name);
		if (value && *value) enabled = 0;
	}
	__atomic_store_n(&gEnabled, enabled, __ATOMIC_RELEASE);
	return enabled == 1;
#else
	return false;
#endif
}

void PortSessionLog(PortSessionCategory category, bool error, const char *format, ...)
{
	if (!PortSessionEnabled() || category < 0 || category >= PORT_SESSION_CATEGORIES) return;
	ErrorGuard guard;
	unsigned *count = error ? &gErrors : &gNormal;
	unsigned limit = error ? kErrorLimit : kNormalLimit;
	unsigned n = __atomic_load_n(count, __ATOMIC_RELAXED);
	while (n <= limit && !__atomic_compare_exchange_n(count, &n, n + 1u, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
	if (n >= limit)
	{
		if (n == limit) Line("log", error ? "error event limit reached; counters and final summary remain active" :
			"ordinary event limit reached; errors, counters and final summary remain active");
		return;
	}
	char text[640];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof text, format, args);
	va_end(args);
	Clean(text);
	Line(kNames[category], text);
}

void PortSessionAsset(PortSessionCategory category, const char *action, const char *path, long result)
{
	if (!PortSessionEnabled()) return;
	// Only an asset's basename, never an absolute game/user folder or career name, enters these lines.
	const char *base = path ? path : "(none)";
	for (const char *p = base; *p; ++p)
		if (*p == '/' || *p == '\\') base = p + 1;
	PortSessionLog(category, result < 0, "%s asset=%.120s result=%08lx", action, base, (unsigned long)result);
}

void PortSessionMediaActive(PortSessionCategory category, const void *token, bool playing)
{
	if (!PortSessionEnabled() || !token || (category != PORT_SESSION_MUSIC && category != PORT_SESSION_FILM)) return;
	while (__atomic_test_and_set(&gMediaLock, __ATOMIC_ACQUIRE)) {}
	int music = 0, film = 0, freeSlot = -1, found = -1;
	for (int i = 0; i < 16; ++i)
	{
		if (!gMedia[i].token) freeSlot = i;
		else if (gMedia[i].token == token && gMedia[i].kind == category) found = i;
	}
	bool overflow = playing && found < 0 && freeSlot < 0;
	if (playing && found < 0 && freeSlot >= 0) gMedia[freeSlot] = {token, category};
	if (!playing && found >= 0) gMedia[found].token = NULL;
	for (const Media &m : gMedia)
	{
		if (m.token && m.kind == PORT_SESSION_MUSIC) ++music;
		if (m.token && m.kind == PORT_SESSION_FILM) ++film;
	}
	__atomic_clear(&gMediaLock, __ATOMIC_RELEASE);
	PortSessionLog(category, overflow, "start_lifetime=%s outstanding_music_starts=%d outstanding_film_starts=%d "
		"request_lifetimes_overlap=%d tracking_overflow=%d (not active-voice or audible-content proof; EOF may precede close)",
		playing ? "begun" : "stopped_or_closed", music, film, music && film ? 1 : 0, overflow ? 1 : 0);
}

void PortSessionCount(PortSessionCounter counter)
{
	// The audio callback reads only this already-published main-thread admission. It never calls getenv or SDL.
	if (__atomic_load_n(&gEnabled, __ATOMIC_ACQUIRE) != 1) return;
	if (counter >= 0 && counter < PORT_SESSION_COUNTERS)
		__atomic_fetch_add(&gCounts[counter], 1u, __ATOMIC_RELAXED);
}

void PortSessionAudioError(const char *error)
{
	if (__atomic_load_n(&gEnabled, __ATOMIC_ACQUIRE) != 1) return;
	// Only the first callback error is copied, into fixed storage. No lock, formatting, clock or I/O on that thread.
	unsigned expected = 0;
	if (!__atomic_compare_exchange_n(&gAudioErrorState, &expected, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
	unsigned i = 0;
	if (error) for (; error[i] && i + 1 < sizeof gAudioError; ++i) gAudioError[i] = error[i];
	gAudioError[i] = 0;
	__atomic_store_n(&gAudioErrorState, 2u, __ATOMIC_RELEASE);
}

void PortSessionSetFinalMedia(const PortSessionFinalMedia *snapshot)
{
	if (!PortSessionEnabled() || !snapshot) return;
	ErrorGuard guard;
	while (__atomic_test_and_set(&gFinalMediaLock, __ATOMIC_ACQUIRE)) {}
	gFinalMedia = *snapshot;
	gFinalMediaPending = true;
	__atomic_clear(&gFinalMediaLock, __ATOMIC_RELEASE);
}

void PortSessionPulse(unsigned frame)
{
	if (!PortSessionEnabled()) return;
	__atomic_store_n(&gFrame, frame, __ATOMIC_RELAXED);
	if (frame % 300) return;
	// Count activity, but print periodic lines only when an error counter changed. This is not an underrun detector:
	// successful callback submission cannot establish that a physical output device played every sample.
	bool error = false;
	for (int i = 0; i < PORT_SESSION_COUNTERS; ++i)
	{
		unsigned n = __atomic_load_n(&gCounts[i], __ATOMIC_RELAXED);
		if (i != PORT_COUNT_AUDIO_CALLBACKS && i != PORT_COUNT_MUSIC_REFILLS && i != PORT_COUNT_MUSIC_SHORT_READS &&
			n != gLast[i]) error = true;
		gLast[i] = n;
	}
	if (error && gPulseSummaries < 32) { ++gPulseSummaries; PortSessionSummary(); }
}

void PortSessionSummary()
{
	if (!PortSessionEnabled()) return;
	ErrorGuard guard;
	unsigned expected = 2;
	if (__atomic_compare_exchange_n(&gAudioErrorState, &expected, 3u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		PortSessionLog(PORT_SESSION_AUDIO, true, "first callback submission error: %s", gAudioError);
	char text[1024];
	int used = snprintf(text, sizeof text, "events=%u errors=%u", __atomic_load_n(&gNormal, __ATOMIC_RELAXED),
		__atomic_load_n(&gErrors, __ATOMIC_RELAXED));
	for (int i = 0; i < PORT_SESSION_COUNTERS; ++i)
	{
		int n = snprintf(text + used, sizeof text - (size_t)used, " %s=%u", kCounters[i],
			__atomic_load_n(&gCounts[i], __ATOMIC_RELAXED));
		if (n < 0 || (size_t)n >= sizeof text - (size_t)used) break;
		used += n;
	}
	PortSessionFinalMedia media{};
	bool haveFinalMedia;
	while (__atomic_test_and_set(&gFinalMediaLock, __ATOMIC_ACQUIRE)) {}
	haveFinalMedia = gFinalMediaPending;
	if (haveFinalMedia) { media = gFinalMedia; gFinalMediaPending = false; }
	__atomic_clear(&gFinalMediaLock, __ATOMIC_RELEASE);
	if (haveFinalMedia)
	{
		// The shared event budgets do not gate this already-existing summary line. The fixed buffer is big enough
		// for every field even at its maximum value; no callback formats or emits this observation.
		snprintf(text + used, sizeof text - (size_t)used,
			" media_final=1 mixed_music_frames=%llu mixed_film_frames=%llu mixed_both_role_frames=%llu"
			" callback_frames=%llu demand_max_frames=%u queued_input_last_bytes=%d queued_input_max_bytes=%d"
			" queue_samples=%llu queue_failures=%llu queue_mix_frame=%llu queue_current_generation=%u"
			" retired_queue_observations=%llu input=44100Hz-S16LE-stereo silence_or_mute_possible=1 output_device_unobserved=1",
			media.musicFrames, media.filmFrames, media.bothRoleFrames, media.callbackFrames, media.callbackMaxFrames,
			media.queueLastBytes, media.queueMaxBytes, media.queueSamples, media.queueFailures, media.queueAtMixed,
			media.queueCurrentGeneration, media.retiredQueueObservations);
	}
	Line("summary", text);
}
