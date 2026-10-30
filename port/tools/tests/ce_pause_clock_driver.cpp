// SPDX-License-Identifier: GPL-3.0-only
// Runs the real clock's stand-still (port/platform/common/clock.cpp: PortClockPauseBegin/End, PortClockShiftNs), as window.cpp's GetMessage wait uses it while the game's window is minimized:
// with the CE layer on and the real clock, these sequential reads (the operating system's, less the shift) stand still for the wait and go on from where they stood;
// outside the CE layer, in a virtual clock or with BEA_PORT_MINIMIZED_CLOCK=run, nothing changes. One mode per process (real, off, virtual, run): ce_pause_clock_tests.py.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform.h"

static bool layer = true;
bool PortCeLayer() { return layer; }

static int failures;
static void expect(bool ok, const char *what)
{
	if (ok) return;
	printf("FAIL %s\n", what);
	++failures;
}
static void Nap(long ms) { timespec ts = { 0, ms * 1000000L }; nanosleep(&ts, NULL); }
// The clock the game would read: the operating system's, less the shift taken from that one reading (the platform wrappers' formula).
static long long Game()
{
	const unsigned long long raw = PortClockRawNs();
	return (long long)(raw - PortClockShiftNs(raw));
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "real";
	if (!strcmp(mode, "off")) layer = false;
	const bool stands = !strcmp(mode, "real");		// "virtual": the layer is on but BEA_PORT_CLOCK names a virtual clock; "run": BEA_PORT_MINIMIZED_CLOCK=run: nothing stands still either
	// before any wait: no shift
	expect(PortClockShiftNs(PortClockRawNs()) == 0, "no shift before a wait");
	const long long before = Game();
	PortClockPauseBegin();
	const long long a = Game();
	Nap(200);
	const long long b = Game();
	Nap(100);
	const long long c = Game();
	PortClockPauseEnd();
	const long long afterEnd = Game();
	Nap(100);
	const long long later = Game();
	const long long waited = 300 * 1000000ll;
	if (stands)
	{
		expect(a >= before, "the clock does not go back when the wait begins");
		expect(b == a && c == a, "the clock stands still for the whole wait");
		expect(afterEnd >= c && afterEnd - c < 50 * 1000000ll, "it goes on from where it stood when the wait ends");
		expect(later - afterEnd >= 90 * 1000000ll && later - afterEnd < 400 * 1000000ll, "and runs in real time afterwards");
		expect(later - before < waited, "the wait is not in the game's time");
		PortClockPauseEnd();	// a second end changes nothing
		expect(Game() >= later, "an end without a wait changes nothing");
		PortClockPauseBegin();
		const long long again = Game();
		Nap(50);
		expect(Game() == again, "a second wait stands still too");
		PortClockPauseEnd();
		Nap(20);
		expect(Game() >= again && Game() - again < 100 * 1000000ll, "and ends as well");
	}
	else
	{
		expect(c - a >= 250 * 1000000ll && later - before >= 380 * 1000000ll, "the clock runs through the wait when it is not the CE layer's real clock");
		expect(PortClockShiftNs(PortClockRawNs()) == 0, "and there is no shift");
	}
	printf(failures ? "FAIL\n" : "PASS\n");
	return failures ? 1 : 0;
}
