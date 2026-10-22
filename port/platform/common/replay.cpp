// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/replay.cpp: the input tapes and probe readout (REPORT-6), so a port run compares with the
// retail goldens (golden-640/levelN.retail.probes) Present by Present. Off unless BEA_PORT_TAPE or BEA_PORT_PROBES is set.
//   BEA_PORT_TAPE=PATH      the tape: `K <frame> <down|up> <vk hex> <scan hex> <name>`, `M <frame> <dx> <dy> <wheel> <buttons>`
//   BEA_PORT_PROBES=PATH    probe lines `P <frame> <NAME> <hex>`, every BEA_PORT_PROBE_EVERY (50) Presents
//   BEA_PORT_REPLAY_MAX=N   after frame N's probes, post WM_CLOSE (the game's own shutdown)
//   (with BEA_PORT_PROBES) FE: in the front end, its frame step, page, transition and counter (28 bytes) each probe
//   (with BEA_PORT_PROBES) ELD: END_LEVEL_DATA's bytes at the first Present and whenever they change
//   BEA_PORT_PROBE_OBJECTS=1  also BEO/BEW/BEJ: the whole battle engine (0x63c bytes) and its walker (+0x578, 0x4c bytes)
//                           and jet (+0x57c, 0x54 bytes) parts, to find the first field that differs from retail
//   BEA_PORT_RECORD=PATH    record a tape from live play: keys (no repeats) and the mouse are queued and delivered at
//                           the Present hook exactly as a tape's, and written to PATH in the tape format, so replaying
//                           PATH reproduces the run. Presents are paced to BEA_PORT_RECORD_FPS (30) real time, for play
//                           on the shim clock. Window mouse, text and focus messages are not delivered (a tape has none).
//   BEA_PORT_AUTOPILOT=CMDS   record mode only: from Present BEA_PORT_AUTOPILOT_START (3300) the input is made, not
//                           read: `wp:X,Y` walks the battle engine to within 1.5 of map point X,Y (mouse X turns it,
//                           W walks when it faces the point), `wait:N` waits N Presents, `fire:N` holds the left mouse
//                           button N Presents and releases it, `turn:DX,N` moves the mouse DX a Present for N Presents,
//                           `face:X,Y` turns to face map point X,Y, `look:DY,N` moves the mouse DY up/down a Present for N Presents, `key:VK,SCAN` (hex) presses a key for one Present,
//                           separated by ';'. The tape it writes is an ordinary tape.
// Timing as the retail shim's: frame F is the F-th Present; after it returns, frame F's probes are read and then frame F's
// events delivered, so the game sees them first while making Present F+1. Keys are WM_KEYDOWN/WM_KEYUP messages to the
// device window (retail reads the keyboard only through messages); mouse movement is summed and returned by the first
// DirectInput mouse GetDeviceState after the frame's events (later reads in that frame see none), buttons as a level.
// The probes read the game's objects at retail's offsets; the port's class layouts match them (gdb, ptype /o on CGame,
// CPlayer, CBattleEngine and CEventManager of the Linux build).
#include <windows.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "platform.h"
extern "C" void PortCrtRandState(unsigned int *calls, unsigned int *hold);	// crtrand.cpp

extern "C" char GAME[];
extern "C" char EVENT_MANAGER[];
extern "C" char END_LEVEL_DATA[];
extern "C" char FRONTEND[];			// CFrontEnd: mActivePage +0x1f8, transition from/to/count/time +0x1fc..+0x208, mCounter +0xbe18
extern "C" float gFrontEndFrameStep;	// FrontEnd.cpp: the filtered frame step
extern "C" bool gInFrontEnd;		// set while CFrontEnd::Run runs	// CEndLevelData, 0x5cc bytes in this build (nm -S)

