// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/replay.cpp: the input tapes and probe readout (REPORT-6), so a port run compares with the
// retail goldens (golden-640/levelN.retail.probes) Present by Present. Off unless BEA_PORT_TAPE or BEA_PORT_PROBES is set.
//   BEA_PORT_TAPE=PATH      the tape: `K <frame> <down|up> <vk hex> <scan hex> <name>`, `M <frame> <dx> <dy> <wheel> <buttons>`,
//                           and (tape 2, `# onslaught tape 2`) the window mouse, in the retail replay harness's format:
//                           `W_CLIENT <w> <h>` (the game's back buffer, the size the front end divides the window mouse
//                           by, when the W lines after it were made) and
//                           `W <frame> <move|ldown|lup|rdown|rup|mdown|mup|wheel> <x> <y> <wparam hex>`, posted as the window
//                           messages with the recorded lParam unchanged (x, y; wheel: wparam's high word is the delta); a
//                           replay whose back buffer is not W_CLIENT's stops with a message (replay it with the recorded
//                           -res). Typed text: `C <frame> <char hex> <scan hex>`, a WM_CHAR after its key's K line, as
//                           the live port makes one from a key-down (port only: retail's harness has no C lines). Other
//                           lines are counted and ignored.
//   BEA_PORT_PROBES=PATH    probe lines `P <frame> <NAME> <hex>`, every BEA_PORT_PROBE_EVERY (50) Presents
//   BEA_PORT_REPLAY_MAX=N   after frame N's probes, post WM_CLOSE (the game's own shutdown)
//   (with BEA_PORT_PROBES) FE: in the front end, its frame step, page, transition and counter (28 bytes) each probe
//   (with BEA_PORT_PROBES) ELD: END_LEVEL_DATA's bytes at the first Present and whenever they change
//   BEA_PORT_PROBE_OBJECTS=1  also BEO/BEW/BEJ: the whole battle engine (0x63c bytes) and its walker (+0x578, 0x4c bytes)
//                           and jet (+0x57c, 0x54 bytes) parts, to find the first field that differs from retail
//   BEA_PORT_RECORD=PATH    record a tape from live play: keys (no repeats) and the mouse are queued and delivered at
//                           the Present hook exactly as a tape's, and written to PATH in the tape format, so replaying
//                           PATH reproduces the run. Presents are paced to BEA_PORT_RECORD_FPS (30) real time, for play
//                           on the shim clock. While the game holds the DirectInput mouse (play) the mouse is written as
//                           M lines; otherwise (menus, pause menu) as W lines, the window messages the front end reads,
//                           each button preceded by a move to its position. A key-down's character (the live port's
//                           WM_CHAR, window.cpp KeyChar) is a C line, so a career can be named; focus messages are not
//                           delivered (a tape has none). Input is written to an exclusive sibling .incomplete stage;
//                           only successful normal finalization publishes PATH, without replacing an existing file.
//                           Queue or I/O failure leaves the stage for diagnosis and keeps paced runtime input active.
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
// W lines are posted in tape order with the K lines. In a tape-2 replay or a recording, GetCursorPos answers the last W
// position and the window sits at the screen's origin, so the game's cursor never depends on the desktop.
// The probes read the game's objects at retail's offsets; the port's class layouts match them (gdb, ptype /o on CGame,
// CPlayer, CBattleEngine and CEventManager of the Linux build).
#include <windows.h>

#include <math.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <SDL3/SDL.h>

#include "platform.h"
extern "C" void PortCrtRandState(unsigned int *calls, unsigned int *hold);	// crtrand.cpp
extern "C" int PortBackBuffer(unsigned *w, unsigned *h);	// d3d9hooks.cpp: the game's back buffer (0 before the device)
extern "C" BOOL WINAPI PortTryPostMessageA(HWND, UINT, WPARAM, LPARAM);	// window.cpp: bounded recorder delivery

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
	bool prepared;		// recording delivery deferred by a full window queue; do not rewrite its ownership/coordinates
	char kind;			// 'K', 'M', 'W' or 'C' (0: dropped); C: vk is the character, scan its key's
	bool down;
	unsigned vk, scan;
	int dx, dy, wheel, buttons;
	UINT msg;			// W: the window message, its lParam's point and wParam, and the client size it was made in
	int x, y;
	unsigned wparam;
	unsigned cw, ch;	// W: the W_CLIENT before it (0: none)
};

