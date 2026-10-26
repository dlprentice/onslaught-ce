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
void PortSessionSummary();
// Cheap counters only: safe on the audio callback; the main thread/teardown formats their bounded summaries.
enum PortSessionCounter { PORT_COUNT_AUDIO_CALLBACKS, PORT_COUNT_AUDIO_PUT_FAILED, PORT_COUNT_AUDIO_ALLOC_FAILED,
	PORT_COUNT_MUSIC_REFILLS, PORT_COUNT_MUSIC_SHORT_READS, PORT_COUNT_MUSIC_READ_FAILED, PORT_COUNT_MUSIC_WAIT_FAILED,
	PORT_COUNT_MONITOR_DROPS, PORT_COUNT_MONITOR_PUT_FAILED, PORT_SESSION_COUNTERS };
void PortSessionCount(PortSessionCounter counter);
void PortSessionAudioError(const char *error);
void PortSessionPulse(unsigned frame);

#endif
