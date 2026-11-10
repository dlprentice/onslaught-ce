// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/feel.cpp: frame-feel measurement (feel.h says what it is, and the environment it reads).
//
// Everything here runs on the game's main thread, which also issues every Direct3D call (the game draws from one thread), so
// the buffer has no lock. A record is a fixed-size struct in a chunk allocated on demand; text is made only when the log is
// written. The Direct3D wrappers count and stamp; they do not look at what the game draws beyond the transform it sets.
#include <windows.h>
#include <d3d9.h>

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>
#ifndef _WIN32
#include <dirent.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#endif

#include "feel.h"
#include "feel_script.h"
#include "portlog.h"

bool PortCeLayer();

namespace
{
enum Kind
{
	K_PRESENT = 'P', K_TICK = 'T', K_PUMP = 'U', K_EVENT = 'N', K_MOUSE = 'M', K_KEYS = 'K', K_BEGIN = 'B',
	K_END = 'E', K_VIEW = 'V', K_PROJ = 'J', K_GAME = 'F', K_GPU = 'G', K_NOTE = 'Q', K_FLUSH = 'X', K_THREAD = 'H'
};

struct Ev
{
	Uint64 t;			// nanoseconds on the real clock since the log opened
	Uint64 u[4];		// more 64-bit values (a Present's end times and thread CPU times, a GPU interval)
	Uint32 k, a, b, c;	// the kind (a letter) and three integers
	float f[16];		// numbers; a matrix for the transform events
};

const size_t kChunk = 16384;

int gOn = -1;
FILE *gOut;
Uint64 gT0;
std::vector<Ev *> gChunks;
size_t gCount, gMax = 4000000, gDropped, gFlushed;
const size_t kBufferCap = 1500000;	// events held between writes (about 180 MB at most)
std::vector<std::string> gNotes;
bool gHeader;
double gFlushEveryS = 30.0;
Uint64 gLastFlush;
bool gGpuOn = true, gHeapOn;
Uint64 gPaceNs, gGrid;
unsigned gFrame;				// Presents seen
unsigned gSceneInFrame, gDrawsInFrame, gTrisInFrame, gDrawsInScene, gTrisInScene;
bool gPassOpen;					// a Present has returned and no pump has run since
void (*gGameSampler)(PortFeelGameSample *);
bool gFlushDue;					// the buffer is nearly full: write it at the next Present

Uint64 Now() { return SDL_GetTicksNS(); }

feel::Script gScript;
bool gScriptLoaded;
unsigned gEnvShotFirst, gEnvShotCount;		// BEA_PORT_FEEL_SHOTS=FIRST:COUNT:DIR: COUNT consecutive frames from Present FIRST
std::string gEnvShotDir;

Ev *Next(char kind)
{
	if (gOn != 1 || gFlushed + gCount >= gMax || gCount >= kBufferCap)
	{
		if (gOn == 1) { ++gDropped; gFlushDue = true; }
		return NULL;
	}
	if (gCount + kChunk >= kBufferCap) gFlushDue = true;
	if (gCount == gChunks.size() * kChunk)
	{
		Ev *chunk = (Ev *)calloc(kChunk, sizeof(Ev));
		if (!chunk)
		{
			++gDropped;
			return NULL;
		}
		gChunks.push_back(chunk);
	}
	Ev *e = &gChunks[gCount / kChunk][gCount % kChunk];
	++gCount;
	e->t = Now() - gT0;
	e->k = (Uint32)(unsigned char)kind;
	return e;
}

#ifndef _WIN32
Uint64 ThreadCpuNs()
{
	timespec ts;
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
	return (Uint64)ts.tv_sec * 1000000000ull + (Uint64)ts.tv_nsec;
}
#else
Uint64 ThreadCpuNs()
{
	FILETIME created, exited, kernel, user;
	if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0;
	ULARGE_INTEGER k, u;
	k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
	u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
	return (k.QuadPart + u.QuadPart) * 100ull;
}
#endif

// ---- Direct3D timestamp queries ---------------------------------------------------------------------------------------------
// Each Present is bracketed: a timestamp is written just before the Present (the end of the frame's commands) and one just
// after it returns (the start of the next frame's). A pair is read when the GPU has executed both. The interval between a
// frame's start and end includes any time the GPU waited for the CPU to submit; the gap between successive ends is the
// cadence at which frames finish. Neither is a pure "GPU busy" time, and the log says which it is.
const int kGpuSlots = 12;
struct GpuSlot
{
	IDirect3DQuery9 *start, *end;
	unsigned frame;
	bool startIssued, endIssued;
};
GpuSlot gGpu[kGpuSlots];
IDirect3DQuery9 *gFreq;
UINT64 gFreqValue;
bool gGpuTried, gGpuUsable;
unsigned gGpuMissed;

void GpuInit(IDirect3DDevice9 *dev)
{
	if (gGpuTried) return;
	gGpuTried = true;
	if (!gGpuOn) return;
	if (FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &gFreq)) || !gFreq)
	{
		gFreq = NULL;
		return;
	}
	for (GpuSlot &s : gGpu)
	{
		if (FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &s.start)) || FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &s.end)))
		{
			if (s.start) { s.start->Release(); s.start = NULL; }
			if (s.end) { s.end->Release(); s.end = NULL; }
			return;
		}
	}
	gFreq->Issue(D3DISSUE_END);
	gGpuUsable = true;
}