const struct
{
	const char *name;
	UINT msg;
} kWin[] = {{"move", WM_MOUSEMOVE}, {"ldown", WM_LBUTTONDOWN}, {"lup", WM_LBUTTONUP}, {"rdown", WM_RBUTTONDOWN},
			{"rup", WM_RBUTTONUP}, {"mdown", WM_MBUTTONDOWN}, {"mup", WM_MBUTTONUP}, {"wheel", WM_MOUSEWHEEL}};

const char *WinName(UINT msg)
{
	for (const auto &k : kWin)
		if (k.msg == msg)
			return k.name;
	return NULL;
}

Event *gEvents;
size_t gCount, gNext;
FILE *gProbes;
unsigned gEvery = 50, gMax;
bool gActive, gInit, gObjects;
long gMouseX, gMouseY, gMouseZ;
int gButtons;
FILE *gRecord;
bool gRecording, gRecordFailed, gRecordFinalized;
const char *gRecordDest;
char *gRecordStage;
bool gRecordStageOwned;
Event gPending[1024];		// record mode: keys and window-mouse events in SDL's order, until the next Present
size_t gPendingCount;
unsigned gRecMk;			// record mode: the window mouse's button level (MK_ bits), the recorder's own
unsigned gRecCw, gRecCh;	// the last W_CLIENT written
bool gTapeCursor;			// a tape-2 replay or a recording: GetCursorPos answers gCurX, gCurY
int gCurX, gCurY;			// the last W position delivered
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
// Failure disables publication, never the record-mode input path or its pacing. A failed stage stays non-tape.
void RecordFailure(const char *why)
{
	if (gRecordFailed)
		return;
	gRecordFailed = true;
	fprintf(stderr, PORT_LOG_PREFIX "recording failed: %s; no final tape will be published%s%s\n", why,
		gRecordStageOwned ? "; incomplete stage: " : "", gRecordStageOwned ? gRecordStage : "");
}

void RecordWrite(const char *format, ...)
{
	if (gRecordFailed || !gRecord)
		return;
	va_list args;
	va_start(args, format);
	int result = vfprintf(gRecord, format, args);
	va_end(args);
	if (result < 0 || ferror(gRecord))
		RecordFailure("writing the input stream failed");
}

void RecordFlush()
{
	if (gRecord && !gRecordFailed && (fflush(gRecord) != 0 || ferror(gRecord)))
		RecordFailure("flushing the input stream failed");
}

bool RecordSpace(size_t slots)
{
	const size_t capacity = sizeof(gPending) / sizeof(gPending[0]);
	if (gPendingCount <= capacity && slots <= capacity - gPendingCount)
		return true;
	RecordFailure("input queue capacity exceeded");
	return false;
}

void FinalizeRecord()
{
	if (!gRecording || gRecordFinalized)
		return;
	gRecordFinalized = true;
	RecordFlush();
	if (gRecord)
	{
		FILE *stream = gRecord;
		gRecord = NULL;
		if (fclose(stream) != 0)
			RecordFailure("closing the input stream failed");
	}
	if (gRecordFailed)
		return;
	// Both operations are one no-replace rename in the stage's own directory. An older destination always wins.
#if defined(_WIN32)
	if (!MoveFileA(gRecordStage, gRecordDest))
#else
	if (renameat2(AT_FDCWD, gRecordStage, AT_FDCWD, gRecordDest, RENAME_NOREPLACE) != 0)
#endif
		RecordFailure("publishing the final tape failed (an existing destination is never replaced)");
	else
		fprintf(stderr, PORT_LOG_PREFIX "recording finalized: final tape published %s\n", gRecordDest);
}

