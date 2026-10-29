// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/kernel.cpp: the KERNEL32/WINMM subset the game calls (port/PLAN.md, "API surface"), on POSIX:
// handles for events, mutexes, threads, files and searches; waits; timers; heap; strings and code pages. Windows
// builds use the real API. Allocation is malloc-based (the game's operator new serves every C++ allocation).
#include "handles.h"
#include "../common/platform.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <unistd.h>

static thread_local DWORD tLastError;

extern "C" DWORD WINAPI GetLastError(void) { return tLastError; }
extern "C" void WINAPI SetLastError(DWORD e) { tLastError = e; }
void PortSetLastError(DWORD e) { tLastError = e; }

// ---- handles ------------------------------------------------------------------------------------------------------
// Every handle the port gives out is recorded, and every call checks a handle before touching it, as Windows does: a
// call with anything else fails with ERROR_INVALID_HANDLE. (Retail's StopMusicStream resets the address of
// PlayMusicStream as an event, 005285d7; on Windows that call just fails.)
static pthread_mutex_t sHandlesLock = PTHREAD_MUTEX_INITIALIZER;
static PortHandle *sHandles[8192];		// open addressing, keyed by pointer

static unsigned Slot(const void *p) { return (unsigned)(((uintptr_t)p >> 4) * 2654435761u) % 8192; }

static void Register(PortHandle *h)
{
	pthread_mutex_lock(&sHandlesLock);
	unsigned i = Slot(h);
	while (sHandles[i] && sHandles[i] != (PortHandle *)1)
		i = (i + 1) % 8192;
	sHandles[i] = h;
	pthread_mutex_unlock(&sHandlesLock);
}

static void Unregister(PortHandle *h)
{
	pthread_mutex_lock(&sHandlesLock);
	for (unsigned i = Slot(h), n = 0; sHandles[i] && n < 8192; i = (i + 1) % 8192, n++)
		if (sHandles[i] == h)
		{
			sHandles[i] = (PortHandle *)1;	// tombstone
			break;
		}
	pthread_mutex_unlock(&sHandlesLock);
}

PortHandle *PortCheck(HANDLE handle, int kind)
{
	PortHandle *found = NULL;
	pthread_mutex_lock(&sHandlesLock);
	for (unsigned i = Slot(handle), n = 0; sHandles[i] && n < 8192; i = (i + 1) % 8192, n++)
		if (sHandles[i] == (PortHandle *)handle)
		{
			found = sHandles[i];
			break;
		}
	pthread_mutex_unlock(&sHandlesLock);
	if (found && kind && found->kind != kind)
		found = NULL;
	if (!found)
		PortSetLastError(ERROR_INVALID_HANDLE);
	return found;
}

PortHandle *PortHandleNew(PortHandleKind kind, size_t size)
{
	PortHandle *h = (PortHandle *)calloc(1, size);
	h->kind = kind;
	pthread_mutex_init(&h->lock, NULL);
	pthread_cond_init(&h->cond, NULL);
	Register(h);
	return h;
}

void PortHandleFree(PortHandle *h)
{
	Unregister(h);
	pthread_cond_destroy(&h->cond);
	pthread_mutex_destroy(&h->lock);
	free(h);
}

static void HandleFree(PortHandle *h) { PortHandleFree(h); }

extern "C" BOOL WINAPI CloseHandle(HANDLE handle)
{
	PortHandle *h = PortCheck(handle, 0);
	if (!h)
		return FALSE;
	switch (h->kind)
	{
	case PORT_FILE:
		PortFileClose(h);
		break;
	case PORT_THREAD:
	{
		PortThread *t = (PortThread *)h;
		// A thread's handle may close while it runs (the game closes its music thread's after the wait): detach, and
		// let the thread free the record when it finishes.
		pthread_mutex_lock(&h->lock);
		bool done = t->finished;
		t->orphaned = true;
		pthread_mutex_unlock(&h->lock);
		if (done)
		{
			pthread_join(t->thread, NULL);
			HandleFree(h);
		}
		else
			pthread_detach(t->thread);
		return TRUE;
	}
	default:
		break;
	}
	HandleFree(h);
	return TRUE;
}