GpuSlot *GpuFor(unsigned frame) { return &gGpu[frame % kGpuSlots]; }

void GpuIssueEnd(unsigned frame)
{
	if (!gGpuUsable) return;
	GpuSlot *s = GpuFor(frame);
	if (s->frame != frame)
	{
		// The slot still holds a frame nobody read in time: it is given up, and the loss is counted.
		if (s->startIssued || s->endIssued) ++gGpuMissed;
		s->startIssued = false;
		s->frame = frame;
	}
	s->endIssued = SUCCEEDED(s->end->Issue(D3DISSUE_END));
}

void GpuIssueStart(unsigned nextFrame)
{
	if (!gGpuUsable) return;
	GpuSlot *s = GpuFor(nextFrame);
	if (s->frame != nextFrame)
	{
		if (s->startIssued || s->endIssued) ++gGpuMissed;
		s->endIssued = false;
		s->frame = nextFrame;
	}
	s->startIssued = SUCCEEDED(s->start->Issue(D3DISSUE_END));
}

void GpuPoll(unsigned currentFrame)
{
	if (!gGpuUsable) return;
	if (!gFreqValue)
	{
		UINT64 v = 0;
		if (gFreq->GetData(&v, sizeof v, 0) == S_OK && v) gFreqValue = v;
		else return;
	}
	for (unsigned back = 2; back < (unsigned)kGpuSlots - 1 && back <= currentFrame; ++back)
	{
		unsigned frame = currentFrame - back;
		GpuSlot *s = GpuFor(frame);
		if (s->frame != frame || !s->startIssued || !s->endIssued) continue;
		UINT64 a = 0, b = 0;
		if (s->start->GetData(&a, sizeof a, 0) != S_OK) continue;
		if (s->end->GetData(&b, sizeof b, 0) != S_OK) continue;
		if (Ev *e = Next(K_GPU))
		{
			e->a = frame;
			e->u[0] = (Uint64)((double)a * 1e9 / (double)gFreqValue);
			e->u[1] = (Uint64)((double)b * 1e9 / (double)gFreqValue);
		}
		s->startIssued = s->endIssued = false;
	}
}

// ---- display model ----------------------------------------------------------------------------------------------------------
Uint64 PaceAfterPresent(Uint64 now)
{
	if (!gPaceNs) return now;
	if (!gGrid)
	{
		gGrid = now;
		return now;
	}
	Uint64 target = feel::PaceTarget(now, gGrid, gPaceNs);
	for (;;)
	{
		now = Now();
		if (now >= target) break;
		Uint64 left = target - now;
		if (left > 1500000) SDL_DelayNS(left - 1000000);
		else if (left > 200000) SDL_DelayNS(left - 100000);
	}
	return now;
}