void OpenRecord(const char *path)
{
	gRecording = true;
	if (atexit(FinalizeRecord) != 0)
		RecordFailure("cannot register recording finalization");
	size_t length = strlen(path);
	char *names = NULL;
	if (length > ((size_t)-1 - 81) / 2 || !(names = (char *)malloc(2 * length + 81)))
	{
		RecordFailure("cannot allocate the recording stage name");
		return;
	}
	memcpy(names, path, length + 1);
	gRecordDest = names;	// own the original destination even if another subsystem changes the environment later
	gRecordStage = names + length + 1;
#if defined(_WIN32)
	unsigned long pid = (unsigned long)_getpid();
#else
	unsigned long pid = (unsigned long)getpid();
#endif
	for (unsigned attempt = 0; attempt < 256; attempt++)
	{
		snprintf(gRecordStage, length + 80, "%s.recording-%lu-%u.incomplete", gRecordDest, pid, attempt);
		gRecord = fopen(gRecordStage, "wx");	// exclusive on both CRTs; retain the original text-mode tape bytes
		if (gRecord)
		{
			gRecordStageOwned = true;
#if !defined(_WIN32)
			if (fchmod(fileno(gRecord), S_IRUSR | S_IWUSR) != 0)
				RecordFailure("cannot make the recording stage private");
#endif
			RecordWrite("# onslaught tape 2\n");
			return;
		}
		if (errno != EEXIST)
			break;
	}
	RecordFailure("cannot create an exclusive recording stage");
}

