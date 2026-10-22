// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/crtrand.cpp: the C runtime's rand and srand as retail's statically linked Visual C++ 6 runtime
// (LIBCMT) has them: a linear congruential generator per thread, seed 1 until the thread calls srand,
// holdrand = holdrand * 214013 + 2531011, result (holdrand >> 16) & 0x7fff. Game objects call these through
// port/build.py's renames (rand -> bea_crt_rand); the host's libc rand (a different generator) stays for the libraries.
// Effects drawn with rand (particles, among others) then match retail's.
// BEA_PORT_CRT_CALLERS=PATH:FIRST-LAST[,FIRST-LAST...]: every rand call made while building one of those Presents
// (1-based, the probes' numbering) is logged as "present return-address" for mapping to functions (addr2line).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <execinfo.h>
#endif
extern "C" unsigned PortPresentCount(void);	// d3d9hooks.cpp: Presents completed so far
static thread_local unsigned int tHoldRand = 1;
static thread_local unsigned int tCalls;	// rand calls on this thread (the CRT probe reads the main thread's)

extern "C" void bea_crt_srand(unsigned int seed) { tHoldRand = seed; }

extern "C" int bea_crt_rand(void)
{
	tCalls++;
	static int mode = -1;
	static FILE *log;
	static unsigned ranges[16][2];
	static int nranges;
	if (mode < 0)
	{
		mode = 0;
		if (const char *spec = getenv("BEA_PORT_CRT_CALLERS"))
			if (const char *colon = strrchr(spec, ':'))
			{
				char path[512];
				snprintf(path, sizeof path, "%.*s", (int)(colon - spec), spec);
				for (const char *p = colon + 1; *p && nranges < 16;)
				{
					unsigned a = 0, b = 0;
					if (sscanf(p, "%u-%u", &a, &b) == 2) { ranges[nranges][0] = a; ranges[nranges][1] = b; nranges++; }
					const char *c = strchr(p, ',');
					if (!c) break;
					p = c + 1;
				}
				log = fopen(path, "w");
				mode = log ? 1 : 0;
			}
	}
	if (mode == 1)
	{
		unsigned present = PortPresentCount() + 1;
		for (int i = 0; i < nranges; i++)
			if (present >= ranges[i][0] && present <= ranges[i][1])
			{
#ifdef __linux__
				// BEA_PORT_CRT_DEPTH=N (1..24): the N innermost callers, joined by '<' (glibc backtrace walks unwind tables)
				static int depth = -1;
				if (depth < 0) { const char *d = getenv("BEA_PORT_CRT_DEPTH"); depth = d ? atoi(d) : 1; if (depth < 1) depth = 1; if (depth > 24) depth = 24; }
				if (depth > 1)
				{
					void *frames[25];
					int n = backtrace(frames, depth + 1);
					fprintf(log, "%u", present);
					for (int k = 1; k < n; k++) fprintf(log, "%s%p", k == 1 ? " " : "<", frames[k]);
					fputc('\n', log);
					fflush(log);	// the port's ExitProcess ends with _exit, which would drop a buffered tail
					break;
				}
#endif
				fprintf(log, "%u %p\n", present, __builtin_return_address(0));
				fflush(log);
				break;
			}
	}
	tHoldRand = tHoldRand * 214013u + 2531011u;
	return (int)((tHoldRand >> 16) & 0x7fff);
}

// The calling thread's state for the probes (replay.cpp, on the main thread): call count and holdrand.
extern "C" void PortCrtRandState(unsigned int *calls, unsigned int *hold)
{
	*calls = tCalls;
	*hold = tHoldRand;
}