// ---- thread CPU census (Linux) ---------------------------------------------------------------------------------------------
void ThreadCensus()
{
#ifndef _WIN32
	DIR *d = opendir("/proc/self/task");
	if (!d) return;
	while (dirent *ent = readdir(d))
	{
		if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
		char path[96], line[512];
		snprintf(path, sizeof path, "/proc/self/task/%s/stat", ent->d_name);
		FILE *f = fopen(path, "r");
		if (!f) continue;
		size_t n = fread(line, 1, sizeof line - 1, f);
		fclose(f);
		line[n] = 0;
		// pid (comm) state ... utime(14) stime(15); comm may hold spaces and parentheses: find the last ')'.
		char *close = strrchr(line, ')');
		char *open = strchr(line, '(');
		if (!close || !open) continue;
		*close = 0;
		unsigned long utime = 0, stime = 0;
		if (sscanf(close + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &utime, &stime) != 2) continue;
		long tick = sysconf(_SC_CLK_TCK);
		if (tick <= 0) tick = 100;
		if (Ev *e = Next(K_THREAD))
		{
			e->a = (Uint32)atoi(ent->d_name);
			e->u[0] = (Uint64)(utime + stime) * 1000000000ull / (Uint64)tick;
			e->u[1] = (Uint64)stime * 1000000000ull / (Uint64)tick;
			char name[16];
			snprintf(name, sizeof name, "%s", open + 1);
			memcpy(e->f, name, sizeof name);
		}
	}
	closedir(d);
#endif
}

// ---- writing ------------------------------------------------------------------------------------------------------------------
void WriteEvent(FILE *out, const Ev &e)
{
	const unsigned long long t = e.t;
	switch (e.k)
	{
	case K_PRESENT:
		fprintf(out, "P %u %llu %llu %llu %08x %llu %llu %.0f %.0f %.0f\n", e.a, t, (unsigned long long)e.u[0], (unsigned long long)e.u[1],
				e.b, (unsigned long long)e.u[2], (unsigned long long)e.u[3], e.f[0], e.f[1], e.f[2]);
		break;
	case K_TICK:
		fprintf(out, "T %llu %u %d %d %.0f %.9g\n", t, e.a, (int)e.b, (int)e.c, e.f[0], e.f[1]);
		break;
	case K_PUMP: fprintf(out, "U %llu\n", t); break;
	case K_EVENT: fprintf(out, "N %llu %u %llu\n", t, e.a, (unsigned long long)e.u[0]); break;
	case K_MOUSE: fprintf(out, "M %llu %.0f %.0f %u\n", t, e.f[0], e.f[1], e.a); break;
	case K_KEYS: fprintf(out, "K %llu %u %08x\n", t, e.a, e.b); break;
	case K_BEGIN: fprintf(out, "B %llu\n", t); break;
	case K_END: fprintf(out, "E %llu %u %u %u\n", t, e.a, e.b, e.c); break;
	case K_VIEW:
	case K_PROJ:
		fprintf(out, "%c %llu %u %u", (char)e.k, t, e.a, e.b);
		for (int i = 0; i < 16; ++i) fprintf(out, " %.9g", e.f[i]);
		fputc('\n', out);
		break;
	case K_GAME:
		fprintf(out, "F %llu %.9g %.9g %d %d %d", t, e.f[0], e.f[1], (int)e.a, (int)e.b, (int)e.c);
		if (e.u[0])
		{
			for (int i = 0; i < 12; ++i) fprintf(out, " %.9g", e.f[2 + i]);
			if (e.u[0] == 2) fputs(" m", out);		// the numbers are the paused map's window (corner x, corner y, span), not a unit's pose
		}
		fputc('\n', out);
		break;
	case K_GPU: fprintf(out, "G %llu %u %llu %llu\n", t, e.a, (unsigned long long)e.u[0], (unsigned long long)e.u[1]); break;
	case K_NOTE: fprintf(out, "Q %llu %s\n", t, e.a < gNotes.size() ? gNotes[e.a].c_str() : ""); break;
	case K_FLUSH: fprintf(out, "X %llu %llu %u\n", t, (unsigned long long)e.u[0], e.a); break;
	case K_THREAD:
	{
		char name[17];
		memcpy(name, e.f, 16);
		name[16] = 0;
		fprintf(out, "H %llu %u %llu %llu %s\n", t, e.a, (unsigned long long)e.u[0], (unsigned long long)e.u[1], name);
		break;
	}
	default: break;
	}
}

void WriteHeader()
{
	if (gHeader) return;
	gHeader = true;
	fprintf(gOut, "# feel 1\n# clock ns since the log opened (SDL_GetTicksNS); wall at write %lld\n", (long long)time(NULL));
	fprintf(gOut, "# pace_hz %.6g gpu %d heap %d max_events %zu\n", gPaceNs ? 1e9 / (double)gPaceNs : 0.0, (int)gGpuOn, (int)gHeapOn, gMax);
#ifdef CE_VERSION
	fprintf(gOut, "# build %s\n", CE_VERSION);
#endif
	fprintf(gOut, "# sdl %d video %s\n", (int)SDL_GetVersion(), SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "-");
}