namespace
{
struct Event
{
	unsigned frame;
	char kind;			// 'K' or 'M'
	bool down;
	unsigned vk, scan;
	int dx, dy, wheel, buttons;
};

Event *gEvents;
size_t gCount, gNext;
FILE *gProbes;
unsigned gEvery = 50, gMax;
bool gActive, gInit, gObjects;
long gMouseX, gMouseY, gMouseZ;
int gButtons;
FILE *gRecord;
Event gPending[256];
size_t gPendingCount;
float gRecFracX, gRecFracY;
long gRecWheel;
int gRecButtons;			// the last written button level (a tape starts with none down)
int gRecLive;			// live buttons (bit 0 left, 1 right, 2 middle)
unsigned long long gRecStart;
double gRecFps = 30;
const char *gPilot;		// the autopilot's next command
unsigned gPilotStart = 3300, gPilotUntil;
bool gPilotW;			// W held
int gPilotButtons;

}
static void EldAtExit();	// below: the end-level data at exit
namespace
{
void Load(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
	{
		fprintf(stderr, "bea-port: replay: cannot open tape %s\n", path);
		exit(3);
	}
	size_t cap = 0;
	char line[256];
	while (fgets(line, sizeof(line), f))
	{
		Event e = {};
		char dir[8] = "";
		if (line[0] == 'K' && sscanf(line, "K %u %7s %x %x", &e.frame, dir, &e.vk, &e.scan) == 4)
		{
			e.kind = 'K';
			e.down = strcmp(dir, "down") == 0;
		}
		else if (line[0] == 'M' && sscanf(line, "M %u %d %d %d %d", &e.frame, &e.dx, &e.dy, &e.wheel, &e.buttons) == 5)
			e.kind = 'M';
		else
			continue;
		if (gCount == cap)
			gEvents = (Event *)realloc(gEvents, (cap = cap ? cap * 2 : 4096) * sizeof(Event));
		gEvents[gCount++] = e;
	}
	fclose(f);
	fprintf(stderr, "bea-port: replay: %zu tape events from %s\n", gCount, path);
}

void Init()
{
	gInit = true;
	const char *tape = getenv("BEA_PORT_TAPE");
	const char *probes = getenv("BEA_PORT_PROBES");
	if (const char *every = getenv("BEA_PORT_PROBE_EVERY"))
		gEvery = (unsigned)atoi(every);
	if (const char *objects = getenv("BEA_PORT_PROBE_OBJECTS"))
		gObjects = *objects && *objects != '0';
	if (const char *max = getenv("BEA_PORT_REPLAY_MAX"))
		gMax = (unsigned)atoi(max);
	if (tape && *tape)
	{
		Load(tape);
		gActive = true;
	}
	if (const char *rec = getenv("BEA_PORT_RECORD"); rec && *rec)
	{
		if (gActive)
		{
			fprintf(stderr, "bea-port: replay: BEA_PORT_RECORD and BEA_PORT_TAPE are exclusive\n");
			exit(3);
		}
		if (!(gRecord = fopen(rec, "w")))
		{
			fprintf(stderr, "bea-port: replay: cannot write tape %s\n", rec);
			exit(3);
		}
		if (const char *fps = getenv("BEA_PORT_RECORD_FPS"))
			gRecFps = atof(fps) > 0 ? atof(fps) : 30;
		gActive = true;
		fprintf(stderr, "bea-port: replay: recording a tape to %s at %g Presents a second\n", rec, gRecFps);
		if (const char *pilot = getenv("BEA_PORT_AUTOPILOT"); pilot && *pilot)
		{
			gPilot = pilot;
			if (const char *st = getenv("BEA_PORT_AUTOPILOT_START"))
				gPilotStart = (unsigned)atoi(st);
		}
	}
	if (probes && *probes && !(gProbes = fopen(probes, "w")))
	{
		fprintf(stderr, "bea-port: replay: cannot write probes %s\n", probes);
		exit(3);
	}
	if (gProbes)
		atexit(EldAtExit);
}

void Hex(unsigned frame, const char *name, const void *p, size_t n)
{
	fprintf(gProbes, "P %u %s ", frame, name);
	for (size_t i = 0; i < n; i++)
		fprintf(gProbes, "%02x", ((const unsigned char *)p)[i]);
	fputc('\n', gProbes);
}

template <class T> T At(const char *base, size_t off)
{
	T v;
	memcpy(&v, base + off, sizeof(v));
	return v;
}

// REPORT-6 section 3. A missing player or battle engine gives zero bytes, as an unreadable pointer gave in the shim.
void Probe(unsigned frame)
{
	Hex(frame, "EVT", EVENT_MANAGER + 0x8, 4);
	Hex(frame, "EVN", EVENT_MANAGER + 0xc, 4);		// mTotalEventProcessedNum (EVN)
	if (gInFrontEnd)
	{
		// FE: the front end's time state: frame step, active page, transition from/to/count/time, mCounter
		unsigned char fe[28];
		memcpy(fe, &gFrontEndFrameStep, 4);
		memcpy(fe + 4, FRONTEND + 0x1f8, 20);
		memcpy(fe + 24, FRONTEND + 0xbe18, 4);
		Hex(frame, "FE", fe, sizeof(fe));
	}
	Hex(frame, "FT", GAME + 0x398, 4);
	Hex(frame, "SC", GAME + 0xf4, 4);
	Hex(frame, "LVL", GAME + 0x2a0, 4);
	const char *rng = At<const char *>(GAME, 0x304);
	char zero[12] = {};
	Hex(frame, "RNG", rng ? rng : zero, 8);
	{
		// CRT: the main thread's C runtime rand state (call count, holdrand), for effects and sim code that draw from
		// rand (particles, debris, segment destruction); retail's probe set has no counterpart.
		unsigned int crt[2];
		PortCrtRandState(&crt[0], &crt[1]);
		Hex(frame, "CRT", crt, 8);
	}
	const char *player = At<const char *>(GAME, 0x2a4);
	const char *be = player ? At<const char *>(player, 0x1c) : NULL;
	Hex(frame, "POS", be ? be + 0x1c : zero, 12);
	Hex(frame, "LIFE", be ? be + 0xf8 : zero, 8);
	if (gObjects && be)
	{
		Hex(frame, "BEO", be, 0x63c);
		if (const char *walker = At<const char *>(be, 0x578))
			Hex(frame, "BEW", walker, 0x4c);
		if (const char *jet = At<const char *>(be, 0x57c))
			Hex(frame, "BEJ", jet, 0x54);
	}
	fflush(gProbes);
}
}