void Load(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
	{
		fprintf(stderr, PORT_LOG_PREFIX "replay: cannot open tape %s\n", path);
		exit(3);
	}
	size_t cap = 0, unknown = 0;
	unsigned version = 1, cw = 0, ch = 0;
	char line[256];
	while (fgets(line, sizeof(line), f))
	{
		Event e = {};
		char dir[8] = "";
		char kind[16] = "";
		if (line[0] == '#')
		{
			sscanf(line, "# onslaught tape %u", &version);
			continue;
		}
		if (line[0] == '\n' || line[0] == '\r' || line[0] == 0)
			continue;
		if (strncmp(line, "W_CLIENT ", 9) == 0)
		{
			if (sscanf(line, "W_CLIENT %u %u", &cw, &ch) != 2)
				unknown++;
			continue;
		}
		if (line[0] == 'W' && sscanf(line, "W %u %15s %d %d %x", &e.frame, kind, &e.x, &e.y, &e.wparam) == 5)
		{
			for (const auto &k : kWin)
				if (strcmp(k.name, kind) == 0)
					e.msg = k.msg;
			if (!e.msg)
			{
				unknown++;
				continue;
			}
			e.kind = 'W';
			e.cw = cw;
			e.ch = ch;
			gTapeCursor = true;
		}
		else if (line[0] == 'C' && sscanf(line, "C %u %x %x", &e.frame, &e.vk, &e.scan) == 3)
			e.kind = 'C';
		else if (line[0] == 'K' && sscanf(line, "K %u %7s %x %x", &e.frame, dir, &e.vk, &e.scan) == 4)
		{
			e.kind = 'K';
			e.down = strcmp(dir, "down") == 0;
		}
		else if (line[0] == 'M' && sscanf(line, "M %u %d %d %d %d", &e.frame, &e.dx, &e.dy, &e.wheel, &e.buttons) == 5)
			e.kind = 'M';
		else
		{
			unknown++;
			continue;
		}
		if (gCount == cap)
			gEvents = (Event *)realloc(gEvents, (cap = cap ? cap * 2 : 4096) * sizeof(Event));
		gEvents[gCount++] = e;
	}
	fclose(f);
	if (version >= 2)
		gTapeCursor = true;
	fprintf(stderr, PORT_LOG_PREFIX "replay: %zu tape events from %s (tape %u)\n", gCount, path, version);
	if (unknown)
		fprintf(stderr, PORT_LOG_PREFIX "replay: %zu tape lines not understood, ignored\n", unknown);
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
			fprintf(stderr, PORT_LOG_PREFIX "replay: BEA_PORT_RECORD and BEA_PORT_TAPE are exclusive\n");
			exit(3);
		}
		OpenRecord(rec);
		gTapeCursor = true;
		if (const char *fps = getenv("BEA_PORT_RECORD_FPS"))
			gRecFps = atof(fps) > 0 ? atof(fps) : 30;
		gActive = true;
		if (!gRecordFailed)
			fprintf(stderr, PORT_LOG_PREFIX "replay: recording to an incomplete stage for %s at %g Presents a second; "
				"the final tape is published only on successful exit\n", rec, gRecFps);
		if (const char *pilot = getenv("BEA_PORT_AUTOPILOT"); pilot && *pilot)
		{
			gPilot = pilot;
			if (const char *st = getenv("BEA_PORT_AUTOPILOT_START"))
				gPilotStart = (unsigned)atoi(st);
		}
	}
	if (probes && *probes && !(gProbes = fopen(probes, "w")))
	{
		if (gRecording) RecordFailure("probe output initialization failed");
		fprintf(stderr, PORT_LOG_PREFIX "replay: cannot write probes %s\n", probes);
		exit(3);
	}
	if (gProbes)
		if (atexit(EldAtExit) != 0 && gRecording)
			RecordFailure("cannot register probe finalization");
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
static bool PilotKey(bool down)
{
	if (gPilotW == down)
		return true;
	if (!RecordSpace(1))
		return false;
	gPilotW = down;
	Event e = {};
	e.kind = 'K';
	e.down = down;
	e.vk = 0x57;	// W
	e.scan = 0x11;
	gPending[gPendingCount++] = e;
	return true;
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
				fprintf(stderr, PORT_LOG_PREFIX "autopilot: P%u reached %.1f,%.1f (at %.2f,%.2f)\n", frame, a, b, x, y);
				if (!PilotKey(false)) return;
				gPilot = next ? next + 1 : NULL;
				continue;
			}
			float err = atan2f(fx * ty - fy * tx, fx * tx + fy * ty) * 57.29578f;	// + = the point is to the left
			int turn = (int)(-err * 0.15f);
			*dx = turn > 6 ? 6 : turn < -6 ? -6 : turn;
			if (!PilotKey(fabsf(err) < 25.f)) return;
			return;
		}
		unsigned kv = 0, ks = 0;
		if (sscanf(gPilot, "key:%x,%x", &kv, &ks) == 2)
		{
			// a key press: down this Present, up the next
			if (!PilotKey(false)) return;
			if (RecordSpace(1))
			{
				Event e = {};
				e.kind = 'K';
				e.down = !gPilotUntil;
				e.vk = kv;
				e.scan = ks;
				gPending[gPendingCount++] = e;
			}
			else return;
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
			if (!PilotKey(false)) return;
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
			fprintf(stderr, PORT_LOG_PREFIX "autopilot: P%u faces %.1f,%.1f (error %.2f deg)\n", frame, a, b, err);
			gPilotUntil = 0;
			gPilot = next ? next + 1 : NULL;
			continue;
		}
		if (sscanf(gPilot, "wait:%d", &n) == 1 || sscanf(gPilot, "fire:%d", &n) == 1 || sscanf(gPilot, "turn:%f,%d", &a, &n) == 2 ||
			sscanf(gPilot, "look:%f,%d", &a, &n) == 2)
		{
			if (!PilotKey(false)) return;
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
		fprintf(stderr, PORT_LOG_PREFIX "autopilot: cannot read '%s'\n", gPilot);
		gPilot = NULL;
	}
	PilotKey(false);
}

// The size the front end divides the window mouse by: the game's back buffer (0 x 0 before the device exists).
static void MouseSpace(unsigned *w, unsigned *h)
{
	if (!PortBackBuffer(w, h))
		*w = *h = 0;
}

