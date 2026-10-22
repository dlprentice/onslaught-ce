// SPDX-License-Identifier: GPL-3.0-only
// port/platform/windows/clock.cpp: the Windows build's clocks: the real ones, or the virtual clock
// (BEA_PORT_CLOCK, port/platform/common/clock.cpp) with the same values as the Linux build. port.h renames the game's
// clock calls (and Sleep, which passes virtual time on the main thread) to these.
#include <windows.h>
#include <mmsystem.h>

#include "../common/platform.h"

extern "C" {

BOOL WINAPI PortQueryPerformanceFrequency(LARGE_INTEGER *f)
{
	if (!PortClockVirtual())
		return QueryPerformanceFrequency(f);
	f->QuadPart = PortClockQpf();
	return TRUE;
}

BOOL WINAPI PortQueryPerformanceCounter(LARGE_INTEGER *c)
{
	if (!PortClockVirtual())
		return QueryPerformanceCounter(c);
	c->QuadPart = PortClockQpc();
	return TRUE;
}

DWORD WINAPI PortTimeGetTime(void) { return PortClockVirtual() ? (DWORD)PortClockMs() : timeGetTime(); }
DWORD WINAPI PortGetTickCount(void) { return PortClockVirtual() ? (DWORD)PortClockMs() : GetTickCount(); }

void WINAPI PortSleep(DWORD ms)
{
	PortClockSleep(ms);
	Sleep(ms);
}

void WINAPI PortGetSystemTimeAsFileTime(LPFILETIME ft)
{
	if (!PortClockVirtual())
	{
		GetSystemTimeAsFileTime(ft);
		return;
	}
	unsigned long long t = PortClockFiletime();
	ft->dwLowDateTime = (DWORD)t;
	ft->dwHighDateTime = (DWORD)(t >> 32);
}

void WINAPI PortGetSystemTime(LPSYSTEMTIME st)
{
	if (!PortClockVirtual())
	{
		GetSystemTime(st);
		return;
	}
	FILETIME ft;
	PortGetSystemTimeAsFileTime(&ft);
	FileTimeToSystemTime(&ft, st);
}

// The virtual clock has no time zone, as on Linux.
void WINAPI PortGetLocalTime(LPSYSTEMTIME st)
{
	if (!PortClockVirtual())
		GetLocalTime(st);
	else
		PortGetSystemTime(st);
}
}
