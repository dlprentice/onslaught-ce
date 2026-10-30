// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/clock.cpp: the clocks the game reads (performance counter and frequency, timeGetTime and
// GetTickCount, the system time), on both targets. BEA_PORT_CLOCK chooses:
// - unset: real time.
// - present: deterministic. A fixed epoch plus BEA_PORT_CLOCK_DT seconds (default 1/30) per IDirect3DDevice9::Present,
//   from the first clock read, with a 1 GHz performance counter. Between frames only the main thread moves it: its
//   Sleep(n) passes n ms, and every 4,096 of its clock reads pass 1 ms (so wait loops end the same way every run); other
//   threads never move it.
// - shim: the retail replay shim's clock (the companion repository's runtime harness, REPORT-5/6)
//   exactly, so the port's ticks fall on the same Presents as retail's golden traces: real time until the first Present
//   returns; then bases sampled from real time, a 10 MHz counter at base + n * (int64)(10e6 * dt), milliseconds at
//   base + (DWORD)(n * dt * 1000.0), file time at base + (uint64)(n * dt * 1e7), where n counts Presents plus one
//   "spin escape" per 2,000,000 clock reads (any thread) since the last Present. dt comes from BEA_PORT_CLOCK_DT exactly
//   as the shim's BEA_REPLAY_DT (atof). Not deterministic before the first frame, as in the shim.
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "platform.h"

namespace
{
enum { REAL = 0, PRESENT = 1, SHIM = 2 };
std::atomic<int> gMode{ -1 };
double gDt = 1.0 / 30;
unsigned long long gDtNs = 1000000000ull / 30;

// present
std::atomic<unsigned long long> gFrames{ 0 };
std::atomic<unsigned long long> gEscapesMs{ 0 };
std::atomic<bool> gMainClaimed{ false };
thread_local int tIsMain = -1;
thread_local unsigned tReads = 0;
const unsigned long long kEpochFiletime = 126921456000000000ull;	// 2003-03-14 12:00:00 UTC
const unsigned long long kEpochNs = 1000000000000ull;				// 1,000 s: differences never go negative

// shim
std::atomic<long> gShimFrames{ 0 };
std::atomic<long> gSpin{ 0 };
std::atomic<long> gSpinEsc{ 0 };
// BEA_SLEEP_VIRTUAL=1 (shim only): the main thread's Sleep(n) after the first Present adds n ms to every clock (the FMV
// loop's Sleep(1) between due frames; the rule the retail replay shim shares for A/V comparisons). Off: nothing changes.
std::atomic<unsigned long long> gShimSleepMs{ 0 };
int gSleepVirtual = -1;
std::atomic<bool> gShimVirtual{ false };
long long gQpcBase, gQpcDt;
unsigned long gMsBase;
unsigned long long gFtBase;

// The real clock the game reads leaves out the time it waited for its window to be restored (PortClockPauseBegin/End, window.cpp's GetMessage while the window is minimized: the original's
// idle wait, which it never accounted for: CGame::MainLoop keeps its base time and catches up one frame length per pass, so a game that was away for a minute ran that minute at full speed
// afterwards). Bracket a real read with the generation so a thread suspended across a wait cannot apply a newer
// offset to its older sample. The window thread is the only writer; an odd generation lasts only while publishing
// the start or end, never throughout the blocking wait itself. Keep the generation and snapshot fields sequentially
// consistent: seeing a new field must make the final generation check see the intervening publication as well.
std::atomic<unsigned long long> gOffsetNs{ 0 };		// the raw clock minus the clock the game reads, over the waits that are over
std::atomic<unsigned long long> gFrozenNs{ 0 };		// the clock the game reads while it stands still, in the raw clock's own count
std::atomic<bool> gFrozen{ false };
std::atomic<unsigned long long> gClockGeneration{ 0 };
#ifdef CE_VERSION
bool gPauseRawValid = true;						// only the window thread writes or reads this
#endif
#if defined(_WIN32)
std::atomic<long long> gRawFrequency{ 0 };
std::atomic<unsigned long long> gLastRawNs{ 0 };
#endif

unsigned long long ReadBegin()
{
	for (;;)
	{
		const auto generation = gClockGeneration.load();
		if (!(generation & 1)) return generation;
		std::this_thread::yield();
	}
}
bool ReadValid(unsigned long long generation) { return gClockGeneration.load() == generation; }

bool TryRawNs(unsigned long long &raw)
{
#if defined(_WIN32)
	LARGE_INTEGER c{};
	if (!QueryPerformanceCounter(&c)) return false;
	long long frequency = gRawFrequency.load();
	if (!frequency)
	{
		LARGE_INTEGER f{};
		if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) return false;
		frequency = f.QuadPart;
		// Windows documents this frequency as fixed at boot, so a successful query can be reused.
		gRawFrequency.store(frequency);
	}
	raw = (unsigned long long)(c.QuadPart / frequency) * 1000000000ull +
		  (unsigned long long)(c.QuadPart % frequency) * 1000000000ull / (unsigned long long)frequency;
	// A thread with an older OS sample can finish after a newer one. Keep the fallback monotonic.
	auto last = gLastRawNs.load();
	while (last < raw && !gLastRawNs.compare_exchange_weak(last, raw)) {}
	return true;
#else
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	raw = (unsigned long long)ts.tv_sec * 1000000000ull + ts.tv_nsec;
	return true;
#endif
}