void WriteAll(bool final)
{
	if (!gOut) return;
	Uint64 began = Now();
	WriteHeader();
	// A census of every thread's CPU time brackets the run, so the share the game's own thread and the translation layer's
	// threads used can be told apart.
	ThreadCensus();
	size_t written = 0;
	for (size_t i = 0; i < gCount; ++i)
	{
		WriteEvent(gOut, gChunks[i / kChunk][i % kChunk]);
		++written;
	}
	for (Ev *chunk : gChunks) free(chunk);
	gChunks.clear();
	gFlushed += gCount;
	gCount = 0;
	if (gDropped)
		fprintf(gOut, "# dropped %zu events over the %zu limit\n", gDropped, gMax);
	if (gGpuMissed)
		fprintf(gOut, "# gpu_missed %u\n", gGpuMissed);
	Uint64 took = Now() - began;
	if (!final)
	{
		// The write is itself an event: the frame it delayed is not a measurement of the game.
		fprintf(gOut, "X %llu %llu %zu\n", (unsigned long long)(Now() - gT0), (unsigned long long)took, written);
	}
	fflush(gOut);
	gLastFlush = Now();
	gFlushDue = false;
}

void AtExit()
{
	PortFeelFlush();
}

// ---- the input script ---------------------------------------------------------------------------------------------------------
void NoteText(const char *text)
{
	if (Ev *e = Next(K_NOTE))
	{
		e->a = (Uint32)gNotes.size();
		gNotes.push_back(text);
	}
}

void ArmNow(const char *why)
{
	gScript.Arm(Now());
	char note[64];
	snprintf(note, sizeof note, "arm %s frame %u", why, gFrame);
	NoteText(note);
}

bool LoadScript(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return false;
	std::string text;
	char buf[512];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
	fclose(f);
	gScript.Parse(text.c_str());
	for (const std::string &line : gScript.rejected)
		fprintf(stderr, PORT_LOG_PREFIX "feel: script line not understood: %s\n", line.c_str());
	// What the run was given goes into the log first, so the analysis knows the input exactly.
	for (const std::string &line : gScript.accepted)
		NoteText(("script " + line).c_str());
	gScriptLoaded = !gScript.Empty();
	return gScriptLoaded;
}

void ScriptPass()
{
	if (!gScriptLoaded) return;
	if (gScript.PresentArm(gFrame, Now())) { NoteText(("arm present frame " + std::to_string(gFrame)).c_str()); }
	if (gScript.QuitDue(Now()))
	{
		NoteText("quit");
		SDL_Event q{};
		q.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&q);
	}
}

// ---- Direct3D hooks -------------------------------------------------------------------------------------------------------------
typedef HRESULT (STDMETHODCALLTYPE *BeginFn)(IDirect3DDevice9 *);
typedef HRESULT (STDMETHODCALLTYPE *EndFn)(IDirect3DDevice9 *);
typedef HRESULT (STDMETHODCALLTYPE *XformFn)(IDirect3DDevice9 *, D3DTRANSFORMSTATETYPE, const D3DMATRIX *);
typedef HRESULT (STDMETHODCALLTYPE *DrawFn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *DrawIdxFn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *DrawUPFn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *DrawIdxUPFn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT, const void *, UINT);
BeginFn gBegin;
EndFn gEnd;
XformFn gXform;
DrawFn gDraw;
DrawIdxFn gDrawIdx;
DrawUPFn gDrawUP;
DrawIdxUPFn gDrawIdxUP;
void *gHookedTable;
IDirect3DDevice9 *gDevice;

HRESULT STDMETHODCALLTYPE HookBegin(IDirect3DDevice9 *d)
{
	if (Ev *e = Next(K_BEGIN)) (void)e;
	gDrawsInScene = gTrisInScene = 0;
	return gBegin(d);
}

HRESULT STDMETHODCALLTYPE HookEnd(IDirect3DDevice9 *d)
{
	if (Ev *e = Next(K_END))
	{
		e->a = gDrawsInScene;
		e->b = gTrisInScene;
		e->c = gSceneInFrame;
	}
	++gSceneInFrame;
	return gEnd(d);
}