// The autopilot (BEA_PORT_AUTOPILOT): this Present's input from the battle engine's position and heading.
static void PilotKey(bool down)
{
	if (gPilotW == down || gPendingCount >= sizeof(gPending) / sizeof(gPending[0]))
		return;
	gPilotW = down;
	Event e = {};
	e.kind = 'K';
	e.down = down;
	e.vk = 0x57;	// W
	e.scan = 0x11;
	gPending[gPendingCount++] = e;
}

static int gPilotDy;	// look: this Present's vertical mouse

static void Pilot(unsigned frame, int *dx, int *buttons)
{
	*dx = 0;
	gPilotDy = 0;
	*buttons = gPilotButtons;
	while (gPilot && *gPilot)
	{
		const char *player = At<const char *>(GAME, 0x2a4);
		const char *be = player ? At<const char *>(player, 0x1c) : NULL;
		float x = 0, y = 0, fx = 0, fy = 1;
		if (be)
		{
			x = At<float>(be, 0x1c);
			y = At<float>(be, 0x20);
			fx = At<float>(be, 0x40);	// forward = the orientation's second column (rows at 0x3c and 0x4c)
			fy = At<float>(be, 0x50);
		}
		float a = 0, b = 0;
		int n = 0;
		const char *next = strchr(gPilot, ';');
		if (sscanf(gPilot, "wp:%f,%f", &a, &b) == 2)
		{
			float tx = a - x, ty = b - y, d = sqrtf(tx * tx + ty * ty);
			if (d < 1.5f || !be)
			{
				fprintf(stderr, "bea-port: autopilot: P%u reached %.1f,%.1f (at %.2f,%.2f)\n", frame, a, b, x, y);
				PilotKey(false);
				gPilot = next ? next + 1 : NULL;
				continue;
			}
			float err = atan2f(fx * ty - fy * tx, fx * tx + fy * ty) * 57.29578f;	// + = the point is to the left
			int turn = (int)(-err * 0.15f);
			*dx = turn > 6 ? 6 : turn < -6 ? -6 : turn;
			PilotKey(fabsf(err) < 25.f);
			return;
		}
		unsigned kv = 0, ks = 0;
		if (sscanf(gPilot, "key:%x,%x", &kv, &ks) == 2)
		{
			// a key press: down this Present, up the next
			PilotKey(false);
			if (gPendingCount + 1 < sizeof(gPending) / sizeof(gPending[0]))
			{
				Event e = {};
				e.kind = 'K';
				e.down = !gPilotUntil;
				e.vk = kv;
				e.scan = ks;
				gPending[gPendingCount++] = e;
			}
			if (!gPilotUntil)
			{
				gPilotUntil = frame + 1;
				return;
			}
			gPilotUntil = 0;
			gPilot = next ? next + 1 : NULL;
			return;
		}
		if (sscanf(gPilot, "face:%f,%f", &a, &b) == 2)
		{
			float tx = a - x, ty = b - y;
			float err = atan2f(fx * ty - fy * tx, fx * tx + fy * ty) * 57.29578f;
			PilotKey(false);
			if (!gPilotUntil)
				gPilotUntil = frame;
			// turn until it faces the point and has settled (the turn eases in over ~20 Presents)
			if (fabsf(err) > 1.5f || frame < gPilotUntil + 30)
			{
				if (fabsf(err) > 1.5f)
					gPilotUntil = frame;
				int turn = (int)(-err * 0.15f);
				if (turn == 0 && fabsf(err) > 1.5f)
					turn = err > 0 ? -1 : 1;	// the last degrees: one count a Present
				*dx = turn > 6 ? 6 : turn < -6 ? -6 : turn;
				return;
			}
			fprintf(stderr, "bea-port: autopilot: P%u faces %.1f,%.1f (error %.2f deg)\n", frame, a, b, err);
			gPilotUntil = 0;
			gPilot = next ? next + 1 : NULL;
			continue;
		}
		if (sscanf(gPilot, "wait:%d", &n) == 1 || sscanf(gPilot, "fire:%d", &n) == 1 || sscanf(gPilot, "turn:%f,%d", &a, &n) == 2 ||
			sscanf(gPilot, "look:%f,%d", &a, &n) == 2)
		{
			PilotKey(false);
			if (!gPilotUntil)
				gPilotUntil = frame + (unsigned)n;
			bool fire = gPilot[0] == 'f', turn = gPilot[0] == 't';
			if (frame < gPilotUntil)
			{
				*buttons = gPilotButtons = fire ? 1 : 0;
				*dx = turn ? (int)a : 0;
				gPilotDy = gPilot[0] == 'l' ? (int)a : 0;
				return;
			}
			gPilotUntil = 0;
			*buttons = gPilotButtons = 0;	// a burst ends with the release (the pulse cannon fires on it)
			gPilot = next ? next + 1 : NULL;
			return;
		}
		fprintf(stderr, "bea-port: autopilot: cannot read '%s'\n", gPilot);
		gPilot = NULL;
	}
	PilotKey(false);
}