unsigned long long LastRawNs()
{
#if defined(_WIN32)
	// If the OS cannot provide a counter, retain the last valid sample (zero before the first).
	return gLastRawNs.load();
#else
	return 0;
#endif
}

unsigned long long RawNs()
{
	unsigned long long raw = 0;
	return TryRawNs(raw) ? raw : LastRawNs();
}

// How far the clock the game reads is behind the operating system's right now, given the operating system's clock (rawNs, read once by the caller): the wait so far while it lasts (the clock
// stands still), the waits that are over after it.
unsigned long long Shift(unsigned long long rawNs)
{
	if (gFrozen.load())
	{
		// A raw reading older than the frozen game time keeps the completed-wait offset instead of underflowing. After earlier waits, a stale reading still newer than that time can be clamped forward.
		const unsigned long long frozen = gFrozenNs.load();
		return rawNs >= frozen ? rawNs - frozen : gOffsetNs.load();
	}
	return gOffsetNs.load();
}

unsigned long long RealNs()
{
	for (;;)
	{
		const auto generation = ReadBegin();
		const unsigned long long raw = RawNs();
		const unsigned long long value = raw - Shift(raw);
		if (ReadValid(generation)) return value;
	}
}

unsigned long long RealFiletime()
{
#if defined(_WIN32)
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
#else
	timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ((unsigned long long)ts.tv_sec + 11644473600ull) * 10000000ull + ts.tv_nsec / 100;
#endif
}

const unsigned long long kStartRealNs = RealNs();

int Mode()
{
	int m = gMode.load();
	if (m < 0)
	{
		const char *c = getenv("BEA_PORT_CLOCK");
		m = !c ? REAL : strcmp(c, "present") == 0 ? PRESENT : (strcmp(c, "shim") == 0 || strcmp(c, "present-c") == 0) ? SHIM : REAL;
		if (const char *dt = getenv("BEA_PORT_CLOCK_DT"))
			if (double s = atof(dt); s > 0)
				gDt = s;
		gDtNs = (unsigned long long)(gDt * 1e9 + 0.5);
		gQpcDt = (long long)(10000000.0 * gDt);
		gMode.store(m);
	}
	return m;
}

void ClaimMain()
{
	if (tIsMain < 0)
	{
		bool expected = false;
		tIsMain = gMainClaimed.compare_exchange_strong(expected, true) ? 1 : 0;	// the first thread to read: main
	}
}

// present: virtual nanoseconds; a main-thread read may pass a millisecond.
unsigned long long PresentNs()
{
	ClaimMain();
	if (tIsMain == 1 && ++tReads % 4096 == 0)
		gEscapesMs.fetch_add(1);
	return kEpochNs + gFrames.load() * gDtNs + gEscapesMs.load() * 1000000ull;
}

