// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/portlog.h: the port's console messages. Public builds (CE_VERSION) say "onslaught-ce:", private
// builds "bea-port:"; port/tools/run.py and winrun.py read either. Developer chatter (the frame counter, input-mode
// retries, the game's own debugging printf lines) is printed only when PortDevLog() says so.
#ifndef PORT_PORTLOG_H
#define PORT_PORTLOG_H

#if defined(CE_VERSION)
#define PORT_LOG_PREFIX "onslaught-ce: "
#else
#define PORT_LOG_PREFIX "bea-port: "
#endif

// Private builds: always. Public builds: only with BEA_PORT_VERBOSE=1 (ce.cpp).
bool PortDevLog();

// Ordinary CE session diagnostics. These never open a file/device or send data: the launcher already captures stderr
// into its bounded per-user log. Faithful/replay/recording and private builds return before reading diagnostic state.
enum PortSessionCategory { PORT_SESSION_GAME, PORT_SESSION_WINDOW, PORT_SESSION_VIDEO, PORT_SESSION_AUDIO,
	PORT_SESSION_MUSIC, PORT_SESSION_FILM, PORT_SESSION_CATEGORIES };
bool PortSessionEnabled();
void PortSessionLog(PortSessionCategory category, bool error, const char *format, ...);
void PortSessionAsset(PortSessionCategory category, const char *action, const char *path, long result = 0);
void PortSessionMediaActive(PortSessionCategory category, const void *token, bool playing);
// Internal replacement-buffer identity only. Tags never imply nonzero samples or physical playback.
bool PortAudioMediaRole(const void *buffer, PortSessionCategory category);
// A fixed-size, process-local observation from the mixer at device stop. These counters describe tagged input
// mixed by this process, not audible output; samples may be silent or muted and OS/device queues are unobserved.
struct PortSessionFinalMedia {
	unsigned long long musicFrames, filmFrames, bothRoleFrames, callbackFrames;
	unsigned long long queueSamples, queueFailures, queueAtMixed, retiredQueueObservations;
	unsigned callbackMaxFrames;
	int queueLastBytes, queueMaxBytes;
	unsigned queueCurrentGeneration;
};
// Called outside the audio callback and mixer mutex; staged once for the next existing unbudgeted summary line.
void PortSessionSetFinalMedia(const PortSessionFinalMedia *snapshot);
void PortSessionSummary();
// Cheap counters only: safe on the audio callback; the main thread/teardown formats their bounded summaries.
enum PortSessionCounter { PORT_COUNT_AUDIO_CALLBACKS, PORT_COUNT_AUDIO_PUT_FAILED, PORT_COUNT_AUDIO_ALLOC_FAILED,
	PORT_COUNT_MUSIC_REFILLS, PORT_COUNT_MUSIC_SHORT_READS, PORT_COUNT_MUSIC_READ_FAILED, PORT_COUNT_MUSIC_WAIT_FAILED,
	PORT_COUNT_MONITOR_DROPS, PORT_COUNT_MONITOR_PUT_FAILED, PORT_SESSION_COUNTERS };
void PortSessionCount(PortSessionCounter counter);
// Observations of the music worker and the mixer (patch 0187): counters and maxima that are counted before any line budget
// and printed on their own "audio-obs" line after each summary, so that the summary line keeps its text. Same fences and the
// same callback rules as the counters above: ignored unless the session diagnostics are admitted, no lock, clock, format or I/O.
// Counters add (saturating); the rest keep the largest value seen. Microsecond maxima saturate at 4294 seconds. None is
// evidence of audible output.
enum PortSessionObservation {
	PORT_OBS_MUSIC_RISK_REFILLS,		// refills the diagnostics flag as underrun risk, whether or not their line was logged
	PORT_OBS_MUSIC_PAIR_REFILLS,		// a refill of the same half within half a second of the previous refill (two refill sources)
	PORT_OBS_MUSIC_ORDER_ANOMALIES,		// handled wakes out of half order, whether or not their line was logged
	PORT_OBS_MUSIC_LINES_DROPPED,		// refill and order lines not logged because their own line budget was spent
	PORT_OBS_MUSIC_VOICE_STARTS,		// music-tagged buffers started
	PORT_OBS_MUSIC_STALE_HALVES,		// a music half was entered that was not refilled since it was last entered
	PORT_OBS_MUSIC_OVERWRITTEN_HALVES,	// a music half was refilled while it still held data that had not been entered yet
	PORT_OBS_AUDIO_CALLBACKS_LATE,		// device callbacks more than three chunks after the previous one
	PORT_OBS_EVENTS_DROPPED,			// ordinary session events not logged because the session limit was reached
	PORT_OBS_ERRORS_DROPPED,			// error session events not logged because the session limit was reached
	PORT_OBS_MUSIC_REFILL_US_MAX,		// the longest refill, from before the read to after the unlock
	PORT_OBS_MUSIC_NOTIFY_AGE_US_MAX,	// the largest time from the buffer's latest position notification to the start of a refill
	PORT_OBS_MUSIC_VOICES_MAX,			// the most music-tagged buffers playing at once
	PORT_OBS_AUDIO_GAP_US_MAX,			// the longest time between two device callbacks
	PORT_OBS_DEVICE_RATE,				// the granted device's sample rate
	PORT_OBS_DEVICE_CHANNELS,			// the granted device's channel count
	PORT_OBS_DEVICE_PERIOD_FRAMES,		// the granted device's buffer size in frames
	PORT_SESSION_OBSERVATIONS };
void PortSessionObserve(PortSessionObservation which, unsigned amount = 1);
void PortSessionObserveMax(PortSessionObservation which, unsigned value);
void PortSessionAudioError(const char *error);
void PortSessionPulse(unsigned frame);
// One-off inventories for problem reports (inventory.cpp): every display, input and audio playback device, and the
// CE settings that change how the game runs. Each writes once and inherits the session log's limits and fences.
struct SDL_Window;
void PortSessionDisplayInventory(SDL_Window *window);
void PortSessionInputInventory();
void PortSessionAudioInventory();
void PortSessionSettingsInventory();

#endif