// Record mode: the frame's live input becomes tape events (written, then delivered by the loop below as a tape's).
static void RecordFrame(unsigned frame)
{
	// pace: Present F completes no earlier than F / fps seconds after the first
	unsigned long long now = SDL_GetTicksNS();
	if (!gRecStart)
		gRecStart = now - (unsigned long long)(frame * 1e9 / gRecFps);
	unsigned long long due = gRecStart + (unsigned long long)(frame * 1e9 / gRecFps);
	if (due > now)
		SDL_DelayNS(due - now);
	for (size_t i = 0; i < gPendingCount; i++)
	{
		Event &e = gPending[i];
		e.frame = frame;
		fprintf(gRecord, "K %u %s %02x %02x vk%02x\n", frame, e.down ? "down" : "up", e.vk, e.scan, e.vk);
	}
	// the mouse: motion events' relative movement and button events since the last Present (window.cpp)
	int dx = (int)gRecFracX, dy = (int)gRecFracY;
	gRecFracX -= dx;
	gRecFracY -= dy;
	int buttons = gRecLive;
	if (gPilot && frame >= gPilotStart)
	{
		size_t before = gPendingCount;
		Pilot(frame, &dx, &buttons);
		dy = gPilotDy;
		for (size_t i = before; i < gPendingCount; i++)	// the autopilot's keys, made after the loop above wrote the live ones
			fprintf(gRecord, "K %u %s %02x %02x vk%02x\n", frame, gPending[i].down ? "down" : "up", gPending[i].vk, gPending[i].scan, gPending[i].vk);
	}
	int wheel = (int)gRecWheel;
	gRecWheel = 0;
	if (dx || dy || wheel || buttons != gRecButtons)
	{
		fprintf(gRecord, "M %u %d %d %d %d\n", frame, dx, dy, wheel, buttons);
		gMouseX += dx;
		gMouseY += dy;
		gMouseZ += wheel;
		gButtons = buttons;
		gRecButtons = buttons;
	}
	fflush(gRecord);
}