// shim: n, as the shim's VPolls() (each read counts toward a spin escape).
long ShimN()
{
	long s = gSpin.fetch_add(1) + 1;
	if (s > 2000000)
	{
		gSpin.store(0);
		gSpinEsc.fetch_add(1);
		if (getenv("BEA_PORT_CLOCK_LOG"))
		{
			ClaimMain();
			fprintf(stderr, PORT_LOG_PREFIX "clock: spin escape %ld after Present %ld (%s thread)\n", gSpinEsc.load(), gShimFrames.load(), tIsMain == 1 ? "main" : "other");
		}
	}
	return gShimFrames.load() + gSpinEsc.load();
}
}

bool PortClockVirtual() { return Mode() != REAL; }

// The shim clock without BEA_SLEEP_VIRTUAL stands still between Presents (only a spin escape, 2,000,000 reads, moves it),
// so a loop that waits for the next due moment without presenting never ends. Retail under the shim plays its films one
// frame per Present (LTLogo's 229 frames in Presents 1-230, OpeningFMV's 2054 in 231-2285, measured under the retail shim), and
// port/bink's BinkWait follows that rule while this is true.
bool PortClockStandsBetweenPresents()
{
	if (Mode() != SHIM)
		return false;
	if (gSleepVirtual < 0)
	{
		const char *v = getenv("BEA_SLEEP_VIRTUAL");
		gSleepVirtual = v && *v == '1';
	}
	return !gSleepVirtual;
}

// Patch 0053 (CDXFrontEndVideo::NextFrame's waiting form, the film player's loop): retail's binkw32 times a film by the
// wall clock even under the shim, and a frame takes far less than the 40 ms a 25 fps frame lasts, so the player's first
// look after each Present finds the next frame not yet due: NextFrame returns -1, CDXFMV::Play sleeps and polls again,
// and a skip seen in that poll ends the film without presenting another frame (the shim's breakpoints, 2026-10-23).
// While the shim clock stands still, the first look after each Present waits once, as retail's does; later looks find
// the frame due (BinkWait), so films still show one frame per Present.
bool PortClockFilmFrameWaits()
{
	static long sLastFrames = -1;
	if (!PortClockStandsBetweenPresents())
		return false;
	long frames = gShimFrames.load();
	if (frames == sLastFrames)
		return false;
	sLastFrames = frames;
	return true;
}

void PortClockFrame()
{
	switch (Mode())
	{
	case PRESENT:
		gFrames.fetch_add(1);
		tReads = 0;
		break;
	case SHIM:
		if (gShimFrames.fetch_add(1) + 1 == 1)
		{
			gQpcBase = (long long)(RealNs() / 100);
			gMsBase = (unsigned long)((RealNs() - kStartRealNs) / 1000000ull);
			gFtBase = RealFiletime();
			gShimVirtual.store(true);
		}
		gSpin.store(0);
		break;
	default:
		break;
	}
}

#ifdef CE_VERSION
bool PortCeLayer();
#ifdef _WIN32
void PortWindowsClockPauseBegin();
void PortWindowsClockPauseEnd();
#endif
#endif

// The operating system's clock in nanoseconds, as RawNs reads it. Linux brackets its own raw reading and shift;
// Windows tracks its native counter and millisecond readings separately.
unsigned long long PortClockRawNs() { return RawNs(); }

unsigned long long PortClockShiftNs(unsigned long long rawNs) { return Mode() == REAL ? Shift(rawNs) : 0; }
unsigned long long PortClockReadBegin() { return ReadBegin(); }
bool PortClockReadValid(unsigned long long generation) { return ReadValid(generation); }

