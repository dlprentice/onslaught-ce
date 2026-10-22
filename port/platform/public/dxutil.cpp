// SPDX-License-Identifier: GPL-3.0-only
// port/platform/public/dxutil.cpp: the game's application timer (DXUtil_Timer, declared in port/include/DXUtil.h),
// written for the public build in place of src/dxutil.cpp. It keeps the game's behaviour and the same clock reads:
// the performance counter's frequency once, at the first call, then one counter read per call except while the
// timer is stopped (when the stop time stands in, save for TIMER_START and TIMER_GETABSOLUTETIME); without a
// performance counter, timeGetTime in seconds. Starting adds the stopped span to the base time.
#include "common.h"
#include "mathsconst.h"
#include <windows.h>
#include <mmsystem.h>
#include "DXUtil.h"

namespace
{
// The counter in ticks; `stop` is 0 while running.
struct CounterClock
{
	LONGLONG	perSecond;
	LONGLONG	stop;
	LONGLONG	lastElapsed;
	LONGLONG	base;
};

// The fallback in seconds; `stop` is 0 while running.
struct SecondsClock
{
	double		lastElapsed;
	double		base;
	double		stop;
};

FLOAT CounterCommand(CounterClock &c, TIMER_COMMAND command)
{
	LARGE_INTEGER	now;
	if (c.stop != 0 && command != TIMER_START && command != TIMER_GETABSOLUTETIME)
		now.QuadPart = c.stop;
	else
		QueryPerformanceCounter(&now);

	switch (command)
	{
	case TIMER_GETELAPSEDTIME:
	{
		double	elapsed = (double)(now.QuadPart - c.lastElapsed) / (double)c.perSecond;
		c.lastElapsed = now.QuadPart;
		return (FLOAT)elapsed;
	}
	case TIMER_GETAPPTIME:
		return (FLOAT)((double)(now.QuadPart - c.base) / (double)c.perSecond);
	case TIMER_RESET:
		c.base = now.QuadPart;
		c.lastElapsed = now.QuadPart;
		return 0.0f;
	case TIMER_START:
		c.base += now.QuadPart - c.stop;
		c.stop = 0;
		c.lastElapsed = now.QuadPart;
		return 0.0f;
	case TIMER_STOP:
		c.stop = now.QuadPart;
		c.lastElapsed = now.QuadPart;
		return 0.0f;
	case TIMER_ADVANCE:
		c.stop += c.perSecond / 10;
		return 0.0f;
	case TIMER_GETABSOLUTETIME:
		return (FLOAT)(now.QuadPart / (double)c.perSecond);
	}
	return -1.0f;
}

FLOAT SecondsCommand(SecondsClock &c, TIMER_COMMAND command)
{
	double	now;
	if (c.stop != 0.0 && command != TIMER_START && command != TIMER_GETABSOLUTETIME)
		now = c.stop;
	else
		now = timeGetTime() * 0.001;

	switch (command)
	{
	case TIMER_GETELAPSEDTIME:
	{
		double	elapsed = now - c.lastElapsed;
		c.lastElapsed = now;
		return (FLOAT)elapsed;
	}
	case TIMER_GETAPPTIME:
		return (FLOAT)(now - c.base);
	case TIMER_RESET:
		c.base = now;
		c.lastElapsed = now;
		return 0.0f;
	case TIMER_START:
		c.base += now - c.stop;
		c.stop = 0.0f;
		c.lastElapsed = now;
		return 0.0f;
	case TIMER_STOP:
		// The fallback keeps the last elapsed reading while stopped.
		c.stop = now;
		return 0.0f;
	case TIMER_ADVANCE:
		c.stop += 0.1f;
		return 0.0f;
	case TIMER_GETABSOLUTETIME:
		return (FLOAT)now;
	}
	return -1.0f;
}
}

FLOAT __stdcall DXUtil_Timer(TIMER_COMMAND command)
{
	static bool			sStarted = false;
	static BOOL			sCounter = FALSE;
	static CounterClock	sTicks = {0, 0, 0, 0};
	static SecondsClock	sSeconds = {0.0, 0.0, 0.0};

	if (!sStarted)
	{
		sStarted = true;
		LARGE_INTEGER	frequency;
		sCounter = QueryPerformanceFrequency(&frequency);
		if (sCounter)
			sTicks.perSecond = frequency.QuadPart;
	}
	return sCounter ? CounterCommand(sTicks, command) : SecondsCommand(sSeconds, command);
}
