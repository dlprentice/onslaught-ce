// SPDX-License-Identifier: GPL-3.0-only
// Runs the Dodge Button's actual code (port/game/ce_dodge_assist_rules.h and ce_dodge_assist.cpp) beside the game's own: the
// actual CController::DoMappings as patch 0220 leaves it (or as it stands without the patch, for the comparisons), the actual
// StrafeLeft and StrafeRight of the walker, the actual GetMappedInputValue and the CE gamepad provider (ce_gamepad.cpp), against
// synthetic game, clock, console and device boundaries. A script presses keys and pad buttons update by update; the driver logs
// every strafe action that reaches the player and what the walker's own code made of it.
//
// The fixture plays the game faithfully where the assist depends on it: the walker's StrafeLeft and StrafeRight are the game's own
// code, they do nothing in the air or during a dash, and a strafe action reaches them only while a running game drives the player
// (a paused game and the front end take none, which was read in the real game with a debugger).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <strings.h>
#include <cstdarg>
#ifdef __linux__
#include <sys/prctl.h>
#endif
using WCHAR = wchar_t;
using SINT = int;
using BOOL = int;
using LONG = long;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define GAME_STATE_PLAYING 3
#define MAX_JOYPADS 4
#define TRACE(...) ((void)0)

static int checks;
static void Check(bool ok, const char *why) { ++checks; if (!ok) { fprintf(stderr, "FAIL: %s\n", why); exit(2); } }

#include "control.inc"
#include "bindings.inc"
#include "lookup.inc"

// ---- the game's objects, as small as the code under test needs ----------------------------------------------------------------
struct EventManager { float time = 1; unsigned frame = 0; float GetTime() { return time; } unsigned GetFrameCount() { return frame; } } EVENT_MANAGER;
struct { void AddMessage(const char *, ...) {} } LOG;
int PLAYABLE_DEMO = 0;
struct { int mAutoConfigTest = 0; bool mRecordDemo = false, mPlaybackDemo = false, mStressTest = false, mAttractMode = false; } CLIPARAMS;
#include "console_types.inc"
#define stricmp strcasecmp
struct CConsole {
	CConsoleVar *mVariables = nullptr;
	#include "console_find.inc"
} CONSOLE;
// the walker (the game's StrafeLeft and StrafeRight run below) and the engine around it
#define DASH_BOOST_ROLL 0.08f
#define ENGINE_VOLUME 1.f
#define ST_FOLLOWANDDIE 0
#define BATTLE_ENGINE_SLOW_MOVEMENT_FACTOR 4.f
#define BATTLE_ENGINE_STATE_WALKER 1
struct FVector { float x, y, z; FVector(float a, float b, float c) : x(a), y(b), z(c) {} void operator/=(float f) { x /= f; y /= f; z /= f; } };
struct FMatrix { FMatrix(float, float, float) {} FVector operator*(const FVector &v) { return v; } };
struct IController { virtual ~IController() {} };
struct WalkerMain
{
	bool ground = true, mSlowMovement = false, dying = false, powered = true, walker = true; int mStrafeSound = 0; float mZoomOutTime = 1, mRollvel = 0;
	struct { float mYaw = 0; } mCurrentOrientation;
	bool IsOnGround() { return ground; } void AddVelocity(FVector) {}
};
struct Sound { void PlayEffect(int, WalkerMain *, float, int) {} bool IsInitialised() { return false; } } SOUND;
struct CBattleEngineWalkerPart
{
	WalkerMain *mMainPart; float mLastStartHardRightTime = -10, mLastStartHardLeftTime = -10, mLastMoveXVal = 0; int mDoingDashCount = 0;
	static float mDashTime, mDashStart, mDashEnd, mDashVelocity; static int mDashLength;
	explicit CBattleEngineWalkerPart(WalkerMain *p) : mMainPart(p) {} float GetCurrentAccleration() { return 1; } void LoseWeaponCharge() {}
	void StrafeLeft(float); void StrafeRight(float); int GetDashCount() { return mDoingDashCount; }
	void EndOfFrame() { if (mDoingDashCount > 0) --mDoingDashCount; }	// the walker's own Move counts a dash down once an update
};
#include "walker.inc"
struct CBattleEngine : WalkerMain
{
	CBattleEngineWalkerPart part; CBattleEngine() : part(this) {}
	CBattleEngine(const CBattleEngine &o) : WalkerMain(o), part(o.part) { part.mMainPart = this; }
	CBattleEngine &operator=(const CBattleEngine &o) { WalkerMain::operator=(o); part = o.part; part.mMainPart = this; return *this; }
	bool IsDying() { return dying; } bool GetIsPoweredUp() { return powered; } int GetState() { return walker ? BATTLE_ENGINE_STATE_WALKER : 2; }
	CBattleEngineWalkerPart *GetWalkerPart() { return &part; }
};
struct CPlayer : IController { CBattleEngine engine; CBattleEngine *GetBattleEngine() { return &engine; } } players[2];
struct CController;
struct CGame
{
	int n = 1, state = GAME_STATE_PLAYING; bool paused = false, multi = false; CController *controllers[2] = {};
	int GetNPlayers() { return n; } CPlayer *GetPlayer(int i) { return i >= 0 && i < n ? &players[i] : nullptr; }
	int GetGameState() { return state; } bool IsPaused() { return paused; } bool IsMultiplayer() { return multi; }
} GAME;
struct Platform { float GetSysTimeFloat() { return 1.f; } bool KeyOn(int) { return false; } int GetWindowWidth() { return 640; } int GetWindowHeight() { return 480; } } PLATFORM;
struct DIDEVCAPS { unsigned long dwSize, dwFlags, dwDevType; };
using DWORD = unsigned long;
using HRESULT = int;
#define SUCCEEDED(x) ((x) >= 0)
#define DI8DEVTYPE_JOYSTICK 0x14
#define DI8DEVTYPE_GAMEPAD 0x15
struct IDirectInputDevice8A {};
struct Device : IDirectInputDevice8A { bool stick = false; HRESULT GetCapabilities(DIDEVCAPS *c) { c->dwDevType = stick ? DI8DEVTYPE_JOYSTICK : DI8DEVTYPE_GAMEPAD; return 0; } } devices[MAX_JOYPADS];
struct { int mJoypads = 2; bool mUnknown33474[MAX_JOYPADS] = {true, true, true, true}; bool mJoypadIsNewtype[MAX_JOYPADS] = {true, true, true, true};
	Device *pJoystick[MAX_JOYPADS] = {&devices[0], &devices[1], &devices[2], &devices[3]}; bool KeyOn(int) { return false; }
	bool keytrap = false; bool GetKeytrap() { return keytrap; } } LT;
static bool gCeLayer = true, gRecording = false;
bool PortCeLayer() { return gCeLayer; }
bool PortRecording() { return gRecording; }
static int gBoot = 0, gSession = 0;
extern "C" int CE_BootRequested(void) { return gBoot; }
extern "C" int CE_SessionActive(void) { return gSession; }
static int gIni = -1, gIniReads = 0;		// ce.ini dodge_button: -1 absent
extern "C" int CE_SettingGetBool(const char *key, int def) { ++gIniReads; return !strcmp(key, "dodge_button") && gIni >= 0 ? gIni : def; }
enum PortSessionCategory { PORT_SESSION_GAME };
static std::vector<std::string> sessionLog;
void PortSessionLog(PortSessionCategory, bool, const char *format, ...)
{
	char text[200]; va_list args; va_start(args, format); vsnprintf(text, sizeof text, format, args); va_end(args); sessionLog.emplace_back(text);
}
// The pad: a settable button level and axis (the actual ce_gamepad.cpp reads the buttons through this)
static unsigned gPadButtons[MAX_JOYPADS]; static float gPadAxis[MAX_JOYPADS];
int PortCePadNext(void *device, bool, unsigned *out) { for (int p = 0; p < MAX_JOYPADS; ++p) if (device == &devices[p]) { *out = gPadButtons[p]; return 1; } *out = 0; return 0; }
void PortCePadForget(void *) {}
#include "gamepad.inc"

// ---- the controller: the actual DoMappings and mapped input, the keys of a script -------------------------------------------------
#include "analog_dead_zone.inc"
struct CController
{
	int mPadNumber = 0, mNumMappings = 4; bool mPlaying = false, mRecording = false, mPausedBeforeMap = false;
	float mLastRepeatTime = 0, mNextDelayTime = 0;
	IController *toControl = nullptr; bool keys[256] = {}, keysBefore[256] = {};
	bool set[128] = {};
	unsigned update = 0; int player = 0;
	IController *GetToControl() { return toControl; } int GetPadNumber() { return mPadNumber; }
	void SetButton(int id) { set[id] = true; }
	void CalcNumMappings() {} void ReadControllerState() {} void RecordControllerState() {}
	bool IsButtonSet(int id) { return set[id]; } void UnsetButton(int id) { set[id] = false; }
	void SendButtonAction(int button, float ana_val);
	BOOL GetKeyOn(int key) { return keys[key & 255]; } BOOL GetKeyOnce(int key) { return keys[key & 255] && !keysBefore[key & 255]; }
	BOOL GetKeyRelease(int key) { return !keys[key & 255] && keysBefore[key & 255]; }
	BOOL GetJoyButtonOn(int pad, int button) { int e = PortCeGamepadOn(pad, button); return e > 0; }
	BOOL GetJoyButtonOnce(int pad, int button) { int e = PortCeGamepadEdge(pad, button, false); return e > 0; }
	BOOL GetJoyButtonRelease(int pad, int button) { int e = PortCeGamepadEdge(pad, button, true); return e > 0; }
	bool RepeatOccured(bool, SControlBinding *) { return false; }
	float GetJoyAnalogueLeftX(int pad) { return gPadAxis[pad]; } float GetJoyAnalogueLeftY(int) { return 0; }
	float GetJoyAnalogueRightX(int) { return 0; } float GetJoyAnalogueRightY(int) { return 0; } float GetJoyPovX(int) { return 0; } float GetJoyPovY(int) { return 0; }
	float GetMappedInputValue(int pad, int key);
	void DoMappings();
	void Flush() { memset(set, 0, sizeof set); DoMappings(); }
};
#include "mapped_input.inc"
static bool gMouseAxisLocked[2] = {}, sSendingMouseRest = false, sMouseAxisRead = false;
static int sCeMouseRestId = -1, MOUSE_X = 320, MOUSE_Y = 240, MOUSE_WHEEL = 0;
static float gMouseSensitivity = 1.f;
static void *sMouse = nullptr;
struct { bool mReleased; } MOUSE_BUTTONS[3] = {};
static bool MOUSE_HELD[3] = {};
int PortCeDodgeAssistKey(int pad);
int PortCeDodgeAssistFilter(CController *controller, int pad, int button, float value);
int PortCeDodgeAssistEnd(CController *controller, int pad, int key_down, int *ids, int *hard);
static bool sCeDodgeSending = false;
#include "mapping.inc"

