// SPDX-License-Identifier: GPL-3.0-only
// Runs the Windows build's real clock reads (port/platform/windows/clock.cpp: PortQueryPerformanceCounter, PortTimeGetTime, PortGetTickCount) with the operating system's clocks replaced by stand-ins
// that follow the same raw clock the shift is taken against (common/clock.cpp RawNs), at a counter frequency chosen by the first argument: while the game waits for its minimized window
// (PortClockPauseBegin/End) the counter and the millisecond clocks the game reads stand still, and afterwards go on from where they stood. ce_pause_clock_tests.py builds and runs this on the host.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <windows.h>
#include <mmsystem.h>

#include "platform.h"

static unsigned long long gFreq = 0;
static unsigned long long Frequency()
{
	if (!gFreq)
	{
		const char *text = getenv("BEA_TEST_CLOCK_FREQUENCY");
		gFreq = text ? strtoull(text, NULL, 10) : 10000000ull;
	}
	return gFreq;
}
static bool layer = true;
bool PortCeLayer() { return layer; }

// the operating system's clocks, as a Windows machine counts them: the raw clock in counts of gFreq, and in milliseconds
static unsigned long long Counts()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	const unsigned long long ns = (unsigned long long)ts.tv_sec * 1000000000ull + ts.tv_nsec;
	const auto frequency = Frequency();
	return (ns / 1000000000ull) * frequency + (ns % 1000000000ull) * frequency / 1000000000ull;
}
static DWORD RawMs()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (DWORD)((unsigned long long)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000);
}
extern "C" {
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = (LONGLONG)Frequency(); return TRUE; }
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *c) { c->QuadPart = (LONGLONG)Counts(); return TRUE; }
DWORD WINAPI timeGetTime(void) { return RawMs(); }
DWORD WINAPI GetTickCount(void) { return RawMs(); }
void WINAPI Sleep(DWORD) {}
void WINAPI GetSystemTimeAsFileTime(LPFILETIME) {}
void WINAPI GetSystemTime(LPSYSTEMTIME) {}
BOOL WINAPI FileTimeToSystemTime(const FILETIME *, LPSYSTEMTIME) { return TRUE; }
void WINAPI GetLocalTime(LPSYSTEMTIME) {}
BOOL WINAPI PortQueryPerformanceCounter(LARGE_INTEGER *c);
DWORD WINAPI PortTimeGetTime(void);
DWORD WINAPI PortGetTickCount(void);
}

static int failures;
static void expect(bool ok, const char *what, long long a = 0, long long b = 0)
{
	if (ok) return;
	printf("FAIL %s (%lld, %lld)\n", what, a, b);
	++failures;
}
static void Nap(long ms) { timespec ts = { 0, ms * 1000000L }; nanosleep(&ts, NULL); }
static long long Game() { LARGE_INTEGER c; PortQueryPerformanceCounter(&c); return (long long)c.QuadPart; }

int main(int argc, char **argv)
{
	if (argc > 1) gFreq = strtoull(argv[1], NULL, 10);
	const long long perMs = (long long)(gFreq / 1000);		// counts in a millisecond (a few thousand: the tolerances below are in counts)
	const long long before = Game();
	const DWORD msBefore = PortTimeGetTime(), tickBefore = PortGetTickCount();
	expect(llabs(before - (long long)Counts()) <= 2 * perMs, "no shift before a wait", before, (long long)Counts());
	PortClockPauseBegin();
	const long long a = Game();
	const DWORD msA = PortTimeGetTime(), tickA = PortGetTickCount();
	Nap(300);
	const long long b = Game();
	const DWORD msB = PortTimeGetTime(), tickB = PortGetTickCount();
	expect(a >= before - 2, "the counter does not go back when the wait begins", a, before);
	expect(llabs(b - a) <= 3, "the counter stands still for the wait", b, a);
	expect(msB - msA <= 1u && tickB - tickA <= 1u, "and so do timeGetTime and GetTickCount", (long long)(msB - msA), (long long)(tickB - tickA));
	PortClockPauseEnd();
	const long long afterEnd = Game();
	expect(afterEnd >= b - 3 && afterEnd - b < 50 * perMs, "it goes on from where it stood when the wait ends", afterEnd, b);
	Nap(200);
	const long long later = Game();
	const DWORD msLater = PortTimeGetTime(), tickLater = PortGetTickCount();
	expect(later - afterEnd >= 180 * perMs && later - afterEnd < 600 * perMs, "and runs in real time afterwards", later - afterEnd, perMs);
	expect(later - before < 600 * perMs, "the 300 ms wait is not in the game's counter", later - before, perMs);
	expect(msLater - msA >= 150u && msLater - msA < 600u && tickLater - tickA >= 150u && tickLater - tickA < 600u, "nor in its millisecond clocks", (long long)(msLater - msA), (long long)(tickLater - tickA));
	(void)msBefore; (void)tickBefore;
	// a second wait stands still the same way, on top of the first
	PortClockPauseBegin();
	const long long c1 = Game();
	const DWORD msC = PortTimeGetTime(), tickC = PortGetTickCount();
	Nap(100);
	expect(llabs(Game() - c1) <= 3, "a second wait stands still too", Game(), c1);
	expect(PortTimeGetTime() == msC && PortGetTickCount() == tickC, "both millisecond clocks hold during the second wait");
	PortClockPauseEnd();
	Nap(50);
	expect(Game() - c1 >= 40 * perMs && Game() - c1 < 300 * perMs, "and ends as well", Game() - c1, perMs);
	expect(PortTimeGetTime() - msC >= 40u && PortTimeGetTime() - msC < 300u && PortGetTickCount() - tickC >= 40u && PortGetTickCount() - tickC < 300u, "both millisecond clocks resume after the second wait");
	// outside the CE layer nothing stands still
	layer = false;
	PortClockPauseBegin();
	const long long d1 = Game();
	Nap(150);
	expect(Game() - d1 >= 140 * perMs, "the counter runs through the wait outside the CE layer", Game() - d1, perMs);
	PortClockPauseEnd();
	printf(failures ? "FAIL\n" : "PASS\n");
	return failures ? 1 : 0;
}