HRESULT STDMETHODCALLTYPE HookXform(IDirect3DDevice9 *d, D3DTRANSFORMSTATETYPE type, const D3DMATRIX *m)
{
	if ((type == D3DTS_VIEW || type == D3DTS_PROJECTION) && m)
	{
		if (Ev *e = Next(type == D3DTS_VIEW ? K_VIEW : K_PROJ))
		{
			e->a = gDrawsInFrame;
			e->b = gTrisInFrame;
			memcpy(e->f, m, sizeof e->f);
		}
	}
	return gXform(d, type, m);
}

HRESULT STDMETHODCALLTYPE HookDraw(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT start, UINT count)
{
	++gDrawsInScene; ++gDrawsInFrame;
	gTrisInScene += count; gTrisInFrame += count;
	return gDraw(d, t, start, count);
}

HRESULT STDMETHODCALLTYPE HookDrawIdx(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, INT base, UINT minv, UINT nv, UINT si, UINT pc)
{
	++gDrawsInScene; ++gDrawsInFrame;
	gTrisInScene += pc; gTrisInFrame += pc;
	return gDrawIdx(d, t, base, minv, nv, si, pc);
}

HRESULT STDMETHODCALLTYPE HookDrawUP(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT count, const void *data, UINT stride)
{
	++gDrawsInScene; ++gDrawsInFrame;
	gTrisInScene += count; gTrisInFrame += count;
	return gDrawUP(d, t, count, data, stride);
}

HRESULT STDMETHODCALLTYPE HookDrawIdxUP(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT minv, UINT nv, UINT count, const void *index,
										D3DFORMAT format, const void *data, UINT stride)
{
	++gDrawsInScene; ++gDrawsInFrame;
	gTrisInScene += count; gTrisInFrame += count;
	return gDrawIdxUP(d, t, minv, nv, count, index, format, data, stride);
}
}

// ---- the interface -------------------------------------------------------------------------------------------------------------

extern "C" int PortFeelOn(void)
{
	if (gOn >= 0) return gOn;
	gOn = 0;
	const char *path = getenv("BEA_PORT_FEEL_LOG");
	if (!path || !*path) return 0;
	if (!PortCeLayer())
	{
		fprintf(stderr, PORT_LOG_PREFIX "feel: not measuring (this run is a replay, a recording, probes or a virtual clock)\n");
		return 0;
	}
	gOut = fopen(path, "w");
	if (!gOut)
	{
		fprintf(stderr, PORT_LOG_PREFIX "feel: cannot write %s\n", path);
		return 0;
	}
	gT0 = Now();
	if (const char *v = getenv("BEA_PORT_FEEL_PACE_HZ"))
		if (double hz = atof(v); hz >= 1.0 && hz <= 1000.0)
			gPaceNs = (Uint64)(1e9 / hz + 0.5);
	if (const char *v = getenv("BEA_PORT_FEEL_GPU")) gGpuOn = !(v[0] == '0' && !v[1]);
	if (const char *v = getenv("BEA_PORT_FEEL_HEAP")) gHeapOn = v[0] == '1' && !v[1];
	if (const char *v = getenv("BEA_PORT_FEEL_FLUSH_S")) gFlushEveryS = atof(v);
	if (const char *v = getenv("BEA_PORT_FEEL_MAX_EVENTS"))
		if (long long n = atoll(v); n > 1000) gMax = (size_t)n;
	if (const char *v = getenv("BEA_PORT_FEEL_SHOTS"))
	{
		unsigned first = 0, count = 0;
		int used = 0;
		if (sscanf(v, "%u:%u:%n", &first, &count, &used) == 2 && used > 0 && v[used] && strlen(v + used) < 560)
		{
			gEnvShotFirst = first;
			gEnvShotCount = count;
			gEnvShotDir = v + used;
		}
	}
	gLastFlush = Now();
	atexit(AtExit);
	gOn = 1;
	if (const char *script = getenv("BEA_PORT_FEEL_SCRIPT"); script && *script)
		if (!LoadScript(script))
			fprintf(stderr, PORT_LOG_PREFIX "feel: input script %s is missing, empty or has no arm line\n", script);
	fprintf(stderr, PORT_LOG_PREFIX "feel: measuring to %s%s\n", path, gPaceNs ? " (display model on)" : "");
	return 1;
}