// One tape event to the game: a key or window-mouse message, posted (never merged with another) as retail's shim does.
static bool Deliver(HWND window, const Event &e, bool recording = false)
{
	if (e.kind == 'K')
	{
		LPARAM l = 1 | ((LPARAM)(e.scan & 0x1ff) << 16);	// as retail's shim: scan 0x1xx sets the extended flag (bit 24)
		if (!e.down)
			l |= (LPARAM)0xC0000000;
		return recording ? PortTryPostMessageA(window, e.down ? WM_KEYDOWN : WM_KEYUP, e.vk, l) != FALSE
			: PortPostMessageA(window, e.down ? WM_KEYDOWN : WM_KEYUP, e.vk, l) != FALSE;
	}
	if (e.kind == 'C')
	{
		// as window.cpp posts a key-down's character: the key-down's lParam
		LPARAM l = 1 | ((LPARAM)(e.scan & 0x1ff) << 16);
		return recording ? PortTryPostMessageA(window, WM_CHAR, e.vk, l) != FALSE
			: PortPostMessageA(window, WM_CHAR, e.vk, l) != FALSE;
	}
	if (e.kind != 'W')
		return true;
	// The recorded lParam, unchanged: the front end reads x * 640 / back buffer width (and the pause menu raw pixels),
	// so the point means the same only at the recorded back buffer. A replay at another size would put the clicks
	// elsewhere and go its own way, so it stops instead (the session's .json pins the recorded -res in game_args).
	unsigned w = 0, h = 0;
	MouseSpace(&w, &h);
	if (e.cw && e.ch && (e.cw != w || e.ch != h))
	{
		if (recording)
			RecordFailure("pending window input changed client size");	// retain the live edge; a failed capture is not replay
		else
		{
			fprintf(stderr, PORT_LOG_PREFIX "replay: this tape's window mouse was recorded with the game drawing at %ux%u, "
					"but it draws at %ux%u now; replay it at the recorded size (-res %u %u, as the session's .json game_args "
					"say). Replay stopped.\n", e.cw, e.ch, w, h, e.cw, e.ch);
			fflush(NULL);
			_Exit(3);	// not exit(): dxvk's threads are running (main.cpp: its statics must not be destroyed under them)
		}
	}
	bool posted = recording ? PortTryPostMessageA(window, e.msg, (WPARAM)e.wparam, MAKELPARAM(e.x, e.y)) != FALSE
		: PortPostMessageA(window, e.msg, (WPARAM)e.wparam, MAKELPARAM(e.x, e.y)) != FALSE;
	if (posted && e.msg != WM_MOUSEWHEEL)
	{
		gCurX = e.x;
		gCurY = e.y;
	}
	return posted;
}