// What the player's controller does with an action: the player hands the two strafe actions to the walker (CPlayer::ReceiveButtonAction)
struct Event { unsigned update; int player, id; float value; bool tap; float time; };
static std::vector<Event> events;
struct Dash { int frame = -1, side = 0, count = 0; std::vector<int> sides; } dashes[2];
// Each dash a tap made, with the side the assist had been asked for (its own Side(), set before the first push is sent)
struct Cause { unsigned update; int side; int asked; };
static std::vector<Cause> causes;
static int AssistSide(int pad);
static void Reach(CController &c, int button, float value)
{
	events.push_back(Event{c.update, c.player, button, value, sCeDodgeSending, EVENT_MANAGER.time});
	if (GAME.paused || !c.toControl) return;			// a paused game and the front end do not hand strafe actions to the walker
	// CPlayer::ReceiveButtonAction returns before any action when the engine is dying or not powered up, and its strafe case is inside the walker-state block
	if (players[c.player].engine.dying || !players[c.player].engine.powered || !players[c.player].engine.walker) return;
	CBattleEngineWalkerPart &w = players[c.player].engine.part;
	int before = w.mDoingDashCount;
	if (button == BUTTON_MECH_STRAFE_LEFT) w.StrafeLeft(value);
	else if (button == BUTTON_MECH_STRAFE_RIGHT) w.StrafeRight(value);
	if (w.mDoingDashCount > 1 && before <= 1)
	{
		Dash &d = dashes[c.player];
		if (d.frame < 0) { d.frame = (int)c.update; d.side = button == BUTTON_MECH_STRAFE_LEFT ? -1 : 1; }
		++d.count; d.sides.push_back(button == BUTTON_MECH_STRAFE_LEFT ? -1 : 1);
		if (sCeDodgeSending) causes.push_back(Cause{c.update, button == BUTTON_MECH_STRAFE_LEFT ? -1 : 1, AssistSide(c.mPadNumber)});
	}
}
#include "send.inc"
#define private public			// the tests read the assist's copy of the walker's memory
#include "ce_dodge_assist.h"
#include "dodge_glue.inc"
#undef private
static int AssistSide(int pad) { return gState[pad].mAssist.Side(); }

// ---- scripts -----------------------------------------------------------------------------------------------------------------------
struct Step { bool ctrl = false, a = false, d = false, padB = false, extra = false; float stick = 0; float dt = 1.f / 60; };
static void Reset(CController &c)
{
	for (int p = 0; p < 2; ++p) { players[p] = CPlayer(); dashes[p] = Dash(); }
	GAME = CGame(); GAME.controllers[0] = &c; c = CController(); c.toControl = &players[0];
	events.clear(); sessionLog.clear(); causes.clear();
	memset(gPadButtons, 0, sizeof gPadButtons); memset(gPadAxis, 0, sizeof gPadAxis);
	EVENT_MANAGER.time = 1.f; EVENT_MANAGER.frame = 0;
	CBattleEngineWalkerPart::mDashTime = 0.2f;
	static CConsoleVar vars[3];
	const char *names[3] = {"g_dash_time", "g_dash_start", "g_dash_end"};
	float *data[3] = {&CBattleEngineWalkerPart::mDashTime, &CBattleEngineWalkerPart::mDashStart, &CBattleEngineWalkerPart::mDashEnd};
	for (int i = 0; i < 3; ++i) { strcpy(vars[i].mName, names[i]); vars[i].mType = CVar_float; vars[i].mData = data[i]; vars[i].mNext = i < 2 ? &vars[i + 1] : nullptr; }
	CONSOLE.mVariables = &vars[0];
}
static void RunStep(CController &c, const Step &s, unsigned index)
{
	EVENT_MANAGER.time += s.dt; ++EVENT_MANAGER.frame; c.update = index;
	memcpy(c.keysBefore, c.keys, sizeof c.keys);
	c.keys[DIK_A] = s.a; c.keys[DIK_D] = s.d; c.keys[DIK_LCONTROL] = s.ctrl; c.keys[DIK_LSHIFT] = s.extra;
	gPadButtons[c.mPadNumber] = s.padB ? 1u : 0; gPadAxis[c.mPadNumber] = s.stick;		// the pad's game button 0 is B
	c.Flush();
	players[0].engine.part.EndOfFrame();
}
static void Play(CController &c, const std::vector<Step> &steps) { for (size_t i = 0; i < steps.size(); ++i) RunStep(c, steps[i], (unsigned)i); }
static std::vector<Step> Frames(int n, std::function<void(int, Step &)> f, float dt = 1.f / 60)
{
	std::vector<Step> v(n); for (int i = 0; i < n; ++i) { v[i].dt = dt; f(i, v[i]); } return v;
}
static int Taps() { int n = 0; for (auto &e : events) n += e.tap; return n; }
static std::string Digest()
{
	std::string s; char b[64];
	for (auto &e : events) { snprintf(b, sizeof b, "%u:%d:%d:%g%s;", e.update, e.player, e.id, e.value, e.tap ? "t" : ""); s += b; }
	return s;
}
static void Env(const char *key, const char *value) { setenv(key, value, 1); }
static bool Is(const char *mode, const char *name) { return !strcmp(mode, name); }

static CController gC, gC2;
static void SetInput(int id, int slot, int pad, int type, int key) { SControlInput &i = FindControlBinding(id)->mInput[slot]; i.mPad = pad; i.mType = type; i.mKey = key; }
static const int L = BUTTON_MECH_STRAFE_LEFT, R = BUTTON_MECH_STRAFE_RIGHT;
static unsigned Fnv(const std::string &s) { unsigned h = 2166136261u; for (unsigned char c : s) { h ^= c; h *= 16777619u; } return h; }
static std::vector<Event> Taps(int player) { std::vector<Event> v; for (auto &e : events) if (e.tap && e.player == player) v.push_back(e); return v; }
// The updates in which the player's own action for an id reached the player
static std::vector<int> Real(int id, int player = 0) { std::vector<int> v; for (auto &e : events) if (!e.tap && e.id == id && e.player == player) v.push_back((int)e.update); return v; }
static bool Range(const std::vector<int> &v, int from, int to) { std::vector<int> w; for (int i = from; i < to; ++i) w.push_back(i); return v == w; }

// The assist's copy of what the walker remembers (the last strafe value, when each side's full push began) against the walker's own
static bool Near(float x, float y) { return fabsf(x - y) < 1e-4f || (x < -5.f && y < -5.f); }		// "never" is -10 for the walker, -1e9 for the copy
static bool CopyIsRight(int pad)
{
	const CeDodge::Assist &m = gState[pad].mAssist; const CBattleEngineWalkerPart &w = players[pad].engine.part;
	return Near(m.mLastX, w.mLastMoveXVal) && Near(m.mStart[0], w.mLastStartHardLeftTime) && Near(m.mStart[1], w.mLastStartHardRightTime);
}

// Random play: the button, A and D on the keys and the gamepad's B and stick (several values, partial ones among them), update lengths of 10 to
// 50 ms; with `hard` also lost footing, pauses, new levels (the front end for a moment, the clock restarts at 0.3, the walker is a new one) and lost
// players (the engine is seen dying for some updates, then a new engine with a new walker takes its place while the clock runs on).
// After every update (but those of a dying engine, whose copy the assist has given up) the assist's copy of the walker's memory must be the walker's
// own, and every dash the assist's own pushes caused must be toward the side it was asked for.
struct Chaos { long updates = 0, dodges = 0, byPlayer = 0, wrongWay = 0, diverged = 0, refused = 0, restarts = 0, paused = 0, respawns = 0, unpowered = 0, jets = 0; };
static Chaos RandomPlay(bool hard, int seeds, int length)
{
	Chaos t;
	for (int seed = 1; seed <= seeds; ++seed)
	{
		Reset(gC); unsigned r = (unsigned)seed * 2654435761u + (hard ? 12345u : 777u);
		GAME.state = 1; gC.toControl = nullptr; RunStep(gC, Step(), 0); GAME.state = GAME_STATE_PLAYING; gC.toControl = &players[0];		// a seed begins as a level does
		auto rnd = [&]() { r = r * 1664525u + 1013904223u; return (r >> 8) & 0xffff; };
		bool ctrl = false, a = false, d = false, padB = false, throughMenu = false; float stick = 0; int airLeft = 0, pauseLeft = 0, frontEnd = 0, lastLevel = -1000, dyingLeft = 0, offLeft = 0, jetLeft = 0;
		static const float values[] = {0.f, 0.f, 0.3f, -0.3f, 0.6f, -0.6f, 0.85f, -0.85f, 1.f, -1.f};
		for (int i = 0; i < length; ++i)
		{
			if (rnd() % 13 == 0) ctrl = !ctrl;
			if (rnd() % 7 == 0) a = !a;
			if (rnd() % 7 == 0) d = !d;
			if (rnd() % 11 == 0) padB = !padB;
			if (rnd() % 5 == 0) stick = values[rnd() % 10];
			if (hard)
			{
				if (airLeft == 0 && rnd() % 90 == 0) airLeft = 2 + (int)(rnd() % 14);
				if (pauseLeft == 0 && rnd() % 200 == 0) pauseLeft = 2 + (int)(rnd() % 9);
				if (frontEnd == 0 && i - lastLevel > 40 && rnd() % 700 == 0) { frontEnd = 3; ++t.restarts; throughMenu = rnd() % 2 == 0; lastLevel = i; }
				if (frontEnd == 0 && dyingLeft == 0 && rnd() % 900 == 0) { dyingLeft = 3 + (int)(rnd() % 8); ++t.respawns; }
				if (offLeft == 0 && rnd() % 120 == 0) offLeft = 2 + (int)(rnd() % 12);			// power cut: the game drops every action of an unpowered engine
				if (jetLeft == 0 && rnd() % 150 == 0) jetLeft = 3 + (int)(rnd() % 20);			// jet mode: the walker is not driven
			}
			players[0].engine.ground = airLeft == 0; if (airLeft) --airLeft;
			players[0].engine.powered = offLeft == 0; if (offLeft) { --offLeft; ++t.unpowered; }
			players[0].engine.walker = jetLeft == 0; if (jetLeft) { --jetLeft; ++t.jets; }
			GAME.paused = pauseLeft > 0; if (pauseLeft) { --pauseLeft; ++t.paused; }
			if (frontEnd)
			{
				gC.toControl = nullptr;
				GAME.state = throughMenu ? 1 : GAME_STATE_PLAYING;				// half of the new levels come by way of a state that is not the running game
				if (--frontEnd == 0) { players[0] = CPlayer(); dashes[0] = Dash(); gC.toControl = &players[0]; GAME.state = GAME_STATE_PLAYING; EVENT_MANAGER.time = 0.3f; }
			}
			if (dyingLeft)
			{
				players[0].engine.dying = true;
				if (!GAME.paused && --dyingLeft == 0) { players[0] = CPlayer(); dashes[0] = Dash(); }	// the new engine, a new walker, comes with the game's clock running (a pause stops its events)
			}
			Step s; s.ctrl = ctrl; s.a = a; s.d = d; s.padB = padB; s.stick = stick; s.dt = GAME.paused ? 0.f : 1.f / (20 + (float)(rnd() % 100));
			const size_t eventsBefore = events.size(), causesBefore = causes.size(); const int dashesBefore = dashes[0].count;
			RunStep(gC, s, (unsigned)i); ++t.updates;
			t.byPlayer += dashes[0].count - dashesBefore - (long)(causes.size() - causesBefore);
			for (size_t k = eventsBefore; k < events.size(); ++k) if (events[k].tap && !players[0].engine.ground) ++t.refused;
			for (size_t k = causesBefore; k < causes.size(); ++k) { ++t.dodges; if (causes[k].side != causes[k].asked) ++t.wrongWay; }
			if (gState[0].mActive && !players[0].engine.dying && !CopyIsRight(0))
			{
				++t.diverged;
				if (getenv("DODGE_TRACE"))
				{
					const CeDodge::Assist &m = gState[0].mAssist; const CBattleEngineWalkerPart &w = players[0].engine.part;
					fprintf(stderr, "seed %d update %d t=%.4f paused=%d ground=%d keys=%c%c%c pad=%d stick=%g: copy last %g starts %g %g, walker last %g starts %g %g\n", seed, i, EVENT_MANAGER.time, (int)GAME.paused,
						(int)players[0].engine.ground, s.ctrl ? 'C' : '.', s.a ? 'A' : '.', s.d ? 'D' : '.', (int)s.padB, s.stick, m.mLastX, m.mStart[0], m.mStart[1], w.mLastMoveXVal,
						w.mLastStartHardLeftTime, w.mLastStartHardRightTime);
				}
			}
		}
	}
	return t;
}