extern "C" void PortFeelNote(const char *text)
{
	if (!PortFeelOn()) return;
	if (Ev *e = Next(K_NOTE))
	{
		e->a = (Uint32)gNotes.size();
		gNotes.push_back(text ? text : "");
	}
}

extern "C" void PortFeelTick(int level, int state, int paused, unsigned eventTimeBits)
{
	if (!PortFeelOn()) return;
	static unsigned ticks;
	if (gScriptLoaded && gScript.TickArm(state, paused, Now())) NoteText(("arm state frame " + std::to_string(gFrame)).c_str());
	if (Ev *e = Next(K_TICK))
	{
		e->a = ++ticks;
		e->b = (Uint32)level;
		e->c = (Uint32)state;
		e->f[0] = (float)paused;
		float v;
		memcpy(&v, &eventTimeBits, sizeof v);
		e->f[1] = v;
	}
}

extern "C" void PortFeelSetGameSampler(void (*sampler)(PortFeelGameSample *)) { gGameSampler = sampler; }

extern "C" void PortFeelPump(void)
{
	if (!PortFeelOn()) return;
	ScriptPass();
	if (!gPassOpen) return;			// only the first pump after a Present starts a pass
	gPassOpen = false;
	Next(K_PUMP);
}

extern "C" void PortFeelEvent(unsigned type, unsigned long long ageNs)
{
	if (!PortFeelOn()) return;
	if (Ev *e = Next(K_EVENT))
	{
		e->a = type;
		e->u[0] = ageNs;
	}
}

extern "C" void PortFeelMousePoll(long dx, long dy, unsigned buttons)
{
	if (!PortFeelOn()) return;
	if (Ev *e = Next(K_MOUSE))
	{
		e->f[0] = (float)dx;
		e->f[1] = (float)dy;
		e->a = buttons;
	}
}

extern "C" void PortFeelKeyPoll(unsigned keysDown, unsigned stateHash)
{
	if (!PortFeelOn()) return;
	static unsigned last = 0xffffffffu, lastHash;
	if (keysDown == last && stateHash == lastHash) return;		// a poll that saw no change is not an event
	last = keysDown;
	lastHash = stateHash;
	if (Ev *e = Next(K_KEYS))
	{
		e->a = keysDown;
		e->b = stateHash;
	}
}

extern "C" void PortFeelMouseTake(long *dx, long *dy)
{
	if (!PortFeelOn() || !gScriptLoaded || !dx || !dy) return;
	gScript.Take(Now(), dx, dy);
}

extern "C" int PortFeelShotDue(char *dir, unsigned size, unsigned present)
{
	if (!PortFeelOn() || !dir || !size) return 0;
	std::string where;
	if (gScriptLoaded && gScript.ShotDue(Now(), &where))
		;
	else if (gEnvShotCount && present >= gEnvShotFirst && present - gEnvShotFirst < gEnvShotCount)
		where = gEnvShotDir;
	else
		return 0;
	if (where.size() + 1 > size) return 0;
	memcpy(dir, where.c_str(), where.size() + 1);
	return 1;
}

extern "C" int PortFeelKeyDue(unsigned *scancode, unsigned *raw, int *down)
{
	if (!PortFeelOn() || !gScriptLoaded || !scancode || !raw || !down) return 0;
	feel::KeyStep k;
	if (!gScript.KeyDue(Now(), &k)) return 0;
	*scancode = k.sc;
	*raw = k.raw;
	*down = k.down ? 1 : 0;
	char note[48];
	snprintf(note, sizeof note, "key %s %u", k.down ? "down" : "up", k.sc);
	NoteText(note);
	return 1;
}

extern "C" void PortFeelDeviceCreated(void *device, void **table, PortFeelInstallFn install)
{
	if (!PortFeelOn() || !table || !install) return;
	if (gHookedTable == (void *)table) return;
	gHookedTable = table;
	// What each entry holds now is what the wrapper calls: the device's own, or a hook installed before this one.
	gBegin = (BeginFn)table[41];
	gEnd = (EndFn)table[42];
	gXform = (XformFn)table[44];
	gDraw = (DrawFn)table[81];
	gDrawIdx = (DrawIdxFn)table[82];
	gDrawUP = (DrawUPFn)table[83];
	gDrawIdxUP = (DrawIdxUPFn)table[84];
	install(table, 41, (void *)&HookBegin);
	install(table, 42, (void *)&HookEnd);
	install(table, 44, (void *)&HookXform);
	install(table, 81, (void *)&HookDraw);
	install(table, 82, (void *)&HookDrawIdx);
	install(table, 83, (void *)&HookDrawUP);
	install(table, 84, (void *)&HookDrawIdxUP);
	gGpuTried = false;
	gGpuUsable = false;
	for (GpuSlot &s : gGpu) { s.start = s.end = NULL; s.frame = ~0u; s.startIssued = s.endIssued = false; }
	gDevice = (IDirect3DDevice9 *)device;
	if (device) GpuInit(gDevice);
	PortFeelNote(gGpuUsable ? "device created: gpu timestamps on" : "device created: gpu timestamps unavailable");
}

