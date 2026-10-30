// SPDX-License-Identifier: GPL-3.0-only
// Deterministic OS-clock stand-ins for the actual Linux and Windows clock wrappers. The reader can be
// suspended after sampling the OS and resumed after a minimized wait, or in the middle of one.
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <time.h>
#include <windows.h>
#include <mmsystem.h>
#include "platform.h"

static std::atomic<unsigned long long> nowNs{3000000000ull};
static std::mutex mu;
static std::condition_variable cv;
static bool started, proceed, parked, releaseReader, finished;
static thread_local bool parkClock;

static void MaybePark()
{
	if (!parkClock) return;
	std::unique_lock<std::mutex> lock(mu);
	parked = true;
	cv.notify_all();
	cv.wait(lock, [] { return releaseReader; });
	parkClock = false;
}

#if defined(TEST_WINDOWS)
static std::atomic<unsigned long long> frequency{0};
static unsigned long long Frequency()
{
	auto value = frequency.load();
	if (!value)
	{
		const char *text = getenv("BEA_TEST_CLOCK_FREQUENCY");
		value = text ? strtoull(text, NULL, 10) : 3579545ull;
		frequency.store(value);
	}
	return value;
}
#if defined(TEST_FAIL_QPF_START)
static std::atomic<bool> failFrequency{true};
#else
static std::atomic<bool> failFrequency{false};
#endif
#if defined(TEST_FAIL_INVALID_QPF_START)
static std::atomic<bool> invalidFrequency{true};
#else
static std::atomic<bool> invalidFrequency{false};
#endif
#if defined(TEST_FAIL_QPC_START)
static std::atomic<bool> failCounter{true};
#else
static std::atomic<bool> failCounter{false};
#endif
static std::atomic<unsigned> frequencyReads{0};
extern "C" {
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *f)
{
	++frequencyReads;
	if (failFrequency.load()) { f->QuadPart = 0; return FALSE; }
	if (invalidFrequency.load()) { f->QuadPart = 0; return TRUE; }
	f->QuadPart = (LONGLONG)Frequency();
	return TRUE;
}
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *c)
{
	if (failCounter.load()) { c->QuadPart = 0; return FALSE; }
	const auto value = nowNs.load();
	const auto freq = Frequency();
	c->QuadPart = (LONGLONG)((value / 1000000000ull) * freq + (value % 1000000000ull) * freq / 1000000000ull);
	MaybePark();
	return TRUE;
}
DWORD WINAPI timeGetTime() { const auto value = (DWORD)(nowNs.load() / 1000000ull); MaybePark(); return value; }
DWORD WINAPI GetTickCount() { const auto value = (DWORD)(nowNs.load() / 1000000ull); MaybePark(); return value; }
void WINAPI Sleep(DWORD) {}
void WINAPI GetSystemTimeAsFileTime(LPFILETIME) {}
void WINAPI GetSystemTime(LPSYSTEMTIME) {}
BOOL WINAPI FileTimeToSystemTime(const FILETIME *, LPSYSTEMTIME) { return TRUE; }
void WINAPI GetLocalTime(LPSYSTEMTIME) {}
BOOL WINAPI PortQueryPerformanceCounter(LARGE_INTEGER *);
BOOL WINAPI PortQueryPerformanceFrequency(LARGE_INTEGER *);
DWORD WINAPI PortTimeGetTime();
DWORD WINAPI PortGetTickCount();
}
#else
extern "C" int __wrap_clock_gettime(clockid_t, timespec *out)
{
	const auto sampled = nowNs.load();
	out->tv_sec = (time_t)(sampled / 1000000000ull);
	out->tv_nsec = (long)(sampled % 1000000000ull);
	MaybePark();
	return 0;
}
#endif

static long long Read(const char *which)
{
#if defined(TEST_WINDOWS)
	if (!strcmp(which, "time")) return PortTimeGetTime();
	if (!strcmp(which, "tick")) return PortGetTickCount();
	LARGE_INTEGER c;
	if (!PortQueryPerformanceCounter(&c)) { puts("FAIL QPC"); exit(2); }
#else
	if (!strcmp(which, "time")) return timeGetTime();
	if (!strcmp(which, "tick")) return GetTickCount();
	LARGE_INTEGER c;
	if (!QueryPerformanceCounter(&c)) { puts("FAIL QPC"); exit(2); }
#endif
	return c.QuadPart;
}

static long long Elapsed(const char *which, long long begin, long long end)
{
	return !strcmp(which, "qpc") ? end - begin : (unsigned)(DWORD(end) - DWORD(begin));
}