// ---- time ---------------------------------------------------------------------------------------------------------
static ULONGLONG MonotonicNs()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ULONGLONG)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static const ULONGLONG kStartNs = MonotonicNs();

extern "C" void WINAPI Sleep(DWORD ms)
{
	PortClockSleep(ms);
	timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
	{
	}
}

extern "C" DWORD WINAPI SleepEx(DWORD ms, BOOL) { Sleep(ms); return 0; }
extern "C" BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *f)
{
	long long v = PortClockQpf();
	f->QuadPart = v ? v : 1000000000ll;
	return TRUE;
}
// Real time, or the port's virtual clocks (BEA_PORT_CLOCK, port/platform/common/clock.cpp).
extern "C" BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *c)
{
	c->QuadPart = PortClockVirtual() ? PortClockQpc() : (LONGLONG)MonotonicNs();
	return TRUE;
}
static DWORD Milliseconds() { return PortClockVirtual() ? (DWORD)PortClockMs() : (DWORD)((MonotonicNs() - kStartNs) / 1000000ull); }
extern "C" DWORD WINAPI GetTickCount(void) { return Milliseconds(); }
extern "C" DWORD WINAPI timeGetTime(void) { return Milliseconds(); }
extern "C" MMRESULT WINAPI timeBeginPeriod(UINT) { return MMSYSERR_NOERROR; }
extern "C" MMRESULT WINAPI timeEndPeriod(UINT) { return MMSYSERR_NOERROR; }

static void FillSystemTime(LPSYSTEMTIME st, bool local)
{
	timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	if (PortClockVirtual())
	{
		// FILETIME (1601) to Unix time, no time zone (the virtual clock is the same everywhere).
		unsigned long long ft = PortClockFiletime();
		ts.tv_sec = (time_t)(ft / 10000000ull - 11644473600ull);
		ts.tv_nsec = (long)(ft % 10000000ull) * 100;
		local = false;
	}
	struct tm t;
	if (local)
		localtime_r(&ts.tv_sec, &t);
	else
		gmtime_r(&ts.tv_sec, &t);
	st->wYear = t.tm_year + 1900;
	st->wMonth = t.tm_mon + 1;
	st->wDayOfWeek = t.tm_wday;
	st->wDay = t.tm_mday;
	st->wHour = t.tm_hour;
	st->wMinute = t.tm_min;
	st->wSecond = t.tm_sec;
	st->wMilliseconds = ts.tv_nsec / 1000000;
}

extern "C" void WINAPI GetLocalTime(LPSYSTEMTIME st) { FillSystemTime(st, true); }
extern "C" void WINAPI GetSystemTime(LPSYSTEMTIME st) { FillSystemTime(st, false); }

// ---- events, mutexes, waits -----------------------------------------------------------------------------------------
extern "C" HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES, BOOL manual, BOOL initial, LPCSTR)
{
	PortEvent *e = (PortEvent *)PortHandleNew(PORT_EVENT, sizeof(PortEvent));
	e->manual = manual != 0;
	e->signaled = initial != 0;
	return e;
}

extern "C" BOOL WINAPI SetEvent(HANDLE handle)
{
	PortEvent *e = (PortEvent *)PortCheck(handle, PORT_EVENT);
	if (!e)
		return FALSE;
	pthread_mutex_lock(&e->h.lock);
	e->signaled = true;
	pthread_cond_broadcast(&e->h.cond);
	pthread_mutex_unlock(&e->h.lock);
	PortWaitersWake();
	return TRUE;
}