// Record mode: the frame's live input becomes tape events (written, then delivered by the loop below as a tape's).
static void RecordFrame(unsigned frame)
{
	// pace: Present F completes no earlier than F / fps seconds after the first. A Present more than a few frames late
	// (a level load, a film opening) starts the schedule again from itself: otherwise the Presents after a stall run
	// unpaced until they catch up with it, and films and the game play fast for that long. Only wall time changes; the
	// tape's lines are written by Present, so the same input gives the same tape.
	unsigned long long now = SDL_GetTicksNS();
	unsigned long long period = (unsigned long long)(1e9 / gRecFps);
	if (!gRecStart)
		gRecStart = now - (unsigned long long)(frame * 1e9 / gRecFps);
	unsigned long long due = gRecStart + (unsigned long long)(frame * 1e9 / gRecFps);
	if (now > due + 4 * period)
	{
		gRecStart = now - (unsigned long long)(frame * 1e9 / gRecFps);
		due = now;
	}
	if (due > now)
		SDL_DelayNS(due - now);
	// The window mouse reaches the game only while DirectInput does not hold the mouse (exclusive DirectInput on Windows
	// takes the window's mouse messages); while it does, the movement goes to M lines below and the window events are
	// dropped. Decided here, at the Present, from the state the game's next frame reads.
	bool held = PortDinputMouseAcquired();
	for (size_t i = 0; i < gPendingCount; i++)
	{
		Event &e = gPending[i];
		if (e.prepared) continue;
		e.prepared = true;
		e.frame = frame;
		if (e.kind == 'K')
			RecordWrite("K %u %s %02x %02x vk%02x\n", frame, e.down ? "down" : "up", e.vk, e.scan, e.vk);
		else if (e.kind == 'C')
			RecordWrite("C %u %02x %02x\n", frame, e.vk, e.scan);
		else if (e.kind == 'W')
		{
			if (held)
			{
				e.kind = 0;
				continue;
			}
			unsigned w = 0, h = 0;
			MouseSpace(&w, &h);
			if (w != gRecCw || h != gRecCh)
			{
				RecordWrite("W_CLIENT %u %u\n", w, h);
				gRecCw = w;
				gRecCh = h;
			}
			e.cw = w;
			e.ch = h;
			RecordWrite("W %u %s %d %d %x\n", frame, WinName(e.msg), e.x, e.y, e.wparam);
		}
	}
	if (!held)
	{
		// the menus' movement is in the W lines: none of it piles up for the first in-game GetDeviceState
		gRecFracX = gRecFracY = 0;
		gRecWheel = 0;
	}
	// the mouse: motion events' relative movement and button events since the last Present (window.cpp)
	int dx = (int)gRecFracX, dy = (int)gRecFracY;
	gRecFracX -= dx;
	gRecFracY -= dy;
	int buttons = gRecLive;
	if ((gPilot || gPilotW) && frame >= gPilotStart)
	{
		size_t before = gPendingCount;
		Pilot(frame, &dx, &buttons);
		dy = gPilotDy;
		for (size_t i = before; i < gPendingCount; i++) // the autopilot's keys, made after the live-input loop
		{
			gPending[i].frame = frame;
			gPending[i].prepared = true;
			RecordWrite("K %u %s %02x %02x vk%02x\n", frame, gPending[i].down ? "down" : "up", gPending[i].vk, gPending[i].scan, gPending[i].vk);
		}
	}
	int wheel = (int)gRecWheel;
	gRecWheel = 0;
	if (held && (dx || dy || wheel || buttons != gRecButtons))
	{
		RecordWrite("M %u %d %d %d %d\n", frame, dx, dy, wheel, buttons);
		gMouseX += dx;
		gMouseY += dy;
		gMouseZ += wheel;
		gButtons = buttons;
		gRecButtons = buttons;
	}
	RecordFlush();
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
	static bool done;	// once: from PortReplayAtExit (Linux's main) or from exit()'s handlers
	if (done)
		return;
	done = true;
	if (gProbes && gEldSeen && memcmp(gEldLast, END_LEVEL_DATA, sizeof(gEldLast)))
	{
		Hex(gEldFrame, "ELDEXIT", END_LEVEL_DATA, sizeof(gEldLast));
		fflush(gProbes);
	}
}

void PortReplayAtExit()
{
	EldAtExit();
	FinalizeRecord();
}