int main(int argc, char **argv)
{
	if (argc != 5) { puts("scenario clock mode frequency"); return 2; }
	const char *scenario = argv[1], *which = argv[2], *mode = argv[3];
#if defined(TEST_WINDOWS)
	frequency.store(strtoull(argv[4], NULL, 10));
	const auto units = !strcmp(which, "qpc") ? frequency.load() : 1000ull;
#else
	(void)argv;
	const auto units = !strcmp(which, "qpc") ? 1000000000ull : 1000ull;
#endif
	const bool frozen = !strcmp(mode, "freeze");
	const bool during = !strcmp(scenario, "during");
	const bool twice = !strcmp(scenario, "twice");
	const bool wrap = !strcmp(scenario, "wrap");
#if defined(TEST_WINDOWS)
	const auto wrapBase = 4294966795000000ull;
#else
	// Linux's millisecond clock is relative to the initial 3 s monotonic reading.
	const auto wrapBase = 4294969795000000ull;
#endif
	const auto base = wrap ? wrapBase : !strcmp(scenario, "small") ? 3000000000ull : 1000000000000ull;
	nowNs.store(base);
#if defined(TEST_WINDOWS)
	if (!strcmp(scenario, "startup_recovery"))
	{
		// The configured startup failure occurs in the common clock's static initializer.
		// Once the fake APIs recover, a real QPC read should work and report the native value.
		failFrequency.store(false); invalidFrequency.store(false); failCounter.store(false);
		LARGE_INTEGER counter{};
		const bool ok = PortQueryPerformanceCounter(&counter) &&
			counter.QuadPart == (LONGLONG)(base / 1000000000ull * frequency.load());
		printf("%s scenario=startup_recovery\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 1;
	}
	if (!strcmp(scenario, "startup_frequency_failed_wait"))
	{
		// Frequency remains unavailable from process start until after this whole wait.
		// The native clocks still have enough information to exclude the wait themselves.
		PortClockPauseBegin();
		nowNs.store(base + 10000000000ull);
		PortClockPauseEnd();
		failFrequency.store(false); invalidFrequency.store(false);
		nowNs.store(base + 11000000000ull);
		LARGE_INTEGER counter{};
		const DWORD expectedMs = (DWORD)(base / 1000000ull + 1000);
		const bool ok = PortQueryPerformanceCounter(&counter) &&
			counter.QuadPart == (LONGLONG)((base / 1000000000ull + 1) * frequency.load()) &&
			PortTimeGetTime() == expectedMs && PortGetTickCount() == expectedMs &&
			PortClockShiftNs(PortClockRawNs()) == 0 && !(PortClockReadBegin() & 1);
		printf("%s scenario=startup_frequency_failed_wait\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 1;
	}
	if (!strcmp(scenario, "last_sample_fallback"))
	{
		unsigned long long older = 0;
		std::thread reader([&] { parkClock = true; older = PortClockRawNs(); });
		{
			std::unique_lock<std::mutex> lock(mu);
			cv.wait(lock, [] { return parked; });
		}
		nowNs.store(base + 2000000000ull);
		const auto newer = PortClockRawNs();
		{ std::lock_guard<std::mutex> lock(mu); releaseReader = true; cv.notify_all(); }
		reader.join();
		failCounter.store(true);
		const auto fallback = PortClockRawNs();
		const bool ok = older < newer && fallback == newer;
		printf("%s scenario=last_sample_fallback older=%llu newer=%llu fallback=%llu\n",
			ok ? "PASS" : "FAIL", older, newer, fallback);
		return ok ? 0 : 1;
	}
	if (!strncmp(scenario, "qpf_", 4) || !strncmp(scenario, "qpc_", 4))
	{
		const bool counterFault = !strncmp(scenario, "qpc_", 4);
		const bool beginFault = strstr(scenario, "begin") != NULL;
		// Establish a completed wait first: a failed later edge must not erase it.
		PortClockPauseBegin(); nowNs.store(base + 5000000000ull); PortClockPauseEnd();
		nowNs.store(base + 6000000000ull);
		if (beginFault) (counterFault ? failCounter : failFrequency).store(true);
		PortClockPauseBegin();
		if (beginFault) (counterFault ? failCounter : failFrequency).store(false);
		nowNs.store(base + 16000000000ull);
		if (!beginFault) (counterFault ? failCounter : failFrequency).store(true);
		PortClockPauseEnd();
		failCounter.store(false); failFrequency.store(false);
		nowNs.store(base + 17000000000ull);
		LARGE_INTEGER counter{};
		const bool qpcOk = PortQueryPerformanceCounter(&counter);
		const bool counterCorrect = counterFault ? !qpcOk : qpcOk &&
			counter.QuadPart == (LONGLONG)((base / 1000000000ull + 2) * frequency.load());
		const DWORD expectedMs = (DWORD)(base / 1000000ull + 2000);
		const bool millisecondsCorrect = PortTimeGetTime() == expectedMs && PortGetTickCount() == expectedMs;
		const auto commonShift = PortClockShiftNs(PortClockRawNs());
		const auto expectedShift = (counterFault ? 5000000000ull : 15000000000ull);
		const bool generationEven = !(PortClockReadBegin() & 1);
		const bool ok = counterCorrect && millisecondsCorrect && commonShift == expectedShift && generationEven;
		printf("%s scenario=%s counter_correct=%d ms_correct=%d common_shift=%llu expected_shift=%llu generation_even=%d\n",
			ok ? "PASS" : "FAIL", scenario, counterCorrect, millisecondsCorrect, commonShift, expectedShift, generationEven);
		return ok ? 0 : 1;
	}
	if (!strcmp(scenario, "frequency_fail") || !strcmp(scenario, "frequency_fail_after_wait"))
	{
		// The native counter still works when the frequency query fails. A real-clock counter
		// read must neither call frequency nor turn its independent failure into a QPC failure.
		const bool afterWait = !strcmp(scenario, "frequency_fail_after_wait");
		if (afterWait)
		{
			PortClockPauseBegin();
			nowNs.store(base + 10000000000ull);
			PortClockPauseEnd();
			nowNs.store(base + 11000000000ull);
		}
		failFrequency.store(true);
		const auto before = frequencyReads.load();
		LARGE_INTEGER counter{}, ignored{};
		const bool counterOk = PortQueryPerformanceCounter(&counter);
		const bool queriedFrequency = frequencyReads.load() != before;
		const bool frequencyOk = PortQueryPerformanceFrequency(&ignored);
		const auto expected = (LONGLONG)((base / 1000000000ull + (afterWait ? 1 : 0)) * frequency.load());
		const bool ok = counterOk && !queriedFrequency && !frequencyOk && counter.QuadPart == expected;
		printf("%s scenario=frequency_fail counter_ok=%d counter=%lld expected=%lld frequency_was_queried=%d frequency_ok=%d\n",
			ok ? "PASS" : "FAIL", counterOk, counter.QuadPart, expected, queriedFrequency, frequencyOk);
		return ok ? 0 : 1;
	}
	if (!strcmp(scenario, "counter_fail"))
	{
		failCounter.store(true);
		LARGE_INTEGER counter{};
		const bool failed = !PortQueryPerformanceCounter(&counter);
		failCounter.store(false);
		const bool recovered = PortQueryPerformanceCounter(&counter) &&
			counter.QuadPart == (LONGLONG)(base / 1000000000ull * frequency.load());
		const bool ok = failed && recovered;
		printf("%s scenario=counter_fail\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 1;
	}
#endif
	if (!strcmp(scenario, "rounding"))
	{
		// A fractional-millisecond start exercises conversions between counter and millisecond units.
		nowNs.store(base + 123456ull);
		PortClockPauseBegin();
		const auto held = Read(which);
		unsigned changes = 0, backwards = 0;
		auto previous = held;
		for (unsigned i = 1; i <= 50000; ++i)
		{
			nowNs.store(base + 123456ull + 101ull * i);
			const auto value = Read(which);
			changes += value != held;
			backwards += value < previous;
			previous = value;
		}
		PortClockPauseEnd();
		printf("%s scenario=rounding clock=%s held=%lld changes=%u backwards=%u\n", changes ? "FAIL" : "PASS", which, held, changes, backwards);
		return changes ? 1 : 0;
	}
	unsigned long long startAt = base;
	if (twice)
	{
		nowNs.store(base + 1000000000ull); PortClockPauseBegin();
		nowNs.store(base + 6000000000ull); PortClockPauseEnd();
		startAt = base + 7000000000ull;
		nowNs.store(startAt);
	}
	long long first = 0, delayed = 0;
	std::thread reader([&] {
		first = Read(which);
		{
			std::unique_lock<std::mutex> lock(mu);
			started = true; cv.notify_all(); cv.wait(lock, [] { return proceed; });
		}
		parkClock = true;
		delayed = Read(which);
		{
			std::lock_guard<std::mutex> lock(mu);
			finished = true; cv.notify_all();
		}
	});
	{
		std::unique_lock<std::mutex> lock(mu);
		cv.wait(lock, [] { return started; });
		if (during) { nowNs.store(startAt + 1000000000ull); PortClockPauseBegin(); }
		nowNs.store(startAt + (during ? 2000000000ull : 1000000000ull));
		proceed = true; cv.notify_all();
		// A correct implementation can return the held value without touching the OS clock.
		cv.wait(lock, [] { return parked || finished; });
	}
	if (!during) { nowNs.store(startAt + 2000000000ull); PortClockPauseBegin(); }
	nowNs.store(startAt + 12000000000ull);
	if (!during) PortClockPauseEnd();
	{ std::lock_guard<std::mutex> lock(mu); releaseReader = true; cv.notify_all(); }
	reader.join();
	const auto fresh = Read(which);
	if (during) PortClockPauseEnd();
	const long long got = Elapsed(which, first, delayed);
	const long long expected = (long long)((frozen ? (during ? 1 : 2) : 1) * units);
	const long long freshElapsed = Elapsed(which, first, fresh);
	const long long freshExpected = (long long)((frozen ? (during ? 1 : 2) : 12) * units);
	const bool ok = got == expected && freshElapsed == freshExpected;
	printf("%s scenario=%s clock=%s first=%lld delayed=%lld fresh=%lld elapsed=%lld expected=%lld fresh_elapsed=%lld expected_fresh=%lld\n",
		ok ? "PASS" : "FAIL", scenario, which, first, delayed, fresh, got, expected, freshElapsed, freshExpected);
	return ok ? 0 : 1;
}