extern "C" BOOL WINAPI ResetEvent(HANDLE handle)
{
	PortEvent *e = (PortEvent *)PortCheck(handle, PORT_EVENT);
	if (!e)
		return FALSE;
	pthread_mutex_lock(&e->h.lock);
	e->signaled = false;
	pthread_mutex_unlock(&e->h.lock);
	return TRUE;
}

extern "C" HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL initialOwner, LPCSTR)
{
	PortMutex *m = (PortMutex *)PortHandleNew(PORT_MUTEX, sizeof(PortMutex));
	if (initialOwner)
	{
		m->owner = pthread_self();
		m->count = 1;
	}
	return m;
}

extern "C" BOOL WINAPI ReleaseMutex(HANDLE handle)
{
	PortMutex *m = (PortMutex *)PortCheck(handle, PORT_MUTEX);
	if (!m)
		return FALSE;
	pthread_mutex_lock(&m->h.lock);
	BOOL ok = m->count > 0 && pthread_equal(m->owner, pthread_self());
	if (ok && --m->count == 0)
		pthread_cond_broadcast(&m->h.cond);
	pthread_mutex_unlock(&m->h.lock);
	if (ok)
		PortWaitersWake();
	return ok;
}

// Tries to take one object without blocking; called with the object's lock held.
static bool TryAcquire(PortHandle *h)
{
	switch (h->kind)
	{
	case PORT_EVENT:
	{
		PortEvent *e = (PortEvent *)h;
		if (!e->signaled)
			return false;
		if (!e->manual)
			e->signaled = false;
		return true;
	}
	case PORT_MUTEX:
	{
		PortMutex *m = (PortMutex *)h;
		if (m->count == 0 || pthread_equal(m->owner, pthread_self()))
		{
			m->owner = pthread_self();
			m->count++;
			return true;
		}
		return false;
	}
	case PORT_THREAD:
		return ((PortThread *)h)->finished;
	default:
		return true;
	}
}