// Ctrl and a direction key pressed together at update 10 (a tap of `len` updates), the direction held to `holdTo`
static std::vector<Step> Press(bool left, int len, int holdTo, int ctrlAt = 10, int dirAt = 10, int n = 70, float dt = 1.f / 60)
{
	return Frames(n, [=](int i, Step &s) { s.ctrl = i >= ctrlAt && i < ctrlAt + len; (left ? s.a : s.d) = i >= dirAt && i < holdTo; }, dt);
}

static void KeyDodge(bool left)
{
	Reset(gC); Play(gC, Press(left, 3, 40));
	auto taps = Taps(0); const int first = left ? R : L, second = left ? L : R, sign = left ? -1 : 1;
	Check(taps.size() == 2 && taps[0].id == first && taps[0].value == (float)-sign && taps[0].update == 10 &&
		taps[1].id == second && taps[1].value == (float)sign && taps[1].update == 17, "a double tap: the push to the other side, then, 7 updates later (117 ms), to the chosen side");
	Check(dashes[0].side == sign && dashes[0].frame == 17 && dashes[0].count == 1, "the walker's own code dashed once, toward the chosen side, on the second push");
	// the player's own strafe reached the player in update 10, was held back while the double tap ran, and passes again after it
	auto own = Real(second); std::vector<int> want{10}; for (int i = 18; i < 40; ++i) want.push_back(i);
	Check(own == want, "the player's own strafe is not passed on while the double tap runs, and passes again after it");
	Check(Real(first).empty() && sessionLog.size() == 1 && sessionLog[0].find(left ? "left" : "right") != std::string::npos, "nothing else on the other side; one log line");
}
static void PadDodge(bool left)
{
	Reset(gC);
	Play(gC, Frames(70, [=](int i, Step &s) { s.padB = i >= 10 && i < 13; s.stick = i >= 10 && i < 40 ? (left ? -1.f : 1.f) : 0.f; }));
	auto taps = Taps(0); Check(taps.size() == 2 && dashes[0].side == (left ? -1 : 1) && dashes[0].frame == 17 && dashes[0].count == 1, "the gamepad button B with the stick dodges the way the stick pushes");
}

// The same script on the same environment, as the comparison with the unpatched controller needs: prints the digest of what reached the player
static void Report(const char *name) { printf("%s %u %zu taps=%d\n", name, Fnv(Digest()), events.size(), Taps()); }