void PortClockPauseBegin()
{
#ifdef CE_VERSION
	// BEA_PORT_MINIMIZED_CLOCK=run keeps the original's behaviour (the clock runs through the wait), to compare a build with retail's.
	static const bool keepRunning = [] { const char *v = getenv("BEA_PORT_MINIMIZED_CLOCK"); return v && !strcmp(v, "run"); }();
	if (keepRunning || Mode() != REAL || !PortCeLayer() || gFrozen.load())
		return;
	gClockGeneration.fetch_add(1);
	unsigned long long raw = 0;
	gPauseRawValid = TryRawNs(raw);
	if (!gPauseRawValid) raw = LastRawNs();
	if (!gPauseRawValid && raw < gOffsetNs.load()) raw = gOffsetNs.load();
	gFrozenNs.store(raw - gOffsetNs.load());
#ifdef _WIN32
	PortWindowsClockPauseBegin();
#endif
	gFrozen.store(true);
	gClockGeneration.fetch_add(1);
#endif
}

void PortClockPauseEnd()
{
#ifdef CE_VERSION
	if (!gFrozen.load())
		return;
	gClockGeneration.fetch_add(1);
	unsigned long long raw = 0;
	if (gPauseRawValid && TryRawNs(raw))
		gOffsetNs.store(raw - gFrozenNs.load());	// a failed edge cannot replace the last known offset
#ifdef _WIN32
	PortWindowsClockPauseEnd();
#endif
	gFrozen.store(false);
	gClockGeneration.fetch_add(1);
#endif
}

// For DirectSound's mixer (port/dsound): the virtual time, including the main thread's sleeps and escapes, without
// counting as a read, so audio following the clock does not move it.
unsigned long long PortClockPeekNs()
{
	switch (Mode())
	{
	case PRESENT: return kEpochNs + gFrames.load() * gDtNs + gEscapesMs.load() * 1000000ull;
	case SHIM: return gShimVirtual.load() ? (unsigned long long)(gShimFrames.load() + gSpinEsc.load()) * gDtNs +
								 gShimSleepMs.load() * 1000000ull : 0;
	default: return 0;
	}
}

unsigned long long PortClockFrames() { return Mode() == SHIM ? (unsigned long long)gShimFrames.load() : gFrames.load(); }

long long PortClockQpf()
{
	switch (Mode())
	{
	case PRESENT: return 1000000000ll;
	case SHIM: return 10000000ll;
	default: return 0;		// real: the caller's own frequency
	}
}

long long PortClockQpc()
{
	switch (Mode())
	{
	case PRESENT: return (long long)PresentNs();
	case SHIM: return gShimVirtual.load() ? gQpcBase + (long long)ShimN() * gQpcDt + (long long)gShimSleepMs.load() * 10000
										  : (long long)(RealNs() / 100);
	default: return 0;
	}
}

unsigned long PortClockMs()
{
	switch (Mode())
	{
	case PRESENT: return (unsigned long)(PresentNs() / 1000000ull);
	case SHIM:
		return gShimVirtual.load() ? gMsBase + (unsigned long)((double)ShimN() * gDt * 1000.0) + (unsigned long)gShimSleepMs.load()
								   : (unsigned long)((RealNs() - kStartRealNs) / 1000000ull);
	default: return 0;
	}
}

unsigned long long PortClockFiletime()
{
	switch (Mode())
	{
	case PRESENT: return kEpochFiletime + (PresentNs() - kEpochNs) / 100;
	case SHIM: return gShimVirtual.load() ? gFtBase + (unsigned long long)((double)ShimN() * gDt * 10000000.0) +
										  gShimSleepMs.load() * 10000ull : RealFiletime();
	default: return RealFiletime();
	}
}

// In the present clock the main thread's sleep is time passing (a loop that sleeps until a video frame is due would
// otherwise wait forever). The real sleep still happens.
void PortClockSleep(unsigned long ms)
{
	if (Mode() == PRESENT)
	{
		ClaimMain();
		if (tIsMain == 1)
			gEscapesMs.fetch_add(ms);
	}
	else if (Mode() == SHIM && gShimVirtual.load())
	{
		if (gSleepVirtual < 0)
		{
			const char *v = getenv("BEA_SLEEP_VIRTUAL");
			gSleepVirtual = v && *v == '1';
		}
		ClaimMain();
		if (gSleepVirtual && tIsMain == 1)
			gShimSleepMs.fetch_add(ms);
	}
}