static void Deadline(timespec *ts, DWORD ms)
{
	clock_gettime(CLOCK_REALTIME, ts);
	ts->tv_sec += ms / 1000;
	ts->tv_nsec += (long)(ms % 1000) * 1000000L;
	if (ts->tv_nsec >= 1000000000L)
	{
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

extern "C" DWORD WINAPI WaitForSingleObject(HANDLE handle, DWORD ms)
{
	PortHandle *h = PortCheck(handle, 0);
	if (!h)
		return WAIT_FAILED;
	timespec deadline;
	if (ms != INFINITE)
		Deadline(&deadline, ms);
	pthread_mutex_lock(&h->lock);
	DWORD result = WAIT_OBJECT_0;
	while (!TryAcquire(h))
	{
		int rc = ms == INFINITE ? pthread_cond_wait(&h->cond, &h->lock) : pthread_cond_timedwait(&h->cond, &h->lock, &deadline);
		if (rc == ETIMEDOUT)
		{
			result = WAIT_TIMEOUT;
			break;
		}
	}
	pthread_mutex_unlock(&h->lock);
	return result;
}

// Several objects: a process-wide generation counter that every signal bumps, and a scan of the objects after each.
static pthread_mutex_t sWaitLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sWaitCond = PTHREAD_COND_INITIALIZER;
static unsigned sWaitGeneration;

void PortWaitersWake()
{
	pthread_mutex_lock(&sWaitLock);
	sWaitGeneration++;
	pthread_cond_broadcast(&sWaitCond);
	pthread_mutex_unlock(&sWaitLock);
}

extern "C" DWORD WINAPI WaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL all, DWORD ms)
{
	for (DWORD i = 0; i < count; i++)
		if (!PortCheck(handles[i], 0))
			return WAIT_FAILED;
	timespec deadline;
	if (ms != INFINITE)
		Deadline(&deadline, ms);
	for (;;)
	{
		pthread_mutex_lock(&sWaitLock);
		unsigned generation = sWaitGeneration;
		pthread_mutex_unlock(&sWaitLock);
		if (all)
		{
			bool every = true;
			for (DWORD i = 0; i < count && every; i++)
			{
				PortHandle *h = (PortHandle *)handles[i];
				pthread_mutex_lock(&h->lock);
				every = h->kind == PORT_EVENT ? ((PortEvent *)h)->signaled : h->kind == PORT_THREAD ? ((PortThread *)h)->finished : true;
				pthread_mutex_unlock(&h->lock);
			}
			if (every)
			{
				for (DWORD i = 0; i < count; i++)
					WaitForSingleObject(handles[i], 0);
				return WAIT_OBJECT_0;
			}
		}
		else
		{
			for (DWORD i = 0; i < count; i++)
			{
				PortHandle *h = (PortHandle *)handles[i];
				pthread_mutex_lock(&h->lock);
				bool got = TryAcquire(h);
				pthread_mutex_unlock(&h->lock);
				if (got)
					return WAIT_OBJECT_0 + i;
			}
		}
		pthread_mutex_lock(&sWaitLock);
		int rc = 0;
		while (generation == sWaitGeneration && rc != ETIMEDOUT)
			rc = ms == INFINITE ? pthread_cond_wait(&sWaitCond, &sWaitLock) : pthread_cond_timedwait(&sWaitCond, &sWaitLock, &deadline);
		pthread_mutex_unlock(&sWaitLock);
		if (rc == ETIMEDOUT)
			return WAIT_TIMEOUT;
	}
}

// ---- threads --------------------------------------------------------------------------------------------------------
static void *ThreadMain(void *arg)
{
	// Windows starts every thread at x87 control word 0x27F (53-bit precision, round to nearest, all exceptions masked),
	// whatever its creator runs at; the main thread's 0x07F after CreateDevice is not inherited. The game's music
	// decoder and voice loader run here, and libvorbis gives retail's PCM only at 0x27F (port/vorbis).
	unsigned short cw = 0x27F;
	__asm__ volatile("fldcw %0" : : "m"(cw));
	PortThread *t = (PortThread *)arg;
	DWORD code = t->start(t->param);
	pthread_mutex_lock(&t->h.lock);
	t->exitCode = code;
	t->finished = true;
	bool orphaned = t->orphaned;
	pthread_cond_broadcast(&t->h.cond);
	pthread_mutex_unlock(&t->h.lock);
	PortWaitersWake();
	if (orphaned)
		HandleFree(&t->h);
	return NULL;
}

extern "C" HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID param, DWORD,
									  LPDWORD id)
{
	PortThread *t = (PortThread *)PortHandleNew(PORT_THREAD, sizeof(PortThread));
	t->start = start;
	t->param = param;
	t->exitCode = STILL_ACTIVE;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	if (stack)
		pthread_attr_setstacksize(&attr, stack < 65536 ? 65536 : stack);
	int rc = pthread_create(&t->thread, &attr, ThreadMain, t);
	pthread_attr_destroy(&attr);
	if (rc != 0)
	{
		HandleFree(&t->h);
		return NULL;
	}
	if (id)
		*id = (DWORD)(uintptr_t)t;
	return t;
}

extern "C" BOOL WINAPI GetExitCodeThread(HANDLE handle, LPDWORD code)
{
	PortThread *t = (PortThread *)PortCheck(handle, PORT_THREAD);
	if (!t)
		return FALSE;
	pthread_mutex_lock(&t->h.lock);
	*code = t->finished ? t->exitCode : STILL_ACTIVE;
	pthread_mutex_unlock(&t->h.lock);
	return TRUE;
}