// ELD: the end-level data (what RunLevel's close fills for the debriefing and the career), whenever it changes; at exit
// too (labelled with the last Present), since a level quit from -level ends the process before another Present.
static unsigned char gEldLast[0x5cc];
static bool gEldSeen;
static unsigned gEldFrame;

static void EldProbe(unsigned frame)
{
	gEldFrame = frame;
	if (!gEldSeen || memcmp(gEldLast, END_LEVEL_DATA, sizeof(gEldLast)))
	{
		memcpy(gEldLast, END_LEVEL_DATA, sizeof(gEldLast));
		gEldSeen = true;
		Hex(frame, "ELD", gEldLast, sizeof(gEldLast));
		fflush(gProbes);
	}
}

static void EldAtExit()
{
	if (gProbes && gEldSeen && memcmp(gEldLast, END_LEVEL_DATA, sizeof(gEldLast)))
	{
		Hex(gEldFrame, "ELDEXIT", END_LEVEL_DATA, sizeof(gEldLast));
		fflush(gProbes);
	}
}

void PortReplayFrame(unsigned frame, HWND window)
{
	if (!gInit)
		Init();
	static long sleepUs = getenv("BEA_PORT_REPLAY_SLEEP_US") ? atol(getenv("BEA_PORT_REPLAY_SLEEP_US")) : 0;
	if (sleepUs > 0)
		SDL_DelayNS((Uint64)sleepUs * 1000);	// a test switch: the main thread sleeps at each Present, as record mode's pacing does
	if (gRecord)
	{
		RecordFrame(frame);
		for (size_t i = 0; i < gPendingCount; i++)
		{
			const Event &e = gPending[i];
			LPARAM l = 1 | ((LPARAM)(e.scan & 0x1ff) << 16);	// as retail's shim: scan 0x1xx sets the extended flag (bit 24)
			if (!e.down)
				l |= (LPARAM)0xC0000000;
			PortPostMessageA(window, e.down ? WM_KEYDOWN : WM_KEYUP, e.vk, l);
		}
		gPendingCount = 0;
	}
	if (gProbes && gEvery && frame % gEvery == 0 && (!gMax || frame <= gMax))
		Probe(frame);
	if (gProbes)
		EldProbe(frame);
	for (; gNext < gCount && gEvents[gNext].frame <= frame; gNext++)
	{
		const Event &e = gEvents[gNext];
		if (e.frame < frame)
			continue;	// before the first Present the hook saw: not deliverable
		if (e.kind == 'K')
		{
			LPARAM l = 1 | ((LPARAM)(e.scan & 0x1ff) << 16);	// as retail's shim: scan 0x1xx sets the extended flag (bit 24)
			if (!e.down)
				l |= (LPARAM)0xC0000000;
			PortPostMessageA(window, e.down ? WM_KEYDOWN : WM_KEYUP, e.vk, l);
		}
		else
		{
			gMouseX += e.dx;
			gMouseY += e.dy;
			gMouseZ += e.wheel;
			gButtons = e.buttons;
		}
	}
	if (gMax && frame == gMax)
	{
		fprintf(stderr, "bea-port: replay: frame %u reached, WM_CLOSE posted\n", frame);
		PortPostMessageA(window, WM_CLOSE, 0, 0);
	}
}

bool PortReplayMouse(long *dx, long *dy, long *dz, int *buttons)
{
	if (!gInit)
		Init();
	if (!gActive)
		return false;
	*dx = gMouseX;
	*dy = gMouseY;
	*dz = gMouseZ;
	*buttons = gButtons;
	gMouseX = gMouseY = gMouseZ = 0;
	return true;
}

bool PortRecording()
{
	if (!gInit)
		Init();
	return gRecord != NULL;
}

// Record mode's live input (window.cpp): a key change or wheel step, held until the next Present.
void PortRecordKey(bool down, unsigned vk, unsigned scan)
{
	if (gPendingCount < sizeof(gPending) / sizeof(gPending[0]))
	{
		Event e = {};
		e.kind = 'K';
		e.down = down;
		e.vk = vk;
		e.scan = scan;
		gPending[gPendingCount++] = e;
	}
}

void PortRecordWheel(float steps) { gRecWheel += (long)(steps * WHEEL_DELTA); }

void PortRecordMotion(float dx, float dy)
{
	gRecFracX += dx;
	gRecFracY += dy;
}

void PortRecordButton(int bit, bool down)
{
	if (down)
		gRecLive |= 1 << bit;
	else
		gRecLive &= ~(1 << bit);
}
