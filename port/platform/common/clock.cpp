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

unsigned long long RealNs()
{
#if defined(_WIN32)
	LARGE_INTEGER c, f;
	QueryPerformanceCounter(&c);
	QueryPerformanceFrequency(&f);
	return (unsigned long long)(c.QuadPart / f.QuadPart) * 1000000000ull +
		   (unsigned long long)(c.QuadPart % f.QuadPart) * 1000000000ull / (unsigned long long)f.QuadPart;
#else
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ull + ts.tv_nsec;
#endif
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
			fprintf(stderr, "bea-port: clock: spin escape %ld after Present %ld (%s thread)\n", gSpinEsc.load(), gShimFrames.load(), tIsMain == 1 ? "main" : "other");
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