extern "C" BOOL WINAPI SetThreadPriority(HANDLE, int) { return TRUE; }
extern "C" HANDLE WINAPI GetCurrentThread(void) { return (HANDLE)(intptr_t)-2; }
extern "C" DWORD WINAPI GetCurrentThreadId(void) { return (DWORD)pthread_self(); }
extern "C" HANDLE WINAPI GetCurrentProcess(void) { return (HANDLE)(intptr_t)-1; }
extern "C" DWORD WINAPI GetCurrentProcessId(void) { return (DWORD)getpid(); }
// Windows' ExitProcess ends the process without the C runtime's exit work: the game's static destructors do not run
// (CConsole::FatalError calls it with half the game built; running ~DeviceObject then crashed, A's empty-folder run).
// Streams are flushed so the port's logs keep their last lines.
// PortExitHook is weak: only a test-only diagnostic build defines it (to seal its record, which no atexit handler can,
// as the process ends with _exit); in every other build the pointer is null and nothing is called.
extern "C" void PortExitHook(int status) __attribute__((weak));
extern "C" DECLSPEC_NORETURN void WINAPI ExitProcess(UINT code)
{
	fflush(NULL);
	if (PortExitHook)
		PortExitHook((int)code);
	_exit((int)code);
}

extern "C" void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
	pthread_mutexattr_t a;
	pthread_mutexattr_init(&a);
	pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(m, &a);
	pthread_mutexattr_destroy(&a);
	cs->DebugInfo = m;
}
extern "C" void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION cs)
{
	pthread_mutex_destroy((pthread_mutex_t *)cs->DebugInfo);
	free(cs->DebugInfo);
}
extern "C" void WINAPI EnterCriticalSection(LPCRITICAL_SECTION cs) { pthread_mutex_lock((pthread_mutex_t *)cs->DebugInfo); }
extern "C" void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION cs) { pthread_mutex_unlock((pthread_mutex_t *)cs->DebugInfo); }
extern "C" LONG WINAPI InterlockedIncrement(LONG volatile *p) { return __sync_add_and_fetch(p, 1); }
extern "C" LONG WINAPI InterlockedDecrement(LONG volatile *p) { return __sync_sub_and_fetch(p, 1); }

// Retail's crash handler is installed and never reached in normal play; the port keeps the default signal behaviour.
extern "C" LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER) { return NULL; }

// ---- memory -----------------------------------------------------------------------------------------------------------
extern "C" void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS ms)
{
	struct sysinfo si;
	sysinfo(&si);
	ULONGLONG total = (ULONGLONG)si.totalram * si.mem_unit, avail = (ULONGLONG)si.freeram * si.mem_unit;
	// A 32-bit program saw at most 2 GB of each (SIZE_T fields).
	const ULONGLONG cap = 0x7fffffffull;
	ms->dwLength = sizeof(MEMORYSTATUS);
	ms->dwMemoryLoad = total ? (DWORD)(100 - avail * 100 / total) : 0;
	ms->dwTotalPhys = (SIZE_T)(total < cap ? total : cap);
	ms->dwAvailPhys = (SIZE_T)(avail < cap ? avail : cap);
	ms->dwTotalPageFile = ms->dwTotalPhys;
	ms->dwAvailPageFile = ms->dwAvailPhys;
	ms->dwTotalVirtual = (SIZE_T)cap;
	ms->dwAvailVirtual = (SIZE_T)cap;
}

extern "C" HANDLE WINAPI GetProcessHeap(void) { return (HANDLE)(intptr_t)0x48454150; }
extern "C" LPVOID WINAPI HeapAlloc(HANDLE, DWORD flags, SIZE_T n) { return flags & HEAP_ZERO_MEMORY ? calloc(1, n) : malloc(n); }
extern "C" BOOL WINAPI HeapFree(HANDLE, DWORD, LPVOID p) { free(p); return TRUE; }

// ---- module, debug output ------------------------------------------------------------------------------------------
extern "C" HMODULE WINAPI GetModuleHandleA(LPCSTR) { return (HMODULE)(intptr_t)0x00400000; }

extern "C" DWORD WINAPI GetModuleFileNameA(HMODULE, LPSTR out, DWORD n)
{
	// Retail's path: the game folder's BEA.exe, spelt as the game spells paths.
	int len = snprintf(out, n, "%s\\BEA.exe", ".");
	return len < 0 ? 0 : (DWORD)len;
}