void PortReplayFrame(unsigned frame, HWND window)
{
	if (!gInit)
		Init();
	static long sleepUs = getenv("BEA_PORT_REPLAY_SLEEP_US") ? atol(getenv("BEA_PORT_REPLAY_SLEEP_US")) : 0;
	if (sleepUs > 0)
		SDL_DelayNS((Uint64)sleepUs * 1000);	// a test switch: the main thread sleeps at each Present, as record mode's pacing does
	if (gRecording)
	{
		RecordFrame(frame);
		size_t posted = 0;
		for (; posted < gPendingCount; posted++)
			if (!Deliver(window, gPending[posted], true))
			{
				RecordFailure("window input queue capacity exceeded");
				break;
			}
		if (posted)
		{
			memmove(gPending, gPending + posted, (gPendingCount - posted) * sizeof(gPending[0]));
			gPendingCount -= posted;
		}
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
		if (e.kind != 'M')
			Deliver(window, e);
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
		fprintf(stderr, PORT_LOG_PREFIX "replay: frame %u reached, WM_CLOSE posted\n", frame);
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
	return gRecording;
}

// Record mode's live input (window.cpp): a key change or wheel step, held until the next Present.
void PortRecordKey(bool down, unsigned vk, unsigned scan)
{
	if (RecordSpace(1))
	{
		Event e = {};
		e.kind = 'K';
		e.down = down;
		e.vk = vk;
		e.scan = scan;
		gPending[gPendingCount++] = e;
	}
}

// Record mode's typed text (window.cpp): the character a key-down makes (KeyChar), queued right after the key's K event.
void PortRecordChar(unsigned c, unsigned scan)
{
	if (RecordSpace(1))
	{
		Event e = {};
		e.kind = 'C';
		e.vk = c;
		e.scan = scan;
		gPending[gPendingCount++] = e;
	}
}

// Record mode's window mouse (window.cpp): msg is WM_MOUSEMOVE, a button message or WM_MOUSEWHEEL (delta in wheel
// units), l the lParam the live port would post. Moves merge into a move just before them; each button gets a move to
// its position first, so the cursor is where the click is even when no move came between.
void PortRecordWindowMouse(UINT msg, LPARAM l, int wheelDelta)
{
	int x = LOWORD(l), y = HIWORD(l);
	auto move = [&]() -> bool
	{
		if (gPendingCount && !gPending[gPendingCount - 1].prepared && gPending[gPendingCount - 1].kind == 'W' &&
			gPending[gPendingCount - 1].msg == WM_MOUSEMOVE)
		{
			gPending[gPendingCount - 1].x = x;
			gPending[gPendingCount - 1].y = y;
			return true;
		}
		if (!RecordSpace(1))
			return false;
		Event e = {};
		e.kind = 'W';
		e.msg = WM_MOUSEMOVE;
		e.x = x;
		e.y = y;
		e.wparam = gRecMk;
		gPending[gPendingCount++] = e;
		return true;
	};
	if (msg == WM_MOUSEMOVE)
	{
		move();
		return;
	}
	if (msg == WM_MOUSEWHEEL && (short)wheelDelta == 0)
		return;
	bool coalesces = gPendingCount && !gPending[gPendingCount - 1].prepared && gPending[gPendingCount - 1].kind == 'W' &&
		gPending[gPendingCount - 1].msg == WM_MOUSEMOVE;
	if (!RecordSpace(msg == WM_MOUSEWHEEL || coalesces ? 1 : 2)) return;
	if (msg != WM_MOUSEWHEEL)
		move();
	switch (msg)
	{
	case WM_LBUTTONDOWN: gRecMk |= MK_LBUTTON; break;
	case WM_LBUTTONUP: gRecMk &= ~MK_LBUTTON; break;
	case WM_RBUTTONDOWN: gRecMk |= MK_RBUTTON; break;
	case WM_RBUTTONUP: gRecMk &= ~MK_RBUTTON; break;
	case WM_MBUTTONDOWN: gRecMk |= MK_MBUTTON; break;
	case WM_MBUTTONUP: gRecMk &= ~MK_MBUTTON; break;
	}
	Event e = {};
	e.kind = 'W';
	e.msg = msg;
	e.x = x;
	e.y = y;
	e.wparam = msg == WM_MOUSEWHEEL ? (unsigned)MAKEWPARAM(gRecMk, (short)wheelDelta) : gRecMk;
	gPending[gPendingCount++] = e;
}

// A tape-2 replay or a recording: the cursor is the last W position, in client pixels (the window at the screen's origin).
bool PortTapeCursor(POINT *p)
{
	if (!gInit)
		Init();
	if (!gTapeCursor)
		return false;
	if (p)
	{
		p->x = gCurX;
		p->y = gCurY;
	}
	return true;
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

// window.cpp uses this before consuming a deferred SDL event or clearing a synthetic held-key lifetime.
bool PortRecordInputSpace(unsigned slots) { return RecordSpace(slots); }