int main(int argc, char **argv)
{
#ifdef __linux__
	prctl(PR_SET_DUMPABLE, 0);
#endif
	if (argc != 2) return 1;
	const char *mode = argv[1];
	Check(CE_DODGE_PAD_BUTTON == 0, "the Dodge Button is the pad's game button 0 (B, after retail's rotation of the face buttons)");
	const char *envs[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};
	for (const char *e : envs) unsetenv(e);
	CBattleEngineWalkerPart::mDashStart = 0.9f; CBattleEngineWalkerPart::mDashEnd = 0.8f;

	if (Is(mode, "key-right")) KeyDodge(false);
	else if (Is(mode, "key-left")) KeyDodge(true);
	else if (Is(mode, "pad-right")) PadDodge(false);
	else if (Is(mode, "pad-left")) PadDodge(true);
	else if (Is(mode, "tap-and-hold-alike"))
	{
		// a tap of both for one update dodges as a long hold does, and with the direction pressed after the button
		for (int variant = 0; variant < 4; ++variant)
		{
			Reset(gC);
			Play(gC, Frames(70, [=](int i, Step &s) {
				if (variant == 0) { s.ctrl = i == 10; s.d = i == 10; }
				if (variant == 1) { s.ctrl = i >= 10 && i < 50; s.d = i >= 14 && i < 50; }
				if (variant == 2) { s.d = i >= 10 && i < 50; s.ctrl = i >= 14 && i < 18; }
				if (variant == 3) { s.d = i == 10; s.ctrl = i >= 5 && i < 40; }
			}));
			Check(dashes[0].side == 1 && dashes[0].count == 1 && Taps() == 2, "tap, hold, button first, direction first: one dodge to the right");
		}
	}
	else if (Is(mode, "typed-equals-assisted"))
	{
		// the same double tap typed by hand with the keys, at the timing the assist chose: the same actions, the same walker state
		Reset(gC); Play(gC, Press(false, 1, 11));
		std::vector<Event> assisted = Taps(0); Dash da = dashes[0]; CBattleEngineWalkerPart wa = players[0].engine.part;
		Reset(gC); Play(gC, Frames(70, [](int i, Step &s) { s.a = i == 10; s.d = i == 17; }));
		Check(Taps() == 0 && events.size() == 2 && assisted.size() == 2, "typed: two actions, no taps");
		for (int k = 0; k < 2; ++k)
			Check(assisted[k].update == events[k].update && assisted[k].id == events[k].id && assisted[k].value == events[k].value, "the assist's actions are the typed double tap's, update for update");
		CBattleEngineWalkerPart &wt = players[0].engine.part;
		Check(dashes[0].frame == da.frame && dashes[0].side == da.side && dashes[0].count == da.count, "the same dash, in the same update, the same way");
		Check(wt.mDoingDashCount == wa.mDoingDashCount && wt.mLastMoveXVal == wa.mLastMoveXVal && wt.mLastStartHardLeftTime == wa.mLastStartHardLeftTime &&
			wt.mLastStartHardRightTime == wa.mLastStartHardRightTime, "the walker's remembered pushes and dash are the same as after the double tap typed by hand");
	}
	else if (Is(mode, "sweep"))
	{
		// every frame rate from 20 to 144, the button and the direction within 15 updates of each other either way, tapped or held, both sides
		int runs = 0;
		for (float fps : {20.f, 24.f, 30.f, 45.f, 60.f, 72.f, 90.f, 120.f, 144.f})
			for (int side = 0; side < 2; ++side)
				for (int offset = -15; offset <= 15; ++offset)
					for (int hold = 0; hold < 2; ++hold)
					{
						Reset(gC);
						const int ctrlAt = 30 + (offset < 0 ? -offset : 0), dirAt = 30 + (offset > 0 ? offset : 0), lastStart = std::max(ctrlAt, dirAt);
						// held: both to the end; tapped: both let go two updates after the later of the two went down
						Play(gC, Press(side == 0, hold ? 130 : lastStart + 2 - ctrlAt, hold ? 160 : lastStart + 2, ctrlAt, dirAt, 200, 1.f / fps));
						const int want = side == 0 ? -1 : 1;
						char why[160]; snprintf(why, sizeof why, "one dash the chosen way (fps %g side %d offset %d hold %d: dashes %d side %d)", fps, side, offset, hold, dashes[0].count, dashes[0].side);
						Check(dashes[0].count == 1 && dashes[0].side == want, why);
						const double latency = (dashes[0].frame - std::max(ctrlAt, dirAt)) / (double)fps;
						snprintf(why, sizeof why, "the dash comes within 0.36 s of the press (fps %g offset %d: %.3f s)", fps, offset, latency);
						Check(latency > 0.1 && latency < 0.36, why);
						++runs;
					}
		printf("sweep %d runs\n", runs);
	}
	else if (Is(mode, "hazard"))
	{
		// the player pushed D, then pressed the button 0 to 20 updates later (up to 0.33 s): never a dash to the left, always one to the right
		for (int gap = 0; gap <= 20; ++gap)
		{
			Reset(gC); Play(gC, Frames(100, [=](int i, Step &s) { s.d = i >= 10 && i < 80; s.ctrl = i >= 10 + gap && i < 12 + gap; }));
			char why[96]; snprintf(why, sizeof why, "one dash to the right after D and, %d updates later, the button (dashes %d side %d)", gap, dashes[0].count, dashes[0].side);
			Check(dashes[0].count == 1 && dashes[0].side == 1, why);
			Check(Taps() == 2, "a double tap, no more");
		}
	}
	else if (Is(mode, "chain"))
	{
		// two dodges to one side and one to the other, the button held throughout, the direction tapped; and a dodge asked for during a dash waits for it
		Reset(gC);
		Play(gC, Frames(220, [](int i, Step &s) { s.ctrl = true; s.d = (i >= 10 && i < 14) || (i >= 60 && i < 64); s.a = i >= 110 && i < 114; }));
		Check(dashes[0].count == 3 && Taps() == 6, "three dodges: right, right, left");
		Reset(gC);
		Play(gC, Frames(100, [](int i, Step &s) { s.ctrl = true; s.d = (i >= 10 && i < 14) || (i >= 28 && i < 30); }));
		Check(dashes[0].count == 2, "a press made during a dash is kept and dodges when the dash is over");
		Reset(gC);
		Play(gC, Frames(100, [](int i, Step &s) { s.ctrl = i >= 10; s.d = i >= 10; }));
		{ char why[100]; snprintf(why, sizeof why, "the button and the direction held together dodge once (dashes %d, taps %d)", dashes[0].count, Taps()); Check(dashes[0].count == 1, why); }
		// a dodge left, the right pushed during its dash (the walker takes none of it) and let go of, and then a dodge right: what the
		// walker did not take is not what the assist remembers
		Reset(gC);
		Play(gC, Frames(140, [](int i, Step &s) { s.ctrl = (i >= 10 && i < 13) || (i >= 40 && i < 43); s.a = i >= 10 && i < 14; s.d = i >= 20 && i < 100; }));
		Check(dashes[0].sides == std::vector<int>({-1, 1}), "a dodge left, then a dodge right after a push the walker refused: no dash the wrong way");
	}
	else if (Is(mode, "old-history"))
	{
		// the walker's last push was a full strafe left, long ago: the first push of a double tap to the right is a left push the game
		// would not count as a beginning; a plain double tap typed now fails, the Dodge Button's does not
		Reset(gC); Play(gC, Frames(120, [](int i, Step &s) { s.a = i >= 5 && i < 25; s.a = s.a || false; s.ctrl = i >= 60 && i < 63; s.d = i >= 60 && i < 63; }));
		Check(dashes[0].count == 1 && dashes[0].side == 1, "a dodge to the right after strafing left works");
		Reset(gC); Play(gC, Frames(120, [](int i, Step &s) { s.a = (i >= 5 && i < 25) || i == 60; s.d = i == 67; }));
		Check(dashes[0].count == 0, "the same double tap typed by hand fails after strafing left (the game's own rule)");
	}
	else if (Is(mode, "hitch") || Is(mode, "slow"))
	{
		const bool hitch = Is(mode, "hitch");
		for (int spike = 1; spike < 12; ++spike)
		{
			Reset(gC);
			auto v = Press(false, 3, 100, 10, 10, 100);
			if (hitch) v[10 + spike].dt = 0.09f + 0.01f * (spike % 5);	// one long frame somewhere in the wait
			else for (auto &s : v) s.dt = 0.1f;				// 10 updates a second: the window can hardly be hit
			Play(gC, v);
			Check(dashes[0].count <= 1 && (dashes[0].count == 0 || dashes[0].side == 1), "never a dash the wrong way");
			Check(Taps() <= 5, "a dodge that does not fit is tried at most twice");
			if (hitch && spike < 6) Check(dashes[0].count == 1, "a slow frame in the wait still gives the dodge");
		}
		if (hitch)
		{
			// a 0.2 s frame four updates into the wait carries the second push past the window: once more, with a neutral action first
			// (the game counts the first push only after a last value short of full that way), and then the dodge
			Reset(gC); auto v = Press(false, 3, 100, 10, 10, 100); v[14].dt = 0.2f; Play(gC, v);
			auto taps = Taps(0);
			Check(taps.size() == 4 && taps[0].id == L && taps[0].value == -1.f && taps[1].id == R && taps[1].value == 0.f && taps[2].id == L && taps[2].value == -1.f &&
				taps[3].id == R && taps[3].value == 1.f && taps[1].update == 14 && taps[2].update == 14 && taps[3].update == 21, "a missed window: neutral, first push again, second push");
			Check(dashes[0].count == 1 && dashes[0].side == 1 && dashes[0].frame == 21, "and the dodge comes");
		}
		else
		{
			Reset(gC); auto v = Press(false, 3, 100, 10, 10, 100); for (int i = 12; i < 70; i += 3) v[i].dt = 0.3f; Play(gC, v);
			Check(dashes[0].count <= 1 && (dashes[0].count == 0 || dashes[0].side == 1), "frames of 0.3 s: never a dash the wrong way");
			// every update is 0.3 s: the first push, then the double tap once more (a neutral action and the first push), then no more
			Check(Taps() == 3, "frames of 0.3 s: the first push and one more try, three pushes in all");
		}
	}
	else if (Is(mode, "dash-time"))
	{
		// g_dash_time is a console variable: the double tap follows it
		for (float time : {0.1f, 0.2f, 0.4f, 0.8f})
		{
			Reset(gC); CBattleEngineWalkerPart::mDashTime = time;
			Play(gC, Press(false, 3, 160, 10, 10, 160));
			char why[96]; snprintf(why, sizeof why, "the dodge follows g_dash_time (%g: dashes %d)", time, dashes[0].count);
			Check(dashes[0].count == 1 && dashes[0].side == 1, why);
			const double seconds = (dashes[0].frame - 10) / 60.0;
			snprintf(why, sizeof why, "the second push is in the window at g_dash_time %g (%.3f s)", time, seconds);
			Check(seconds > time * 0.5 && seconds < time, why);
		}
	}
	else if (Is(mode, "pause-memory"))
	{
		// the player pushed D, the game paused for ten updates (its clock stopped) and, eight updates of play after the push, the button:
		// the game still remembers the push, so the first push waits and the dodge is to the right, never to the left
		Reset(gC);
		for (int i = 0; i < 90; ++i)
		{
			GAME.paused = i >= 14 && i < 24;
			Step s; s.d = i >= 10 && i < 80; s.ctrl = i >= 28 && i < 30; s.dt = GAME.paused ? 0.f : 1.f / 60; RunStep(gC, s, i);
		}
		Check(dashes[0].sides == std::vector<int>({1}), "after a pause the assist still knows the push the game remembers: one dodge, to the right");
	}
	else if (Is(mode, "pause-mid"))
	{
		// the game pauses for ten updates (its clock stops) in the middle of the wait
		Reset(gC);
		for (int i = 0; i < 60; ++i)
		{
			GAME.paused = i >= 13 && i < 24;
			Step s; s.ctrl = i >= 10 && i < 13; s.d = i >= 10 && i < 50; s.dt = GAME.paused ? 0.f : 1.f / 60; RunStep(gC, s, i);
		}
		auto taps = Taps(0);
		Check(taps.size() >= 1 && taps[0].id == L && taps[0].update == 10, "the first push was made before the pause");
		for (auto &e : taps) Check(!(e.update >= 13 && e.update < 24), "nothing is sent while paused");
		Check(dashes[0].side != -1, "never a dash the wrong way");
		Check(Real(R).size() > 20, "after the pause the player's own strafe passes again");
		for (auto &e : taps) Check(e.update < 13, "the pause abandons the double tap: nothing is sent after it");
		auto own = Real(R); Check(std::find(own.begin(), own.end(), 24) != own.end(), "and the player's own strafe passes in the first update after the pause");
	}
	else if (Is(mode, "two-players"))
	{
		Reset(gC); GAME.n = 2; GAME.controllers[1] = &gC2; gC2 = CController(); gC2.mPadNumber = 1; gC2.player = 1; gC2.toControl = &players[1];
		for (int i = 0; i < 70; ++i)
		{
			EVENT_MANAGER.time += 1.f / 60; ++EVENT_MANAGER.frame;
			Step s1; s1.ctrl = i >= 10 && i < 13; s1.d = i >= 10 && i < 40;
			memcpy(gC.keysBefore, gC.keys, sizeof gC.keys); gC.keys[DIK_D] = s1.d; gC.keys[DIK_LCONTROL] = s1.ctrl; gC.update = i;
			gPadButtons[1] = i >= 20 && i < 23 ? 1u : 0; gPadAxis[1] = i >= 20 && i < 50 ? -1.f : 0.f; gPadAxis[0] = 0; gPadButtons[0] = 0;
			gC2.update = i;
			gC.Flush(); gC2.Flush();
			players[0].engine.part.EndOfFrame(); players[1].engine.part.EndOfFrame();
		}
		Check(dashes[0].count == 1 && dashes[0].side == 1 && dashes[1].count == 1 && dashes[1].side == -1, "each player dodges the way they push, with their own controls");
		Check(Taps(0).size() == 2 && Taps(1).size() == 2, "two taps each");
	}
	else if (Is(mode, "ini-reload"))
	{
		// the line is read when the game is next in a level, not every update; a change in the front end applies from the next level
		Reset(gC); gIni = 0; gIniReads = 0;
		Play(gC, Press(false, 3, 40, 10, 10, 40));
		Check(dashes[0].count == 0 && Taps() == 0, "dodge_button=0: no double tap");
		gC.toControl = nullptr;											// the front end
		Play(gC, Frames(5, [](int, Step &) {}));
		{	// the Level 100 cue and the help ask whether the Dodge Button is on every frame they are drawn: the assist's value, not ce.ini
			const int before = gIniReads;
			bool on = true;
			for (int k = 0; k < 100; ++k) on = on && PortCeDodgeAssistOn();
			Check(!on && gIniReads == before, "the cue and the help take the assist's value (off here) and do not read ce.ini");
		}
		gIni = 1; gC.toControl = &players[0];							// the next level
		events.clear();
		Play(gC, Press(false, 3, 40, 10, 10, 50));
		Check(dashes[0].count == 1 && Taps() == 2, "dodge_button=1 from the next level");
		Check(gIniReads >= 2 && gIniReads <= 3, "ce.ini is read when a level is entered, not every update");
	}
	else if (Is(mode, "air-refusal"))
	{
		// The walker loses its footing while the double tap runs, so it refuses the second push. The player then pushes the direction again and asks for the dodge
		// 0.1 to 0.2 s after that fresh push (the case the first push waits for): the dodge must go the way asked, never the other. (The assist once took the refused push
		// for one the walker remembered, did not see the new one begin, and the walker's own code dashed the other way in 24 of these 56 timings.)
		int total = 0;
		for (int left = 0; left < 2; ++left)
			for (int air = 8; air <= 14; air += 2)
				for (int gap = 3; gap <= 16; ++gap)
				{
					Reset(gC);
					for (int i = 0; i < 140; ++i)
					{
						players[0].engine.ground = !(i >= 12 && i < 12 + air);
						Step s; s.dt = 1.f / 60;
						(left ? s.a : s.d) = (i >= 10 && i < 14) || (i >= 34 && i < 110);
						s.ctrl = (i >= 10 && i < 13) || (i >= 34 + gap && i < 37 + gap);
						RunStep(gC, s, (unsigned)i);
					}
					char why[160]; snprintf(why, sizeof why, "after a refused second push, a dodge %s goes %s (air %d, gap %d: dashes %d)", left ? "left" : "right", left ? "left" : "right", air, gap, dashes[0].count);
					Check(!dashes[0].sides.empty(), why);
					for (int side : dashes[0].sides) Check(side == (left ? -1 : 1), why);
					++total;
				}
		printf("air-refusal %d timings\n", total);
	}
	else if (Is(mode, "idle-begin"))
	{
		// The game is paused (the assist idle) and resumes, and the player taps a direction in the first update after it: the walker takes the push and remembers
		// when it began. The player asks for the dodge that way 0.1 to 0.2 s later: the assist must know of the push although it was idle when it came.
		int total = 0;
		for (int left = 0; left < 2; ++left)
			for (int gap = 3; gap <= 16; ++gap)
			{
				Reset(gC);
				for (int i = 0; i < 100; ++i)
				{
					GAME.paused = i >= 5 && i < 11;
					Step s; s.dt = GAME.paused ? 0.f : 1.f / 60;
					(left ? s.a : s.d) = i == 11 || (i >= 11 + gap && i < 90);
					s.ctrl = i >= 11 + gap && i < 14 + gap;
					RunStep(gC, s, (unsigned)i);
				}
				char why[160]; snprintf(why, sizeof why, "after a push taken in the first update after a pause, a dodge %s goes %s (gap %d: dashes %d)", left ? "left" : "right", left ? "left" : "right", gap, dashes[0].count);
				Check(!dashes[0].sides.empty(), why);
				for (int side : dashes[0].sides) Check(side == (left ? -1 : 1), why);
				++total;
			}
		printf("idle-begin %d timings\n", total);
	}
	else if (Is(mode, "chaos") || Is(mode, "chaos-air"))
	{
		const bool hard = Is(mode, "chaos-air");
		const Chaos t = RandomPlay(hard, 60, 3000);
		printf("%s updates=%ld assisted-dodges=%ld dodges-by-the-player=%ld taps-in-the-air=%ld pauses=%ld new-levels=%ld lost-players=%ld unpowered-updates=%ld jet-updates=%ld wrong-way=%ld copy-differs=%ld\n", mode, t.updates, t.dodges,
			t.byPlayer, t.refused, t.paused, t.restarts, t.respawns, t.unpowered, t.jets, t.wrongWay, t.diverged);
		Check(t.dodges > 1000 && t.byPlayer > 100, "the random play makes plenty of dodges, the assist's and the player's own");
		if (hard) Check(t.refused > 50 && t.paused > 1000 && t.restarts > 100 && t.respawns > 100, "and loses its footing, pauses, starts new levels and loses players");
		if (hard) Check(t.unpowered > 10000 && t.jets > 10000, "and loses power and flies (the game drops the strafe actions of an unpowered engine and of a jet)");
		Check(t.wrongWay == 0, "never a dash the wrong way caused by the assist's own pushes");
		Check(t.diverged == 0, "after every update the assist's copy of the walker's memory is the walker's own");
	}
	else if (Is(mode, "typed-sweep"))
	{
		// For every case of the sweep: the actions the assist sent (update, side, value) typed by hand with the keys A and D give the same dash, in the same update,
		// and leave the walker with the same remembered pushes.
		struct Mem { float last, ls, rs; int count; };
		auto mem = [&]() { CBattleEngineWalkerPart &w = players[0].engine.part; return Mem{w.mLastMoveXVal, w.mLastStartHardLeftTime, w.mLastStartHardRightTime, w.mDoingDashCount}; };
		int runs = 0, compared = 0;
		for (float fps : {20.f, 24.f, 30.f, 45.f, 60.f, 72.f, 90.f, 120.f, 144.f})
			for (int side = 0; side < 2; ++side)
				for (int offset = -15; offset <= 15; ++offset)
					for (int hold = 0; hold < 2; ++hold)
					{
						Reset(gC);
						const int ctrlAt = 30 + (offset < 0 ? -offset : 0), dirAt = 30 + (offset > 0 ? offset : 0), lastStart = std::max(ctrlAt, dirAt);
						auto script = Press(side == 0, hold ? 130 : lastStart + 2 - ctrlAt, hold ? 160 : lastStart + 2, ctrlAt, dirAt, 200, 1.f / fps);
						std::vector<Mem> trail;
						for (size_t i = 0; i < script.size(); ++i) { RunStep(gC, script[i], (unsigned)i); trail.push_back(mem()); }
						++runs;
						auto taps = Taps(0);
						bool neutral = false; for (auto &t : taps) if (t.value == 0.f) neutral = true;
						if (taps.size() != 2 || neutral) continue;
						const Dash assisted = dashes[0];
						std::vector<Step> typed(script.size());
						for (size_t i = 0; i < typed.size(); ++i) typed[i].dt = script[i].dt;
						for (auto &t : taps) { if (t.id == L) typed[t.update].a = true; else typed[t.update].d = true; }
						Reset(gC);
						std::vector<Mem> trail2;
						for (size_t i = 0; i < typed.size(); ++i) { RunStep(gC, typed[i], (unsigned)i); trail2.push_back(mem()); }
						bool ok = events.size() == taps.size() && Taps() == 0;
						for (size_t k = 0; ok && k < taps.size(); ++k) ok = events[k].update == taps[k].update && events[k].id == taps[k].id && events[k].value == taps[k].value;
						const unsigned u2 = taps.back().update;
						ok = ok && trail[u2].last == trail2[u2].last && trail[u2].ls == trail2[u2].ls && trail[u2].rs == trail2[u2].rs && trail[u2].count == trail2[u2].count &&
							dashes[0].frame == assisted.frame && dashes[0].side == assisted.side && dashes[0].count == assisted.count;
						char why[120]; snprintf(why, sizeof why, "typed by hand it is the same (fps %g side %d offset %d hold %d)", fps, side, offset, hold);
						Check(ok, why);
						++compared;
					}
		printf("typed-sweep runs=%d compared=%d\n", runs, compared);
		Check(compared > 900, "most cases have no neutral action and can be typed");
	}
	else if (Is(mode, "switch"))
	{
		// The button held throughout; D held, then the player moves to A with no update between (D let go and A pressed in the same update) or with a gap of 1, 2 or 4:
		// each is a press, so a dodge right and then a dodge left follow, at 60 and at 20 updates a second.
		for (float fps : {60.f, 20.f})
			for (int gap : {0, 1, 2, 4})
			{
				Reset(gC);
				Play(gC, Frames(300, [=](int i, Step &s) { s.ctrl = i >= 10; s.d = i >= 10 && i < 80; s.a = i >= 80 + gap && i < 140; }, 1.f / fps));
				char why[120]; snprintf(why, sizeof why, "a dodge right, then one left, whatever the gap when the direction is changed (fps %g gap %d)", fps, gap);
				Check(dashes[0].sides == std::vector<int>({1, -1}), why);
			}
	}
	else if (Is(mode, "buffer"))
	{
		// The game's own rate (20 updates a second; a dash lasts 15 updates, 0.75 s). The button held; a dodge right (D tapped); then A tapped k updates after that dash began.
		// The press is kept 0.35 s: one made in the first half of the dash is dropped, one made in the last 0.35 s dodges when the dash is over.
		for (int k = 0; k <= 14; ++k)
		{
			Reset(gC);
			Play(gC, Frames(120, [=](int i, Step &s) { s.ctrl = true; s.d = i >= 10 && i < 12; s.a = i >= 13 + k && i < 16 + k; }, 1.f / 20));
			char why[120]; snprintf(why, sizeof why, "k=%d updates into the dash: %d dashes", k, dashes[0].count);
			if (k <= 5) Check(dashes[0].sides == std::vector<int>({1}), why);
			if (k >= 9) Check(dashes[0].sides == std::vector<int>({1, -1}), why);
			Check(dashes[0].count <= 2, why);
		}
	}
	else if (Is(mode, "stick-threshold"))
	{
		// The button with the stick pushed part of the way: 0.6 of its travel counts as a direction, 0.55 does not (the game's dead zone and scale take them to 0.53 and
		// 0.47 of a full push; the assist's threshold is a half).
		for (float raw : {0.3f, 0.5f, 0.55f, 0.6f, 0.7f, 0.9f, 1.f})
			for (int sign : {1, -1})
			{
				Reset(gC);
				Play(gC, Frames(100, [=](int i, Step &s) { s.padB = i >= 10 && i < 14; s.stick = i >= 10 && i < 60 ? raw * (float)sign : 0.f; }));
				char why[120]; snprintf(why, sizeof why, "stick at %g to the %s (dashes %d)", raw, sign > 0 ? "right" : "left", dashes[0].count);
				if (raw >= 0.6f) Check(dashes[0].sides == std::vector<int>({sign}), why);
				else Check(dashes[0].count == 0 && Taps() == 0, why);
			}
	}
	else if (Is(mode, "window"))
	{
		// The second push goes out between 0.55 and 0.95 of g_dash_time after the first by the game's own clock, whatever the length of an update (5 to 105 ms in steps that
		// put an update on each edge of the window); a dodge comes when an update lands inside, and never one the wrong way.
		const float lo = 0.2f * (0.5f + CeDodge::Assist::Margin()), hi = 0.2f * (1.f - CeDodge::Assist::Margin());
		int dodged = 0, missed = 0;
		for (int k = 0; k <= 400; ++k)
		{
			const float dt = 0.005f + 0.00025f * (float)k;
			Reset(gC);
			Play(gC, Press(false, 3, 500, 10, 10, 500, dt));
			bool inside = false;
			for (int n = 1; n < 100; ++n) if ((float)n * dt >= lo + 0.0005f && (float)n * dt <= hi - 0.0005f) inside = true;
			float firstAt = -1.f;
			for (auto &e : Taps(0))
			{
				if (e.id == L && e.value == -1.f) firstAt = e.time;
				if (e.id == R && e.value == 1.f && firstAt >= 0.f)
				{
					const float spacing = e.time - firstAt;
					char why[140]; snprintf(why, sizeof why, "the second push comes %.4f s after the first (dt %.4f), not in [%.4f, %.4f]", spacing, dt, lo, hi);
					Check(spacing >= lo && spacing <= hi, why);
				}
			}
			char why[100]; snprintf(why, sizeof why, "dt %.4f: dashes %d", dt, dashes[0].count);
			for (int side : dashes[0].sides) Check(side == 1, why);
			if (inside) { Check(dashes[0].sides == std::vector<int>({1}), why); ++dodged; } else ++missed;
		}
		printf("window dt 5..105 ms: %d with an update inside the window all dodged, %d without\n", dodged, missed);
		Check(dodged > 300 && missed > 20, "the sweep has update lengths on both sides of the window");
	}
	else if (Is(mode, "rules"))
	{
		// The rules alone, with explicit game times, where a comparison of floats decides: the edges of the send window and of the hazard, how far a stick must be pushed, what the
		// walker is taken to remember, how long a press is kept, and what an idle stretch (a pause, the front end) forgets.
		using namespace CeDodge;
		const Walker w = {0.2f, 0.9f, 0.8f};
		const float lo = w.mTime * (0.5f + Assist::Margin()), hi = w.mTime * (1.f - Assist::Margin());
		const float hazardLo = w.mTime * (0.5f - Assist::Margin()), hazardHi = w.mTime * (1.f + Assist::Margin());
		struct Rig
		{
			Assist a; Update u; Tap taps[kMaxTaps];
			Rig() { u = Update(); u.mEligible = u.mCanDodge = u.mAccepts = true; u.mWalker = Walker{0.2f, 0.9f, 0.8f}; }
			int Step(float now, bool down) { u.mNow = now; u.mDodgeDown = down; return a.Step(u, taps); }
			// the player pushes toward `side` in this update (the walker takes it: accepted) and the button is held: a press at `now`
			int Press(int side, float value, float now, bool accepted = true) { a.Real(side, value, now, u.mWalker, accepted); return Step(now, true); }
		};
		auto is = [](const Tap &t, int side, bool hard) { return t.mSide == side && t.mHard == hard; };
		{	// the first push, then the second at the very edges of the send window: sent at both, and not a hair after the upper one
			for (int edge = 0; edge < 3; ++edge)
			{
				Rig r; r.Step(0.f, false);
				int n = r.Press(1, 1.f, 0.f, false);
				Check(n == 1 && is(r.taps[0], -1, true) && r.a.Waiting() && r.a.Owning() && r.a.Side() == 1, "a press toward the right: the first push goes left, and the player's own strafe is held back");
				const float at = edge == 0 ? lo : edge == 1 ? hi : nextafterf(hi, 1.f);
				n = r.Step(at, true);
				if (edge < 2) Check(n == 1 && is(r.taps[0], 1, true) && !r.a.Waiting() && !r.a.Owning(), "the second push goes out at the edges of the window, lower and upper");
				else Check(n == 2 && is(r.taps[0], 1, false) && is(r.taps[1], -1, true), "a hair past the upper edge it does not: the double tap starts once more, with a neutral action first");
			}
			Rig r; r.Step(0.f, false); r.Press(1, 1.f, 0.f, false);
			Check(r.Step(0.f, true) == 0 && r.a.Waiting(), "two updates at the same game time change nothing (the clock has not run backwards)");
			Check(r.Step(nextafterf(lo, 0.f), true) == 0 && r.a.Waiting(), "a hair before the lower edge nothing is sent and the double tap is kept");
			Check(r.Step(lo, true) == 1, "and at it the second push goes");
		}
		{	// the hazard: the chosen side's push began `age` ago; the first push waits when the walker's own window (0.5 to 1 of g_dash_time) is within the margin of it
			for (float age : {hazardLo, 0.15f, hazardHi})
			{
				Rig r; r.Step(0.f, false); r.a.Real(1, 1.f, 0.f, w, true); r.Step(0.f, false);
				const int n = r.Press(1, 1.f, age, false);
				const bool waits = age == 0.15f;
				char why[100]; snprintf(why, sizeof why, "a press %g s after the push began: %s", age, waits ? "the first push waits" : "at the very edge of the margin it goes");
				Check(waits ? n == 0 : n >= 1, why);
			}
		}
		{	// the first push needs a neutral action before it when the last value was already full that way (the game counts a push as a beginning only from a last value short of g_dash_start)
			for (float last : {1.f, 0.9f, 0.85f, 0.8f, 0.5f})
			{
				Rig r; r.Step(0.f, false); r.a.Real(-1, -last, 0.f, w, true); r.Step(0.f, false);
				const int n = r.Press(1, 1.f, 0.f, false);
				char why[100]; snprintf(why, sizeof why, "the last value was %g toward the side of the first push", last);
				if (last >= 0.9f) Check(n == 2 && is(r.taps[0], 1, false) && is(r.taps[1], -1, true), why);
				else Check(n == 1 && is(r.taps[0], -1, true), why);
			}
		}
		{	// a push the walker counts as a beginning: from a last value short of g_dash_start to one beyond it; the press 0.15 s later waits for it, or not
			struct Case { float first, second; bool began; const char *why; };
			const Case cases[] = {{-1.f, 1.f, true, "from nothing to a full push"}, {0.5f, 1.f, true, "from half to a full push"}, {-1.f, 0.9f, false, "to exactly g_dash_start"},
				{-1.f, 0.85f, false, "to a value between g_dash_end and g_dash_start"}, {0.9f, 1.f, false, "from exactly g_dash_start"}, {0.85f, 1.f, true, "from a value between g_dash_end and g_dash_start"}};
			for (const Case &c : cases)
			{
				Rig r; r.Step(0.f, false);
				if (c.first >= 0.f) { r.a.Real(1, c.first, 0.f, w, true); r.Step(0.f, false); }
				r.a.Real(1, c.second, 0.f, w, true); r.Step(0.f, false);
				const int n = r.Press(1, 1.f, 0.15f, false);
				Check((n == 0) == c.began, c.why);
			}
		}
		{	// a fresh assist remembers no push at all: the first press goes at once, a moment after the start of the game's clock too
			Rig r; r.Step(0.1f, false);
			Check(r.Press(1, 1.f, 0.15f, false) == 1, "nothing was remembered, so nothing is waited for");
		}
		{	// how far a stick must be pushed to be a direction: half
			for (float value : {0.5f, nextafterf(0.5f, 0.f), 0.3f, 0.95f})
			{
				Rig r; r.Step(0.f, false);
				const int n = r.Press(1, value, 0.f, false);
				char why[100]; snprintf(why, sizeof why, "a push of %g of the full one", value);
				Check((n > 0) == (value >= 0.5f), why);
			}
		}
		{	// the walker's numbers are not usable: nothing is done
			for (float time : {0.f, -1.f, NAN})
			{
				Rig r; r.u.mWalker.mTime = time; r.Step(0.f, false);
				Check(r.Press(1, 1.f, 0.f) == 0 && !r.a.Waiting(), "no walker window, no double tap");
			}
		}
		{	// a press is kept 0.35 s while the walker is busy, not a hair longer
			for (int late = 0; late < 2; ++late)
			{
				Rig r; r.Step(0.f, false); r.u.mCanDodge = false;
				r.Press(1, 1.f, 0.f, false);
				r.u.mCanDodge = true;
				const int n = r.Step(late ? nextafterf(Assist::Patience(), 1.f) : Assist::Patience(), true);
				Check(late ? n == 0 : n >= 1, late ? "a press is dropped after 0.35 s" : "a press is kept for 0.35 s");
			}
		}
		{	// while the double tap runs every strafe action of the player's is held back, a neutral one too; the player's push before it is not
			Rig r; r.Step(0.f, false);
			Check(!r.a.Real(1, 1.f, 0.f, w, true), "before the double tap the player's push passes");
			r.Step(0.f, true); r.a.Real(1, 1.f, 0.f, w, true); r.Step(0.f, true);
			Check(r.a.Owning() && r.a.Real(1, 0.f, 0.01f, w, true) && r.a.Real(-1, -1.f, 0.01f, w, true), "during it the player's pushes are held back, a neutral action among them");
		}
		{	// an idle stretch (a pause, the front end) forgets the double tap, the waiting press and the button held; the walker's own memory it keeps
			Rig r; r.Step(0.f, false); r.Press(1, 1.f, 0.f);
			r.u.mEligible = false; r.Step(0.05f, false);
			Check(!r.a.Waiting() && !r.a.Owning(), "the double tap under way is abandoned");
			r.u.mEligible = true;
			Check(r.Press(1, 1.f, 0.1f) == 0 && r.Press(1, 1.f, 0.15f) == 0, "a button and a direction held through the idle stretch are not a press");
			r.Step(0.2f, false);
			Check(r.Press(1, 1.f, 0.25f, false) == 0, "and the walker's memory is kept: the push that began at 0.1 s is waited for");
			Rig q; q.Step(0.f, false); q.u.mCanDodge = false; q.Press(1, 1.f, 0.f, false);
			q.u.mEligible = false; q.Step(0.05f, false); q.u.mEligible = true; q.u.mCanDodge = true;
			Check(q.Step(0.1f, false) == 0, "a press waiting for the walker is forgotten");
		}
		{	// the clock runs backwards (a new level): the assist starts again, the first action of the new level included
			Rig r; r.Step(6.f, false); r.Step(6.05f, false);
			r.a.Real(1, 1.f, 0.3f, w, true);
			r.Step(0.3f, false);
			Check(r.Press(1, 1.f, 0.45f, false) == 0, "the push of the new level's first update is remembered: 0.15 s later the first push waits");
		}
		{	// the button held; the same direction held is not another press, a change of direction with no update between (D let go, A pressed) is
			Rig r; r.Step(0.f, false);
			Check(r.Press(1, 1.f, 0.f, false) == 1 && r.Press(1, 1.f, lo, false) == 1 && !r.a.Waiting(), "a dodge right: the first push, then the second");
			Check(r.Press(1, 1.f, 0.3f, false) == 0 && r.Press(1, 1.f, 0.35f, false) == 0, "the same direction held is not another press");
			const int n = r.Press(-1, -1.f, 0.4f, false);
			Check(n >= 1 && r.a.Side() == -1 && r.a.Waiting(), "D let go and A pressed in the same update is a press: a dodge left starts");
		}
	}
	else if (Is(mode, "pause-want"))
	{
		// A press made while the walker is off the ground is kept for it; a pause in between forgets it (early in a level, with the clock near 0, where a press that was
		// not forgotten would still be within its 0.35 s when the game comes back)
		Reset(gC); EVENT_MANAGER.time = 0.f;
		for (int i = 0; i < 100; ++i)
		{
			players[0].engine.ground = !(i >= 10 && i < 15);
			GAME.paused = i >= 13 && i < 20;
			Step s; s.dt = GAME.paused ? 0.f : 1.f / 60;
			s.d = i >= 10 && i < 12; s.ctrl = i >= 10 && i < 12;
			RunStep(gC, s, (unsigned)i);
		}
		Check(Taps() == 0 && dashes[0].count == 0, "a press that waited for the walker is forgotten by a pause");
	}
	else if (Is(mode, "env-empty"))
	{
		// A fence variable that is set but empty is not set, as the game's own fences read them: the assist stays on
		for (const char *e : envs)
		{
			Env(e, ""); Reset(gC); Play(gC, Press(false, 3, 40, 10, 10, 50)); unsetenv(e);
			char why[100]; snprintf(why, sizeof why, "%s empty does not fence the assist", e);
			Check(Taps() == 2 && dashes[0].count == 1, why);
		}
	}
	else if (Is(mode, "dash-time-range"))
	{
		// A g_dash_time that is not believable (or not a number) is not believed: the assist works with the retail window of 0.2 s
		const float lo = 0.2f * (0.5f + CeDodge::Assist::Margin()), hi = 0.2f * (1.f - CeDodge::Assist::Margin());
		for (float time : {0.001f, 0.003f, 20.f, 1000.f, NAN})
		{
			Reset(gC); CBattleEngineWalkerPart::mDashTime = time;
			Play(gC, Press(false, 3, 100, 10, 10, 100));
			auto taps = Taps(0);
			char why[100]; snprintf(why, sizeof why, "g_dash_time %g: the assist sends two pushes (%zu)", time, taps.size());
			Check(taps.size() == 2, why);
			const float spacing = taps[1].time - taps[0].time;
			snprintf(why, sizeof why, "g_dash_time %g: the retail window of 0.2 s is used (%.4f s apart)", time, spacing);
			Check(spacing >= lo && spacing <= hi, why);
		}
	}
	else if (Is(mode, "ini-quiet"))
	{
		// The filter runs for every strafe action, in the pause menu and the front end too, and must not ask ce.ini there; a level asks once
		Reset(gC); gIni = 1; gIniReads = 0;
		Play(gC, Press(false, 3, 40, 10, 10, 50));
		Check(gIniReads == 1 && Taps() == 2, "a level asks ce.ini once");
		auto keys = [](int i, Step &s) { s.a = i % 7 < 3; s.d = i % 5 < 2; s.ctrl = i % 11 < 5; s.dt = 0.f; };
		GAME.paused = true;
		Play(gC, Frames(200, keys));
		GAME.paused = false; GAME.state = 1; gC.toControl = nullptr;
		Play(gC, Frames(200, keys));
		Check(gIniReads == 1, "the pause menu and the front end, with strafe keys pressed, never ask ce.ini");
		GAME.state = GAME_STATE_PLAYING; gC.toControl = &players[0]; events.clear();
		Play(gC, Press(false, 3, 40, 10, 10, 50));
		Check(gIniReads == 2 && Taps() == 2, "the next level asks again, once, and has its dodge");
	}
	else if (Is(mode, "two-players-shared-keyboard"))
	{
		// There is one keyboard: Left Ctrl held for the first player is not the second player's button, whose stick is pushed left meanwhile
		Reset(gC); GAME.n = 2; GAME.controllers[1] = &gC2; gC2 = CController(); gC2.mPadNumber = 1; gC2.player = 1; gC2.toControl = &players[1];
		for (int i = 0; i < 70; ++i)
		{
			EVENT_MANAGER.time += 1.f / 60; ++EVENT_MANAGER.frame;
			memcpy(gC.keysBefore, gC.keys, sizeof gC.keys); memcpy(gC2.keysBefore, gC2.keys, sizeof gC2.keys);
			gC.keys[DIK_LCONTROL] = gC2.keys[DIK_LCONTROL] = i >= 10 && i < 40;
			gC.update = gC2.update = (unsigned)i;
			gPadButtons[0] = gPadButtons[1] = 0; gPadAxis[0] = 0; gPadAxis[1] = i >= 10 && i < 40 ? -1.f : 0.f;
			gC.Flush(); gC2.Flush();
			players[0].engine.part.EndOfFrame(); players[1].engine.part.EndOfFrame();
		}
		Check(Taps(0).empty() && Taps(1).empty() && dashes[1].count == 0, "Left Ctrl is the first player's: the second player's stick alone does not dodge");
	}
	else if (Is(mode, "key-unused-slot"))
	{
		// A binding slot that is not in use (pad -1) does not make Left Ctrl "bound": the Dodge Button stays free
		Reset(gC); SetInput(BUTTON_MECH_LANDING_JETS, 0, -1, KEY_ON, DIK_LCONTROL);
		Play(gC, Press(false, 3, 40, 10, 10, 50));
		Check(Taps() == 2 && dashes[0].count == 1, "an unused binding slot does not take the key");
	}
	else if (Is(mode, "other-actions"))
	{
		// Other bound actions (the morph button, here on Left Shift) pass every update they are held, during the double tap too: only the strafe actions are held back
		Reset(gC);
		SetInput(BUTTON_MECH_MORPH, 0, 0, KEY_ON, DIK_LSHIFT);
		Play(gC, Frames(60, [](int i, Step &s) { s.ctrl = i >= 10 && i < 13; s.d = i >= 10 && i < 40; s.extra = i >= 11 && i < 16; }));
		Check(dashes[0].count == 1 && dashes[0].side == 1, "a dodge right");
		Check(Real(BUTTON_MECH_MORPH) == std::vector<int>({11, 12, 13, 14, 15}), "the morph action passes every update it is held, during the double tap too");
	}
	else if (Is(mode, "clock-restart"))
	{
		// A new level: the clock starts again near 0 and the walker is a new one, by way of the front end or with nothing in between. The first dodge of the new level
		// goes at once and the right way, as the first dodge of any level does.
		for (int menu = 0; menu < 2; ++menu)
			for (int round = 0; round < 3; ++round)
			{
				Reset(gC);
				EVENT_MANAGER.time = 40.f + (float)round * 17.f;
				Play(gC, Press(round == 1, 3, 60, 10, 10, 60));
				Check(dashes[0].count == 1 && dashes[0].side == (round == 1 ? -1 : 1), "a dodge in the first level");
				players[0] = CPlayer(); dashes[0] = Dash(); events.clear(); causes.clear();
				if (menu) { gC.toControl = nullptr; GAME.state = 1; Play(gC, Frames(3, [](int, Step &) {})); GAME.state = GAME_STATE_PLAYING; gC.toControl = &players[0]; }
				EVENT_MANAGER.time = 0.3f;
				Play(gC, Press(round != 1, 3, 60, 6, 6, 40));
				char why[120]; snprintf(why, sizeof why, "the first dodge of the next level (menu %d round %d: dashes %d side %d)", menu, round, dashes[0].count, dashes[0].side);
				Check(dashes[0].count == 1 && dashes[0].side == (round != 1 ? -1 : 1), why);
			}
	}
	else if (Is(mode, "dash-time-change"))
	{
		// The button is tapped for one update and g_dash_time is changed (a console command) while the double tap waits: the second push follows the new value
		Reset(gC);
		for (int i = 0; i < 100; ++i)
		{
			if (i == 12) CBattleEngineWalkerPart::mDashTime = 0.4f;
			Step s; s.d = i >= 10 && i < 80; s.ctrl = i == 10;
			RunStep(gC, s, (unsigned)i);
		}
		auto taps = Taps(0);
		Check(taps.size() == 2, "a double tap");
		const float spacing = taps[1].time - taps[0].time;
		char why[100]; snprintf(why, sizeof why, "the second push follows the new g_dash_time (%.4f s after the first)", spacing);
		Check(spacing >= 0.4f * 0.55f && spacing <= 0.4f * 0.95f && dashes[0].count == 1 && dashes[0].side == 1, why);
	}
	else if (Is(mode, "no-console-vars"))
	{
		// The game has not registered the walker's variables: the assist works with the retail numbers (a window of 0.1 to 0.2 s), whatever the length of an update
		Reset(gC); CONSOLE.mVariables = nullptr;
		Play(gC, Press(false, 3, 100, 10, 10, 100, 0.0625f));
		auto taps = Taps(0);
		Check(taps.size() == 2, "a double tap");
		char why[100]; snprintf(why, sizeof why, "the retail window: the second push two updates after the first (%.4f s)", taps[1].time - taps[0].time);
		Check(taps[1].time - taps[0].time == 0.125f && dashes[0].count == 1, why);
	}
	else if (Is(mode, "dash-start") || Is(mode, "dash-start-retail"))
	{
		// Whether a push counts as the beginning of one for the walker depends on g_dash_start (0.9 in retail): a stick pushed to 0.8 of a full push counts when the number is 0.7 and not when
		// it is 0.9. The press comes 0.15 s after the push, in the window the first push must wait out when the walker counts that push.
		// dash-start: the walker's number is 0.7 and the console has it: the assist must read it, or it would not wait and the walker's own code would dash the other way.
		// dash-start-retail: the console has no variables: the assist takes retail's 0.9, so a push to 0.86 does not make it wait.
		const bool console = Is(mode, "dash-start");
		Reset(gC);
		CBattleEngineWalkerPart::mDashStart = console ? 0.7f : 0.9f;
		if (!console) CONSOLE.mVariables = nullptr;
		const float raw = console ? 0.83f : 0.88f;			// 0.8 and 0.86 of a full push after the game's dead zone and scale
		Play(gC, Frames(120, [=](int i, Step &s) { s.stick = i >= 10 && i < 100 ? raw : 0.f; s.padB = i >= 19 && i < 22; }));
		auto taps = Taps(0);
		char why[160]; snprintf(why, sizeof why, "g_dash_start %s: the dodge goes right (dashes %d)", console ? "0.7" : "retail's", dashes[0].count);
		Check(dashes[0].sides == std::vector<int>({1}), why);
		if (console) Check(taps.size() == 2 && taps[0].update > 19, "a push to 0.8 begins one at g_dash_start 0.7: the first push waits");
		else Check(taps.size() == 2 && taps[0].update == 19, "a push to 0.86 does not begin one at retail's 0.9: the first push goes at the press");
	}
	else if (Is(mode, "air-retry"))
	{
		// A slow frame carries the second push past the window while the walker is off the ground: the walker would not take another try either, so none is made
		Reset(gC);
		for (int i = 0; i < 100; ++i)
		{
			players[0].engine.ground = !(i >= 12 && i < 30);
			Step s; s.dt = i == 14 ? 0.2f : 1.f / 60;
			s.d = i >= 10 && i < 90; s.ctrl = i >= 10 && i < 13;
			RunStep(gC, s, (unsigned)i);
		}
		Check(Taps() == 1 && dashes[0].count == 0, "no second try while the walker is off the ground: the first push only");
	}
	else if (Is(mode, "new-level"))
	{
		// A new level, however the game tells it: the clock starts again near 0 with the front end in between or not (a quick retry), or the clock is later than where the old level
		// ended (a short level, a long wait in the menus) with the front end between. The walker is a new one, and the old level left the last value full. The player taps the direction
		// two updates into the new level and asks for the dodge that way 0.1 to 0.2 s later: the assist must have forgotten the old walker, so that the first push waits for the new
		// walker's memory of the tap and the dodge goes the way asked.
		int total = 0;
		for (int how = 0; how < 3; ++how)			// 0: the clock backwards, no front end; 1: backwards, by way of the front end; 2: forward, by way of the front end
			for (int left = 0; left < 2; ++left)
				for (int gap = 3; gap <= 16; ++gap)
				{
					Reset(gC);
					Play(gC, Frames(60, [=](int i, Step &s) { (left ? s.a : s.d) = i >= 5 && i < 40; }));		// the first level: the direction held, then let go (the last value stays full)
					players[0] = CPlayer(); dashes[0] = Dash(); events.clear(); causes.clear();
					if (how > 0) { gC.toControl = nullptr; GAME.state = 1; Play(gC, Frames(3, [](int, Step &) {})); GAME.state = GAME_STATE_PLAYING; gC.toControl = &players[0]; }
					EVENT_MANAGER.time = how == 2 ? EVENT_MANAGER.time + 3.f : 0.3f;
					Play(gC, Frames(100, [=](int i, Step &s) { (left ? s.a : s.d) = i == 2 || (i >= 2 + gap && i < 90); s.ctrl = i >= 2 + gap && i < 5 + gap; }));
					char why[140]; snprintf(why, sizeof why, "the first dodge of a new level goes the way asked (how %d, %s, gap %d: dashes %d)", how, left ? "left" : "right", gap, dashes[0].count);
					Check(!dashes[0].sides.empty(), why);
					for (int side : dashes[0].sides) Check(side == (left ? -1 : 1), why);
					++total;
				}
		printf("new-level %d timings\n", total);
	}
	else if (Is(mode, "new-walker"))
	{
		// A player is lost in a co-op or versus game and the game spawns a new Battle Engine, with a new walker that remembers nothing, while its clock runs on. The old walker's last
		// strafe value is full toward the side the player then pushes (the game keeps it while nothing is pressed), and the player asks for the dodge that way 0.1 to 0.2 s after the
		// push the new walker takes as the beginning of one: the assist must not take what it saw of the old walker for the new one's memory, or it would not wait, and the walker's own
		// code would dash the other way.
		// how 0: the old engine is seen dying for some updates and the new one is built in its place; 1: another engine takes the player at once, nothing is seen dying.
		// kept 1: the direction is held through the loss; 0: it was let go before it and is pushed again after the new engine is there (no push reaches the assist in between).
		// At 60, 20 (the game's own) and 15 updates a second: a copy that began a push one update late is still safe at 60 and 20 and not at 15.
		int total = 0;
		for (float fps : {60.f, 20.f, 15.f})
		for (int how = 0; how < 2; ++how)
			for (int kept = 0; kept < 2; ++kept)
				for (int left = 0; left < 2; ++left)
					for (int gap = fps > 30.f ? 3 : 1; gap <= (fps > 30.f ? 16 : 8); ++gap)
					{
						Reset(gC);
						const int dieAt = 30, respawnAt = how == 0 ? 38 : 30, beginAt = kept ? respawnAt : respawnAt + 2, other = how == 0 ? 0 : 1;
						for (int i = 0; i < 120; ++i)
						{
							if (how == 0) players[0].engine.dying = i >= dieAt && i < respawnAt;
							if (i == respawnAt)
							{
								if (how == 0) { players[0] = CPlayer(); dashes[0] = Dash(); }
								else { GAME.n = 2; gC.toControl = &players[1]; gC.player = 1; }
							}
							Step s; s.dt = 1.f / fps;
							(left ? s.a : s.d) = (i >= 5 && i < 25) || (kept ? i >= 25 && i < 110 : i >= beginAt && i < 110);
							s.ctrl = i >= beginAt + gap && i < beginAt + gap + 3;
							RunStep(gC, s, (unsigned)i);
							if (how == 1 && i >= respawnAt) players[1].engine.part.EndOfFrame();		// RunStep counts down the first player's dash only
						}
						char why[170]; snprintf(why, sizeof why, "after a lost player a dodge %s goes %s (%g updates a second, how %d, direction %s, gap %d: dashes %d)", left ? "left" : "right", left ? "left" : "right", fps, how, kept ? "held" : "pushed again", gap, dashes[other].count);
						Check(!dashes[other].sides.empty(), why);
						for (int side : dashes[other].sides) Check(side == (left ? -1 : 1), why);
						++total;
					}
		printf("new-walker %d timings\n", total);
	}
	else if (Is(mode, "power-start"))
	{
		// The Battle Engine has no power (a level's scripted start): the game drops every action of an unpowered engine (CPlayer::ReceiveButtonAction returns first), so the walker
		// sees none of the pushes made meanwhile. The player holds a direction from update 20, power comes at update 100 and the button is pressed `gap` updates later, 0.1 to 0.2 s
		// after the first push the walker takes: the dodge must go the way asked, never the other, and it must be made.
		int total = 0;
		for (int left = 0; left < 2; ++left)
			for (float fps : {60.f, 20.f})
				for (int gap = 1; gap <= 20; ++gap)
				{
					Reset(gC);
					for (int i = 0; i < 260; ++i)
					{
						players[0].engine.powered = i >= 100;
						Step s; s.dt = 1.f / fps;
						(left ? s.a : s.d) = i >= 20 && i < 200;
						s.ctrl = i >= 100 + gap && i < 103 + gap;
						RunStep(gC, s, (unsigned)i);
					}
					char why[150]; snprintf(why, sizeof why, "after an unpowered stretch a dodge %s goes %s (%g updates a second, gap %d: dashes %d)", left ? "left" : "right", left ? "left" : "right", fps, gap, dashes[0].count);
					for (int side : dashes[0].sides) Check(side == (left ? -1 : 1), why);
					Check(!dashes[0].sides.empty(), why);
					++total;
				}
		printf("power-start %d timings\n", total);
	}
	else if (!strncmp(mode, "plain-", 6))
	{
		// no use of the button: random strafe holds on the keys and the pad; what reaches the player is printed for the comparison with the
		// controller that has no patch
		Reset(gC); unsigned seed = 12345 + (unsigned)Fnv(mode);
		auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) & 0xffff; };
		bool a = false, d = false; float stick = 0; std::vector<Step> v;
		for (int i = 0; i < 1200; ++i)
		{
			if (rnd() % 17 == 0) a = !a;
			if (rnd() % 19 == 0) d = !d;
			if (rnd() % 23 == 0) stick = (float)((int)(rnd() % 21) - 10) / 10.f;
			Step s; s.a = a; s.d = d; s.stick = Is(mode, "plain-pad") ? stick : 0; s.dt = 1.f / (30 + rnd() % 90); v.push_back(s);
		}
		Play(gC, v);
		Check(Taps() == 0, "no taps without the button");
		Report(mode);
	}
	else if (!strncmp(mode, "off-", 4) || !strncmp(mode, "gate-", 5))
	{
		Reset(gC);
		bool pad = false, ctrlOnly = false, both = false;
		if (Is(mode, "off-setting")) gIni = 0;
		if (Is(mode, "off-layer")) gCeLayer = false;
		if (Is(mode, "off-recording")) gRecording = true;
		if (Is(mode, "off-boot")) gBoot = 1;
		if (Is(mode, "off-session")) gSession = 1;
		if (Is(mode, "off-demo")) PLAYABLE_DEMO = 1;
		if (Is(mode, "off-autoconfig")) CLIPARAMS.mAutoConfigTest = 1;
		if (Is(mode, "off-record-demo")) CLIPARAMS.mRecordDemo = true;
		if (Is(mode, "off-play-demo")) CLIPARAMS.mPlaybackDemo = true;
		if (Is(mode, "off-stress")) CLIPARAMS.mStressTest = true;
		if (Is(mode, "off-attract")) CLIPARAMS.mAttractMode = true;
		if (!strncmp(mode, "off-env-", 8)) Env(mode + 8, "1");
		if (Is(mode, "off-env-empty-is-on")) {}
		if (Is(mode, "gate-bound-key")) SetInput(BUTTON_MECH_LANDING_JETS, 0, 0, KEY_ON, DIK_LCONTROL);
		if (Is(mode, "gate-pad-bound")) { pad = true; SetInput(BUTTON_MECH_MORPH, 1, 0, BUTTON_ONCE, 0); SetInput(L, 1, 0, ANALOGUE_MINUS, ANALOGUE_X1); SetInput(R, 1, 0, ANALOGUE_PLUS, ANALOGUE_X1); }
		if (Is(mode, "gate-pad-bound-repeat")) { pad = true; SetInput(BUTTON_MECH_MORPH, 1, 0, BUTTON_REPEAT, 0); SetInput(L, 1, 0, ANALOGUE_MINUS, ANALOGUE_X1); SetInput(R, 1, 0, ANALOGUE_PLUS, ANALOGUE_X1); }
		if (Is(mode, "gate-bound-key-once")) SetInput(BUTTON_MECH_LANDING_JETS, 0, 0, KEY_ONCE, DIK_LCONTROL);
		if (Is(mode, "gate-bound-key-release")) SetInput(BUTTON_MECH_LANDING_JETS, 0, 0, KEY_RELEASE, DIK_LCONTROL);
		if (Is(mode, "gate-pad-bound-on")) { pad = true; SetInput(BUTTON_MECH_MORPH, 1, 0, BUTTON_ON, 0); SetInput(L, 1, 0, ANALOGUE_MINUS, ANALOGUE_X1); SetInput(R, 1, 0, ANALOGUE_PLUS, ANALOGUE_X1); }
		if (Is(mode, "gate-pad-bound-release")) { pad = true; SetInput(BUTTON_MECH_MORPH, 1, 0, BUTTON_RELEASE, 0); SetInput(L, 1, 0, ANALOGUE_MINUS, ANALOGUE_X1); SetInput(R, 1, 0, ANALOGUE_PLUS, ANALOGUE_X1); }
		if (Is(mode, "gate-stick")) { pad = true; devices[0].stick = true; }
		if (Is(mode, "gate-pad-off-key")) {}
		if (Is(mode, "gate-paused")) GAME.paused = true;
		if (Is(mode, "gate-menu")) gC.toControl = nullptr;
		if (Is(mode, "gate-not-playing")) GAME.state = 1;
		if (Is(mode, "gate-airborne")) players[0].engine.ground = false;
		if (Is(mode, "gate-dying")) players[0].engine.dying = true;
		if (Is(mode, "gate-jet")) players[0].engine.walker = false;
		if (Is(mode, "gate-unpowered")) players[0].engine.powered = false;
		if (Is(mode, "gate-ctrl-only")) ctrlOnly = true;
		if (Is(mode, "gate-both-directions")) both = true;
		if (Is(mode, "gate-keytrap")) LT.keytrap = true;
		if (Is(mode, "gate-player-two-key")) { gC.mPadNumber = 1; SetInput(L, 1, 0, KEY_ON, DIK_A); SetInput(R, 1, 0, KEY_ON, DIK_D); }
		if (Is(mode, "gate-pad-disconnected")) { pad = true; LT.mUnknown33474[0] = false; }
		Play(gC, Frames(70, [=](int i, Step &s) {
			if (pad) { s.padB = i >= 10 && i < 13; s.stick = i >= 10 && i < 40 ? 1.f : 0.f; }
			else { s.ctrl = i >= 10 && i < 13; s.d = !ctrlOnly && i >= 10 && i < 40; s.a = both && i >= 10 && i < 40; }
		}));
		Check(Taps() == 0, "the assist is off here: no tap");
		Report(mode);
	}
	else { fprintf(stderr, "unknown mode %s\n", mode); return 1; }
	printf("ok %s %d checks\n", mode, checks);
	return 0;
}