extern "C" void WINAPI OutputDebugStringA(LPCSTR s)
{
	if (getenv("BEA_PORT_DEBUG"))
		fputs(s, stderr);
}

extern "C" int WINAPI MulDiv(int a, int b, int c)
{
	if (c == 0)
		return -1;
	long long r = (long long)a * b;
	r = r >= 0 ? (r + (c > 0 ? c : -c) / 2) / c : (r - (c > 0 ? c : -c) / 2) / c;
	return (int)r;
}

// ---- strings ------------------------------------------------------------------------------------------------------
extern "C" LPSTR WINAPI lstrcpyA(LPSTR d, LPCSTR s) { return strcpy(d, s); }
extern "C" LPSTR WINAPI lstrcpynA(LPSTR d, LPCSTR s, int n)
{
	if (n <= 0)
		return d;
	strncpy(d, s, n - 1);
	d[n - 1] = 0;
	return d;
}
extern "C" LPSTR WINAPI lstrcatA(LPSTR d, LPCSTR s) { return strcat(d, s); }
extern "C" int WINAPI lstrlenA(LPCSTR s) { return s ? (int)strlen(s) : 0; }
extern "C" int WINAPI lstrcmpA(LPCSTR a, LPCSTR b) { return strcmp(a, b); }
extern "C" int WINAPI lstrcmpiA(LPCSTR a, LPCSTR b) { return strcasecmp(a, b); }
extern "C" int WINAPIV wsprintfA(LPSTR d, LPCSTR f, ...)
{
	va_list ap;
	va_start(ap, f);
	int n = vsprintf(d, f, ap);
	va_end(ap);
	return n;
}
extern "C" int WINAPI wvsprintfA(LPSTR d, LPCSTR f, va_list ap) { return vsprintf(d, f, ap); }

// Code pages: the game's text is Windows-1252 bytes and UTF-16 (2-byte WCHAR, -fshort-wchar). Retail ran with the
// player's ANSI code page; 1252 covers the shipped languages (English, French, German, Italian, Spanish).
static const unsigned short k1252High[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

static unsigned short Widen1252(unsigned char c) { return c >= 0x80 && c < 0xA0 ? k1252High[c - 0x80] : c; }

static int Narrow1252(unsigned short w)
{
	if (w < 0x80 || (w >= 0xA0 && w < 0x100))
		return w;
	for (int i = 0; i < 32; i++)
		if (k1252High[i] == w)
			return 0x80 + i;
	return -1;
}

extern "C" int WINAPI MultiByteToWideChar(UINT, DWORD, LPCSTR s, int n, LPWSTR d, int dn)
{
	if (n < 0)
		n = (int)strlen(s) + 1;
	if (dn == 0)
		return n;
	int count = n < dn ? n : dn;
	unsigned short *out = (unsigned short *)d;
	for (int i = 0; i < count; i++)
		out[i] = Widen1252((unsigned char)s[i]);
	if (count < n)
	{
		PortSetLastError(122);	// ERROR_INSUFFICIENT_BUFFER
		return 0;
	}
	return count;
}

extern "C" int WINAPI WideCharToMultiByte(UINT, DWORD, LPCWSTR s, int n, LPSTR d, int dn, LPCSTR defchar, LPBOOL used)
{
	const unsigned short *in = (const unsigned short *)s;
	if (n < 0)
	{
		n = 0;
		while (in[n])
			n++;
		n++;
	}
	if (used)
		*used = FALSE;
	if (dn == 0)
		return n;
	int count = n < dn ? n : dn;
	for (int i = 0; i < count; i++)
	{
		int c = Narrow1252(in[i]);
		if (c < 0)
		{
			c = defchar ? (unsigned char)*defchar : '?';
			if (used)
				*used = TRUE;
		}
		d[i] = (char)c;
	}
	if (count < n)
	{
		PortSetLastError(122);
		return 0;
	}
	return count;
}
