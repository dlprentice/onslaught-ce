// SPDX-License-Identifier: GPL-3.0-only
// port/platform/windows/clock.cpp: the Windows build's clocks: the real ones, or the virtual clock
// (BEA_PORT_CLOCK, port/platform/common/clock.cpp) with the same values as the Linux build. port.h renames the game's
// clock calls (and Sleep, which passes virtual time on the main thread) to these.
#include <windows.h>
#include <mmsystem.h>
#include <atomic>

#include "../common/platform.h"

namespace
{
// Each native clock keeps its own held value and cumulative wait in its native units. Sampling a
// millisecond clock and QPC separately is not safe if the reader is delayed between those samples.
// The common clock's generation brackets these updates and every reader; the fields are atomic so
// even a reader which has to retry is free of data races. DWORD subtraction intentionally wraps.
std::atomic<bool> gHeld{false}, gCounterValid{true};
std::atomic<unsigned long long> gCounterStart{0}, gCounterOffset{0}, gCounterHeld{0};
std::atomic<DWORD> gTimeStart{0}, gTimeOffset{0}, gTimeHeld{0};
std::atomic<DWORD> gTickStart{0}, gTickOffset{0}, gTickHeld{0};
}

// Called by the single window thread while the common clock's generation is odd. No thread holds
// that generation across the window's blocking wait. If the native counter fails while publishing,
// later QPC reads fail rather than returning a counter with an unknowable pause offset.
void PortWindowsClockPauseBegin()
{
	LARGE_INTEGER c;
	if (gCounterValid.load() && QueryPerformanceCounter(&c))
	{
		const auto raw = (unsigned long long)c.QuadPart;
		gCounterStart.store(raw);
		gCounterHeld.store(raw - gCounterOffset.load());
	}
	else gCounterValid.store(false);
	const DWORD ms = timeGetTime(), tick = GetTickCount();
	gTimeStart.store(ms); gTimeHeld.store(ms - gTimeOffset.load());
	gTickStart.store(tick); gTickHeld.store(tick - gTickOffset.load());
	gHeld.store(true);
}

void PortWindowsClockPauseEnd()
{
	LARGE_INTEGER c;
	if (gCounterValid.load() && QueryPerformanceCounter(&c))
		gCounterOffset.store(gCounterOffset.load() + (unsigned long long)c.QuadPart - gCounterStart.load());
	else gCounterValid.store(false);
	gTimeOffset.store(gTimeOffset.load() + (DWORD)(timeGetTime() - gTimeStart.load()));
	gTickOffset.store(gTickOffset.load() + (DWORD)(GetTickCount() - gTickStart.load()));
	gHeld.store(false);
}

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
	{
		for (;;)
		{
			const auto generation = PortClockReadBegin();
			const bool valid = gCounterValid.load();
			const bool held = gHeld.load();
			LARGE_INTEGER raw{};
			const bool sampled = held || QueryPerformanceCounter(&raw);
			const auto value = held ? gCounterHeld.load() : (unsigned long long)raw.QuadPart - gCounterOffset.load();
			if (!PortClockReadValid(generation)) continue;
			if (!valid || !sampled) return FALSE;
			c->QuadPart = (LONGLONG)value;
			return TRUE;
		}
	}
	c->QuadPart = PortClockQpc();
	return TRUE;
}

// Each millisecond clock uses a held value from itself, so a delayed read inside a wait cannot
// combine an old millisecond sample with a newer QPC sample and run backwards.
static DWORD GameMs(bool tick)
{
	for (;;)
	{
		const auto generation = PortClockReadBegin();
		const bool held = gHeld.load();
		const DWORD value = held ? (tick ? gTickHeld.load() : gTimeHeld.load()) :
			(tick ? GetTickCount() - gTickOffset.load() : timeGetTime() - gTimeOffset.load());
		if (PortClockReadValid(generation)) return value;
	}
}
DWORD WINAPI PortTimeGetTime(void) { return PortClockVirtual() ? (DWORD)PortClockMs() : GameMs(false); }
DWORD WINAPI PortGetTickCount(void) { return PortClockVirtual() ? (DWORD)PortClockMs() : GameMs(true); }

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