extern "C" void PortFeelBeforeReset(void)
{
	if (!PortFeelOn()) return;
	// Default-pool resources, queries among them, must be gone before a real Direct3D 9 Reset.
	for (GpuSlot &s : gGpu)
	{
		if (s.start) { s.start->Release(); s.start = NULL; }
		if (s.end) { s.end->Release(); s.end = NULL; }
		s.startIssued = s.endIssued = false;
		s.frame = ~0u;
	}
	if (gFreq) { gFreq->Release(); gFreq = NULL; }
	gFreqValue = 0;
	gGpuUsable = false;
	gGpuTried = false;			// made again at the next Present
	PortFeelNote("device reset");
}

static Uint64 gPresentBegan, gCpuAtBegin;

extern "C" void PortFeelPresentBegin(void *device)
{
	if (!PortFeelOn()) return;
	gPresentBegan = Now() - gT0;
	gCpuAtBegin = ThreadCpuNs();
	if (!gGpuTried && gDevice) GpuInit(gDevice);
	if (device && gGpuUsable) GpuIssueEnd(gFrame);
}

extern "C" void PortFeelPresentEnd(void *device, long result)
{
	if (!PortFeelOn()) return;
	Uint64 realEnd = Now() - gT0;
	Uint64 cpuEnd = ThreadCpuNs();
	if (device && gGpuUsable) GpuIssueStart(gFrame + 1);
	Ev *game = Next(K_GAME);
	if (game)
	{
		PortFeelGameSample s = {};
		s.fraction = s.fraction2 = -1.f;
		if (gGameSampler) gGameSampler(&s);
		game->f[0] = s.fraction;
		game->f[1] = s.fraction2;
		game->a = (Uint32)s.level;
		game->b = (Uint32)s.state;
		game->c = (Uint32)s.paused;
		game->u[0] = s.focus > 0 ? (Uint64)s.focus : 0;		// 1: a driven unit's pose; 2: the paused map's window
		if (s.focus)
			for (int i = 0; i < 12; ++i) game->f[2 + i] = s.pose[i];
	}
	Uint64 paced = PaceAfterPresent(Now()) - gT0;
	if (Ev *e = Next(K_PRESENT))
	{
		e->t = gPresentBegan;
		e->a = gFrame;
		e->b = (Uint32)result;
		e->u[0] = realEnd;
		e->u[1] = paced;
		e->u[2] = gCpuAtBegin;
		e->u[3] = cpuEnd;
#ifndef _WIN32
		rusage ru;
		if (getrusage(RUSAGE_SELF, &ru) == 0)
		{
			e->f[0] = (float)ru.ru_minflt;
			e->f[1] = (float)(ru.ru_nvcsw + ru.ru_nivcsw);
		}
#ifdef __GLIBC__
		if (gHeapOn)
		{
			struct mallinfo mi = mallinfo();
			e->f[2] = (float)((unsigned)mi.uordblks / 1024u);
		}
#endif
#endif
	}
	if (device && gGpuUsable) GpuPoll(gFrame);
	if (gFrame == 0) ThreadCensus();		// the census of the run's start; the last one is at exit
	++gFrame;
	gSceneInFrame = gDrawsInFrame = gTrisInFrame = gDrawsInScene = gTrisInScene = 0;
	gPassOpen = true;
	if (gFlushDue || (gFlushEveryS > 0 && (double)(Now() - gLastFlush) > gFlushEveryS * 1e9))
		WriteAll(false);
}

extern "C" void PortFeelFlush(void)
{
	if (gOn != 1 || !gOut) return;
	WriteAll(true);
	fclose(gOut);
	gOut = NULL;
	gOn = 0;		// nothing is recorded after the log is closed
}
